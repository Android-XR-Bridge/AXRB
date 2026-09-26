"""Your Meta Quest library, and downloads of the games in it.

Uses the sign-in tools/meta_signin.py stored (your own account, DPAPI-encrypted,
never printed). Only what Meta's servers say your account is entitled to is
listed or downloaded; Meta decides, nothing here decides for it. The queries
follow the AXRB launcher (I:/Projects/axrb_reverse/v0.2.3, core/meta.mjs).

  python tools/meta_library.py list
  python tools/meta_library.py plan <app id>           files and sizes, nothing fetched
  python tools/meta_library.py download <app id> --yes [--root I:/qb-<name>]

A download lands as a Quest Bridge root: <root>/data/app/<package>/base.apk
(plus its lib/arm64 unpacked) and OBBs under <root>/sdcard/Android/obb/<package>.
Every file is checked against the size Meta lists for it, and the APK must open as a zip.
"""
import hashlib
import json
import os
import re
import shutil
import sys
import urllib.parse
import zipfile

import requests

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from meta_signin import load, post  # noqa: E402

GRAPH = "https://graph.oculus.com/graphql"
DELIVERY_APP = "1481000308606657"  # the Quest app whose scoped token Meta's CDN takes
QUEST = re.compile(r"QUEST|MONTEREY|HOLLYWOOD|SEACLIFF|EUREKA|PANTHER", re.I)


def nodes(value):
    if isinstance(value, list):
        return value
    if not isinstance(value, dict):
        return []
    if "nodes" in value:
        return value["nodes"] or []
    return [e.get("node") for e in value.get("edges") or [] if e.get("node")]


def quest_app(item) -> bool:
    item = item or {}
    binary = item.get("latest_supported_binary") or {}
    platform = str(item.get("platform") or binary.get("platform") or "").upper()
    devices = item.get("supported_hmd_platforms") or item.get("supported_hmd_types") or item.get("targeted_devices") or []
    quest_devices = any(QUEST.search(str(d)) for d in devices)
    return (platform == "ANDROID_6DOF" or (platform == "ANDROID" and quest_devices)
            or (not platform and quest_devices and binary.get("__typename") == "AndroidBinary"))


def query(token: str, doc: str, variables: dict | None = None) -> dict:
    return post(GRAPH, {"access_token": token, "doc_id": doc, "variables": json.dumps(variables or {})})


def profile_token() -> str:
    try:
        return load()["profile"]
    except FileNotFoundError:
        raise SystemExit("Not signed in: run python tools/meta_signin.py first.")


def library(token: str):
    viewer = (query(token, "4850747515044496").get("data") or {}).get("viewer") or {}
    connection = (viewer.get("user") or {}).get("active_entitlements") or viewer.get("active_entitlements")
    if connection is None:
        raise SystemExit("Meta did not return your library. Try signing in again (meta_signin.py --again).")
    items = [e.get("item") or {} for e in nodes(connection)]
    games = [i for i in items if quest_app(i)]  # Rift purchases are not Quest ownership
    name = (viewer.get("user") or {}).get("alias") or "your account"
    partial = bool((connection.get("page_info") or {}).get("has_next_page")) if isinstance(connection, dict) else False
    return name, games, partial, len(items)


def plan(token: str, app_id: str) -> dict:
    node = (query(token, "2885322071572384", {"applicationID": app_id}).get("data") or {}).get("node") or {}
    application, released = node, None
    if not quest_app(application):
        listing = (query(token, "6549406941839522", {"itemId": app_id, "hmdType": "EUREKA"}).get("data") or {}).get("item") or {}
        if str(listing.get("id")) != app_id or not quest_app(listing):
            raise SystemExit("Meta did not return a Quest build for this app.")
        application, released = listing, listing.get("latest_supported_binary")
    builds = [b for b in nodes(node.get("primary_binaries") or node.get("binaries"))
              if b.get("platform") != "PC" and b.get("__typename") != "RiftBinary"]
    builds.sort(key=lambda b: -int(b.get("version_code") or b.get("versionCode") or 0))
    if released and released.get("id"):
        match = [b for b in builds if str(b.get("id")) == str(released["id"])]
        current = match[0] if match else released
        builds = [dict(current, **released)] + [b for b in builds if b is not current]
    if not builds:
        raise SystemExit("Meta returned no downloadable Quest build for this app.")
    selected = builds[0]
    detail = (query(token, "4734929166632773", {"binaryID": str(selected["id"])}).get("data") or {}).get("node") or {}
    binary = dict(selected, **detail)
    if binary.get("platform") == "PC" or not binary.get("package_name"):
        raise SystemExit("The build Meta returned is not a Quest APK.")
    files = [{"id": str(binary["id"]), "name": "base.apk", "uri": binary.get("uri"),
              "size": int(binary.get("size") or 0), "kind": "apk"}]
    obb = binary.get("obb_binary") or {}
    if obb.get("id"):
        files.append({"id": str(obb["id"]), "name": obb.get("file_name"), "uri": obb.get("uri"),
                      "size": int(obb.get("size") or 0), "kind": "obb"})
    assets = nodes(binary.get("asset_files"))
    if int((binary.get("asset_files") or {}).get("count") or 0) > len(assets):
        raise SystemExit("Meta returned an incomplete file list; nothing downloaded.")
    for asset in assets:
        if asset.get("is_required") is False or asset.get("iap_item"):
            continue  # optional DLC: its own entitlement check, not done here
        if any(f["name"] == asset.get("file_name") for f in files):
            continue
        files.append({"id": str(asset.get("id") or ""), "name": asset["file_name"], "uri": asset.get("uri"),
                      "size": int(asset.get("size") or 0),
                      "kind": "obb" if asset["file_name"].endswith(".obb") else "asset"})
    return {"app": app_id, "name": application.get("display_name") or binary.get("package_name"),
            "package": binary["package_name"], "version": binary.get("version") or "", "files": files}


def delivery_token(profile: str) -> str:
    url = "https://graph.oculus.com/authenticate_application?" + urllib.parse.urlencode(
        {"app_id": DELIVERY_APP, "access_token": profile})
    token = post(url, {}).get("access_token")
    if not token:
        raise SystemExit("Meta did not return a download token. Try meta_signin.py --again.")
    return token


def file_url(file: dict, token: str) -> str:
    if file.get("uri"):
        parts = urllib.parse.urlsplit(file["uri"])
        # The token goes only to Meta's own binary endpoint; signed asset URLs stay as they are.
        if parts.scheme == "https" and re.fullmatch(r"securecdn(-[a-z0-9-]+)?\.oculus\.com", parts.hostname or "") \
                and parts.path == "/binaries/download/":
            query_ = dict(urllib.parse.parse_qsl(parts.query))
            query_["access_token"] = token
            return urllib.parse.urlunsplit(parts._replace(query=urllib.parse.urlencode(query_)))
        return file["uri"]
    if not file["id"].isdigit():
        raise SystemExit("Meta returned an invalid file id.")
    return "https://securecdn.oculus.com/binaries/download/?" + urllib.parse.urlencode(
        {"id": file["id"], "access_token": token})


def fetch(url: str, destination: str, size: int) -> None:
    partial = destination + ".part"
    have = os.path.getsize(partial) if os.path.exists(partial) else 0
    headers = {"Range": f"bytes={have}-"} if have else {}
    with requests.get(url, headers=headers, stream=True, timeout=60) as response:
        if response.status_code == 200:
            have = 0  # the server ignored the range: start over
        elif response.status_code != 206:
            raise SystemExit(f"Download refused ({response.status_code}).")
        with open(partial, "ab" if have else "wb") as out:
            done = have
            for chunk in response.iter_content(1 << 20):
                out.write(chunk)
                done += len(chunk)
                if size:
                    print(f"\r  {os.path.basename(destination)}: {done / 2**20:,.0f} / {size / 2**20:,.0f} MB",
                          end="", flush=True)
    print()
    if size and os.path.getsize(partial) != size:
        raise SystemExit(f"{os.path.basename(destination)}: size differs from what Meta listed; kept as .part.")
    os.replace(partial, destination)


def show_plan(p: dict) -> int:
    total = sum(f["size"] for f in p["files"])
    print(f"{p['name']}  ({p['package']} {p['version']})")
    for f in p["files"]:
        print(f"  {f['kind']:5} {f['name']:48} {f['size'] / 2**20:10,.1f} MB")
    print(f"  total {total / 2**30:.2f} GB in {len(p['files'])} file(s)")
    return total


def main() -> None:
    args = sys.argv[1:]
    if not args or args[0] not in ("list", "plan", "download"):
        raise SystemExit(__doc__)
    token = profile_token()
    if args[0] == "list":
        name, games, partial, total = library(token)
        print(f"Quest games owned by {name}: {len(games)} (of {total} entitlements)")
        for g in sorted(games, key=lambda g: (g.get("display_name") or "").lower()):
            print(f"  {g.get('id'):>17}  {g.get('display_name') or '?'}")
        if partial:
            print("  (Meta says there are more; only the first page was returned)")
        return
    if len(args) < 2 or not args[1].isdigit():
        raise SystemExit("Give the app id (from list).")
    p = plan(token, args[1])
    total = show_plan(p)
    if args[0] == "plan":
        return
    if "--yes" not in args:
        raise SystemExit("Nothing downloaded: add --yes to download these files.")
    root = args[args.index("--root") + 1] if "--root" in args else f"I:/qb-{p['package'].split('.')[-1].lower()}"
    free = shutil.disk_usage(os.path.splitdrive(os.path.abspath(root))[0] + "\\").free
    if free < total + 5 * 2**30:
        raise SystemExit(f"Not enough space on {root}: {free / 2**30:.1f} GB free, need {total / 2**30:.1f} + 5 GB.")
    app_dir = os.path.join(root, "data", "app", p["package"])
    obb_dir = os.path.join(root, "sdcard", "Android", "obb", p["package"])
    os.makedirs(app_dir, exist_ok=True)
    delivery = delivery_token(token)
    for f in p["files"]:
        name = os.path.basename(f["name"] or "")
        if not name or name in (".", ".."):
            raise SystemExit("Meta returned an unsafe file name.")
        target = os.path.join(app_dir if f["kind"] == "apk" else obb_dir, name)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        if os.path.exists(target) and (not f["size"] or os.path.getsize(target) == f["size"]):
            print(f"  {name}: already here")
            continue
        fetch(file_url(f, delivery), target, f["size"])
    apk = os.path.join(app_dir, "base.apk")
    with zipfile.ZipFile(apk) as z:  # a readable zip is also the integrity check
        for member in z.namelist():
            if member.startswith("lib/arm64-v8a/") and member.endswith(".so"):
                out = os.path.join(app_dir, "lib", "arm64", os.path.basename(member))
                os.makedirs(os.path.dirname(out), exist_ok=True)
                with z.open(member) as src, open(out, "wb") as dst:
                    shutil.copyfileobj(src, dst)
    print(f"Done: {root}  (QB_ROOT={root} QB_PACKAGE={p['package']})")


if __name__ == "__main__":
    main()
