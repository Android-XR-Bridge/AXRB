import fs from 'node:fs/promises';
import path from 'node:path';

// Deliberately exact package names: never derive removal targets from a prefix,
// process name or renderer input. Framework/APEX, WebView, input, permissions,
// networking, media, launcher and AXRB/game packages remain installed.
export const OPTIONAL_PACKAGES = Object.freeze([
  'com.google.android.gms', 'com.google.android.gsf', 'com.android.vending',
  'com.google.android.gms.supervision', 'com.google.android.googlequicksearchbox',
  'com.google.android.apps.wellbeing', 'com.google.android.as', 'com.google.android.as.oss',
  'com.google.android.settings.intelligence', 'com.google.android.apps.safetyhub',
  'com.google.android.calendar', 'com.google.android.contacts', 'com.google.android.gm',
  'com.google.android.deskclock', 'com.google.android.apps.messaging', 'com.google.android.dialer',
  'com.google.android.apps.maps', 'com.google.android.apps.photos', 'com.google.android.apps.docs',
  'com.google.android.youtube', 'com.google.android.apps.youtube.music', 'com.android.chrome',
  'com.google.android.apps.wallpaper', 'com.google.android.apps.customization.pixel',
  'com.android.dreams.basic', 'com.android.wallpaper.livepicker', 'com.android.wallpaperbackup',
  'com.android.camera2', 'com.google.android.markup', 'com.google.android.avatarpicker',
  'com.android.printspooler', 'com.android.bips', 'com.google.android.printservice.recommendation',
  'com.google.android.projection.gearhead', 'com.google.android.feedback',
  'com.google.android.apps.restore', 'com.google.android.partnersetup',
  'com.google.android.onetimeinitializer', 'com.android.egg', 'com.android.bookmarkprovider',
  'com.android.providers.partnerbookmarks',
]);
const allowed = new Set(OPTIONAL_PACKAGES);
export function packages(output) {
  return new Set(String(output).split(/\r?\n/).filter(s => s.startsWith('package:')).map(s => s.slice(8).trim()));
}
export class Debloat {
  constructor(runtime, directory, progress = () => {}) {
    this.runtime = runtime; this.file = path.join(directory, 'axrb-debloat.json'); this.progress = progress;
  }
  async read() {
    try {
      const value = JSON.parse(await fs.readFile(this.file, 'utf8'));
      if (value.version !== 1 || !Array.isArray(value.packages) || value.packages.some(p => !allowed.has(p))) throw new Error('Invalid debloat recovery record.');
      return value;
    } catch (error) { if (error.code === 'ENOENT') return { version: 1, packages: [] }; throw error; }
  }
  async save(value) {
    await fs.mkdir(path.dirname(this.file), { recursive: true });
    await fs.writeFile(this.file + '.tmp', JSON.stringify(value, null, 2));
    await fs.rename(this.file + '.tmp', this.file);
  }
  async check() {
    if (this.runtime.child) throw new Error('Close the game before changing Android apps.');
    const hardware = (await this.runtime.adb(['shell', 'getprop', 'ro.hardware'])).trim();
    const api = (await this.runtime.adb(['shell', 'getprop', 'ro.build.version.sdk'])).trim();
    if (hardware !== 'ranchu' || api !== '36') throw new Error('Debloat is supported only on the Android 16 emulator.');
    if ((await this.runtime.adb(['shell', 'getprop', 'sys.boot_completed'])).trim() !== '1') throw new Error('Wait for Android to finish starting.');
    const thirdParty = packages(await this.runtime.adb(['shell', 'pm', 'list', 'packages', '-3', '--user', '0']));
    const processes = await this.runtime.adb(['shell', 'ps', '-A', '-o', 'NAME']);
    if (processes.split(/\r?\n/).some(n => thirdParty.has(n.trim().split(':')[0]) && !n.trim().startsWith('com.axrb.'))) throw new Error('Close running Android games before changing Android apps.');
  }
  async preview() {
    await this.check();
    const installed = packages(await this.runtime.adb(['shell', 'pm', 'list', 'packages', '-s', '--user', '0']));
    return { removable: OPTIONAL_PACKAGES.filter(p => installed.has(p)), restore: (await this.read()).packages };
  }
  async apply(restore = false) {
    const preview = await this.preview(), journal = await this.read();
    const targets = restore ? [...journal.packages] : preview.removable;
    const failures = []; let completed = 0;
    for (const name of targets) {
      this.progress({ completed, total: targets.length, package: name, restore });
      // Write intent before mutation, so interruption cannot lose restoration.
      if (!restore && !journal.packages.includes(name)) { journal.packages.push(name); await this.save(journal); }
      try {
        const output = await this.runtime.adb(restore
          ? ['shell', 'cmd', 'package', 'install-existing', '--user', '0', name]
          : ['shell', 'pm', 'uninstall', '-k', '--user', '0', name], { timeout: 60000 });
        if (/Failure|Error:/i.test(output) || (!restore && !/Success/.test(output))) throw new Error(output.trim());
        const installed = packages(await this.runtime.adb(['shell', 'pm', 'list', 'packages', '--user', '0', name]));
        if (installed.has(name) !== restore) throw new Error('Android did not confirm the package change.');
        if (restore) { journal.packages = journal.packages.filter(p => p !== name); await this.save(journal); }
      } catch (error) { failures.push({ package: name, error: error.message }); }
      completed++;
    }
    const result = { completed, total: targets.length, failures, restore };
    this.progress(result); return result;
  }
}
