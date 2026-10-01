import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { avdConfig, avdDirectory, parseStorageGB, planStorageChange, withStorageGB, STORAGE_MAX_GB, STORAGE_MIN_GB } from '../core/setup.mjs';
import { measureDirectoryBytes, parseAndroidDataUsage } from '../core/storage-info.mjs';

test('the configured partition size is read back from a real AVD config', () => {
  assert.equal(parseStorageGB('AvdId=axrb\ndisk.dataPartition.size=64G\nhw.ramSize=8192\n'), 64);
  assert.equal(parseStorageGB('disk.dataPartition.size = 32g'), 32, 'the emulator rewrites the unit in lower case');
  assert.equal(parseStorageGB('disk.dataPartition.size=2048M'), 2, 'megabyte sizes convert to GB');
  assert.equal(parseStorageGB('hw.ramSize=8192'), null, 'an absent entry is reported, not guessed');
});

test('writing a new size replaces the entry and leaves the rest of the config alone', () => {
  const before = 'AvdId=axrb\ndisk.dataPartition.size=32G\nhw.gpu.mode=host\n';
  const after = withStorageGB(before, 64);
  assert.match(after, /^disk\.dataPartition\.size=64G$/m);
  assert.doesNotMatch(after, /32G/);
  assert.match(after, /AvdId=axrb/);
  assert.match(after, /hw\.gpu\.mode=host/);
  assert.equal(parseStorageGB(after), 64, 'the result reads back as the new size');
});

test('a config without the entry gains one instead of being corrupted', () => {
  const after = withStorageGB('AvdId=axrb\nhw.ramSize=8192', 48);
  assert.match(after, /AvdId=axrb/);
  assert.match(after, /^disk\.dataPartition\.size=48G$/m);
  assert.equal(after.match(/disk\.dataPartition\.size/g).length, 1);
});

test('growing is allowed and an unchanged size is a no-op', () => {
  assert.equal(planStorageChange(32, 64), true);
  assert.equal(planStorageChange(32, 32), false, 'no rewrite when nothing changes');
  assert.equal(planStorageChange(null, 32), true, 'a config with no entry gets one');
});

test('shrinking is refused, because ext4 cannot return space the guest holds', () => {
  assert.throws(() => planStorageChange(64, 32), /can only grow.*erase/s);
  assert.throws(() => planStorageChange(64, 32), /64 GB/);
});

test('sizes outside the supported range are rejected before any file is touched', () => {
  for (const bad of [STORAGE_MIN_GB - 1, STORAGE_MAX_GB + 1, 0, -8, 32.5, NaN, '64']) {
    assert.throws(() => planStorageChange(32, bad), /8–256 GB/, `${bad} must be rejected`);
  }
  assert.equal(planStorageChange(8, STORAGE_MAX_GB), true, 'the documented maximum is usable');
});

test('the AVD directory follows a managed install and falls back to the SDK default', () => {
  const previous = process.env.ANDROID_AVD_HOME;
  try {
    process.env.ANDROID_AVD_HOME = path.join('C:', 'AXRB Runtime', 'avd');
    assert.equal(avdDirectory({ avd: 'axrb-managed-api36' }), path.join('C:', 'AXRB Runtime', 'avd', 'axrb-managed-api36.avd'));
    delete process.env.ANDROID_AVD_HOME;
    assert.match(avdDirectory({ avd: 'dev' }), /[\\/]\.android[\\/]avd[\\/]dev\.avd$/);
  } finally { if (previous === undefined) delete process.env.ANDROID_AVD_HOME; else process.env.ANDROID_AVD_HOME = previous; }
});

test('a freshly provisioned AVD and a resized one agree on the size format', () => {
  const created = avdConfig('C:\\sdk\\system-images\\android-36\\google_apis\\x86_64', { avd: 'axrb', cpuCores: 4, memoryMB: 8192, storageGB: 64 });
  assert.equal(parseStorageGB(created), 64, 'setup and resize must write a size the other can read');
  assert.equal(parseStorageGB(withStorageGB(created, 96)), 96);
});

test('Android data usage reads the mounted /data capacity and rejects incomplete output', () => {
  assert.deepEqual(parseAndroidDataUsage('Filesystem 1K-blocks Used Available Use% Mounted on\n/dev/block/vda 10485760 4194304 6291456 40% /data\n'),
    { totalBytes: 10 * 1024 ** 3, usedBytes: 4 * 1024 ** 3, freeBytes: 6 * 1024 ** 3 });
  assert.throws(() => parseAndroidDataUsage('Filesystem 1K-blocks Used Available Use% Mounted on\n'), /Could not read/);
});

test('host footprint totals files in nested runtime directories', async t => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-storage-'));
  t.after(() => fs.rm(root, { recursive: true, force: true }));
  const avd = path.join(root, 'avd');
  await fs.mkdir(path.join(avd, 'nested'), { recursive: true });
  await fs.writeFile(path.join(avd, 'nested', 'userdata-qemu.img'), Buffer.alloc(2048));
  const usage = await measureDirectoryBytes(avd);
  const stat = await fs.stat(path.join(avd, 'nested', 'userdata-qemu.img'));
  assert.equal(usage, Number.isFinite(stat.blocks) ? stat.blocks * 512 : stat.size);
  assert.equal(await measureDirectoryBytes(path.join(root, 'missing')), 0);
});
