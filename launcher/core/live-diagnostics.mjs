import fs from 'node:fs/promises';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { StringDecoder } from 'node:string_decoder';
import { redact, diagnosticSources, diagnosticTailOffset } from './diagnostics.mjs';

export const MAX_DIAGNOSTIC_ENTRIES = 5000;
const MAX_BYTES = 2 * 1024 * 1024;
const MAX_LINE = 8192;
const READ_BYTES = 64 * 1024;
const SOURCES = new Set(['android', 'emulator', 'host', 'launcher']);
// Listing devices can have to start the ADB server first, which on Windows
// takes seconds while the binary is scanned, and longer again while an
// emulator is still booting.
const ADB_PROBE_TIMEOUT = 15000;
const ADB_SERVER_TIMEOUT = 30000;
// Faults that mean "ask again", not "capture is broken": a server that is
// mid-start, one that a previous client shut down, or a transport that has
// not finished attaching.
export function transientAdbFault(message) {
  return /protocol fault|connection reset|failed to check server version|cannot connect to daemon|device offline|timed out|server is out of date|daemon not running/i.test(String(message ?? ''));
}
const LEVELS = new Set(['V', 'D', 'I', 'W', 'E', 'F']);
const logcatLine = /^(\d\d-\d\d\s+\d\d:\d\d:\d\d\.\d+)\s+(\d+)\s+\d+\s+([VDIWEF])\s+([^:]+):\s?(.*)$/;

function format(entry) {
  return `${entry.receivedAt} [${entry.source}/${entry.level}]${entry.guestTime ? ` [guest ${entry.guestTime}]` : ''}${entry.pid ? ` pid=${entry.pid}` : ''}${entry.tag ? ` ${entry.tag}:` : ''} ${entry.text}`;
}

function streamKey(source, metadata) {
  return `${source}:${metadata.stream || ''}:${metadata.tag || ''}:${metadata.level || ''}`;
}

// Capture belongs to the application, not the panel or the running game.
// Retained data is bounded and gets best-effort redaction before disk or renderer.
export class LiveDiagnostics {
  constructor({ directory, getConfig, onUpdate = () => {}, spawnProcess = spawn, maxEntries = MAX_DIAGNOSTIC_ENTRIES, maxBytes = MAX_BYTES, adbBusy = () => false }) {
    Object.assign(this, { directory, getConfig, onUpdate, spawnProcess, maxEntries, maxBytes, adbBusy });
    this.entries = []; this.head = 0; this.bytes = 0; this.lastId = 0; this.dropped = 0;
    this.partials = new Map(); this.files = new Map(); this.children = new Set();
    this.startedAt = new Date().toISOString();
    this.android = { state: 'waiting', detail: 'Waiting for Android.', serial: '', lastReceivedAt: null };
    this.active = false; this.generation = 0; this.nextConnect = 0; this.dirty = false;
    this.persistTask = Promise.resolve(); this.lastPersist = 0;
  }
  async start() {
    if (this.active) return;
    this.active = true;
    try {
      const file = path.join(this.directory, 'recent.json');
      const stat = await fs.stat(file);
      if (stat.size <= 8 * MAX_BYTES) {
        const saved = JSON.parse(await fs.readFile(file, 'utf8'));
        if (Array.isArray(saved.entries)) for (const entry of saved.entries.slice(-this.maxEntries)) {
          if (SOURCES.has(entry.source) && typeof entry.text === 'string') {
            this.append(entry.source, entry.text, { ...entry, receivedAt: typeof entry.receivedAt === 'string' ? entry.receivedAt : this.startedAt });
          }
        }
      }
    } catch (error) {
      if (error.code !== 'ENOENT') this.append('launcher', `Previous diagnostics could not be read: ${error.message}`, { level: 'W', tag: 'diagnostics' });
    }
    this.append('launcher', 'Launcher session started. Times are host receipt times; guest times are retained separately.', { tag: 'diagnostics' });
    this.loopTask = this.tick();
  }
  append(source, text, metadata = {}) {
    if (!SOURCES.has(source)) throw new Error('Invalid diagnostic source.');
    for (const raw of String(text ?? '').replaceAll('\r', '').split('\n')) {
      if (!raw) continue;
      const match = source === 'android' ? raw.match(logcatLine) : null;
      const level = match?.[3] || metadata.level || (/\b(?:fatal|panic)\b/i.test(raw) ? 'F' : /\b(?:error|failed|exception)\b/i.test(raw) ? 'E' : /\bwarn(?:ing)?\b/i.test(raw) ? 'W' : 'I');
      const fields = {
        receivedAt: metadata.receivedAt || new Date().toISOString(), source,
        level: LEVELS.has(level) ? level : 'I',
        tag: redact(String(match?.[4]?.trim() || metadata.tag || '')).slice(0, 256),
        pid: match?.[2] || (metadata.pid == null ? null : String(metadata.pid).slice(0, 32)),
        guestTime: match?.[1] || (metadata.guestTime == null ? null : String(metadata.guestTime).slice(0, 64)),
      };
      const safe = redact((match?.[5] ?? raw).replace(/\x1b\[[0-?]*[ -/]*[@-~]/g, ''));
      // Redact before splitting so long lines keep their non-secret context.
      for (let offset = 0; offset < safe.length; offset += MAX_LINE) {
        const entry = { id: ++this.lastId, ...fields, text: safe.slice(offset, offset + MAX_LINE) };
        const bytes = Buffer.byteLength(JSON.stringify(entry));
        this.entries.push({ entry, bytes }); this.bytes += bytes;
        while (this.entries.length - this.head > this.maxEntries || this.bytes > this.maxBytes) {
          this.bytes -= this.entries[this.head++].bytes; this.dropped++;
        }
        if (this.head > this.maxEntries) { this.entries = this.entries.slice(this.head); this.head = 0; }
        this.dirty = true;
      }
    }
    this.notify();
  }
  write(source, chunk, metadata = {}) {
    const key = streamKey(source, metadata);
    const text = (this.partials.get(key)?.text || '') + String(chunk);
    const lines = text.split('\n');
    let rest = lines.pop();
    for (const line of lines) this.append(source, line, metadata);
    // Bound unfinished output without throwing away the whole line. Extremely
    // long unterminated lines are redacted in chunks on a best-effort basis.
    if (rest.length > MAX_BYTES) { this.append(source, rest, metadata); rest = ''; }
    if (rest) this.partials.set(key, { source, metadata, text: rest });
    else this.partials.delete(key);
    if (metadata.end) this.flush(source, metadata);
  }
  flush(source, metadata) {
    const selected = metadata ? streamKey(source, metadata) : null;
    for (const [key, partial] of this.partials) if (selected ? key === selected : !source || partial.source === source) {
      this.append(partial.source, partial.text, partial.metadata);
      this.partials.delete(key);
    }
  }
  notify() {
    if (this.updateTimer || !this.active) return;
    this.updateTimer = setTimeout(() => { this.updateTimer = null; this.onUpdate(); }, 250);
    this.updateTimer.unref?.();
  }
  setAndroid(state, detail, serial = this.android.serial) {
    detail = redact(detail);
    if (this.android.state === state && this.android.detail === detail && this.android.serial === serial) return;
    this.android = { ...this.android, state, detail, serial };
    this.append('launcher', `${serial || 'Android'}: ${detail}`, { tag: 'logcat', level: state === 'error' ? 'E' : state === 'disconnected' ? 'W' : 'I' });
  }
  snapshot(afterId = 0) {
    const retained = this.entries.slice(this.head);
    return { entries: retained.filter(({ entry }) => entry.id > afterId).map(({ entry }) => entry),
      firstId: retained[0]?.entry.id ?? this.lastId + 1, lastId: this.lastId, dropped: this.dropped,
      android: { ...this.android }, startedAt: this.startedAt };
  }
  text() {
    return `Android capture: ${this.android.state} — ${this.android.detail}\n${this.dropped ? `[${this.dropped} older entries omitted by capture limits]\n` : ''}${this.entries.slice(this.head).map(({ entry }) => format(entry)).join('\n')}`;
  }
  async tick() {
    if (!this.active) return;
    try {
      const config = this.getConfig();
      const signature = JSON.stringify([config.sdk, config.avd, config.port, config.dataHome]);
      if (signature !== this.configSignature) {
        this.configSignature = signature; this.generation++;
        for (const child of this.children) child.kill();
        this.stream = null; this.nextConnect = 0;
        for (const cursor of this.files.values()) this.write(cursor.source, cursor.decoder.end(), { tag: cursor.label, end: true });
        this.files.clear();
        this.flush('android', { tag: 'logcat' });
        this.flush('launcher', { tag: 'logcat', level: 'W' });
        this.android.lastReceivedAt = null;
        this.setAndroid('waiting', `Waiting for ${config.avd || 'the configured Android device'}.`, '');
      }
      await this.tailFiles(config.dataHome);
      // Setup and emulator startup drive ADB themselves. Two clients racing
      // to fork a server on the same port reset each other, and a capture
      // that is only watching must not be what breaks the work it watches.
      if (this.adbBusy()) { this.nextConnect = Math.max(this.nextConnect, Date.now() + 2000); }
      else if (!this.stream && !this.connectTask && Date.now() >= this.nextConnect) {
        this.connectTask = this.connect(config, this.generation).finally(() => { this.connectTask = null; });
      }
      if (this.stream && this.android.state === 'connecting' && Date.now() - this.streamStarted > 10000) {
        this.setAndroid('error', 'ADB is online but logcat has not produced output. Android may be stalled.');
        this.stream.kill();
      }
      if (this.dirty && Date.now() - this.lastPersist >= 2000) await this.persist();
    } catch (error) {
      if (this.lastFailure !== error.message) { this.lastFailure = error.message; this.append('launcher', `Diagnostics: ${error.message}`, { level: 'E', tag: 'diagnostics' }); }
    } finally {
      if (this.active) {
        this.tickTimer = setTimeout(() => { this.loopTask = this.tick(); }, 750);
        this.tickTimer.unref?.();
      }
    }
  }
  async tailFiles(dataHome) {
    if (!dataHome) return;
    for (const [label, file] of diagnosticSources(dataHome).filter(([label]) => /\.(?:log|err)$/.test(label))) {
      const source = label.startsWith('host.') ? 'host' : 'emulator';
      let handle;
      try {
        handle = await fs.open(file, 'r');
        const stat = await handle.stat();
        let cursor = this.files.get(file);
        if (!cursor || cursor.ino !== stat.ino || stat.size < cursor.offset || (stat.size === cursor.offset && stat.mtimeMs !== cursor.mtime)) {
          if (cursor) this.write(source, cursor.decoder.end(), { tag: label, end: true });
          cursor = { offset: await diagnosticTailOffset(handle, stat.size, READ_BYTES), ino: stat.ino, mtime: stat.mtimeMs, decoder: new StringDecoder('utf8'), source, label };
          this.files.set(file, cursor);
          if (cursor.offset) this.append(source, `[Reading the last ${READ_BYTES} bytes of ${label}]`, { tag: label, level: 'W' });
        }
        if (stat.size - cursor.offset > 4 * READ_BYTES) {
          this.write(source, cursor.decoder.end(), { tag: label, end: true });
          cursor.offset = await diagnosticTailOffset(handle, stat.size, READ_BYTES); cursor.decoder = new StringDecoder('utf8');
          this.append(source, `[Skipped older ${label} output to keep capture responsive]`, { tag: label, level: 'W' });
        }
        const length = Math.min(READ_BYTES, stat.size - cursor.offset);
        if (length > 0) {
          const buffer = Buffer.allocUnsafe(length);
          const { bytesRead } = await handle.read(buffer, 0, length, cursor.offset);
          cursor.offset += bytesRead; cursor.mtime = stat.mtimeMs;
          this.write(source, cursor.decoder.write(buffer.subarray(0, bytesRead)), { tag: label });
        }
      } catch (error) {
        if (error.code !== 'ENOENT' && error.code !== 'EACCES') throw error;
      } finally { await handle?.close(); }
    }
  }
  child(executable, args) {
    const env = { ...process.env, ANDROID_ADB_SERVER_PORT: '5038', ADB_LOCAL_TRANSPORT_MAX_PORT: '5683' };
    delete env.ADB_SERVER_SOCKET;
    const child = this.spawnProcess(executable, args, { windowsHide: true, shell: false, stdio: ['ignore', 'pipe', 'pipe'], env });
    this.children.add(child);
    child.once('close', () => this.children.delete(child));
    child.stdout.setEncoding('utf8'); child.stderr.setEncoding('utf8');
    return child;
  }
  probe(executable, args, timeout = ADB_PROBE_TIMEOUT) {
    return new Promise(resolve => {
      let output = '', error = '', finished = false;
      const child = this.child(executable, args);
      const finish = result => { if (finished) return; finished = true; clearTimeout(timer); resolve(result); };
      // Killing a client that is still waiting for the server it just forked
      // leaves the next client reading a closed socket, which is one of the
      // ways the protocol fault below is produced. Allow for a cold start
      // rather than interrupting one.
      const timer = setTimeout(() => { child.kill(); finish({ error: 'ADB device discovery timed out.' }); }, timeout);
      child.stdout.on('data', chunk => {
        output += chunk;
        if (output.length > 16384) { output = ''; child.kill(); finish({ error: 'ADB device discovery returned too much output.' }); }
      });
      child.stderr.on('data', chunk => { error = (error + chunk).slice(-4096); });
      child.once('error', e => finish({ error: e.message }));
      child.once('close', code => finish(code === 0 ? { output } : { error: error.trim() || output.trim() || 'Android is not connected.' }));
    });
  }
  async discover(executable, config, generation) {
    let listed = await this.probe(executable, ['-P', '5038', 'devices', '-l']);
    // A server that is still starting, or one a departing client took with
    // it, answers once and then works normally. Start it deliberately and ask
    // again before calling the capture broken.
    if (listed.error && transientAdbFault(listed.error)) {
      if (!this.active || generation !== this.generation) return null;
      this.setAndroid('waiting', 'Waiting for the ADB server to accept connections.', '');
      await this.probe(executable, ['-P', '5038', 'start-server'], ADB_SERVER_TIMEOUT);
      if (!this.active || generation !== this.generation) return null;
      listed = await this.probe(executable, ['-P', '5038', 'devices', '-l']);
    }
    if (listed.error) throw new Error(listed.error);
    if (!this.active || generation !== this.generation) return null;
    const devices = listed.output.split(/\r?\n/).flatMap(line => {
      const match = line.match(/^(emulator-\d+)\s+(device|offline|unauthorized)\b/);
      return match ? [{ serial: match[1], state: match[2] }] : [];
    });
    const preferred = `emulator-${config.port}`;
    if (!config.avd) {
      const selected = devices.find(device => device.serial === preferred);
      if (selected?.state === 'device') return selected.serial;
      this.setAndroid('waiting', selected ? `${preferred} is ${selected.state}.` : 'The configured emulator is not connected.', selected?.serial || '');
      return null;
    }
    // AVD names and console ports vary by machine. Discover their relationship
    // rather than attaching to the first emulator, or to a hardcoded name.
    const candidates = devices.filter(device => device.state === 'device').sort((a, b) => Number(b.serial === preferred) - Number(a.serial === preferred));
    const matches = [];
    for (const device of candidates) {
      if (!this.active || generation !== this.generation) return null;
      const name = await this.probe(executable, ['-P', '5038', '-s', device.serial, 'emu', 'avd', 'name']);
      if (name.error) {
        if (device.serial === preferred) throw new Error(name.error);
        continue;
      }
      if (name.output.trim().split(/\r?\n/)[0] === config.avd) {
        if (device.serial === preferred) return device.serial;
        matches.push(device.serial);
      }
    }
    if (matches.length === 1) return matches[0];
    if (matches.length > 1) throw new Error(`More than one emulator runs ${config.avd}; select its console port in runtime settings.`);
    this.setAndroid('waiting', `Waiting for AVD ${config.avd}; other connected devices are not being captured.`, '');
    return null;
  }
  async connect(config, generation) {
    this.nextConnect = Date.now() + 3000;
    const executable = path.join(config.sdk || '', 'platform-tools', process.platform === 'win32' ? 'adb.exe' : 'adb');
    try {
      await fs.access(executable);
      if (!this.active || generation !== this.generation) return;
      const serial = await this.discover(executable, config, generation);
      if (!this.active || generation !== this.generation || !serial) return;
      const args = ['-P', '5038', '-s', serial];
      this.setAndroid('connecting', `ADB is online${config.avd ? ` (${config.avd})` : ''}; waiting for logcat output.`, serial);
      this.streamStarted = Date.now();
      const child = this.child(executable, [...args, 'logcat', '-b', 'main', '-b', 'system', '-b', 'crash', '-v', 'threadtime', '-T', '200']);
      this.stream = child;
      let error = '';
      child.stdout.on('data', chunk => {
        if (!this.active || generation !== this.generation) return;
        this.android.lastReceivedAt = new Date().toISOString();
        this.setAndroid('streaming', 'Receiving Android logs.');
        this.write('android', chunk, { tag: 'logcat' });
      });
      child.stderr.on('data', chunk => {
        if (!this.active || generation !== this.generation) return;
        error = (error + chunk).slice(-4096);
        this.write('launcher', chunk, { tag: 'logcat', level: 'W' });
      });
      child.once('error', e => { error = e.message; });
      child.once('close', code => {
        if (!this.active || generation !== this.generation) return;
        this.flush('android', { tag: 'logcat' }); this.flush('launcher', { tag: 'logcat', level: 'W' });
        this.stream = null; this.nextConnect = Date.now() + 3000;
        this.setAndroid(code ? 'error' : 'disconnected', error.trim() || 'Logcat disconnected; waiting to reconnect.');
      });
    } catch (error) {
      if (!this.active || generation !== this.generation) return;
      if (error.code === 'ENOENT') {
        this.setAndroid('error', 'ADB is not installed at the configured SDK path. Capture will start when it is available.');
      } else if (transientAdbFault(error.message)) {
        // Back off further than the normal retry: these clear on their own
        // once the server settles, and hammering it is what keeps them going.
        this.nextConnect = Date.now() + 10000;
        this.setAndroid('waiting', 'The ADB server is not answering yet; retrying.');
      } else {
        this.setAndroid('error', error.message);
      }
    }
  }
  async persist() {
    if (!this.dirty) return this.persistTask;
    this.dirty = false; this.lastPersist = Date.now();
    const content = JSON.stringify({ entries: this.entries.slice(this.head).map(({ entry }) => entry) });
    this.persistTask = this.persistTask.then(async () => {
      try {
        await fs.mkdir(this.directory, { recursive: true });
        const file = path.join(this.directory, 'recent.json');
        await fs.writeFile(`${file}.tmp`, content);
        await fs.rename(`${file}.tmp`, file);
        this.persistenceFailed = false;
      } catch (error) {
        if (!this.persistenceFailed) this.append('launcher', `Cannot save diagnostic history: ${error.message}`, { level: 'E', tag: 'diagnostics' });
        this.persistenceFailed = true;
      }
    });
    return this.persistTask;
  }
  async stop() {
    if (!this.active) return;
    this.active = false; this.generation++;
    clearTimeout(this.tickTimer); clearTimeout(this.updateTimer); this.updateTimer = null;
    for (const child of this.children) child.kill();
    await this.connectTask;
    await this.loopTask;
    for (const cursor of this.files.values()) this.write(cursor.source, cursor.decoder.end(), { tag: cursor.label, end: true });
    this.stream = null; this.flush();
    this.setAndroid('stopped', 'Capture stopped; retained logs remain available.');
    await this.persist();
  }
}
