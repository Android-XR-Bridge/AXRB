"""Lists the framework/library classes, methods and fields an APK's DEX code
references that AXRB Direct's boot class path does not define.

usage: dexgap.py APK BOOT_DEX
"""
import struct
import sys
import zipfile


class Dex:
    def __init__(self, data):
        self.d = data
        u = lambda o: struct.unpack_from('<I', data, o)[0]
        self.sid_n, self.sid_o = u(56), u(60)
        self.tid_n, self.tid_o = u(64), u(68)
        self.pid_o = u(76)
        self.fid_n, self.fid_o = u(80), u(84)
        self.mid_n, self.mid_o = u(88), u(92)
        self.cd_n, self.cd_o = u(96), u(100)

    def uleb(self, o):
        r = s = 0
        while True:
            b = self.d[o]
            o += 1
            r |= (b & 0x7f) << s
            s += 7
            if b < 0x80:
                return r, o

    def string(self, i):
        off = struct.unpack_from('<I', self.d, self.sid_o + 4 * i)[0]
        _, o = self.uleb(off)
        return self.d[o:self.d.index(b'\0', o)].decode('utf-8', 'replace')

    def type(self, i):
        return self.string(struct.unpack_from('<I', self.d, self.tid_o + 4 * i)[0])

    def proto(self, i):
        shorty, ret, params = struct.unpack_from('<III', self.d, self.pid_o + 12 * i)
        ps = []
        if params:
            n = struct.unpack_from('<I', self.d, params)[0]
            ps = [self.type(struct.unpack_from('<H', self.d, params + 4 + 2 * k)[0]) for k in range(n)]
        return '(' + ''.join(ps) + ')' + self.type(ret)

    def method(self, i):
        c, p, n = struct.unpack_from('<HHI', self.d, self.mid_o + 8 * i)
        return self.type(c), self.string(n), self.proto(p)

    def field(self, i):
        c, t, n = struct.unpack_from('<HHI', self.d, self.fid_o + 8 * i)
        return self.type(c), self.string(n), self.type(t)

    def classes(self):
        """{descriptor: (superclass, interfaces, set of (name, sig) methods, set of field names)}"""
        out = {}
        for i in range(self.cd_n):
            o = self.cd_o + 32 * i
            cls, _, sup, ifaces, _, _, data, _ = struct.unpack_from('<IIIIIIII', self.d, o)
            name = self.type(cls)
            superclass = self.type(sup) if sup != 0xffffffff else None
            interfaces = []
            if ifaces:
                n = struct.unpack_from('<I', self.d, ifaces)[0]
                interfaces = [self.type(struct.unpack_from('<H', self.d, ifaces + 4 + 2 * k)[0]) for k in range(n)]
            methods, fields = set(), set()
            if data:
                o = data
                sf, o = self.uleb(o)
                inf, o = self.uleb(o)
                dm, o = self.uleb(o)
                vm, o = self.uleb(o)
                idx = 0
                for _ in range(sf + inf):
                    d, o = self.uleb(o)
                    _, o = self.uleb(o)
                    idx = idx + d if _ != -1 else idx
                for k, count in enumerate((sf, inf)):
                    pass
                # re-walk fields properly
                o = data
                sf, o = self.uleb(o); inf, o = self.uleb(o); dm, o = self.uleb(o); vm, o = self.uleb(o)
                for count in (sf, inf):
                    idx = 0
                    for _ in range(count):
                        d, o = self.uleb(o)
                        idx += d
                        _, o = self.uleb(o)
                        fields.add(self.field(idx)[1])
                for count in (dm, vm):
                    idx = 0
                    for _ in range(count):
                        d, o = self.uleb(o)
                        idx += d
                        _, o = self.uleb(o)
                        _, o = self.uleb(o)
                        c, n, p = self.method(idx)
                        methods.add((n, p))
            out[name] = (superclass, interfaces, methods, fields)
        return out


def main():
    apk, boot = sys.argv[1], sys.argv[2]
    z = zipfile.ZipFile(apk)
    app = [Dex(z.read(n)) for n in z.namelist() if n.startswith('classes') and n.endswith('.dex')]
    lib = Dex(open(boot, 'rb').read()).classes()
    app_defined = set()
    for d in app:
        app_defined |= set(d.classes())

    def has_method(cls, name, sig, seen=None):
        seen = seen or set()
        if cls in seen or cls not in lib:
            return False
        seen.add(cls)
        sup, ifs, ms, _ = lib[cls]
        if (name, sig) in ms:
            return True
        return any(has_method(c, name, sig, seen) for c in ([sup] if sup else []) + ifs)

    def has_field(cls, name, seen=None):
        seen = seen or set()
        if cls in seen or cls not in lib:
            return False
        seen.add(cls)
        sup, ifs, _, fs = lib[cls]
        return name in fs or any(has_field(c, name, seen) for c in ([sup] if sup else []) + ifs)

    missing_classes, missing = set(), {}
    for d in app:
        for i in range(d.mid_n):
            c, n, p = d.method(i)
            if c in app_defined or c.startswith('['):
                continue
            if c not in lib:
                missing_classes.add(c)
            elif not has_method(c, n, p):
                missing.setdefault(c, set()).add(n + p)
        for i in range(d.fid_n):
            c, n, t = d.field(i)
            if c in app_defined or c.startswith('['):
                continue
            if c not in lib:
                missing_classes.add(c)
            elif not has_field(c, n):
                missing.setdefault(c, set()).add('field ' + n + ':' + t)
        for i in range(d.tid_n):
            t = d.type(i)
            if t.startswith('L') and t not in app_defined and t not in lib:
                missing_classes.add(t)
    print('missing classes (%d):' % len(missing_classes))
    for c in sorted(missing_classes):
        print('  ' + c)
    print('missing members:')
    for c in sorted(missing):
        print('  ' + c)
        for m in sorted(missing[c]):
            print('      ' + m)


main()
