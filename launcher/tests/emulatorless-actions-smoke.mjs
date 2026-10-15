import fs from 'node:fs/promises';
import path from 'node:path';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import assert from 'node:assert/strict';

// Production IPC must reject emulator calls even with forged/stale settings.
export async function libraryActionsSmoke(window, { state, runtime, dialog, persist, publicState }) {
  const js = code => window.webContents.executeJavaScript(code);
  const game = { id: 'emulatorless-smoke', package: 'com.axrb.storageprobe', name: 'Direct smoke', source: 'local', apk: 'C:/fixture/game.apk', patched: true, installed: true };
  state.put(game); await persist();
  window.webContents.send('axrb:changed', publicState());
  await js(`document.querySelector('[data-nav="library"]').click()`);
  const until = async code => {
    for (let i = 0; i < 100; ++i) {
      if (await js(code)) return;
      await new Promise(resolve => setTimeout(resolve, 50));
    }
    throw new Error(code);
  };
  await until(`Boolean(document.querySelector('[data-game="${game.id}"]'))`);
  await js(`document.querySelector('[data-game="${game.id}"]').click()`);
  await until(`Boolean(document.querySelector('[data-direct-play]'))`);
  assert.equal(await js(`Array.from(document.querySelectorAll('#details button')).some(b => ['Install', 'Play', 'Quest Bridge'].includes(b.textContent))`), false);
  for (const expression of ["startAndroid()", "stopAndroid()", "setupResume()", "uninstall('emulatorless-smoke')", "play('emulatorless-smoke', 'android')", "play('emulatorless-smoke', 'quest-bridge')"]) {
    const result = await js(`window.axrb.${expression}`);
    assert.equal(result.ok, false, expression);
    assert.match(result.error, /disabled in this build/, expression);
  }
  const archive = path.join(state.directory, 'emulatorless-import.zip');
  const original = { open: dialog.showOpenDialog, inspect: runtime.inspect, install: runtime.install, downloads: state.data.settings.downloadDir };
  const importedPackage = 'com.axrb.zipprobe';
  try {
    await promisify(execFile)('python', ['-c', 'import sys,zipfile; z=zipfile.ZipFile(sys.argv[1],"w"); z.writestr("base.apk","fixture"); z.close()', archive], { windowsHide: true });
    state.data.settings.downloadDir = path.join(state.directory, 'emulatorless-downloads');
    dialog.showOpenDialog = async () => ({ canceled: false, filePaths: [archive] });
    runtime.inspect = async file => ({ id: 'local:' + importedPackage, package: importedPackage, name: 'Direct ZIP probe', activity: importedPackage + '/.Main', apk: file, source: 'local', patched: true });
    runtime.install = async () => { throw new Error('ZIP import must not invoke the emulator'); };
    const result = await js('window.axrb.importZip()');
    assert.equal(result.ok, true, result.error);
    const deadline = Date.now() + 10000;
    let job;
    do {
      job = state.data.jobs.find(j => j.id === result.value);
      if (['complete', 'failed'].includes(job?.status)) break;
      await new Promise(resolve => setTimeout(resolve, 50));
    } while (Date.now() < deadline);
    assert.equal(job?.status, 'complete', job?.error);
    assert.equal(job.stage, 'Imported for direct mode');
    const imported = state.data.games.find(g => g.package === importedPackage);
    assert.equal(imported.installed, false);
    assert.equal(await fs.readFile(imported.apk, 'utf8'), 'fixture');
  } finally {
    dialog.showOpenDialog = original.open; runtime.inspect = original.inspect; runtime.install = original.install;
    state.data.settings.downloadDir = original.downloads;
    state.data.games = state.data.games.filter(item => item.package !== importedPackage);
  }
  state.data.games = state.data.games.filter(item => item.id !== game.id);
  await persist();
  console.log('Emulatorless actions smoke passed: Direct APK action, ZIP import without emulator installation, and production IPC emulator lockout.');
}
