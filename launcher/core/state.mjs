import fs from 'node:fs/promises';
import path from 'node:path';
export class State {
  constructor(directory) { this.directory = directory; this.data = { games: [], jobs: [], settings: {} }; this.writes = Promise.resolve(); }
  async load({ portableRoot = '' } = {}) {
    await fs.mkdir(this.directory, { recursive: true });
    try { this.data = { ...this.data, ...JSON.parse(await fs.readFile(path.join(this.directory, 'library.json'), 'utf8')) }; }
    catch (error) { if (error.code !== 'ENOENT') throw new Error('Launcher library could not be read. The original file has been preserved.'); }
    this.data.jobs = this.data.jobs.map(j => ['queued', 'downloading', 'installing', 'patching', 'importing', 'uninstalling'].includes(j.status) ? { ...j, status: 'interrupted', error: 'Interrupted when the launcher closed. Retry to continue.' } : j);
    if (portableRoot) {
      const current = path.resolve(portableRoot), previous = this.data.portableRoot;
      if (previous !== current) {
        // Only paths carried inside the old portable folder move. External
        // runtimes and user-selected files remain explicit absolute paths.
        const rebase = value => {
          if (!previous || !path.isAbsolute(previous) || typeof value !== 'string' || !path.isAbsolute(value)) return value;
          const relative = path.relative(previous, value);
          return relative === '..' || relative.startsWith(`..${path.sep}`) || path.isAbsolute(relative) ? value : path.join(current, relative);
        };
        for (const key of ['managedDirectory', 'sdk', 'downloadDir', 'ovrportCli']) {
          if (this.data.settings[key]) this.data.settings[key] = rebase(this.data.settings[key]);
        }
        for (const game of this.data.games) {
          if (game.apk) game.apk = rebase(game.apk);
          for (const file of game.files || []) file.path = rebase(file.path);
        }
        this.data.portableRoot = current;
        await this.save();
      }
    }
  }
  save() {
    const snapshot = JSON.stringify(this.data, null, 2);
    const write = async () => {
      const file = path.join(this.directory, 'library.json');
      await fs.writeFile(`${file}.tmp`, snapshot);
      await fs.rename(`${file}.tmp`, file);
    };
    this.writes = this.writes.catch(() => {}).then(write);
    return this.writes;
  }
  activateRuntime(directory, settings, installed) {
    this.data.settings = settings;
    this.data.settings.managedDirectory = directory;
    for (const game of this.data.games) game.installed = Boolean(game.package && installed.has(game.package));
  }
  put(game) {
    const existing = this.data.games.find(g => g.id === game.id) || this.data.games.find(g => game.package && g.package === game.package);
    if (game.package) {
      const duplicate = this.data.games.find(g => g !== existing && g.package === game.package);
      if (duplicate && existing) { Object.assign(existing, duplicate, { id: existing.id }); this.data.games = this.data.games.filter(g => g !== duplicate); }
    }
    if (existing) {
      const image = game.image || existing.image;
      Object.assign(existing, game);
      if (image) existing.image = image;
    } else this.data.games.push(game);
    return existing || game;
  }
}
