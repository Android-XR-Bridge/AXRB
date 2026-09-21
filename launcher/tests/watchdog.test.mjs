import test from 'node:test';
import assert from 'node:assert/strict';
import { EmulatorWatchdog } from '../core/watchdog.mjs';

async function until(predicate) {
  const deadline = Date.now() + 5000;
  while (!predicate()) {
    if (Date.now() > deadline) throw new Error('Watchdog did not reach the expected state.');
    await new Promise(resolve => setTimeout(resolve, 5));
  }
}

test('reports phase transitions and only notifies when the phase actually changes', async t => {
  const statuses = [
    { running: false, pid: null, count: 0, adbState: '' },
    { running: false, pid: null, count: 0, adbState: '' },
    { running: true, pid: 111, count: 1, adbState: '' },
    { running: true, pid: 111, count: 1, adbState: 'device' },
  ];
  let calls = 0;
  const changes = [];
  const transitions = [];
  const watchdog = new EmulatorWatchdog({
    getStatus: async () => statuses[Math.min(calls++, statuses.length - 1)],
    onChange: () => changes.push(watchdog.snapshot().phase),
    onTransition: (next, prev) => transitions.push([prev.phase, next.phase]),
    pollMs: 10,
  });
  t.after(() => watchdog.stop());
  assert.equal(watchdog.snapshot().phase, 'unknown');
  watchdog.start();
  await until(() => watchdog.snapshot().phase === 'stopped');
  assert.equal(changes.length, 1);
  assert.deepEqual(transitions[0], ['unknown', 'stopped']);
  await until(() => watchdog.snapshot().phase === 'starting');
  const starting = watchdog.snapshot();
  assert.equal(starting.pid, 111); assert.equal(starting.count, 1); assert.equal(starting.adbState, ''); assert.equal(starting.error, null);
  await until(() => watchdog.snapshot().phase === 'online');
  assert.equal(transitions.at(-1)[1], 'online');
  assert.match(watchdog.snapshot().detail, /pid 111/);
  // A repeated identical status must not fire another change.
  const seenBeforeIdle = changes.length;
  await new Promise(resolve => setTimeout(resolve, 60));
  assert.equal(changes.length, seenBeforeIdle);
});

test('a failing status check surfaces as an unknown phase with the error message, then recovers', async t => {
  let fail = true;
  const watchdog = new EmulatorWatchdog({
    getStatus: async () => { if (fail) throw new Error('adb is unreachable'); return { running: false, pid: null, count: 0, adbState: '' }; },
    pollMs: 10,
  });
  t.after(() => watchdog.stop());
  watchdog.start();
  await until(() => watchdog.snapshot().error === 'adb is unreachable');
  assert.equal(watchdog.snapshot().phase, 'unknown');
  fail = false;
  await until(() => watchdog.snapshot().phase === 'stopped');
  assert.equal(watchdog.snapshot().error, null);
});

test('stop() halts polling and no further changes are observed', async t => {
  let calls = 0;
  const watchdog = new EmulatorWatchdog({
    getStatus: async () => { calls++; return { running: false, pid: null, count: 0, adbState: '' }; },
    pollMs: 10,
  });
  watchdog.start();
  await until(() => calls >= 1);
  await watchdog.stop();
  const afterStop = calls;
  await new Promise(resolve => setTimeout(resolve, 50));
  assert.equal(calls, afterStop);
});

test('multiple detected processes are called out in the detail text', async t => {
  const watchdog = new EmulatorWatchdog({
    getStatus: async () => ({ running: true, pid: 22, count: 2, adbState: 'offline' }),
    pollMs: 10,
  });
  t.after(() => watchdog.stop());
  watchdog.start();
  await until(() => watchdog.snapshot().phase === 'starting');
  assert.match(watchdog.snapshot().detail, /2 matching processes/);
  assert.match(watchdog.snapshot().detail, /offline/);
});
