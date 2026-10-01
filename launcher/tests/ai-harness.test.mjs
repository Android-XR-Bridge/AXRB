import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { DiagnosticHarness, diagnosticTools, validateTool } from '../core/ai-tools.mjs';
import { AiDiagnostics } from '../core/ai-diagnostics.mjs';

const signal = () => new AbortController().signal;
function response(output, usage) {
  return new Response(`data: ${JSON.stringify({ type: 'response.completed', response: { status: 'completed', output, usage } })}\n\n`);
}
const message = text => ({ type: 'message', role: 'assistant', content: [{ type: 'output_text', text, annotations: [] }], status: 'completed' });
const call = (name, args = {}, id = 'call1') => ({ type: 'function_call', namespace: 'axrb', name, arguments: JSON.stringify(args), call_id: id });
const input = { consent: true, model: 'model', evidence: 'Reviewed snapshot', question: 'Find the failure' };

test('ChatGPT plan tool-only streams execute completed items when terminal output is empty',async()=>{
  let requests=0,executed=0;
  const ai=new AiDiagnostics({access:async()=> 'token'},async()=>{
    requests++;
    if(requests>1) return response([message('Checked.')]);
    return new Response([{type:'response.output_item.done',output_index:0,item:call('runtime_status')},{type:'response.completed',response:{status:'completed',output:[]}}].map(e=>`data: ${JSON.stringify(e)}\n\n`).join(''));
  },()=>{},{tools:diagnosticTools,begin:()=>({execute:async()=>{executed++;return {evidence:'Checked'};}})});
  ai.models=[{id:'model'}];assert.equal(await ai.analyze(input),'Checked.');assert.equal(executed,1);assert.equal(requests,2);
});

test('agent executes real function loop, replays reasoning, preserves prefix, and counts cached tokens', async () => {
  const requests = [], calls = [];
  const reasoning = { type: 'reasoning', id: 'r1', summary: [], encrypted_content: 'opaque-do-not-redact' };
  const harness = { tools: diagnosticTools, begin: () => ({ execute: async (...args) => { calls.push(args); return { evidence: 'Bearer secret\nGPU device lost', collectedAt: 'now' }; } }) };
  const ai = new AiDiagnostics({ access: async () => 'token' }, async (_url, options) => {
    requests.push(JSON.parse(options.body));
    return requests.length === 1 ? response([reasoning, call('runtime_status')]) : response([message('The GPU device was lost.')], { input_tokens: 1500, output_tokens: 30, input_tokens_details: { cached_tokens: 1024 } });
  }, () => {}, harness);
  ai.models = [{ id: 'model' }];
  assert.equal(await ai.analyze(input), 'The GPU device was lost.');
  assert.equal(calls.length, 1); assert.equal(calls[0][0], 'runtime_status');
  assert.deepEqual(requests[1].input.slice(0, requests[0].input.length), requests[0].input);
  assert.deepEqual(requests[1].input.find(i => i.type === 'reasoning'), reasoning);
  assert.equal(requests[1].input.at(-1).type, 'function_call_output');
  assert.equal(JSON.stringify(requests).includes('Bearer secret'), false);
  assert.equal(ai.messages.at(-1).content[0].type, 'tool-call');
  assert.equal(ai.usage.cachedTokens, 1024); assert.equal(ai.usage.tools, 1);
  await ai.analyze({ ...input, question: 'What next?' });
  assert.deepEqual(requests[2].input.slice(0, requests[1].input.length), requests[1].input);
  assert.equal(requests[2].store, false); assert.equal(requests[2].tools[0].type, 'namespace');
  ai.reset(); assert.equal(ai.turns.length, 0);
});

test('failed tool results are returned to model; repeated calls and total steps are bounded', async () => {
  let executions = 0, requests = 0;
  const ai = new AiDiagnostics({ access: async () => 'token' }, async () => response([call('runtime_status', {}, `c${++requests}`)]), () => {},
    { tools: diagnosticTools, begin: () => ({ execute: async () => { executions++; throw new Error('ADB offline'); } }) });
  ai.models = [{ id: 'model' }];
  await assert.rejects(ai.analyze(input), /step limit/);
  assert.equal(executions, 2); assert.equal(requests, 20);
  assert.equal(ai.messages.at(-1).status.type, 'incomplete');
  assert.equal(ai.history.length, 0);
  assert.ok(ai.messages.at(-1).content.some(p => p.result?.error?.includes('offline')));
});

test('cancel during tool execution prevents another model request', async () => {
  let requests = 0;
  const ai = new AiDiagnostics({ access: async () => 'token' }, async () => { requests++; return response([call('host')]); }, () => {},
    { tools: diagnosticTools, begin: () => ({ execute: async (_name, _args, s) => { ai.cancel(); s.throwIfAborted(); } }) });
  ai.models = [{ id: 'model' }];
  await assert.rejects(ai.analyze(input), /stopped/);
  assert.equal(requests, 1); assert.equal(ai.messages.at(-1).status.reason, 'cancelled');
});

test('diagnostic schemas reject command injection and out-of-range captures', () => {
  for (const [name, args] of [['exec', {command: 'rm'}], ['runtime_status', {extra: true}], ['android_check', {check: 'shell reboot'}], ['performance', {seconds: 300}], ['read_log', {id: 'x', offset: 0.5}], ['search_logs', {query: ''}]]) assert.throws(() => validateTool(name, args));
});

test('harness targets configured emulator, caches bounded checks, rejects changed sessions and aborts', async () => {
  let ctx = { sdk: 'C:\\AXRB\\sdk', port: 5584, avd: 'managed', dataHome: 'C:\\AXRB\\out', package: 'org.example.game', session: 1 };
  const executed = [];
  const harness = new DiagnosticHarness({ context: () => ctx, execute: async (exe, args, options) => { executed.push({ exe, args, options }); return 'booted'; } });
  const session = harness.begin();
  const first = await session.execute('android_check', {check: 'boot'}, signal());
  assert.equal(executed.length, 4); assert.deepEqual(executed[0].args.slice(0,4), ['-P', '5038', '-s', 'emulator-5584']);
  assert.equal(executed[0].options.timeout, 12000);
  const cached = await session.execute('android_check', {check: 'boot'}, signal());
  assert.equal(cached.cached, true); assert.equal(cached.collectedAt, first.collectedAt); assert.equal(executed.length, 4);
  ctx = {...ctx, port: 5586};
  await assert.rejects(session.execute('android_check', {check: 'boot'}, signal()), /changed/);
  const aborted = new AbortController(); aborted.abort();
  await assert.rejects(harness.begin().execute('host', {}, aborted.signal)); assert.equal(executed.length, 4);
});

test('log tools enumerate, page and search allowlisted files with redaction and no path traversal', async t => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-harness-'));
  t.after(() => fs.rm(dir, {recursive: true, force: true}));
  await fs.mkdir(path.join(dir, 'logs/game'), {recursive: true});
  await fs.writeFile(path.join(dir, 'logs/game/host.log'), 'Authorization: Bearer secret\nERROR: device lost\n');
  await fs.writeFile(path.join(dir, 'logs/credentials.json'), 'private');
  const session = new DiagnosticHarness({context: () => ({dataHome: dir})}).begin();
  const listing = await session.execute('list_logs', {}, signal());
  assert.equal(listing.evidence.includes('credentials'), false);
  const log = await session.execute('read_log', {id: 'game/host.log', offset: -1}, signal());
  assert.equal(log.evidence.includes('secret'), false); assert.match(log.evidence, /device lost/);
  await assert.rejects(session.execute('read_log', {id: '../credentials.json', offset: 0}, signal()), /Unknown/);
  const found = await session.execute('search_logs', {query: 'device lost'}, signal()); assert.match(found.evidence, /device lost/);
});

test('stop-game tool only goes through the approval callback', async () => {
  let prompted = 0;
  const session = new DiagnosticHarness({context: () => ({}), approveStop: async () => { prompted++; return { stopped: false, reason: 'User declined' }; }}).begin();
  const value = await session.execute('stop_game', {reason: 'Recover hung game'}, signal());
  assert.equal(prompted, 1); assert.match(value.evidence, /User declined/);
});
