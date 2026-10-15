"""Extract a separate developer x86_64 Android/ART sysroot; no VM is started.

Uses an already installed/licensed SDK image. Does not change the release ARM
sysroot, launcher configuration or Android Emulator backend. Google API images
can store compressed APEX containers with an .apex extension; inspect contents.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import shutil
import struct
import sys
import tempfile
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from make_sysroot import Builder, Ext4, logical_partitions, ROOT_INODE
from code_ranges import generate as generate_code_ranges

def varint(data, offset):
    value = 0
    for shift in range(0, 64, 7):
        if offset >= len(data):
            raise ValueError('Truncated APEX manifest')
        byte = data[offset]; offset += 1
        value |= (byte & 127) << shift
        if not byte & 128:
            return value, offset
    raise ValueError('Invalid APEX manifest varint')

def module_name(manifest):
    # apex_manifest.proto field 1 is the canonical mount name, which can differ
    # from the image's com.google.* filename. Skip other protobuf wire fields.
    offset = 0
    while offset < len(manifest):
        tag, offset = varint(manifest, offset)
        if tag & 7 == 2:
            size, offset = varint(manifest, offset)
            if size > len(manifest) - offset:
                raise ValueError('Truncated APEX manifest field')
            value = manifest[offset:offset + size]; offset += size
            if tag >> 3 == 1:
                name = value.decode('ascii')
                if not re.fullmatch(r'[A-Za-z0-9_]+(?:\.[A-Za-z0-9_]+)+', name):
                    raise ValueError('Invalid APEX mount name')
                return name
        elif tag & 7 == 0:
            _, offset = varint(manifest, offset)
        elif tag & 7 in (1, 5):
            offset += 8 if tag & 7 == 1 else 4
        else:
            raise ValueError('Unsupported APEX manifest wire type')
    raise ValueError('APEX module name missing')

def open_apex(contents):
    for _ in range(3):
        archive = zipfile.ZipFile(io.BytesIO(contents))
        if 'apex_payload.img' in archive.namelist():
            return archive
        if 'original_apex' not in archive.namelist():
            raise ValueError('APEX has neither payload nor compressed original')
        contents = archive.read('original_apex')
    raise ValueError('Too many nested APEX containers')

def build(source, output):
    if output.exists():
        raise ValueError('Output must be a new directory; existing sysroots are never overwritten')
    # Validate the target architecture before creating the output.
    with source.open('rb') as f:
        parts = logical_partitions(f)
        system = Ext4(f, parts['system'])
        app = system.data(system.lookup('system/bin/app_process64'))
        if app[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', app, 18)[0] != 62:
            raise ValueError('The selected image is not x86_64 Android')
        output.mkdir(parents=True)
        builder = Builder(str(output))
        for path in ['system/lib64', 'system/bin', 'system/framework', 'system/fonts', 'system/usr', 'system/build.prop']:
            builder.copy(system, path)
        builder.copy_tree(system, system.lookup('system/etc'), '/system/etc')
        modules = []
        for filename in sorted(system.entries(system.lookup('system/apex'))):
            if not filename.endswith(('.apex', '.capex')):
                continue
            with open_apex(system.data(system.lookup('system/apex/' + filename))) as archive:
                name = module_name(archive.read('apex_manifest.pb'))
                if name in modules:
                    raise ValueError('Duplicate APEX mount: ' + name)
                with tempfile.TemporaryFile() as payload:
                    with archive.open('apex_payload.img') as src:
                        shutil.copyfileobj(src, payload)
                    builder.copy_tree(Ext4(payload, 0), ROOT_INODE, '/apex/' + name)
                modules.append(name)
        if 'vendor' in parts:
            vendor = Ext4(f, parts['vendor'])
            builder.write('/vendor-build.prop', vendor.data(vendor.lookup('build.prop')))
    (output / '.symlinks.json').write_text(json.dumps(builder.links, indent=2))
    checks = {}
    for relative in ['system/bin/app_process64', 'apex/com.android.art/bin/dalvikvm64',
                     'apex/com.android.art/lib64/libart.so', 'apex/com.android.runtime/bin/linker64',
                     'apex/com.android.runtime/lib64/bionic/libc.so']:
        data = (output / relative).read_bytes()
        if data[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', data, 18)[0] != 62:
            raise ValueError('Expected x86_64 ELF: ' + relative)
        checks[relative] = hashlib.sha256(data).hexdigest()
    code_count = 0
    for file in output.rglob('*'):
        if file.is_file() and file.suffix != '.axrb-code':
            with file.open('rb') as stream:
                header = stream.read(20)
            if header[:6] == b'\x7fELF\x02\x01' and struct.unpack_from('<H', header, 18)[0] == 62:
                code_count += bool(generate_code_ranges(file))
    metadata = dict(architecture='x86_64', elfMachine=62, source=str(source.resolve()), codeMetadataFiles=code_count,
                    experimental=True, hypervisor=False, modules=modules, sha256=checks)
    (output / '.axrb-x64-sysroot.json').write_text(json.dumps(metadata, indent=2))
    print('Prepared x86_64 Android/ART sysroot:', output)
    print('Validated', len(checks), 'native ELF binaries and', len(modules), 'canonical APEX mounts')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    build(args.image, args.output)
