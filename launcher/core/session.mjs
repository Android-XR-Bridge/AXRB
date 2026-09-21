import fs from 'node:fs/promises';
import path from 'node:path';

export class MetaSession {
  constructor(directory, { portable = false, safeStorage } = {}) {
    Object.assign(this, { directory, portable, safeStorage });
    this.file = path.join(directory, portable ? 'meta-session.txt' : 'meta-session.bin');
  }
  async save(value) {
    if (!this.portable && !this.safeStorage.isEncryptionAvailable()) throw new Error('Windows credential encryption is unavailable.');
    await fs.writeFile(this.file, this.portable ? value : this.safeStorage.encryptString(value), { mode: 0o600 });
  }
  async load() {
    try {
      if (this.portable) return (await fs.readFile(this.file, 'utf8')).trim();
      if (this.safeStorage.isEncryptionAvailable()) return this.safeStorage.decryptString(await fs.readFile(this.file));
    } catch {}
    return '';
  }
  async clear() {
    await Promise.all(['meta-session.txt', 'meta-session.bin'].map(name => fs.rm(path.join(this.directory, name), { force: true })));
  }
}
