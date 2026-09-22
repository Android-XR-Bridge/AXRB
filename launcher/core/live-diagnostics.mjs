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
const LEVELS = new Set(['V', 'D', 'I', 'W', 'E', 'F']);
const logcatLine = /^(\d\d-\d\d\s+\d\d:\d\d:\d\d\.\d+)\s+(\d+)\s+\d+\s+([VDIWEF])\s+([^:]+):\s?(.*)$/;
// Guest kernel-console shapes. These facilities carry stock boot chatter that
// keyword inference mistakes for failures; kernelCrash lists signatures that
// are never demoted. A timestamp alone is not enough: timestamped lines from
// other facilities (binder, binder_alloc, …) report real guest failures and
// keep their severity.
const kernelTimestamp = /^\[?\s*\d+\.\d+\]?\s*/;
const bootChatterFacility = /^(?:init:|ueventd:|vold:|selinux:|apexd:|servicemanager:|libprocessgroup:|cutils-trace:|logd:|cfg80211:|UprobeStatsBpfLoad:|NetBpfLoad:)/;
const isBootChatter = text => bootChatterFacility.test(text) || (kernelTimestamp.test(text) && bootChatterFacility.test(text.replace(kernelTimestamp, '')));
const kernelCrash = /panic|oops|\bBUG\b|unable to handle|call trace|fatal signal/i;
const perfLine = /^AXRB\.Perf ([\w.-]+): rate=([\d.]+)\/s avg=([\d.]+)ms .*p50=([\d.]+)ms p95=([\d.]+)ms p99=([\d.]+)ms/;
const PERF_WINDOWS = 12;
const PERF_SLOW_FRAME_MS = 250;
const PERF_WARN_INTERVAL_MS = 60000;

function format(entry) {
  return `${entry.receivedAt} [${entry.source}/${entry.level}]${entry.guestTime ? ` [guest ${entry.guestTime}]` : ''}${entry.pid ? ` pid=${entry.pid}` : ''}${entry.tag ? ` ${entry.tag}:` : ''} ${entry.text}`;
}

function streamKey(source, metadata) {
  return `${source}:${metadata.stream || ''}:${metadata.tag || ''}:${metadata.level || ''}`;
}

// Retention classes: guest console chatter (emulator V/D/I) is sacrificed
// first; launcher, host and android lines plus every W/E/F entry survive floods.
function isClassB(entry) {
  return entry.source === 'emulator' && 'VDI'.includes(entry.level);
}

// Keyword inference alone misreads two measured cases: stock guest boot chatter
// carries "error"/"warning" words, and a Windows loader that succeeded still
// reports its ERROR_SUCCESS code as "(Windows error 0)".
function inferLevel(source, text) {
  const level = /\b(?:fatal|panic)\b/i.test(text) ? 'F' : /\b(?:error|failed|exception)\b/i.test(text) ? 'E' : /\bwarn(?:ing)?\b/i.test(text) ? 'W' : 'I';
  if (level === 'F') return level;
  if (source === 'emulator' && level !== 'I' && isBootChatter(text) && !kernelCrash.test(text)) return 'D';
  if (source === 'host' && level === 'E' && reportsWindowsSuccess(text)) return 'I';
  return level;
}

function reportsWindowsSuccess(text) {
  if (!text.includes('(Windows error 0)')) return false;
  const remainder = text.replaceAll('(Windows error 0)', '');
  return text.endsWith('loaded (Windows error 0)') || !/\b(?:fatal|panic|error|failed|exception)\b/i.test(remainder);
}

function freshAttach() {
  return { argv: null, failure: null, retries: 0 };
}

// Frame-pipeline counters whose sustained p95 means the captured experience degraded.
function tracksFramePipeline(counter) {
  return counter === 'host-selected-frame-age' || counter.endsWith('end-frame');
}

// Capture belongs to the application, not the panel or the running game.
// Retained data is bounded and gets best-effort redaction before disk or renderer.
export class LiveDiagnostics {
  constructor({ directory, getConfig, onUpdate = () => {}, spawnProcess = spawn, maxEntries = MAX_DIAGNOSTIC_ENTRIES, maxBytes = MAX_BYTES }) {
    Object.assign(this, { directory, getConfig, onUpdate, spawnProcess, maxEntries, maxBytes });
    this.entries = []; this.head = 0; this.bytes = 0; this.lastId = 0; this.dropped = 0; this.classB = 0;
    this.evictions = []; this.evictionSeq = 0; this.replaying = false;
    this.partials = new Map(); this.files = new Map(); this.children = new Set();
    this.perf = new Map(); this.perfWarnedAt = new Map(); this.missListing = null; this.attach = freshAttach();
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
        // History already holds any perf warnings it produced, and its perf
        // windows describe a previous launcher run, so replay skips perf.
        this.replaying = true;
        try {
          if (Array.isArray(saved.entries)) for (const entry of saved.entries.slice(-this.maxEntries)) {
            if (SOURCES.has(entry.source) && typeof entry.text === 'string') {
              this.append(entry.source, entry.text, { ...entry, receivedAt: typeof entry.receivedAt === 'string' ? entry.receivedAt : this.startedAt });
            }
          }
        } finally { this.replaying = false; }
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
      const level = match?.[3] || metadata.level || inferLevel(source, raw);
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
        if (isClassB(entry)) this.classB++;
        while (this.entries.length - this.head > this.maxEntries || this.bytes > this.maxBytes) {
          // Evict the oldest boot-chatter entry while any exists so guest
          // floods cannot displace launcher, host and failure lines; only
          // once no chatter remains does the oldest entry go.
          let victim = this.head;
          if (this.classB) {
            for (let index = this.head; index < this.entries.length; index++) if (isClassB(this.entries[index].entry)) { victim = index; break; }
          }
          const item = this.entries[victim];
          this.bytes -= item.bytes; this.dropped++;
          if (isClassB(item.entry)) this.classB--;
          if (victim === this.head) this.head++;
          else {
            // Readers holding this entry cannot infer its removal from firstId.
            this.entries.splice(victim, 1);
            this.evictions.push(item.entry.id); this.evictionSeq++;
            if (this.evictions.length > this.maxEntries) this.evictions.shift();
          }
        }
        if (this.head > this.maxEntries) { this.entries = this.entries.slice(this.head); this.head = 0; }
        this.dirty = true;
      }
      // Record the sample only after its own line is retained, so a degraded
      // pipeline warning follows the window that triggered it.
      const perf = !this.replaying && (source === 'host' || source === 'emulator' || source === 'android') ? (match?.[5] ?? raw).match(perfLine) : null;
      if (perf) this.recordPerf(perf[1], fields.receivedAt, perf);
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
  setAndroid(state, detail, serial = this.android.serial, { quiet = false } = {}) {
    detail = redact(detail);
    if (this.android.state === state && this.android.detail === detail && this.android.serial === serial) return;
    this.android = { ...this.android, state, detail, serial };
    if (!quiet) this.append('launcher', `${serial || 'Android'}: ${detail}`, { tag: 'logcat', level: state === 'error' ? 'E' : state === 'disconnected' ? 'W' : 'I' });
  }
  // An attach episode runs from the first logcat attempt until one produces
  // output. Its first failure is reported in full; identical retries only move
  // the panel state and are counted, so a logcat failing every few seconds
  // cannot fill the flood-proof launcher lines with copies of one error.
  attachFailed(state, detail) {
    detail = redact(detail);
    const repeat = detail === this.attach.failure;
    if (repeat) this.attach.retries++;
    else { this.reportRetries(); this.attach.failure = detail; }
    this.setAndroid(state, detail, this.android.serial, { quiet: repeat });
  }
  reportRetries() {
    const { failure, retries } = this.attach;
    if (retries) this.append('launcher', `Logcat failure repeated ${retries} more time${retries === 1 ? '' : 's'}: ${failure}`, { level: 'W', tag: 'logcat' });
    this.attach.retries = 0;
  }
  endAttachEpisode() {
    this.reportRetries();
    this.attach = freshAttach();
  }
  // afterEviction is the evictionSeq a reader last saw. evicted lists ids removed
  // from the middle since then; when that log no longer reaches back far
  // enough, reset asks the reader to replace its entries wholesale.
  snapshot(afterId = 0, afterEviction = this.evictionSeq) {
    const retained = this.entries.slice(this.head);
    const missed = this.evictionSeq - afterEviction;
    const reset = missed < 0 || missed > this.evictions.length;
    return { entries: retained.filter(({ entry }) => reset || entry.id > afterId).map(({ entry }) => entry),
      reset, evicted: reset || !missed ? [] : this.evictions.slice(-missed), evictionSeq: this.evictionSeq,
      firstId: retained[0]?.entry.id ?? this.lastId + 1, lastId: this.lastId, dropped: this.dropped,
      android: { ...this.android }, startedAt: this.startedAt };
  }
  text() {
    const repeats = this.attach.retries ? ` (repeated ${this.attach.retries} more time${this.attach.retries === 1 ? '' : 's'})` : '';
    return `Android capture: ${this.android.state} — ${this.android.detail}${repeats}\n${this.dropped ? `[${this.dropped} entries omitted by capture limits]\n` : ''}${this.entries.slice(this.head).map(({ entry }) => format(entry)).join('\n')}`;
  }
  recordPerf(counter, receivedAt, match) {
    let series = this.perf.get(counter);
    if (!series) { series = []; this.perf.set(counter, series); }
    series.push({ receivedAt, rate: Number(match[2]), p50: Number(match[4]), p95: Number(match[5]), p99: Number(match[6]) });
    if (series.length > PERF_WINDOWS) series.shift();
    const recent = series.slice(-2);
    if (recent.length < 2 || !tracksFramePipeline(counter) || !recent.every(sample => sample.p95 > PERF_SLOW_FRAME_MS)) return;
    // Rate-limit against entry timestamps, not wall clocks, so the interval
    // follows when the guest or host produced its windows rather than when
    // bursty pipes happened to deliver them.
    const last = this.perfWarnedAt.get(counter);
    if (last && Date.parse(receivedAt) - Date.parse(last) < PERF_WARN_INTERVAL_MS) return;
    this.perfWarnedAt.set(counter, receivedAt);
    this.append('launcher', `frame pipeline degraded: ${counter} p95=${recent[1].p95}ms sustained`, { level: 'W', tag: 'perf' });
  }
  // Each game session starts with fresh windows, so a bundle never presents
  // the previous game's frame timings as the current one's.
  resetPerf() {
    this.perf.clear(); this.perfWarnedAt.clear();
  }
  perfText() {
    // Collector contract: one line per counter in first-seen order describing
    // its latest window; '' when no perf lines were seen.
    return [...this.perf].map(([counter, series]) => {
      const latest = series[series.length - 1];
      return `${counter}: rate=${latest.rate.toFixed(1)}/s p50=${latest.p50.toFixed(3)}ms p95=${latest.p95.toFixed(3)}ms p99=${latest.p99.toFixed(3)}ms (${series.length} window${series.length === 1 ? '' : 's'})`;
    }).join('\n');
  }
  async tick() {
    if (!this.active) return;
    try {
      const config = this.getConfig();
      const signature = JSON.stringify([config.sdk, config.avd, config.port, config.dataHome]);
      if (signature !== this.configSignature) {
        this.configSignature = signature; this.generation++;
        this.endAttachEpisode();
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
      if (!this.stream && !this.connectTask && Date.now() >= this.nextConnect) {
        this.connectTask = this.connect(config, this.generation).finally(() => { this.connectTask = null; });
      }
      if (this.stream && this.android.state === 'connecting' && Date.now() - this.streamStarted > 10000) {
        // The stall is the failure; the close that follows the kill is not
        // reported again as a disconnect.
        this.stream.stalled = true;
        this.attachFailed('error', 'ADB is online but logcat has not produced output. Android may be stalled.');
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
  probe(executable, args) {
    return new Promise(resolve => {
      let output = '', error = '', finished = false;
      const child = this.child(executable, args);
      const finish = result => { if (finished) return; finished = true; clearTimeout(timer); resolve(result); };
      const timer = setTimeout(() => { child.kill(); finish({ error: 'ADB device discovery timed out.' }); }, 3000);
      child.stdout.on('data', chunk => {
        output += chunk;
        if (output.length > 16384) { output = ''; child.kill(); finish({ error: 'ADB device discovery returned too much output.' }); }
      });
      child.stderr.on('data', chunk => { error = (error + chunk).slice(-4096); });
      child.once('error', e => finish({ error: e.message }));
      child.once('close', code => finish(code === 0 ? { output } : { error: error.trim() || output.trim() || 'Android is not connected.' }));
    });
  }
  noteMissingDevice(listed) {
    // setAndroid dedupes repeated waiting states into silence, so surface the
    // raw probe output whenever it changes: a capture that never attaches stays
    // explainable, while an idle launcher with no emulator stays quiet. No
    // device yet is the normal waiting state, not a warning.
    const listing = redact(listed.output).slice(0, 400);
    if (listing === this.missListing) return;
    this.missListing = listing;
    this.append('launcher', `No matching Android device; adb devices -l reports:\n${listing}`, { tag: 'logcat' });
  }
  async discover(executable, config, generation) {
    const listed = await this.probe(executable, ['-P', '5038', 'devices', '-l']);
    if (listed.error) throw new Error(listed.error);
    if (!this.active || generation !== this.generation) return null;
    const devices = listed.output.split(/\r?\n/).flatMap(line => {
      const match = line.match(/^(emulator-\d+)\s+(device|offline|unauthorized)\b/);
      return match ? [{ serial: match[1], state: match[2] }] : [];
    });
    const preferred = `emulator-${config.port}`;
    if (!config.avd) {
      const selected = devices.find(device => device.serial === preferred);
      if (selected?.state === 'device') { this.missListing = null; return selected.serial; }
      this.setAndroid('waiting', selected ? `${preferred} is ${selected.state}.` : 'The configured emulator is not connected.', selected?.serial || '');
      this.noteMissingDevice(listed);
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
        if (device.serial === preferred) { this.missListing = null; return device.serial; }
        matches.push(device.serial);
      }
    }
    if (matches.length === 1) { this.missListing = null; return matches[0]; }
    if (matches.length > 1) throw new Error(`More than one emulator runs ${config.avd}; select its console port in runtime settings.`);
    this.setAndroid('waiting', `Waiting for AVD ${config.avd}; other connected devices are not being captured.`, '');
    this.noteMissingDevice(listed);
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
      const retrying = this.attach.failure !== null;
      this.setAndroid('connecting', `ADB is online${config.avd ? ` (${config.avd})` : ''}; waiting for logcat output.`, serial, { quiet: retrying });
      this.streamStarted = Date.now();
      const logcatArgs = [...args, 'logcat', '-b', 'main', '-b', 'system', '-b', 'crash', '-v', 'threadtime', '-T', '200'];
      const child = this.child(executable, logcatArgs);
      // A capture that exits immediately otherwise vanishes without a trace of
      // what was attempted; the exact argv is the shortest reproduction clue.
      // Logged once per attach episode so a logcat that keeps exiting before
      // any output does not repeat it every retry.
      const argv = [path.basename(executable), ...logcatArgs].join(' ');
      if (argv !== this.attach.argv) { this.attach.argv = argv; this.append('launcher', argv, { level: 'I', tag: 'logcat' }); }
      this.stream = child;
      let error = '', produced = false;
      child.stdout.on('data', chunk => {
        if (!this.active || generation !== this.generation) return;
        this.android.lastReceivedAt = new Date().toISOString();
        if (!produced) { produced = true; this.endAttachEpisode(); }
        this.setAndroid('streaming', 'Receiving Android logs.');
        this.write('android', chunk, { tag: 'logcat' });
      });
      child.stderr.on('data', chunk => {
        if (!this.active || generation !== this.generation) return;
        error = (error + chunk).slice(-4096);
        // A retry's stderr is the failure detail reported below; only the
        // first attempt of an episode, or a live stream, logs it as lines.
        if (produced || !retrying) this.write('launcher', chunk, { tag: 'logcat', level: 'W' });
      });
      child.once('error', e => { error = e.message; });
      child.once('close', code => {
        if (!this.active || generation !== this.generation) return;
        this.flush('android', { tag: 'logcat' }); this.flush('launcher', { tag: 'logcat', level: 'W' });
        this.stream = null; this.nextConnect = Date.now() + 3000;
        if (child.stalled) return;
        const state = code ? 'error' : 'disconnected', detail = error.trim() || 'Logcat disconnected; waiting to reconnect.';
        if (produced) this.setAndroid(state, detail);
        else this.attachFailed(state, detail);
      });
    } catch (error) {
      if (this.active && generation === this.generation) this.setAndroid('error', error.code === 'ENOENT' ? 'ADB is not installed at the configured SDK path. Capture will start when it is available.' : error.message);
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
