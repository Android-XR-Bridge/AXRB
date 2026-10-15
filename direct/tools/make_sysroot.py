"""Builds AXRB Direct's Android system root from the SDK's arm64 system image.

AXRB Direct runs the unmodified AOSP arm64 bionic, linker and system libraries
under Dynarmic. They come from Google's Android SDK system image
(sys-img/android/arm64-v8a-36_r02.zip), which the launcher downloads on the
player's machine under the Android SDK license; this script extracts the parts
the runner needs (about 250 MB):

  system/lib64, system/etc, system/bin/linker64, system/usr/share/zoneinfo,
  system/build.prop
  apex/com.android.{runtime,i18n,tzdata}   (unpacked from their .apex payloads)
  vendor-build.prop                        (the vendor partition's build.prop)
  .symlinks.json                           (symbolic links, recreated by the runner)

Usage: make_sysroot.py IMAGE OUTPUT
  IMAGE is the downloaded zip, or the system.img inside it. OUTPUT must not exist.
Only the Python standard library is used (the launcher's embedded Python).
"""

import io
import argparse
import json
import os
import shutil
import struct
import sys
import tempfile
import zipfile

S_IFMT, S_IFDIR, S_IFLNK = 0xF000, 0x4000, 0xA000
FT_REG, FT_DIR, FT_LNK = 1, 2, 7
ROOT_INODE = 2


class Ext4:
    """Read-only access to an ext4 file system inside an image file."""

    def __init__(self, f, base):
        self.f, self.base = f, base
        sb = self.read(1024, 1024)
        (self.magic,) = struct.unpack_from('<H', sb, 56)
        if self.magic != 0xEF53:
            raise ValueError('not an ext4 file system')
        (log_block_size,) = struct.unpack_from('<I', sb, 24)
        self.block_size = 1024 << log_block_size
        (self.inodes_per_group,) = struct.unpack_from('<I', sb, 40)
        (self.inode_size,) = struct.unpack_from('<H', sb, 88)
        (incompat,) = struct.unpack_from('<I', sb, 96)
        (self.first_data_block,) = struct.unpack_from('<I', sb, 20)
        desc_size = struct.unpack_from('<H', sb, 254)[0] if incompat & 0x80 else 32
        self.desc_size = desc_size or 32

    def read(self, offset, size):
        self.f.seek(self.base + offset)
        return self.f.read(size)

    def inode_table(self, group):
        offset = (self.first_data_block + 1) * self.block_size + group * self.desc_size
        d = self.read(offset, self.desc_size)
        (lo,) = struct.unpack_from('<I', d, 8)
        hi = struct.unpack_from('<I', d, 0x28)[0] if self.desc_size >= 64 else 0
        return lo | (hi << 32)

    def inode(self, n):
        group, index = divmod(n - 1, self.inodes_per_group)
        return self.read(self.inode_table(group) * self.block_size + index * self.inode_size, self.inode_size)

    def extents(self, node):
        magic, entries, _, depth = struct.unpack_from('<HHHH', node, 0)
        if magic != 0xF30A:
            raise ValueError('bad extent header')
        out = []
        for k in range(entries):
            o = 12 + k * 12
            if depth == 0:
                logical, length, hi, lo = struct.unpack_from('<IHHI', node, o)
                if length > 32768:  # uninitialized extent
                    length -= 32768
                out.append((logical, length, lo | (hi << 32)))
            else:
                _, lo, hi = struct.unpack_from('<IIH', node, o)
                out += self.extents(self.read((lo | (hi << 32)) * self.block_size, self.block_size))
        return out

    def mode(self, n):
        return struct.unpack_from('<H', self.inode(n), 0)[0]

    def data(self, n):
        ino = self.inode(n)
        (mode,) = struct.unpack_from('<H', ino, 0)
        size = struct.unpack_from('<I', ino, 4)[0] | (struct.unpack_from('<I', ino, 108)[0] << 32)
        (flags,) = struct.unpack_from('<I', ino, 32)
        block = ino[40:100]
        if (mode & S_IFMT) == S_IFLNK and size < 60:
            return block[:size]
        if flags & 0x10000000:  # inline data
            return block[:size]
        if not flags & 0x80000:
            raise ValueError('inode without extents')
        buf = bytearray(size)
        for logical, length, physical in self.extents(block):
            start = logical * self.block_size
            if start >= size:
                continue
            chunk = self.read(physical * self.block_size, length * self.block_size)
            buf[start:start + len(chunk)] = chunk[:size - start]
        return bytes(buf)

    def entries(self, n):
        d, o, out = self.data(n), 0, {}
        while o < len(d):
            ino, rec_len, name_len, file_type = struct.unpack_from('<IHBB', d, o)
            if rec_len == 0:
                break
            if ino:
                out[d[o + 8:o + 8 + name_len].decode('utf-8', 'replace')] = (ino, file_type)
            o += rec_len
        return out

    def lookup(self, path):
        n = ROOT_INODE
        for part in [p for p in path.split('/') if p]:
            n = self.entries(n)[part][0]
        return n


def logical_partitions(f):
    """Offsets of the logical partitions in the image's GPT 'super' partition."""
    f.seek(512)
    header = f.read(92)
    (table_lba,) = struct.unpack_from('<Q', header, 72)
    count, entry_size = struct.unpack_from('<II', header, 80)
    f.seek(table_lba * 512)
    gpt = {}
    for _ in range(count):
        e = f.read(entry_size)
        if e[:16] == b'\0' * 16:
            continue
        (first,) = struct.unpack_from('<Q', e, 32)
        gpt[e[56:128].decode('utf-16le').rstrip('\0')] = first * 512
    super_start = gpt['super']
    f.seek(super_start + 4096 * 3)  # primary liblp metadata
    meta = f.read(256)
    magic, _, _, header_size = struct.unpack_from('<IHHI', meta, 0)
    if magic != 0x414C5030:
        raise ValueError('no logical partition metadata')
    (tables_size,) = struct.unpack_from('<I', meta, 44)
    partitions, extents = struct.unpack_from('<III', meta, 80), struct.unpack_from('<III', meta, 92)
    f.seek(super_start + 4096 * 3 + header_size)
    tables = f.read(tables_size)
    out = {}
    for i in range(partitions[1]):
        o = partitions[0] + i * partitions[2]
        name = tables[o:o + 36].rstrip(b'\0').decode()
        _, first_extent, _, _ = struct.unpack_from('<IIII', tables, o + 36)
        _, _, sector, _ = struct.unpack_from('<QIQI', tables, extents[0] + first_extent * extents[2])
        out[name] = super_start + sector * 512
    return out


class Builder:
    SKIP = {'.', '..', 'lost+found'}
    SKIP_ETC = {'vintf', 'selinux', 'permissions', 'init', 'bpf'}

    def __init__(self, out):
        self.out, self.links = out, {}

    def host(self, guest):
        return os.path.join(self.out, *[p for p in guest.split('/') if p])

    def write(self, guest, data):
        path = self.host(guest)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'wb') as f:
            f.write(data)

    def copy_tree(self, fs, node, guest, skip=()):
        os.makedirs(self.host(guest), exist_ok=True)
        for name, (ino, file_type) in fs.entries(node).items():
            if name in self.SKIP or name in skip:
                continue
            child = guest + '/' + name
            if file_type == FT_DIR:
                self.copy_tree(fs, ino, child)
            elif file_type == FT_LNK:
                self.links[child] = fs.data(ino).decode()
            elif file_type == FT_REG:
                self.write(child, fs.data(ino))

    def copy(self, fs, path):
        n = fs.lookup(path)
        kind = fs.mode(n) & S_IFMT
        if kind == S_IFDIR:
            self.copy_tree(fs, n, '/' + path, skip=self.SKIP_ETC)
        elif kind == S_IFLNK:
            self.links['/' + path] = fs.data(n).decode()
        else:
            self.write('/' + path, fs.data(n))


def build(image, out, art=False):
    builder = Builder(out)
    with open(image, 'rb') as f:
        parts = logical_partitions(f)
        system = Ext4(f, parts['system'])
        for path in ['system/lib64', 'system/etc', 'system/bin/linker64', 'system/usr/share/zoneinfo',
                     'system/build.prop']:
            builder.copy(system, path)
        apex_names = ['com.android.runtime', 'com.android.i18n', 'com.android.tzdata']
        available = system.entries(system.lookup('system/apex'))
        if art:
            # Framework JNI dependencies and bootclasspath fragments come from
            # several APEX modules; retain the image's matched set together.
            apex_names = sorted(name.rsplit('.', 1)[0] for name in available if name.endswith(('.apex', '.capex')))
            for path in ['system/framework', 'system/bin', 'system/fonts', 'system/usr']:
                builder.copy(system, path)
            builder.copy_tree(system, system.lookup('system/etc'), '/system/etc')
        for apex in apex_names:
            compressed = f'{apex}.apex' not in available
            name = f'{apex}.capex' if compressed else f'{apex}.apex'
            contents = system.data(system.lookup(f'system/apex/{name}'))
            if compressed:
                with zipfile.ZipFile(io.BytesIO(contents)) as capex:
                    contents = capex.read('original_apex')
            payload = zipfile.ZipFile(io.BytesIO(contents))
            with tempfile.TemporaryFile() as tmp:
                with payload.open('apex_payload.img') as src:
                    shutil.copyfileobj(src, tmp)
                builder.copy_tree(Ext4(tmp, 0), ROOT_INODE, '/apex/' + apex)
        vendor = Ext4(f, parts['vendor'])
        with open(os.path.join(out, 'vendor-build.prop'), 'wb') as prop:
            prop.write(vendor.data(vendor.lookup('build.prop')))
    with open(os.path.join(out, '.symlinks.json'), 'w') as f:
        json.dump(builder.links, f, indent=1)
    return len(builder.links)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image')
    parser.add_argument('output')
    parser.add_argument('--art', action='store_true', help='Include the matched ART, framework, fonts and APEX modules for Direct mode')
    args = parser.parse_args()
    source, out = args.image, args.output
    if os.path.exists(out):
        sys.exit(f'{out} already exists')
    os.makedirs(out)
    if zipfile.is_zipfile(source):
        # The image is 1.9 GB uncompressed; stage it beside the output.
        staged = out + '.system.img'
        with zipfile.ZipFile(source) as z, z.open('arm64-v8a/system.img') as src, open(staged, 'wb') as dst:
            shutil.copyfileobj(src, dst, 16 << 20)
        try:
            links = build(staged, out, art=args.art)
        finally:
            os.remove(staged)
    else:
        links = build(source, out, art=args.art)
    print(f'system root written to {out} ({links} symlinks)')


if __name__ == '__main__':
    main()
