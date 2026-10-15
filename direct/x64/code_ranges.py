"""Build hash-bound instruction ranges from Android's ELF mini-debug symbols.

ART keeps jump tables inside .text. Linear disassembly is not a valid way to
distinguish these tables from instructions. Keep the ELF unchanged and record
function extents from .gnu_debugdata and the public symbol table instead.
These are developer execution metadata, not a security boundary.
"""
import hashlib
import lzma
import struct
import sys
from pathlib import Path


def sections(data):
    if len(data) < 64 or data[:6] != b'\x7fELF\x02\x01':
        raise ValueError('Expected ELF64 little endian')
    header = struct.unpack_from('<16sHHIQQQIHHHHHH', data)
    if header[2] != 62 or header[11] != 64 or header[12] > 8192:
        raise ValueError('Expected x86_64 section table')
    result = [struct.unpack_from('<IIQQQQIIQQ', data, header[6] + 64 * i)
              for i in range(header[12])]
    names = result[header[13]]
    strings = data[names[4]:names[4] + names[5]]
    return [(strings[s[0]:].split(b'\0')[0].decode('ascii'), s) for s in result]


def functions(data):
    sh = sections(data)
    result = []
    for _, symtab in sh:
        if symtab[1] not in (2, 11):
            continue
        if symtab[9] != 24:
            raise ValueError('Unsupported symbol size')
        for at in range(symtab[4], symtab[4] + symtab[5], 24):
            _, info, _, section, address, size = struct.unpack_from('<IBBHQQ', data, at)
            if info & 15 in (2, 10) and 0 < section < len(sh) and size and sh[section][1][2] & 4:
                result.append((address, address + size))
    return result


def generate(path):
    data = path.read_bytes()
    sh = sections(data)
    mini = next((s for name, s in sh if name == '.gnu_debugdata'), None)
    if mini is None and not any(section[1] == 2 for _, section in sh):
        return False
    ranges = functions(data)
    if mini is not None:
        debug = lzma.decompress(data[mini[4]:mini[4] + mini[5]], memlimit=128 << 20)
        ranges += functions(debug)
    # PLT slots are generated code and do not have individual function symbols.
    for name, s in sh:
        if name in ('.plt', '.plt.got', '.plt.sec', '.init', '.fini') and s[2] & 4:
            ranges.append((s[3], s[3] + s[5]))
    merged = []
    for _, section in sorted(sh, key=lambda item: item[1][3]):
        if section[2] & 6 != 6:
            continue
        start, stop = section[3], section[3] + section[5]
        local = []
        for begin, end in sorted(ranges):
            begin, end = max(begin, start), min(end, stop)
            if begin >= end:
                continue
            if local and begin <= local[-1][1]:
                local[-1] = (local[-1][0], max(end, local[-1][1]))
            else:
                local.append((begin, end))
        merged.extend(local)
    if not merged:
        raise ValueError('No code symbols')
    text = 'AXRB_X64_CODE_V1 ' + hashlib.sha256(data).hexdigest() + '\n'
    text += ''.join(f'{begin:x} {end:x}\n' for begin, end in merged)
    path.with_name(path.name + '.axrb-code').write_text(text, encoding='ascii')
    return True


if __name__ == '__main__':
    for arg in sys.argv[1:]:
        path = Path(arg)
        print(path, 'metadata written' if generate(path) else 'no mini-debug metadata')
