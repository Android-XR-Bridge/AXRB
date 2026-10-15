import pathlib, subprocess, sys, tempfile, hashlib, struct
runner, fixture = map(pathlib.Path, sys.argv[1:])
def run(path, mode='--run-probe'):
    return subprocess.run([str(runner),mode,str(path)],capture_output=True,text=True,timeout=15)
inspection=run(fixture,'--inspect')
assert inspection.returncode==0,inspection.stderr
if 'FSGSBASE=no' in inspection.stdout:
    print('SKIP native execution: this Windows host does not expose FSGSBASE')
    sys.exit(77)
result=run(fixture)
print(result.stdout, end=''); print(result.stderr, end='')
assert result.returncode==0 and 'NATIVE_X64_PROBE_PASS' in result.stdout,result.returncode
with tempfile.TemporaryDirectory(prefix='axrb-x64-elf-') as directory:
    path=pathlib.Path(directory)/'bad.elf'
    original=fixture.read_bytes()
    for name, data in [('truncated',original[:30]),('arm64',original[:18]+b'\xb7\0'+original[20:]),
                       ('unapproved',original.replace(b'AXRB_NATIVE_PROBE_V1',b'NOT_APPROVED_PROBE__'))]:
        path.write_bytes(data)
        result=run(path)
        assert result.returncode!=0,(name,result.stdout)
        print('Rejected:',name)
    path.write_bytes(original)
    header=struct.unpack_from('<16sHHIQQQIHHHHHH',original)
    sections=[struct.unpack_from('<IIQQQQIIQQ',original,header[6]+64*i) for i in range(header[12])]
    ranges=sorted((s[3],s[3]+s[5]) for s in sections if s[2]&6==6 and s[5])
    prefix='AXRB_X64_CODE_V1 '+hashlib.sha256(original).hexdigest()+'\n'
    body=''.join(f'{a:x} {b:x}\n' for a,b in ranges)
    sidecar=path.with_name(path.name+'.axrb-code')
    sidecar.write_text(prefix+body)
    result=run(path)
    assert result.returncode==0,(result.stdout,result.stderr)
    for name,data in [('hash',prefix.replace(hashlib.sha256(original).hexdigest(),'0'*64)+body),
                      ('truncated',prefix+body+'1234'),('outside',prefix+'0 1\n'),
                      ('overlap',prefix+body+body)]:
        sidecar.write_text(data)
        result=run(path)
        assert result.returncode!=0,(name,result.stdout)
        print('Rejected code metadata:',name)
