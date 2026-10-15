// Verify the copied application payload too, before it goes into the installer.
const verify = require('./verify-release.cjs');
const path = require('node:path');
const { promisify } = require('node:util');
const execFile = promisify(require('node:child_process').execFile);
module.exports = async context => {
  await verify(null, context.appOutDir);
  const { buildProfile } = await import('./core/build-profile.mjs');
  if (!buildProfile.emulatorEnabled) {
    const fs = require('node:fs/promises');
    for (const name of ['scripts/emulator/windows_android_emulator.ps1', 'scripts/run/run_windows_game.ps1', 'out/clock/Release/axrb_clock_launcher.exe']) {
      const present = await fs.access(path.join(context.appOutDir, 'resources/runtime', name)).then(() => true, () => false);
      if (present) throw new Error('Emulator component leaked into Emulatorless package: ' + name);
    }
    if (context.packager.config.appId !== 'org.axrb.emulatorless') throw new Error('Emulatorless package must use its own install identity.');
    if (context.packager.config.publish?.length) throw new Error('The old update channel must remain disabled.');
    console.log('Independent Emulatorless identity and excluded emulator payload verified.');
  }
  await require('./verify-nsis-extraction.cjs')(context);
  const root = path.join(context.appOutDir, 'resources/runtime');
  // Exercise the shipped reader with only Windows on PATH, no SDK or Java.
  const env = Object.fromEntries(Object.entries(process.env).filter(([key]) => !/^(path|android_home|android_sdk_root|java_home|pythonpath|pythonhome)$/i.test(key)));
  env.PATH = `${process.env.SystemRoot}\\System32;${process.env.SystemRoot}`;
  const { stdout } = await execFile(path.join(root, 'tools/aapt2/aapt2.exe'), ['dump', 'badging',
    path.join(root, 'out/android/runtime-arm64-v8a/axrb-openxr-runtime-debug.apk')], { env, windowsHide: true });
  if (!/^package: name='com.axrb.openxrruntime'/m.test(stdout)) throw new Error('Bundled APK reader failed its SDK-free smoke test.');
  await execFile(path.join(root, 'tools/python/python.exe'), ['-c',
    'from android_tools import find_aapt2; import subprocess; subprocess.run([str(find_aapt2()), "version"], check=True)'], { env, windowsHide: true });
  await execFile(path.join(root, 'tools/python/python.exe'),
    [path.join(root, 'direct/tools/prepare_art_session.py'), '--help'], { env, windowsHide: true });
  console.log('Bundled APK reader verified without SDK, Java or system Python on PATH.');
};
