import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';

// Exercise real IPC, ZIP writing, batch import and state updates. Only native
// dialogs, APK inspection and the Android install boundary use fixtures.
export async function gameExportSmoke(window, { state, runtime, dialog, persist }) {
  const directory = await fs.mkdtemp(path.join(state.directory, 'export-smoke-'));
  const original = { save: dialog.showSaveDialog, open: dialog.showOpenDialog, inspect: runtime.inspect, install: runtime.install,
    games: structuredClone(state.data.games), jobs: structuredClone(state.data.jobs), downloads: state.data.settings.downloadDir };
  const invoke = async (name, ...args) => {
    const result = await window.webContents.executeJavaScript(`window.axrb[${JSON.stringify(name)}](...${JSON.stringify(args)})`);
    assert.equal(result.ok, true, result.error);
    return result.value;
  };
  const waitJob = async id => {
    const deadline = Date.now() + 10000;
    while (Date.now() < deadline) {
      const job = state.data.jobs.find(job => job.id === id);
      if (job?.status === 'failed') throw Error(job.error);
      if (job?.status === 'complete') return;
      await new Promise(resolve => setTimeout(resolve, 50));
    }
    throw Error('Export/import job timed out');
  };
  try {
    const games = [];
    for (const name of ['one', 'two']) {
      const game = { id: `local:com.export.${name}`, package: `com.export.${name}`, name: `Export ${name}`, versionCode: '1', split: '', source: 'local', apk: path.join(directory, `${name}.apk`) };
      await fs.writeFile(game.apk, JSON.stringify(game));
      state.put(game); games.push(game);
    }
    runtime.inspect = async file => JSON.parse(await fs.readFile(file, 'utf8'));
    const output = path.join(directory, 'games.zip');
    games[0].name = 'Batman: Arkham Shadow';
    state.put(games[0]);
    dialog.showSaveDialog = async (_window, options) => {
      assert.equal(path.basename(options.defaultPath), 'Batman- Arkham Shadow.zip');
      return { canceled: false, filePath: path.join(directory, 'single.zip') };
    };
    await waitJob(await invoke('exportGames', [games[0].id]));
    dialog.showSaveDialog = async () => ({ canceled: false, filePath: output });
    await waitJob(await invoke('exportGames', games.map(game => game.id)));
    assert.ok((await fs.stat(output)).size > 0);
    state.data.games = state.data.games.filter(game => !games.some(item => item.id === game.id));
    state.data.settings.downloadDir = path.join(directory, 'imported');
    dialog.showOpenDialog = async () => ({ canceled: false, filePaths: [output] });
    const installed = [];
    runtime.install = async game => { installed.push(game.package); assert.ok((await fs.stat(game.apk)).isFile()); };
    await waitJob(await invoke('importZip'));
    assert.deepEqual(installed, games.map(game => game.package));
    for (const game of games) assert.equal(state.data.games.find(item => item.package === game.package)?.installed, true);
    console.log('AXRB game export smoke passed: two games exported and reimported through production IPC.');
  } finally {
    dialog.showSaveDialog = original.save; dialog.showOpenDialog = original.open;
    runtime.inspect = original.inspect; runtime.install = original.install;
    state.data.games = original.games; state.data.jobs = original.jobs; state.data.settings.downloadDir = original.downloads;
    await persist();
    await fs.rm(directory, { recursive: true, force: true });
  }
}
