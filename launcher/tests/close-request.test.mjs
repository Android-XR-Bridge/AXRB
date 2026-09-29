import test from 'node:test';
import assert from 'node:assert/strict';
import { createCloseRequest } from '../core/close-request.mjs';
import { Runtime } from '../core/runtime.mjs';

function fixture(overrides = {}) {
  const calls = [];
  const request = createCloseRequest({
    getStatus: async () => ({ running: true }),
    prompt: async () => { calls.push('prompt'); return 'stop'; },
    stop: async () => { calls.push('stop'); },
    quit: async () => { calls.push('quit'); },
    onError: async error => { calls.push(error.message); },
    ...overrides,
  });
  return { request, calls };
}

test('closing a running emulator stops it before quitting', async () => {
  const { request, calls } = fixture();
  await request();
  assert.deepEqual(calls, ['prompt', 'stop', 'quit']);
});
test('cancel keeps the launcher open and allows a later close attempt', async () => {
  let choice = 'cancel';
  const { request, calls } = fixture({ prompt: async () => choice });
  await request();
  assert.deepEqual(calls, []);
  choice = 'leave';
  await request();
  assert.deepEqual(calls, ['quit']);
});
test('stopped emulator closes without a popup or stop request', async () => {
  const { request, calls } = fixture({ getStatus: async () => ({ running: false }) });
  await request();
  assert.deepEqual(calls, ['quit']);
});
test('repeated close events share a single dialog and wait for shutdown', async () => {
  let complete;
  const { request, calls } = fixture({ stop: () => new Promise(resolve => { complete = resolve; }) });
  const first = request();
  assert.equal(request(), first);
  await new Promise(resolve => setImmediate(resolve));
  assert.deepEqual(calls, ['prompt']);
  complete();
  await first;
  assert.deepEqual(calls, ['prompt', 'quit']);
});
test('failed emulator shutdown keeps the launcher open', async () => {
  const { request, calls } = fixture({ stop: async () => { throw new Error('Still saving'); } });
  await request();
  assert.deepEqual(calls, ['prompt', 'Still saving']);
});
test('a failed status check does not silently close the launcher', async () => {
  const { request, calls } = fixture({ getStatus: async () => { throw new Error('Status unavailable'); } });
  await request();
  assert.deepEqual(calls, ['Status unavailable']);
});
test('emulator shutdown waits for the game and checks the process after ADB accepts stop', async () => {
  const runtime = new Runtime('fixture', {}), calls = [];
  runtime.child = { exitCode: null, signalCode: null };
  runtime.stop = async () => { calls.push('game'); runtime.child = null; };
  let running = true;
  runtime.status = async () => { calls.push('status'); return { running }; };
  runtime.adb = async args => { assert.deepEqual(args, ['emu', 'kill']); calls.push('emulator'); running = false; };
  await runtime.stopEmulator();
  assert.deepEqual(calls, ['game', 'status', 'emulator', 'status']);
});
test('a game shutdown error prevents an emulator stop request', async () => {
  const runtime = new Runtime('fixture', {});
  runtime.child = {};
  runtime.stop = async () => { throw new Error('Game shutdown failed'); };
  runtime.adb = async () => assert.fail('Must not stop the emulator');
  await assert.rejects(runtime.stopEmulator(), /Game shutdown failed/);
});
