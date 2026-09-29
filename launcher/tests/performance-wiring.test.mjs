import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { Runtime } from '../core/runtime.mjs';

test('emulator preparation and game fallback preserve the same CPU settings', { skip: process.platform !== 'win32', timeout: 20000 }, async t => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-performance-'));
  t.after(() => fs.rm(root, { recursive: true, force: true }));
  const script = `param($GuestClock, $LocalApic)
Write-Output "POLICY:$GuestClock/$LocalApic"
`;
  for (const name of ['scripts/emulator/windows_android_emulator.ps1', 'scripts/run/run_windows_game.ps1']) {
    await fs.mkdir(path.dirname(path.join(root, name)), { recursive: true });
    await fs.writeFile(path.join(root, name), script);
  }
  for (const [guestClock, localApic] of [['TscCorrected', 'Hypervisor'], ['Default', 'Qemu']]) {
    const runtime = new Runtime(root, { sdk: root, avd: 'test', port: 5584, memoryMB: 8192, guestClock, localApic });
    const expected = `POLICY:${guestClock}/${localApic}`;
    assert.ok((await runtime.startEmulator({})).includes(expected));
    const result = await new Promise(resolve => {
      runtime.launch({ id: 'test', package: 'com.example.game', activity: 'com.example.game/.Main', name: 'Test' },
        (code, output) => resolve({ code, output }));
      t.after(() => runtime.child?.kill());
    });
    assert.equal(result.code, 0, result.output);
    assert.ok(result.output.includes(expected), result.output);
  }
});
