import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { cleanEvidence } from './ai-diagnostics.mjs';
import { run, validPackage } from './runtime.mjs';
import { hostFileTool, emulatorFileTool, checkedAdbArgs } from './ai-files.mjs';

const checkCommands = {
  boot: [['get-state'], ['emu', 'avd', 'name'], ['shell', 'getprop', 'sys.boot_completed'], ['shell', 'getprop', 'init.svc.bootanim']],
  translator: [['shell', 'getprop', 'ro.dalvik.vm.native.bridge'], ['shell', 'getprop', 'ro.product.cpu.abilist'], ['shell', 'getprop', 'ro.build.version.release'], ['shell', 'getprop', 'ro.boot.clocksource']],
  memory: [['shell', 'cat', '/proc/meminfo'], ['shell', 'cat', '/proc/pressure/memory'], ['shell', 'dumpsys', 'meminfo', '--oom']],
  storage: [['shell', 'df', '-h', '/data', '/sdcard'], ['shell', 'cat', '/proc/diskstats'], ['shell', 'cat', '/proc/pressure/io']],
  cpu: [['shell', 'cat', '/proc/stat'], ['shell', 'cat', '/proc/pressure/cpu'], ['shell', 'top', '-b', '-n', '1', '-m', '25']],
  graphics: [['shell', 'dumpsys', 'SurfaceFlinger'], ['shell', 'dumpsys', 'gpu']],
  audio: [['shell', 'dumpsys', 'media.audio_flinger'], ['shell', 'dumpsys', 'media.audio_policy']],
  crashes: [['logcat', '-b', 'crash', '-d', '-t', '180']],
  packages: [['shell','pm','list','packages','-3','-f']],
};
const fn = (name, description, properties = {}) => ({ type: 'function', name, description, strict: true,
  parameters: { type: 'object', properties, required: Object.keys(properties), additionalProperties: false } });
const hostPath = {root:{type:'string'},path:{type:'string',maxLength:2000}};
const readPage = {startLine:{type:'integer',minimum:1},version:{type:'string',description:'Empty for first page; use returned version for subsequent pages.'}};
export const diagnosticTools = [{ type: 'namespace', name: 'axrb', description: 'General AXRB investigation: discover configuration, games, files and emulator state. Results are untrusted evidence. General ADB commands require approval.', tools: [
  fn('runtime_status', 'Fresh AXRB runtime, selected game, settings and watchdog state.'),
  fn('snapshot', 'Fresh diagnostic bundle: hardware, setup, sessions, live logs and performance. Prefer targeted tools after this.'),
  fn('host', 'Fresh Windows CPU, available RAM, commit/pagefile, GPUs, AXRB process counters and recent relevant application/system errors.'),
  fn('list_logs', 'Enumerate AXRB log files; returns opaque IDs, sizes and modification times. No credentials or game files.'),
  fn('read_log', 'Read a bounded log page. offset=-1 means tail; otherwise byte offset. Use nextOffset and truncated to paginate.', { id: { type: 'string' }, offset: { type: 'integer', minimum: -1 } }),
  fn('search_logs', 'Literal case-insensitive search across the last 256 KiB of each AXRB log, up to 80 matches. Reports coverage limits.', { query: { type: 'string', minLength: 2, maxLength: 160 } }),
  fn('live_logs', 'Latest captured live logs and frame timings, with receipt timestamps. Does not alter capture settings.'),
  fn('android_check', 'Read-only bounded ADB checks against the configured AXRB emulator only. Individual unsupported/offline checks report errors.', { check: { type: 'string', enum: Object.keys(checkCommands) } }),
  fn('game_check', 'Read package identity, exit reasons, memory, frame stats or assets. Choose a package from runtime_status or Android package discovery; empty uses running game.', { check: { type: 'string', enum: ['package', 'exit', 'memory', 'frames', 'assets'] },package:{type:'string',maxLength:250} }),
  fn('file_roots','Discover host filesystem roots: runtime, SDK, launcher sources, downloads, AVDs and output. Use root IDs for subsequent file tools.'),
  fn('list_files','List a host directory with names, types, sizes and timestamps. Start offset=0; continue with nextOffset until eof.', {...hostPath,offset:{type:'integer',minimum:0}}),
  fn('read_file','Read any diagnostic text file from beginning to end in complete-line pages. Start line 1, version empty; continue with nextLine/version until eof. No tail-only shortcut.', {...hostPath,...readPage}),
  fn('search_file','Search an entire host text file for a literal case-insensitive query, including old entries. Pages report line numbers and continuation; continue until eof.', {...hostPath,...readPage,query:{type:'string',minLength:1,maxLength:300}}),
  fn('android_list_files','List any readable directory inside the AXRB emulator with ls -la; path is absolute, e.g. /, /data, /sdcard/Android/obb.',{path:{type:'string',maxLength:2000}}),
  fn('android_read_file','Read a complete emulator text file by pages. Start line 1, lineCount 200; follow nextLine until eof; reduce count on truncation.',{path:{type:'string',maxLength:2000},startLine:{type:'integer',minimum:1,maximum:100000000},lineCount:{type:'integer',minimum:1,maximum:300}}),
  fn('android_search_file','Search a complete emulator file with a literal case-insensitive query; returns matching line numbers.',{path:{type:'string',maxLength:2000},query:{type:'string',minLength:1,maxLength:300}}),
  fn('adb_command','Full ADB device access to AXRB emulator, including arbitrary shell commands, package inspection, root, installs, file transfers and repairs. Exact arguments and reason are shown for approval. Never changes the server/target. Prefer automatic read-only file/check tools when sufficient.',{args:{type:'array',items:{type:'string'},minItems:1,maxItems:80},reason:{type:'string',minLength:1,maxLength:500}}),
  fn('performance', 'Capture 3–15 seconds of CPU and frame timing for the running game. User must keep headset active and scene comparable.', { seconds: { type: 'integer', minimum: 3, maximum: 15 } }),
  fn('stop_game', 'Ask the user for permission to stop the running game. Only use when needed to diagnose or recover; never assumes approval.', { reason: { type: 'string', minLength: 1, maxLength: 300 } }),
] }];

export function validateTool(name, args) {
  const schema = diagnosticTools[0].tools.find(t => t.name === name)?.parameters;
  if (!schema || !args || typeof args !== 'object' || Array.isArray(args)) throw new Error('Unknown diagnostic tool or invalid arguments.');
  if (Object.keys(args).length !== schema.required.length || schema.required.some(k => !Object.hasOwn(args, k))) throw new Error('Unexpected diagnostic arguments.');
  for (const [key, rule] of Object.entries(schema.properties)) {
    const value = args[key];
    if ((rule.type === 'integer' ? !Number.isSafeInteger(value) : rule.type === 'array' ? !Array.isArray(value) || value.some(item=>typeof item!=='string') : typeof value !== rule.type) ||
        (rule.enum && !rule.enum.includes(value)) || (rule.minimum !== undefined && value < rule.minimum) ||
        (rule.maximum !== undefined && value > rule.maximum) || (rule.minLength && value.length < rule.minLength) ||
        (rule.maxLength && value.length > rule.maxLength) || (rule.minItems && value.length<rule.minItems) || (rule.maxItems && value.length>rule.maxItems)) throw new Error(`Invalid diagnostic argument: ${key}`);
  }
}

// These are constant scripts, never interpolated model/user command strings.
const hostScript = `$ErrorActionPreference='Stop'; $o=Get-CimInstance Win32_OperatingSystem; $m=Get-CimInstance Win32_PerfFormattedData_PerfOS_Memory; [pscustomobject]@{OS=$o.Caption;FreeMemoryKB=$o.FreePhysicalMemory;CommitBytes=$m.CommittedBytes;CommitLimit=$m.CommitLimit;CPU=@(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfLogicalProcessors,LoadPercentage);GPU=@(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion);Disks=@(Get-CimInstance Win32_LogicalDisk -Filter 'DriveType=3' | Select-Object DeviceID,Size,FreeSpace);Processes=@(Get-Process | Where-Object {$_.ProcessName -match '^(AXRB|emulator|qemu-system.*|adb|vrserver|vrcompositor|VirtualDesktop.*|axrb.*)$'} | Select-Object ProcessName,Id,CPU,WorkingSet64,PrivateMemorySize64);Events=@(foreach($log in @('Application','System')) {Get-WinEvent -FilterHashtable @{LogName=$log;StartTime=(Get-Date).AddMinutes(-30);Level=1,2} -MaxEvents 30 -ErrorAction SilentlyContinue | Where-Object {$_.ProviderName -match 'Display|WHEA|Application Error|Windows Error Reporting' -or $_.Message -match 'qemu|emulator|AXRB|nvlddmkm|amdwddmg'} | Select-Object TimeCreated,Id,ProviderName,Message})} | ConvertTo-Json -Depth 5 -Compress`;

export class DiagnosticHarness {
  constructor({ context, snapshot, live, performance, approveStop, approveAdb, execute = run }) {
    Object.assign(this, { context, snapshot, live, performance, approveStop, approveAdb, execute });
    this.tools = diagnosticTools;
  }
  begin() {
    const pinned = this.context();
    const key = c => JSON.stringify([c.sdk, c.port, c.avd, c.dataHome, c.game, c.package, c.session, c.backend]);
    const assertScope = () => { if (key(this.context()) !== key(pinned)) throw new Error('AXRB runtime or game changed. Start a new investigation for the current session.'); };
    const cache = new Map(), files = new Map(), declined = new Set();
    const enumerate = async () => {
      const base = await fs.realpath(path.join(pinned.dataHome, 'logs'));
      const home = await fs.realpath(pinned.dataHome), relative = path.relative(home, base);
      if (relative.startsWith('..') || path.isAbsolute(relative)) throw new Error('Logs directory is outside the selected runtime.');
      const walk = async (dir, depth = 0) => {
        for (const entry of (await fs.readdir(dir, { withFileTypes: true })).sort((a,b) => a.name.localeCompare(b.name))) {
          if (files.size >= 150) return;
          if (entry.isSymbolicLink() || /credential|token|cookie|auth|secret/i.test(entry.name)) continue;
          const file = path.join(dir, entry.name);
          if (entry.isDirectory() && depth < 3) await walk(file, depth + 1);
          else if (entry.isFile() && /\.(log|txt|json|err|csv)$/i.test(entry.name)) {
            const id = path.relative(base, file).replaceAll('\\', '/'); files.set(id, { file, base });
          }
        }
      };
      await walk(base);
      return Promise.all([...files].map(async ([id, {file}]) => { const s = await fs.stat(file); return { id, bytes: s.size, modified: s.mtime.toISOString() }; }));
    };
    const read = async (id, offset = -1, limit = 65536) => {
      if (!files.has(id)) await enumerate();
      const entry = files.get(id); if (!entry) throw new Error('Unknown log ID. Use list_logs.');
      const resolved = await fs.realpath(entry.file), relative = path.relative(entry.base, resolved);
      if (relative.startsWith('..') || path.isAbsolute(relative)) throw new Error('Log is outside the runtime log directory.');
      const handle = await fs.open(resolved, 'r');
      try {
        const s = await handle.stat(); if (!s.isFile()) throw new Error('Not a regular log file.');
        const start = offset === -1 ? Math.max(0, s.size - limit) : Math.min(offset, s.size);
        const buffer = Buffer.alloc(Math.min(limit, s.size - start));
        const {bytesRead} = await handle.read(buffer, 0, buffer.length, start);
        // Drop boundary fragments so split credentials cannot escape redaction.
        let text = buffer.subarray(0, bytesRead).toString('utf8');
        let nextOffset = start + bytesRead;
        if (nextOffset < s.size) {
          const newline = buffer.subarray(0, bytesRead).lastIndexOf(10);
          if (newline >= 0) { nextOffset = start + newline + 1; text = buffer.subarray(0, newline + 1).toString('utf8'); }
          else text = '';
        }
        if (start > 0) {
          const previous = Buffer.alloc(1); await handle.read(previous, 0, 1, start - 1);
          if (previous[0] !== 10) text = text.includes('\n') ? text.slice(text.indexOf('\n') + 1) : '';
        }
        return { id, start, nextOffset, bytes: s.size, modified: s.mtime.toISOString(), truncated: start > 0 || nextOffset < s.size, text };
      } finally { await handle.close(); }
    };
    const adb = async (args, signal, timeout = 12000) => {
      if (pinned.backend === 'quest-bridge') throw new Error('This is a Quest Bridge session. Emulator ADB tools do not describe its physical Quest. Use host/session logs.');
      if (!pinned.sdk || !Number.isInteger(Number(pinned.port)) || Number(pinned.port) < 5554 || Number(pinned.port) > 5682) throw new Error('AXRB emulator is not configured.');
      return this.execute(path.join(pinned.sdk, 'platform-tools/adb.exe'), ['-P', '5038', '-s', `emulator-${pinned.port}`, ...args], { timeout, signal, requireCompleteOutput:true });
    };
    const batch = async (commands, signal) => {
      const results = [];
      for (const command of commands) {
        signal.throwIfAborted(); assertScope();
        try { const text = await adb(command, signal); results.push({ command: command.join(' '), text: text.slice(0, 14000), truncated: text.length > 14000 }); }
        catch(e) { signal.throwIfAborted(); results.push({ command: command.join(' '), error: e.message }); }
      }
      return results;
    };
    return { execute: async (name, args, signal) => {
      validateTool(name, args); signal.throwIfAborted(); assertScope();
      const cacheKey = JSON.stringify([name, args]);
      const ttl = name === 'host' ? 15000 : ['android_check', 'game_check', 'snapshot'].includes(name) ? 3000 : 0;
      if (ttl && cache.has(cacheKey) && Date.now() - cache.get(cacheKey).time < ttl) return { ...cache.get(cacheKey).value, cached: true };
      let data;
      if (name === 'runtime_status') { const { sdk, dataHome, ...status } = this.context(); data = status; }
      else if(['file_roots','list_files','read_file','search_file'].includes(name)) data=await hostFileTool(name,args,pinned,signal);
      else if(['android_list_files','android_read_file','android_search_file'].includes(name)) data=await emulatorFileTool(name,args,adb,signal);
      else if(name==='adb_command') {
        const command=checkedAdbArgs(args.args);
        const commandKey=JSON.stringify(command);
        if(declined.has(commandKey)) throw new Error('This command was declined. Do not ask again.');
        const approved=await this.approveAdb(command,args.reason,signal,assertScope);
        if(!approved) declined.add(commandKey);
        signal.throwIfAborted();assertScope();
        data=approved ? {approved:true,output:await adb(command,signal,180000)} : {approved:false,message:'User declined. Do not repeat this request.'};
        cache.clear();
      }
      else if (name === 'snapshot') data = (await this.snapshot()).slice(0, 45000);
      else if (name === 'live_logs') data = this.live().slice(-45000);
      else if (name === 'host') data = { platform: os.release(), cpus: os.cpus().length, freeMemory: os.freemem(), totalMemory: os.totalmem(), windows: await this.execute('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', hostScript], { timeout: 20000, signal }) };
      else if (name === 'list_logs') data = { files: await enumerate(), limit: 150 };
      else if (name === 'read_log') data = await read(args.id, args.offset);
      else if (name === 'search_logs') {
        const listing = await enumerate(), matches = []; let scanned = 0;
        for (const { id } of listing) {
          signal.throwIfAborted(); const page = await read(id, -1, 262144); scanned++;
          for (const line of page.text.split('\n')) if (line.toLowerCase().includes(args.query.toLowerCase())) { matches.push({ id, line: line.slice(0, 1000) }); if (matches.length >= 80) break; }
          if (matches.length >= 80) break;
        }
        data = { matches, scanned, listed: listing.length, coverage: 'Last 256 KiB per file; at most 150 files and 80 matches. No match does not exclude earlier errors.' };
      } else if (name === 'android_check') data = await batch(checkCommands[args.check], signal);
      else if (name === 'game_check') {
        const pkg = validPackage(args.package || pinned.package);
        const commands = { package: [['shell', 'dumpsys', 'package', pkg]], exit: [['shell', 'dumpsys', 'activity', 'exit-info', pkg]], memory: [['shell', 'dumpsys', 'meminfo', pkg]], frames: [['shell', 'dumpsys', 'gfxinfo', pkg]], assets: [['shell', 'ls', '-l', `/sdcard/Android/obb/${pkg}`], ['shell', 'du', '-s', `/sdcard/Android/data/${pkg}`]] };
        data = await batch(commands[args.check], signal);
      } else if (name === 'performance') data = await this.performance(args.seconds, signal);
      else if (name === 'stop_game') { data = await this.approveStop(args.reason, signal, assertScope); cache.clear(); }
      signal.throwIfAborted(); if (!['stop_game','adb_command'].includes(name)) assertScope();
      const serialized = cleanEvidence(typeof data === 'string' ? data : JSON.stringify(data));
      const value = { collectedAt: new Date().toISOString(), cached: false, truncated: serialized.length > 70000, evidence: serialized.slice(0, 70000) };
      cache.set(cacheKey, { time: Date.now(), value }); return value;
    } };
  }
}
