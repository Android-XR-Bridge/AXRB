"""Does Meta's own Platform SDK answer for a Quest app when handed your token?

Loads LibOVRPlatform64_1.dll (from the Meta Horizon Link app), initialises it
with the tokens tools/meta_signin.py stored, and asks for the logged-in user,
the entitlement and a user proof. Prints what comes back; never the tokens.

    python tools/platform_token_probe.py [standalone|app] [app_id]
"""

import ctypes
import os
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from meta_signin import BIG_SCARY, load  # noqa: E402

DLL = r"C:\Program Files\Meta Horizon\Support\oculus-runtime\LibOVRPlatform64_1.dll"

mode = next((a for a in sys.argv[1:] if not a.isdigit()), "standalone")
app_id = next((a for a in sys.argv[1:] if a.isdigit()), BIG_SCARY)
tokens = load()
os.add_dll_directory(os.path.dirname(DLL))
lib = ctypes.CDLL(DLL)


def fn(name, restype, *args):
    f = getattr(lib, name)
    f.restype = restype
    f.argtypes = list(args)
    return f


H = ctypes.c_void_p
u64 = ctypes.c_uint64
PopMessage = fn("ovr_PopMessage", H)
GetType = fn("ovr_Message_GetType", ctypes.c_uint32, H)
IsError = fn("ovr_Message_IsError", ctypes.c_bool, H)
GetError = fn("ovr_Message_GetError", H, H)
ErrMsg = fn("ovr_Error_GetMessage", ctypes.c_char_p, H)
ErrCode = fn("ovr_Error_GetCode", ctypes.c_int, H)
RequestID = fn("ovr_Message_GetRequestID", u64, H)
Free = fn("ovr_FreeMessage", None, H)
GetUser = fn("ovr_Message_GetUser", H, H)
UserId = fn("ovr_User_GetID", u64, H)
UserName = fn("ovr_User_GetOculusID", ctypes.c_char_p, H)
GetProof = fn("ovr_Message_GetUserProof", H, H)
ProofNonce = fn("ovr_UserProof_GetNonce", ctypes.c_char_p, H)

scoped = tokens.get("apps", {}).get(app_id)
if not scoped:
    raise SystemExit(f"No token scoped to {app_id}; run tools/meta_signin.py {app_id}")
if mode == "app":
    init = fn("ovr_PlatformInitializeWithAccessToken", ctypes.c_int, u64, ctypes.c_char_p)
    result = init(int(app_id), scoped.encode())
else:
    # (const char* accessToken): the app is whichever the token is scoped to.
    init = fn("ovr_PlatformInitializeStandaloneAccessToken", u64, ctypes.c_char_p)
    token = tokens["profile"] if mode == "standalone-profile" else scoped
    result = init(token.encode())
print(f"{init.__name__} ({mode}, app {app_id}) -> {result}")

names = {}
names[fn("ovr_User_GetLoggedInUser", u64)()] = "logged-in user"
names[fn("ovr_Entitlement_GetIsViewerEntitled", u64)()] = "entitlement"
names[fn("ovr_User_GetUserProof", u64)()] = "user proof"
for tick in range(200):  # 20 seconds
    while True:
        message = PopMessage()
        if not message:
            break
        what = names.get(RequestID(message), f"type 0x{GetType(message):08x}")
        if IsError(message):
            error = GetError(message)
            print(f"{what}: ERROR {ErrCode(error)}: {(ErrMsg(error) or b'').decode(errors='replace')}")
        elif what == "logged-in user":
            user = GetUser(message)
            print(f"{what}: OK, id {UserId(user)}, name {(UserName(user) or b'').decode(errors='replace')}")
        elif what == "user proof":
            nonce = ProofNonce(GetProof(message)) or b""
            print(f"{what}: OK, nonce of {len(nonce)} characters")
        else:
            print(f"{what}: OK")
        Free(message)
    time.sleep(0.1)
