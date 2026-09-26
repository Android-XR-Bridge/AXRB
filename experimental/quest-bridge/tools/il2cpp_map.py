"""Names for IL2CPP code addresses: which C# method a pc in libil2cpp.so is in.

IL2CPP compiles every C# method to a native function and keeps, per assembly,
a table of those functions (Il2CppCodeGenModule.methodPointers) indexed by the
method's metadata token. The names live in global-metadata.dat. Put the two
together and any address inside libil2cpp.so can be named, which is what a
backtrace through game code needs.

The tables are filled in by relocations, so this reads the library as it sits
in memory after loading: run the guest once with QB_DUMPLIB=libil2cpp.so,
which writes <QB_ROOT>/libil2cpp.so.mem.

    python tools/il2cpp_map.py METADATA MEMDUMP BASE [ADDRESS ...]

With addresses (absolute, or libil2cpp offsets written +5add780), prints the
method each is in. Without, writes the whole map to MEMDUMP.map as
"offset size name" lines. Metadata version 31 (Unity 2021.3) layouts.
"""

import bisect
import struct
import sys


def read_metadata(path):
    m = open(path, "rb").read()
    sanity, version = struct.unpack_from("<Ii", m, 0)
    if sanity != 0xFAB11BAF:
        raise SystemExit("not an IL2CPP metadata file")
    if version != 31:
        print(f"warning: metadata version {version}, layouts are for 31", file=sys.stderr)
    sections = struct.unpack_from("<62I", m, 8)
    section = lambda n: (sections[2 * n], sections[2 * n + 1])

    string_off, _ = section(2)

    def string(index):
        end = m.index(b"\0", string_off + index)
        return m[string_off + index:end].decode("utf-8", "replace")

    methods_off, methods_size = section(5)
    methods = []
    for i in range(methods_size // 36):
        name, declaring, _ret, _retparam, _params, _generic, token = struct.unpack_from("<7i", m, methods_off + i * 36)
        methods.append((name, declaring, token))

    types_off, types_size = section(19)
    types = []
    for i in range(types_size // 88):
        base = types_off + i * 88
        name, namespace = struct.unpack_from("<2i", m, base)
        method_start = struct.unpack_from("<i", m, base + 9 * 4)[0]
        method_count = struct.unpack_from("<H", m, base + 64)[0]
        types.append((name, namespace, method_start, method_count))

    images_off, images_size = section(20)
    images = []
    for i in range(images_size // 40):
        name, _assembly, type_start, type_count = struct.unpack_from("<4i", m, images_off + i * 40)
        images.append((string(name), type_start, type_count))
    return string, methods, types, images


def find_modules(mem, base, image_names):
    """Each Il2CppCodeGenModule starts with a pointer to its assembly's name,
    then the method count and the method pointer table."""
    modules = {}
    for name in image_names:
        needle = name.encode() + b"\0"
        at = mem.find(needle)
        while at >= 0:
            # A real name string starts after a NUL (or at a word boundary).
            if at == 0 or mem[at - 1] == 0:
                address = base + at
                packed = struct.pack("<Q", address)
                ref = mem.find(packed)
                while ref >= 0:
                    if ref % 8 == 0:
                        count = struct.unpack_from("<I", mem, ref + 8)[0]
                        table = struct.unpack_from("<Q", mem, ref + 16)[0]
                        if 0 < count < 1_000_000 and base <= table < base + len(mem):
                            modules[name] = (count, table)
                            break
                    ref = mem.find(packed, ref + 1)
                if name in modules:
                    break
            at = mem.find(needle, at + 1)
    return modules


def build_map(metadata, mem, base):
    string, methods, types, images = metadata
    modules = find_modules(mem, base, [image[0] for image in images])
    entries = {}
    for image_name, type_start, type_count in images:
        if image_name not in modules:
            continue
        count, table = modules[image_name]
        for t in range(type_start, type_start + type_count):
            type_name, namespace, method_start, method_count = types[t]
            full_type = (string(namespace) + "." if string(namespace) else "") + string(type_name)
            for mi in range(method_start, method_start + method_count):
                name, _declaring, token = methods[mi]
                rid = token & 0xFFFFFF
                if not 0 < rid <= count:
                    continue
                pointer = struct.unpack_from("<Q", mem, table - base + (rid - 1) * 8)[0]
                if pointer and base <= pointer < base + len(mem):
                    entries.setdefault(pointer - base, f"{full_type}::{string(name)}")
    return sorted(entries.items()), modules


def main():
    if len(sys.argv) < 4:
        raise SystemExit(__doc__)
    metadata_path, mem_path, base_text = sys.argv[1:4]
    base = int(base_text, 16)
    mem = open(mem_path, "rb").read()
    entries, modules = build_map(read_metadata(metadata_path), mem, base)
    print(f"{len(modules)} assemblies with code, {len(entries)} methods mapped", file=sys.stderr)
    offsets = [offset for offset, _ in entries]
    if len(sys.argv) > 4:
        for text in sys.argv[4:]:
            offset = int(text[1:], 16) if text.startswith("+") else int(text, 16) - base
            i = bisect.bisect_right(offsets, offset) - 1
            if i < 0:
                print(f"{text}: before the first method")
            else:
                start, name = entries[i]
                print(f"{text}: {name} +{offset - start:x}")
        return
    with open(mem_path + ".map", "w", encoding="utf-8") as out:
        for i, (offset, name) in enumerate(entries):
            size = (entries[i + 1][0] if i + 1 < len(entries) else offset) - offset
            out.write(f"{offset:x} {size:x} {name}\n")
    print(f"wrote {mem_path}.map", file=sys.stderr)


if __name__ == "__main__":
    main()
