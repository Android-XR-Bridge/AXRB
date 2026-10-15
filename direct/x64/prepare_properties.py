"""Build Bionic's legacy property area for the isolated native ART sysroot."""
import argparse
import struct
from pathlib import Path

def generate(root):
    props = {}
    for name in ("system/build.prop", "vendor-build.prop"):
        path = root / name
        if path.exists():
            for line in path.read_text(errors="replace").splitlines():
                if "=" in line and not line.lstrip().startswith("#"):
                    key, value = line.split("=", 1)
                    props[key.strip()] = value.strip()
    for key in ("brand", "device", "manufacturer", "model", "name"):
        value = props.get("ro.product.vendor." + key)
        if value: props["ro.product." + key] = value
    props.update({
        "ro.product.model": "sdk_gphone64_arm64", "ro.product.manufacturer": "Google",
        "ro.product.brand": "google", "ro.product.name": "sdk_gphone64_arm64",
        "ro.product.cpu.abi": "x86_64", "ro.product.cpu.abilist": "x86_64,arm64-v8a",
        "ro.product.cpu.abilist64": "x86_64,arm64-v8a", "ro.product.cpu.abilist32": "",
        "ro.hardware": "ranchu", "ro.boot.hardware": "ranchu", "ro.boot.qemu": "1",
        "ro.arch": "x86_64", "ro.build.version.sdk": "36", "ro.debuggable": "0",
        "ro.serialno": "AXRBDIRECT0001", "ro.boot.serialno": "AXRBDIRECT0001",
        "ro.dalvik.vm.native.bridge": "libaxrb_nativebridge.so", "dalvik.vm.usejit": "false",
        "dalvik.vm.usejitprofiles": "false", "dalvik.vm.dex2oat-filter": "verify",
        "ro.opengles.version": "196610", "ro.sf.lcd_density": "440", "sys.boot_completed": "1",
    })
    blob = bytearray(1024 * 1024)
    used = 112
    def put(offset, value): struct.pack_into("<I", blob, 128 + offset, value)
    def get(offset): return struct.unpack_from("<I", blob, 128 + offset)[0]
    def alloc(size):
        nonlocal used
        at = used
        used += (size + 3) & ~3
        if used + 128 > len(blob): raise ValueError("Property area overflow")
        return at
    def node(name):
        at = alloc(20 + len(name) + 1)
        put(at, len(name)); blob[128+at+20:128+at+20+len(name)] = name
        return at
    for key, value in sorted(props.items()):
        current = 0
        for token in key.encode().split(b"."):
            link = current + 16
            while get(link):
                at = get(link); length = get(at)
                name = bytes(blob[128+at+20:128+at+20+length])
                if token == name: break
                link = at + (8 if (len(token), token) < (length, name) else 12)
            else:
                at = node(token); put(link, at)
            current = at
        name = key.encode(); raw = value.encode(); info = alloc(96 + len(name) + 1)
        put(current + 4, info); blob[128+info+96:128+info+96+len(name)] = name
        if len(raw) < 92:
            put(info, len(raw) << 24); blob[128+info+4:128+info+4+len(raw)] = raw
        else:
            long_at = alloc(len(raw)+1); blob[128+long_at:128+long_at+len(raw)] = raw
            error = b"Must use __system_property_read_callback() to read"
            blob[128+info+4:128+info+4+len(error)] = error
            put(info, (len(error)<<24) | (1<<16)); put(info+60, long_at-info)
    struct.pack_into("<IIII", blob, 0, used, 0, 0x504f5250, 0xfc6ed0ab)
    target = root / "dev/__properties__"; target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(blob)
    return len(props)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(); parser.add_argument("sysroot", type=Path)
    args = parser.parse_args(); print(f"Prepared {generate(args.sysroot)} Android properties")
