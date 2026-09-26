"""Sign in to your own Meta account, for the Platform SDK stand-in.

The same native-SSO flow as AXRB's launcher (itself adapted from RiftLift's
meta_auth.py): Meta issues a sign-in challenge, you sign in on Meta's own page
(auth.meta.com) in a separate Edge window with a throwaway profile, and Meta
redirects to an oculus:// URL carrying an encrypted blob that Meta then
exchanges for your account's token. Your password only ever goes to Meta's
page; this script never sees it.

From that account token it asks Meta for a token scoped to one app (Big
Scary's Quest app id by default), which is what the Platform SDK acts with.
Both tokens are stored encrypted with Windows DPAPI (only your Windows user
can read them) in %LOCALAPPDATA%\\QuestBridge\\meta.bin. Tokens are never
printed.

    python tools/meta_signin.py [app_id]
    python tools/meta_signin.py --forget
"""

import asyncio
import ctypes
import ctypes.wintypes
import hashlib
import hmac
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.parse

import requests
import websockets

FRL_APP = "512466987071624"
CLIENT = f"FRL|{FRL_APP}|01d4a1f7fd0682aea7ee8ae987704d63"
META = "https://meta.graph.meta.com"
BIG_SCARY = "6288107037945749"
STORE = os.path.join(os.environ.get("LOCALAPPDATA", "."), "QuestBridge", "meta.bin")
EDGE = r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"


# --- DPAPI, through ctypes so nothing extra has to be installed -------------

class _Blob(ctypes.Structure):
    _fields_ = [("cbData", ctypes.wintypes.DWORD), ("pbData", ctypes.POINTER(ctypes.c_char))]


def _dpapi(data: bytes, protect: bool) -> bytes:
    crypt32 = ctypes.windll.crypt32
    source = _Blob(len(data), ctypes.cast(ctypes.create_string_buffer(data, len(data)), ctypes.POINTER(ctypes.c_char)))
    out = _Blob()
    fn = crypt32.CryptProtectData if protect else crypt32.CryptUnprotectData
    if not fn(ctypes.byref(source), None, None, None, None, 0, ctypes.byref(out)):
        raise OSError("DPAPI failed")
    try:
        return ctypes.string_at(out.pbData, out.cbData)
    finally:
        ctypes.windll.kernel32.LocalFree(out.pbData)


def save(tokens: dict) -> None:
    os.makedirs(os.path.dirname(STORE), exist_ok=True)
    with open(STORE, "wb") as f:
        f.write(_dpapi(json.dumps(tokens).encode(), True))


def load() -> dict:
    with open(STORE, "rb") as f:
        return json.loads(_dpapi(f.read(), False))


# --- Meta requests ----------------------------------------------------------

def post(url: str, fields: dict) -> dict:
    response = requests.post(url, data=fields, headers={"Accept": "application/json"}, timeout=30)
    try:
        data = response.json()
    except ValueError:
        raise SystemExit(f"Meta returned something unexpected ({response.status_code}).")
    if not response.ok or data.get("error") or data.get("errors"):
        # Responses can echo credentials, so only the code is shown.
        code = (data.get("error") or {}).get("code") or ((data.get("errors") or [{}])[0]).get("code")
        raise SystemExit(f"Meta refused the request ({response.status_code}, code {code}).")
    return data


# --- Watching the sign-in window for Meta's oculus:// callback --------------

def _free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


async def _wait_for_callback(port: int, deadline: float) -> str:
    """Attach to the Edge window over its DevTools socket and wait for the
    navigation to oculus://; only that one URL is read."""
    while time.time() < deadline:
        try:
            pages = requests.get(f"http://127.0.0.1:{port}/json", timeout=2).json()
            page = next(p for p in pages if p.get("type") == "page")
            break
        except Exception:
            await asyncio.sleep(0.5)
    else:
        raise SystemExit("The sign-in window did not open.")
    async with websockets.connect(page["webSocketDebuggerUrl"], max_size=64 * 1024 * 1024) as ws:
        ident = 0
        for method in ("Page.enable", "Network.enable"):
            ident += 1
            await ws.send(json.dumps({"id": ident, "method": method}))
        while time.time() < deadline:
            try:
                raw = await asyncio.wait_for(ws.recv(), timeout=1)
            except asyncio.TimeoutError:
                continue
            except websockets.ConnectionClosed:
                raise SystemExit("The sign-in window was closed before sign-in finished.")
            event = json.loads(raw)
            params = event.get("params", {})
            candidates = [params.get("url", ""), params.get("request", {}).get("url", ""),
                          params.get("frame", {}).get("url", "")]
            headers = (params.get("redirectResponse") or params.get("response") or {}).get("headers", {})
            candidates += [v for k, v in headers.items() if k.lower() == "location"]
            for url in candidates:
                if url.startswith(("oculus://", "oculus-client://")):
                    return url
    raise SystemExit("Sign-in timed out.")


def sign_in(app_id: str) -> None:
    started = post(f"{META}/webview_tokens_query", {"access_token": CLIENT})
    challenge, etoken = started.get("native_sso_token"), started.get("native_sso_etoken")
    if not challenge or not etoken:
        raise SystemExit("Meta did not return a sign-in challenge.")
    confirm = "https://auth.meta.com/native_sso/confirm?" + urllib.parse.urlencode(
        {"native_app_id": FRL_APP, "source_app_id": FRL_APP, "native_sso_etoken": etoken})

    port = _free_port()
    profile = tempfile.mkdtemp(prefix="qb-meta-signin-")
    edge = subprocess.Popen([EDGE, f"--remote-debugging-port={port}", f"--user-data-dir={profile}",
                             "--no-first-run", "--new-window", confirm])
    print("A Meta sign-in window is open. Sign in there; it closes by itself when done.")
    try:
        callback = asyncio.run(_wait_for_callback(port, time.time() + 600))
    finally:
        edge.terminate()
        time.sleep(1)
        shutil.rmtree(profile, ignore_errors=True)

    query = urllib.parse.parse_qs(urllib.parse.urlsplit(callback).query)
    expected = hashlib.sha256(challenge.encode()).hexdigest()[:16]
    if not hmac.compare_digest((query.get("token") or [""])[0], expected):
        raise SystemExit("The sign-in callback did not match this session.")
    blob = (query.get("blob") or [""])[0]
    if not blob:
        raise SystemExit("The sign-in callback carried no blob.")

    account = post(f"{META}/webview_blobs_decrypt",
                   {"access_token": CLIENT, "blob": blob, "request_token": challenge}).get("access_token")
    profile_result = post(f"{META}/graphql", {"access_token": account, "doc_id": "24112177345042346",
                                             "variables": json.dumps({"app_id": "1582076955407037"})})
    tokens = ((profile_result.get("data") or {}).get("xfr_create_profile_token") or {}).get("profile_tokens") or []
    profile_token = tokens[0].get("access_token") if tokens else None
    if not profile_token:
        raise SystemExit("Meta did not return a Quest profile token.")
    save({"profile": profile_token, "apps": {}})
    print("Signed in.")
    scope(app_id)


def scope(app_id: str) -> None:
    """An app-scoped token for app_id, from the stored profile token."""
    tokens = load()
    result = post(f"https://graph.oculus.com/authenticate_application?app_id={app_id}",
                  {"access_token": tokens["profile"]})
    if not result.get("access_token"):
        raise SystemExit(f"Meta returned no token for app {app_id}.")
    tokens.setdefault("apps", {})[app_id] = result["access_token"]
    save(tokens)
    print(f"Stored a token scoped to app {app_id}.")


if __name__ == "__main__":
    if "--forget" in sys.argv:
        if os.path.exists(STORE):
            os.remove(STORE)
        print("Forgotten.")
    else:
        app = next((a for a in sys.argv[1:] if a.isdigit()), BIG_SCARY)
        if os.path.exists(STORE) and "--again" not in sys.argv:
            scope(app)
        else:
            sign_in(app)
