"""Install the pinned Refract translator into the isolated AXRB test AVD only."""
import argparse, hashlib, pathlib, subprocess, time
p=argparse.ArgumentParser()
p.add_argument('--adb',required=True)
p.add_argument('--bundle',required=True)
p.add_argument('--serial',default='emulator-5586')
a=p.parse_args()
bundle=pathlib.Path(a.bundle).resolve()
def adb(*args,timeout=60):
 r=subprocess.run([a.adb,'-P','5038','-s',a.serial,*args],capture_output=True,text=True,timeout=timeout)
 if r.returncode:raise RuntimeError(r.stderr or r.stdout)
 return r.stdout.strip()
def sh(s):return adb('shell',s)
if 'axrb-digitalis-test' not in adb('emu','avd','name').splitlines():
 raise SystemExit('Refusing to change any AVD except axrb-digitalis-test')
expected='b4dc5a2998c93b96c749dabb4749cdf55415eb824cebfd5a8adb811ca24d14ff'
lib=bundle/'system/lib64/libberberis_arm64.so'
if hashlib.sha256(lib.read_bytes()).hexdigest()!=expected:raise SystemExit('Unexpected translator build')
for row in (bundle/'SHA256SUMS').read_text().splitlines():
 if not row.strip():continue
 digest,name=row.split(maxsplit=1);file=(bundle/name.lstrip('*')).resolve()
 if not file.is_relative_to(bundle) or hashlib.sha256(file.read_bytes()).hexdigest()!=digest:
  raise SystemExit('Bundle checksum mismatch: '+name)
print('Verified Refract Digitalis bundle.',flush=True)
print(adb('root'));adb('wait-for-device')
remount=adb('remount')
print(remount,flush=True)
needs_reboot='reboot' in remount.lower()
try:sh('touch /system/.axrb-digitalis-write-test && rm /system/.axrb-digitalis-write-test')
except RuntimeError:needs_reboot=True
if needs_reboot:
 adb('reboot');time.sleep(5)
 deadline=time.monotonic()+180
 while time.monotonic()<deadline:
  try:
   if sh('getprop sys.boot_completed')=='1':break
  except (RuntimeError,subprocess.TimeoutExpired):pass
  time.sleep(3)
 else:raise SystemExit('Test Android did not boot after enabling writable overlay')
 print(adb('root'));adb('wait-for-device');print(adb('remount'),flush=True)
sh('touch /system/.axrb-digitalis-write-test && rm /system/.axrb-digitalis-write-test')
if '16/' not in sh('getprop ro.build.fingerprint'):raise SystemExit('Android 16 test image required')
# Back up only inside the isolated test disk. Never alter shared host SDK images.
sh('test -f /system/build.prop.axrb-stock || cp -p /system/build.prop /system/build.prop.axrb-stock')
sh('test -d /system/lib64/arm64.axrb-stock || cp -a /system/lib64/arm64 /system/lib64/arm64.axrb-stock')
sh('test -d /system/bin/arm64.axrb-stock || cp -a /system/bin/arm64 /system/bin/arm64.axrb-stock')
sh('test -f /system/etc/ld.config.arm64.txt.axrb-stock || cp -p /system/etc/ld.config.arm64.txt /system/etc/ld.config.arm64.txt.axrb-stock')
for directory in ['lib64','bin']:
 for file in sorted((bundle/'system'/directory).iterdir()):
  print('Installing',file.name,flush=True)
  adb('push',str(file),'/system/'+directory+'/',timeout=120)
adb('push',str(bundle/'system/etc/ld.config.arm64.txt'),'/system/etc/ld.config.arm64.txt')
# Overlayfs userxattr lookup requires readable directories for app UIDs.
sh('chmod 755 /system/bin/arm64 /system/lib64/arm64; find /system/lib64/arm64 -type d -exec chmod 755 {} \\;')
sh('chmod 644 /system/lib64/arm64/* /system/lib64/libberberis_*.so /system/etc/ld.config.arm64.txt; chmod 755 /system/bin/arm64/* /system/bin/berberis_program_runner*')
sh("sed -i 's/^ro.dalvik.vm.native.bridge=.*/ro.dalvik.vm.native.bridge=libberberis_arm64.so/' /system/build.prop")
sh('restorecon -RF /system/lib64/arm64 /system/bin/arm64 /system/etc/ld.config.arm64.txt /system/lib64/libberberis* /system/bin/berberis_program_runner* /system/build.prop')
actual=sh('sha256sum /system/lib64/libberberis_arm64.so').split()[0]
if actual!=expected:raise SystemExit('Guest translator checksum mismatch')
sh('sync')
print('Digitalis installed in isolated AVD; reboot required.',flush=True)
adb('reboot')
