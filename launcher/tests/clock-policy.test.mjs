import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { migrateClockPolicy, selectGuestClock } from '../core/clock-policy.mjs';

test('legacy default clock migrates once to automatic selection', () => {
  for (const guestClock of [undefined, 'Default']) {
    const settings = { guestClock, cpuCores: 4 };
    migrateClockPolicy(settings);
    assert.equal(settings.guestClock, 'Auto');
    assert.equal(settings.cpuCores, 4);
    settings.guestClock = 'Default';
    migrateClockPolicy(settings);
    assert.equal(settings.guestClock, 'Default');
  }
});
test('explicit corrected and experimental clock choices survive migration', () => {
  for (const guestClock of ['TscCorrected', 'Tsc']) {
    const settings = { guestClock };
    migrateClockPolicy(settings);
    assert.equal(settings.guestClock, guestClock);
  }
});
test('automatic clock falls back when the helper is absent or emulator unsupported', async t => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-clock-'));
  t.after(() => fs.rm(root, { recursive: true, force: true }));
  const settings = { guestClock: 'Auto', sdk: root };
  assert.equal(await selectGuestClock(root, settings), 'Default');
  for (const file of ['out/clock/Release/axrb_clock_launcher.exe', 'out/clock/Release/axrb_whpx_clock.dll', 'emulator/qemu/windows-x86_64/qemu-system-x86_64-headless.exe']) {
    await fs.mkdir(path.dirname(path.join(root, file)), { recursive: true });
    await fs.writeFile(path.join(root, file), 'unsupported fixture');
  }
  assert.equal(await selectGuestClock(root, settings), 'Default');
  assert.equal(await selectGuestClock(root, { ...settings, guestClock: 'TscCorrected' }), 'TscCorrected');
});
