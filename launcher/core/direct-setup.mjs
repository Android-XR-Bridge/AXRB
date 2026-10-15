import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { buildProfile } from './build-profile.mjs';
import { portableOutput } from './portable.mjs';

// No emulator hardware/vendor probe, SDK installation, AVD discovery or ADB calls.
export class DirectSetup {
  constructor({ directory, runtime, select, save, changed, portableRoot,
    recommendedDirectory = directory, recommendedDownloadDirectory = runtime?.settings?.downloadDir,
    totalMemory = () => os.totalmem() }) {
    Object.assign(this, { directory, runtime, select, save, changed, totalMemory, portableRoot });
    this.status = { phase: 'checking', directory, recommendedDirectory, recommendedDownloadDirectory,
      downloadDirectory: runtime?.settings?.downloadDir || recommendedDownloadDirectory,
      directOnly: true, emulatorDisabled: true, active: false };
  }
  environment() { process.env.AXRB_DATA_HOME = path.join(this.directory, 'output'); }
  async inspect() {
    const receipt = await fs.readFile(path.join(this.directory, 'license-acceptance.json'), 'utf8').then(JSON.parse).catch(() => null);
    return { ready: receipt?.license === 'android-sdk-license' };
  }
  async check() {
    const { ready } = await this.inspect();
    const memoryGB = this.totalMemory() / 1024 ** 3;
    this.status = { ...this.status, memoryGB,
      phase: memoryGB < buildProfile.minimumMemoryGB ? 'unsupported' : ready ? 'ready' : 'install', directory: this.directory };
    this.changed();
    return this.status;
  }
  async start({ directory, runtimeDirectory, downloadDirectory, accepted } = {}) {
    if (this.totalMemory() < buildProfile.minimumMemoryGB * 1024 ** 3)
      throw new Error(`AXRB-Emulatorless requires at least ${buildProfile.minimumMemoryGB} GB of system RAM.`);
    let selected = this.directory;
    if (directory) {
      if (typeof directory !== 'string' || !path.isAbsolute(directory) || /[\r\n]/.test(directory)) throw new Error('Choose an absolute data folder.');
      selected = path.join(directory, buildProfile.runtimeDirectory);
    }
    if (runtimeDirectory !== undefined) selected = runtimeDirectory;
    const downloads = downloadDirectory ?? this.status.downloadDirectory;
    for (const [label, value] of [['runtime data', selected], ['game downloads', downloads]]) {
      if (value === undefined && label === 'game downloads') continue;
      if (typeof value !== 'string' || !path.isAbsolute(value) || /[\r\n\0]/.test(value))
        throw new Error(`Choose an absolute ${label} folder.`);
      portableOutput(this.portableRoot, value);
    }
    if (accepted !== true) throw new Error('Accept the Android SDK license to use the Android system libraries.');
    if (downloads) await fs.mkdir(downloads, { recursive: true });
    await this.select(selected);
    await fs.mkdir(selected, { recursive: true });
    await fs.writeFile(path.join(selected, 'license-acceptance.json'), JSON.stringify({ license: 'android-sdk-license', acceptedAt: new Date().toISOString() }));
    this.directory = selected;
    this.environment();
    const previousDownloads = this.runtime?.settings?.downloadDir;
    if (downloads) this.runtime.settings.downloadDir = downloads;
    try { await this.save(selected, new Set()); }
    catch (error) { if (downloads) this.runtime.settings.downloadDir = previousDownloads; throw error; }
    this.status.downloadDirectory = downloads;
    return this.check();
  }
  skipEmulator(options) { return this.start(options); }
  cancel() {}
}
