import fs from 'node:fs/promises';
import path from 'node:path';
import http from 'node:http';
import { randomBytes, randomUUID, createHash } from 'node:crypto';
import { createRemoteJWKSet, jwtVerify } from 'jose';

const issuer = 'https://auth.openai.com';
const resource = 'https://api.openai.com/v1';
const tokenEndpoint = `${issuer}/api/accounts/oauth/token`;
const jwks = createRemoteJWKSet(new URL(`${issuer}/.well-known/jwks.json`));
const random = () => randomBytes(32).toString('base64url');
const scopes = 'openid profile email offline_access resource.invoke chatgpt.tokens.use.direct';

export function authorizationRequest(host, account, redirect) {
  const pending = { state: random(), nonce: random(), verifier: random(), redirect, client: account?.client_id, subject: account?.subject };
  const url = new URL(`${issuer}/api/accounts/authorize`);
  url.search = new URLSearchParams({ client_id: pending.client || 'dynamic_agent_client',
    ...(pending.client ? {} : { agent_name_hint: 'AXRB' }), ext_agent_host_id: host,
    ...(account?.id_token ? { id_token_hint: account.id_token } : {}),
    response_type: 'code', redirect_uri: redirect, scope: scopes, resource,
    state: pending.state, nonce: pending.nonce, code_challenge_method: 'S256',
    code_challenge: createHash('sha256').update(pending.verifier).digest('base64url') }).toString();
  return { pending, url: url.href };
}
export function validateCallback(url, pending) {
  if (url.searchParams.get('state') !== pending.state) throw new Error('Sign-in could not be verified.');
  if (url.searchParams.has('error')) throw new Error('ChatGPT sign-in was declined or cancelled.');
  const client = url.searchParams.get('client_id') || pending.client;
  if (!client || client === 'dynamic_agent_client' || (pending.client && client !== pending.client)) throw new Error('ChatGPT returned a different or missing client registration.');
  const code = url.searchParams.get('code');
  if (!code) throw new Error('ChatGPT did not return an authorization code.');
  return { client, code };
}
export async function verifyIdentity(token, client, nonce, subject, key = jwks) {
  const { payload } = await jwtVerify(token, key, { issuer, audience: client, requiredClaims: ['sub', 'exp', 'iat'], clockTolerance: 5, algorithms: ['RS256', 'ES256'] });
  if (payload.nonce !== nonce || !payload.sub || (subject && payload.sub !== subject)) throw new Error('ChatGPT identity did not match this sign-in.');
  return payload;
}
export function validateGrant(tokens, previous) {
  const granted = typeof tokens.scope === 'string' ? tokens.scope.split(/\s+/) : previous?.scopes || [];
  if (!granted.includes('chatgpt.tokens.use.direct') || !granted.includes('resource.invoke')) throw new Error('Enable ChatGPT plan usage during sign-in to use AI diagnostics.');
  if (typeof tokens.access_token !== 'string' || !tokens.access_token || !/^bearer$/i.test(tokens.token_type || '') || !Number.isFinite(tokens.expires_in) || tokens.expires_in <= 0) throw new Error('ChatGPT returned an invalid access grant.');
  return { access_token: tokens.access_token, refresh_token: tokens.refresh_token || previous?.refresh_token,
    scopes: granted, expiresAt: Date.now() + tokens.expires_in * 1000 };
}

// This vault is independent of Meta/Codex credentials. Only safe account labels
// cross IPC. On Windows safeStorage uses DPAPI for the current Windows account.
export class ChatGPTAuth {
  constructor(directory, encryption, openBrowser, fetchImpl = fetch) {
    this.file = path.join(directory, 'chatgpt-credentials.bin'); this.encryption = encryption;
    this.openBrowser = openBrowser; this.fetch = fetchImpl; this.error = ''; this.loading = null;
  }
  async load() {
    if (!this.loading) this.loading = (async () => {
      if (!this.encryption.isEncryptionAvailable()) throw new Error('Windows credential encryption is unavailable.');
      try { this.data = JSON.parse(this.encryption.decryptString(await fs.readFile(this.file))); }
      catch (e) { if (e.code !== 'ENOENT') throw new Error('Saved ChatGPT sign-in could not be decrypted on this Windows account.'); }
      if (!this.data) { this.data = { host: `urn:uuid:${randomUUID()}`, accounts: [], active: null }; await this.save(); }
    })();
    return this.loading;
  }
  async save() {
    await fs.mkdir(path.dirname(this.file), { recursive: true });
    await fs.writeFile(`${this.file}.tmp`, this.encryption.encryptString(JSON.stringify(this.data)), { mode: 0o600 });
    await fs.rename(`${this.file}.tmp`, this.file);
  }
  current() { return this.data?.accounts.find(a => a.client_id === this.data.active); }
  cacheKey() {
    const account = this.current();
    if (!account) return undefined;
    return `axrb-v1-${createHash('sha256').update(`${this.data.host}:${account.client_id}`).digest('hex').slice(0, 48)}`;
  }
  async status() {
    await this.load();
    return { accounts: this.data.accounts.map(a => ({ id: a.client_id, label: `${a.email || 'ChatGPT account'} · ${a.client_id.slice(-6)}`, signedIn: Boolean(a.access_token) })),
      active: this.data.active, signedIn: Boolean(this.current()?.access_token), signingIn: Boolean(this.pending), error: this.error };
  }
  async postToken(body) {
    const response = await this.fetch(tokenEndpoint, { method: 'POST', redirect: 'error', signal: AbortSignal.timeout(30000), body: new URLSearchParams({ ...body, resource }) });
    if (!response.ok) throw new Error(`ChatGPT authorization failed (${response.status}). Please sign in again.`);
    try { return await response.json(); } catch { throw new Error('ChatGPT returned an unreadable authorization response. Please sign in again.'); }
  }
  cancel() { this.pending?.cancel(); }
  async login(client) {
    await this.load();
    if (this.pending || this.refreshing) throw new Error('A ChatGPT account operation is already running.');
    const account = client ? this.data.accounts.find(a => a.client_id === client) : null;
    if (client && !account) throw new Error('Unknown ChatGPT account.');
    this.error = '';
    const server = http.createServer();
    await new Promise((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
    const redirect = `http://127.0.0.1:${server.address().port}/auth/callback`;
    const { pending, url } = authorizationRequest(this.data.host, account, redirect);
    let consumed = false;
    const finish = () => { clearTimeout(timer); server.close(); server.closeAllConnections(); if (this.pending === pending) this.pending = null; };
    pending.cancel = () => { consumed = true; finish(); };
    this.pending = pending;
    const timer = setTimeout(() => { this.error = 'ChatGPT sign-in timed out. Try again.'; pending.cancel(); }, 10 * 60 * 1000);
    server.on('request', async (req, res) => {
      res.setHeader('Content-Type', 'text/plain; charset=utf-8'); res.setHeader('Cache-Control', 'no-store'); res.setHeader('Content-Security-Policy', "default-src 'none'");
      const callback = new URL(req.url, redirect);
      if (req.method !== 'GET' || callback.pathname !== '/auth/callback') { res.writeHead(404).end(); return; }
      if (consumed || callback.searchParams.get('state') !== pending.state) { res.writeHead(400).end('Invalid sign-in request.'); return; }
      consumed = true;
      try {
        const { client: issued, code } = validateCallback(callback, pending);
        const tokens = await this.postToken({ grant_type: 'authorization_code', client_id: issued, code, code_verifier: pending.verifier, redirect_uri: redirect });
        const identity = await verifyIdentity(tokens.id_token, issued, pending.nonce, pending.subject);
        const grant = validateGrant(tokens);
        if (this.pending !== pending) throw new Error('Sign-in was cancelled.');
        const existing = this.data.accounts.find(a => a.client_id === issued);
        if (existing && existing.subject !== identity.sub) throw new Error('ChatGPT account registration changed.');
        const accountId = identity['https://api.openai.com/auth']?.chatgpt_account_id;
        const entry = { client_id: issued, subject: identity.sub, email: identity.email, id_token: tokens.id_token,
          ...(typeof accountId === 'string' && /^[a-zA-Z0-9_-]{1,128}$/.test(accountId) ? { chatgpt_account_id: accountId } : {}), ...grant };
        this.data.accounts = this.data.accounts.filter(a => a.client_id !== issued).concat(entry); this.data.active = issued;
        await this.save(); res.end('Connected to AXRB. You can close this tab.');
      } catch { this.error = 'ChatGPT sign-in failed or plan access was not granted. Please try again.'; res.writeHead(400).end(this.error); }
      finally { finish(); }
    });
    try { await this.openBrowser(url); } catch { pending.cancel(); throw new Error('Could not open the browser for ChatGPT sign-in.'); }
    return this.status();
  }
  async select(client) {
    await this.load();
    if (this.pending || this.refreshing) throw new Error('Finish the current account operation first.');
    if (!this.data.accounts.some(a => a.client_id === client)) throw new Error('Unknown ChatGPT account.');
    this.data.active = client; this.error = ''; await this.save(); return this.status();
  }
  async access() {
    await this.load(); const account = this.current();
    if (!account?.access_token) throw new Error('Continue with ChatGPT first.');
    if (account.expiresAt > Date.now() + 60000) return account.access_token;
    if (!account.refresh_token) throw new Error('Your ChatGPT session expired. Please sign in again.');
    if (!this.refreshing) this.refreshing = (async () => {
      const tokens = await this.postToken({ grant_type: 'refresh_token', client_id: account.client_id, refresh_token: account.refresh_token });
      Object.assign(account, validateGrant(tokens, account)); await this.save(); return account.access_token;
    })().finally(() => { this.refreshing = null; });
    return this.refreshing;
  }
  async logout() {
    this.cancel(); await this.load(); await this.refreshing?.catch(() => {});
    const account = this.current(); let revoked = true;
    if (account?.refresh_token) {
      try {
        const discovery = await this.fetch(`${issuer}/.well-known/openid-configuration`, { signal: AbortSignal.timeout(10000), redirect: 'error' });
        if (!discovery.ok) throw new Error();
        const endpoint = new URL((await discovery.json()).revocation_endpoint);
        if (endpoint.origin !== issuer) throw new Error();
        revoked = false;
        for (let attempt = 0; attempt < 2; attempt++) {
          try {
            const response = await this.fetch(endpoint, { method: 'POST', redirect: 'error', signal: AbortSignal.timeout(10000), body: new URLSearchParams({ token: account.refresh_token, token_type_hint: 'refresh_token', client_id: account.client_id }) });
            if (response.status === 200) { revoked = true; break; }
            if (response.status < 500) break;
          } catch {}
          if (!attempt) await new Promise(resolve => setTimeout(resolve, 500));
        }
      } catch { revoked = false; }
    }
    if (account) for (const key of ['access_token', 'refresh_token', 'id_token', 'expiresAt', 'scopes']) delete account[key];
    this.error = revoked ? '' : 'Signed out locally. Remote revocation was not confirmed; disconnect AXRB in ChatGPT Settings.';
    await this.save(); return this.status();
  }
}
