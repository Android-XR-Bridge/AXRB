import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { Debloat, OPTIONAL_PACKAGES } from '../core/debloat.mjs';

async function fixture(t) {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-debloat-'));
  t.after(() => fs.rm(dir, { recursive: true, force: true }));
  const installed = new Set(['com.google.android.gms', 'com.google.android.gm', 'com.android.systemui', 'com.axrb.openxrruntime', 'com.test.game']);
  const calls = []; let hardware = 'ranchu', running = '', failure = '';
  const runtime = { adb: async args => {
    calls.push(args);
    if (args[1] === 'getprop') return ({'ro.hardware':hardware, 'ro.build.version.sdk':'36', 'sys.boot_completed':'1'})[args[2]];
    if (args[1] === 'ps') return running;
    if (args.includes('list')) return [...(args.includes('-3') ? ['com.test.game'] : installed)].map(p => 'package:' + p).join('\n');
    const p = args.at(-1);
    if (p === failure) return 'Failure [DELETE_FAILED_INTERNAL_ERROR]';
    if (args.includes('uninstall')) { installed.delete(p); return 'Success'; }
    if (args.includes('install-existing')) { installed.add(p); return 'Package installed for user: 0'; }
    throw new Error('Unexpected ADB request');
  }};
  return { debloat:new Debloat(runtime,dir), installed,calls,runtime,
    hardware:v=>hardware=v,running:v=>running=v,fail:v=>failure=v,dir };
}
test('removes only listed apps; repeated removal and restore preserve games and core', async t => {
  const f=await fixture(t);
  const result=await f.debloat.apply(); assert.equal(result.completed,2); assert.deepEqual(result.failures,[]);
  assert(f.installed.has('com.test.game')); assert(f.installed.has('com.android.systemui'));
  assert.equal((await f.debloat.apply()).completed,0);
  assert.equal((await f.debloat.apply(true)).completed,2);
  assert(f.installed.has('com.google.android.gms'));
  assert.equal((await f.debloat.apply(true)).completed,0);
  assert(f.calls.filter(a=>a.includes('uninstall')).every(a=>a.includes('-k') && a.includes('--user') && a.includes('0')));
});
test('rejects physical devices and externally running games before mutation', async t => {
  const f=await fixture(t); f.hardware('qcom'); await assert.rejects(f.debloat.apply(),/emulator/);
  f.hardware('ranchu'); f.running('com.test.game:worker'); await assert.rejects(f.debloat.apply(),/Close running/);
  assert(!f.calls.some(a=>a.includes('uninstall')));
});
test('failed removal remains recoverable; restoration never trusts arbitrary journal packages', async t => {
  const f=await fixture(t); f.fail('com.google.android.gms');
  assert.equal((await f.debloat.apply()).failures.length,1);
  assert((await f.debloat.read()).packages.includes('com.google.android.gms'));
  f.fail(''); await f.debloat.apply(true);
  await fs.writeFile(path.join(f.dir,'axrb-debloat.json'),JSON.stringify({version:1,packages:['com.test.game']}));
  await assert.rejects(f.debloat.apply(true),/Invalid debloat/);
});
test('policy preserves Android and VR dependencies', () => {
  for (const p of ['android','com.android.systemui','com.android.settings','com.google.android.permissioncontroller','com.google.android.webview','com.google.android.networkstack','com.google.android.inputmethod.latin','com.google.android.providers.media.module','com.axrb.openxrruntime']) assert(!OPTIONAL_PACKAGES.includes(p));
});
