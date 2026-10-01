import fs from 'node:fs/promises';
import path from 'node:path';

export function parseAndroidDataUsage(output) {
  const row = String(output ?? '').split(/\r?\n/).map(line => line.trim().split(/\s+/))
    .find(fields => fields.at(-1) === '/data' && fields.length >= 5);
  if (!row || !/^\d+$/.test(row[1]) || !/^\d+$/.test(row[2]) || !/^\d+$/.test(row[3])) {
    throw new Error('Could not read Android /data storage usage.');
  }
  const [total, used, free] = row.slice(1, 4).map(value => Number(value) * 1024);
  if (![total, used, free].every(Number.isSafeInteger) || total <= 0 || used > total || free > total) {
    throw new Error('Android returned invalid /data storage usage.');
  }
  return { totalBytes: total, usedBytes: used, freeBytes: free };
}

export async function measureDirectoryBytes(directory) {
  let total = 0;
  async function walk(current) {
    let entries;
    try { entries = await fs.readdir(current, { withFileTypes: true }); }
    catch (error) {
      if (error.code === 'ENOENT' && current === directory) return;
      throw error;
    }
    for (const entry of entries) {
      if (entry.isSymbolicLink()) continue;
      const file = path.join(current, entry.name);
      if (entry.isDirectory()) await walk(file);
      else if (entry.isFile()) {
        let stat;
        try { stat = await fs.stat(file); }
        catch (error) { if (error.code === 'ENOENT') continue; throw error; }
        total += Number.isFinite(stat.blocks) ? stat.blocks * 512 : stat.size;
      }
    }
  }
  await walk(directory);
  return total;
}
