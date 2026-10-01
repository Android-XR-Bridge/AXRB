import test from 'node:test';
import assert from 'node:assert/strict';
import { AiDiagnostics, cleanEvidence, readResponse, MAX_EVIDENCE } from '../core/ai-diagnostics.mjs';
import { authorizationRequest, validateCallback, validateGrant, verifyIdentity, ChatGPTAuth } from '../core/chatgpt-auth.mjs';
import { generateKeyPair, SignJWT } from 'jose';
import { createHash } from 'node:crypto';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';

const sse = events => events.map(e => `data: ${JSON.stringify(e)}\r\n\r\n`).join('');
const completed = { type: 'response.completed', response: { status: 'completed' } };
function stream(events, split = false) {
  const bytes = new TextEncoder().encode(sse(events));
  return new ReadableStream({ start(c) { if (split) for (const byte of bytes) c.enqueue(Uint8Array.of(byte)); else c.enqueue(bytes); c.close(); } });
}
test('OAuth uses per-attempt PKCE, stable host identity and dynamic registration only for new accounts', () => {
  const first = authorizationRequest('urn:uuid:host', null, 'http://127.0.0.1:12345/auth/callback');
  const url = new URL(first.url);
  assert.equal(url.origin, 'https://auth.openai.com');
  assert.equal(url.searchParams.get('client_id'), 'dynamic_agent_client');
  assert.equal(url.searchParams.get('code_challenge'), createHash('sha256').update(first.pending.verifier).digest('base64url'));
  const second = authorizationRequest('urn:uuid:host', { client_id: 'issued', id_token: 'hint' }, first.pending.redirect);
  assert.notEqual(first.pending.state, second.pending.state);
  assert.equal(new URL(second.url).searchParams.get('client_id'), 'issued');
  assert.equal(new URL(second.url).searchParams.has('agent_name_hint'), false);
  assert.equal(new URL(second.url).searchParams.get('ext_agent_host_id'), 'urn:uuid:host');
});
test('OAuth rejects invalid state, denied consent and substituted registrations', () => {
  const pending = { state: 'expected', client: 'issued' };
  const callback = q => new URL(`http://127.0.0.1/auth/callback?${q}`);
  assert.throws(() => validateCallback(callback('state=wrong&code=secret'), pending), /verified/);
  assert.throws(() => validateCallback(callback('state=expected&error=access_denied'), pending), /declined/);
  assert.throws(() => validateCallback(callback('state=expected&client_id=attacker&code=x'), pending), /different/);
  assert.throws(() => validateCallback(callback('state=expected&code=x'), { state: 'expected' }), /missing/);
  assert.deepEqual(validateCallback(callback('state=expected&code=x'), pending), { client: 'issued', code: 'x' });
});
test('ID token validates signature, issuer, audience, expiration, nonce and returning identity', async () => {
  const keys = await generateKeyPair('RS256');
  const make = (claims = {}, audience = 'issued', issuer = 'https://auth.openai.com', expiry = '5m') => new SignJWT({ nonce: 'nonce', ...claims }).setProtectedHeader({ alg: 'RS256' }).setIssuer(issuer).setAudience(audience).setSubject('user').setIssuedAt().setExpirationTime(expiry).sign(keys.privateKey);
  const valid = await make();
  assert.equal((await verifyIdentity(valid, 'issued', 'nonce', 'user', keys.publicKey)).sub, 'user');
  for (const token of [await make({}, 'wrong'), await make({}, 'issued', 'https://wrong.example'), await make({}, 'issued', 'https://auth.openai.com', '-1h'), await make({ nonce: 'wrong' })]) {
    await assert.rejects(verifyIdentity(token, 'issued', 'nonce', 'user', keys.publicKey));
  }
  await assert.rejects(verifyIdentity(valid, 'issued', 'nonce', 'different', keys.publicKey));
  const foreign = await generateKeyPair('RS256');
  await assert.rejects(verifyIdentity(valid, 'issued', 'nonce', 'user', foreign.publicKey));
});
test('identity-only grants cannot perform inference and refreshed grants retain scopes', () => {
  const tokens = { access_token: 'secret', token_type: 'Bearer', expires_in: 3600, scope: 'openid' };
  assert.throws(() => validateGrant(tokens), /plan usage/);
  tokens.scope = 'resource.invoke chatgpt.tokens.use.direct';
  const previous = validateGrant(tokens);
  assert.equal(validateGrant({ ...tokens, scope: undefined }, previous).access_token, 'secret');
  assert.throws(() => validateGrant({ ...tokens, expires_in: -1 }), /invalid/);
});
test('redaction removes identities and credentials before model input and report export', () => {
  const text = cleanEvidence('C:\\Users\\OtherPerson\\logs test@example.org\nAuthorization: Bearer abc123\nCookie: session=secret\nid_token_hint=secret\nsk-proj-privatekey\neyJhbGciOiJSUzI1NiJ9.eyJzdWIiOiJzZWNyZXQifQ.signature\nhttps://example.com/?code=secret&state=secret');
  for (const value of ['OtherPerson', 'test@example.org', 'abc123', 'session=secret', 'privatekey', 'eyJhbGci', '=secret']) assert.equal(text.includes(value), false, value);
});
test('SSE handles split UTF-8 and CRLF boundaries and requires completed status', async () => {
  assert.equal(await readResponse(stream([{ type: 'response.output_text.delta', delta: 'HÃ©llo' }, completed], true), () => {}), 'HÃ©llo');
  await assert.rejects(readResponse(stream([{ type: 'response.output_text.delta', delta: 'partial' }]), () => {}), /complete answer/);
  await assert.rejects(readResponse(stream([{ type: 'response.failed', response: { error: { code: 'subscription_sharing_usage_limit_exceeded' } } }]), () => {}), /usage limit/);
  await assert.rejects(readResponse(stream([{ type: 'response.incomplete' }]), () => {}), /finish/);
});
test('analysis requires consent and known model, sends bounded redacted data with optional web search and no storage', async () => {
  let request, count = 0;
  const ai = new AiDiagnostics({ access: async () => 'auth-secret' }, async (url, opts) => {
    count++; request = { url, ...opts };
    return new Response(stream([{ type: 'response.output_text.delta', delta: 'Check the emulator log.' }, completed]));
  });
  ai.models = [{ id: 'available' }];
  const input = { consent: true, model: 'available', evidence: 'Bearer private\nERROR: device lost', question: 'Why?' };
  await assert.rejects(ai.analyze({ ...input, consent: false }), /approve/);
  await assert.rejects(ai.analyze({ ...input, model: 'unknown' }), /available/);
  await assert.rejects(ai.analyze({ ...input, evidence: 'x'.repeat(MAX_EVIDENCE + 1) }), /characters/);
  assert.equal(count, 0);
  assert.equal(await ai.analyze(input), 'Check the emulator log.');
  const payload = JSON.parse(request.body);
  assert.equal(request.url, 'https://api.openai.com/v1/responses');
  assert.deepEqual(payload.tools, [{type:'web_search'}]); assert.equal(payload.store, false); assert.equal(payload.stream, true);
  assert.equal(request.body.includes('Bearer private'), false); assert.equal(request.body.includes('auth-secret'), false);
  assert.equal(ai.history.length, 2); assert.equal(ai.status().working, false);
  assert.equal(ai.messages.length, 2); assert.equal(ai.messages[0].role, 'user');
  assert.equal(ai.messages[1].status.type, 'complete');
  assert.notEqual(ai.messages[0].id, ai.messages[1].id);
});
test('failed and cancelled analyses never become successful history entries', async () => {
  const ai = new AiDiagnostics({ access: async () => 'token' }, async () => new Response(stream([{ type: 'response.output_text.delta', delta: 'partial' }])));
  ai.models = [{ id: 'm' }];
  await assert.rejects(ai.analyze({ consent: true, model: 'm', evidence: 'log', question: 'help' }), /complete/);
  assert.equal(ai.history.length, 0); assert.equal(ai.output, 'partial'); assert.ok(ai.error);
  assert.equal(ai.messages.at(-1).content[0].text, 'partial');
  assert.equal(ai.messages.at(-1).status.reason, 'error');
  ai.fetch = async (_url, { signal }) => { ai.cancel(); signal.throwIfAborted(); };
  await assert.rejects(ai.analyze({ consent: true, model: 'm', evidence: 'log', question: 'help' }), /stopped/);
  assert.equal(ai.history.length, 0); assert.equal(ai.controller, null);
  assert.equal(ai.messages.at(-1).status.reason, 'cancelled');
  assert.equal(ai.messages.length, 4);
  ai.reset(); assert.equal(ai.messages.length, 0);
});
test('credential storage requires encryption, preserves host and exposes only account labels', async t => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-ai-test-')); t.after(() => fs.rm(directory, { recursive: true, force: true }));
  // A reversible fixture substitutes for DPAPI, never for production storage.
  const cipher = { isEncryptionAvailable: () => true, encryptString: s => Buffer.from(s).reverse(), decryptString: b => Buffer.from(b).reverse().toString() };
  const auth = new ChatGPTAuth(directory, cipher, async () => {}); await auth.load();
  const host = auth.data.host; auth.data.accounts = [{ client_id: 'issued', subject: 'private-subject', email: 'label@example.org', access_token: 'private-token', id_token: 'private-id-token', refresh_token: 'refresh', expiresAt: Date.now() + 3600000 }]; auth.data.active = 'issued'; await auth.save();
  assert.equal((await fs.readFile(auth.file, 'utf8')).includes('private-token'), false);
  const second = new ChatGPTAuth(directory, cipher, async () => {}); await second.load(); assert.equal(second.data.host, host);
  const status = JSON.stringify(await second.status()); assert.equal(status.includes('private-token'), false); assert.equal(status.includes('private-subject'), false);
  const unavailable = new ChatGPTAuth(directory, { isEncryptionAvailable: () => false }); await assert.rejects(unavailable.load(), /encryption/);
});
test('login callback binds loopback and rejects unsolicited callbacks, cancellation closes listener', async t => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-ai-auth-')); t.after(() => fs.rm(directory, { recursive: true, force: true }));
  let opened;
  const cipher = { isEncryptionAvailable: () => true, encryptString: s => Buffer.from(s), decryptString: b => b.toString() };
  const auth = new ChatGPTAuth(directory, cipher, async url => { opened = new URL(url); }); t.after(() => auth.cancel());
  await auth.login(); const callback = opened.searchParams.get('redirect_uri');
  assert.equal(new URL(callback).hostname, '127.0.0.1');
  assert.equal((await fetch(`${callback}?state=wrong&code=x`)).status, 400);
  assert.ok(auth.pending); auth.cancel(); assert.equal(auth.pending, null);
  await assert.rejects(fetch(callback));
});


test('model catalog fills known rollout omissions without overriding hidden or listed models', async () => {
 const ai=new AiDiagnostics({access:async()=> 'fixture'},async()=>Response.json({models:[{slug:'gpt-6-astra',display_name:'Astra',visibility:'list'},{slug:'gpt-6-sol',visibility:'hide'}]}));
 const models=await ai.catalog();
 assert.deepEqual(models.map(m=>m.id),['gpt-6-astra','gpt-6-luna']);
 assert.equal(models[1].catalogFallback,true);
 ai.fetch=async()=>Response.json({models:[{slug:'gpt-6-sol',display_name:'Server Sol',visibility:'list'},{slug:'gpt-6-luna',display_name:'Server Luna',visibility:'list'}]});
 assert.deepEqual((await ai.catalog()).map(m=>m.name),['Server Sol','Server Luna']);
});
