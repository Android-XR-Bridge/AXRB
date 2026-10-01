import fs from 'node:fs/promises';
import { createReadStream } from 'node:fs';
import path from 'node:path';
import { cleanEvidence } from './ai-diagnostics.mjs';

const privatePath = /(?:^|[\\/])(?:\.git|\.ssh|\.env(?:\..*)?|chatgpt-usage|.*credentials.*|.*cookies.*|.*token.*|auth\.json|adbkey(?:\.pub)?|.*\.pem|.*\.key)(?:[\\/]|$)/i;
export function fileRoots(context) {
  return Object.entries({ output:context.dataHome, sdk:context.sdk, runtime:context.root, launcher:context.launcher,
    downloads:context.downloadDir, avd:context.avdHome }).filter(([,directory])=>directory).map(([id,directory])=>({id,directory:path.resolve(directory)}));
}
export async function resolveDiagnosticFile(roots, id, relative = '') {
  const root=roots.find(item=>item.id===id);
  if(!root || typeof relative!=='string' || relative.length>2000 || path.isAbsolute(relative) || /[:\0]/.test(relative) || relative.split(/[\\/]/).includes('..') || privatePath.test(relative)) throw new Error('Choose a relative diagnostic path inside a listed root. Credential files are excluded.');
  const base=await fs.realpath(root.directory), file=await fs.realpath(path.join(base,relative));
  const resolved=path.relative(base,file);
  if(resolved.startsWith('..') || path.isAbsolute(resolved) || privatePath.test(resolved)) throw new Error('Path leaves the diagnostic root or refers to credentials.');
  return file;
}

// Stream entire files, not only tails. Pages stop on complete lines so redaction
// cannot expose a credential cut at a page boundary. Giant binary/one-line blobs
// are identified explicitly instead of silently presenting them as complete.
async function* lines(file, signal) {
  const stream=createReadStream(file,{encoding:'utf8',highWaterMark:32768,signal});
  let buffer='', line=0, oversized=false;
  try {
    for await(const chunk of stream) {
      signal.throwIfAborted(); if(chunk.includes('\0')) throw new Error('Binary file. Use directory metadata instead of reading it as text.');
      buffer+=chunk; let end;
      while((end=buffer.indexOf('\n'))>=0) {
        const text=buffer.slice(0,end).replace(/\r$/,''); buffer=buffer.slice(end+1);
        yield {line:++line,text:oversized || text.length>48000 ? '[Line exceeds 48,000 characters; omitted]' : cleanEvidence(text), omitted:oversized || text.length>48000}; oversized=false;
      }
      if(buffer.length>48000) {buffer='';oversized=true;}
    }
    if(buffer || oversized) yield {line:++line,text:oversized?'[Oversized line omitted]':cleanEvidence(buffer),omitted:oversized};
  } finally {stream.destroy();}
}
export async function hostFileTool(name,args,context,signal) {
  const roots=fileRoots(context);
  if(name==='file_roots') return {roots:roots.map(({id,directory})=>({id,path:cleanEvidence(directory)})),pathFormat:'Use root id plus a relative path. Emulator paths are absolute POSIX paths.'};
  const file=await resolveDiagnosticFile(roots,args.root,args.path);
  if(name==='list_files') {
    const entries=(await fs.readdir(file,{withFileTypes:true})).filter(e=>!privatePath.test(e.name)).sort((a,b)=>a.name.localeCompare(b.name));
    const page=entries.slice(args.offset,args.offset+150);
    const items=[];
    for(const entry of page) {
      signal.throwIfAborted();
      if(entry.isSymbolicLink()) {items.push({name:entry.name,type:'link',readable:false});continue;}
      const stat=await fs.stat(path.join(file,entry.name)).catch(()=>null);
      items.push({name:entry.name,type:entry.isDirectory()?'directory':'file',bytes:stat?.size,modified:stat?.mtime.toISOString()});
    }
    return {root:args.root,path:args.path,items,nextOffset:args.offset+items.length,eof:args.offset+items.length>=entries.length};
  }
  const stat=await fs.stat(file); if(!stat.isFile()) throw new Error('Choose a regular text file.');
  const version=`${stat.size}:${stat.mtimeMs}`;
  if(args.version && args.version!==version) throw new Error('The file changed. Restart reading at line 1 with an empty version.');
  const output=[]; let size=0, nextLine=args.startLine, eof=true, scanned=0, omitted=false;
  for await(const item of lines(file,signal)) {
    if(item.line<args.startLine) continue;
    scanned++; omitted ||= item.omitted;
    const match=name==='read_file' || item.text.toLowerCase().includes(args.query.toLowerCase());
    if(match) {
      if(JSON.stringify(item).length>48000) {item.text='[Line exceeds the result limit; omitted]';item.omitted=true;omitted=true;}
      const cost=JSON.stringify(item).length;
      if(output.length && (size+cost>50000 || output.length>=200)) {nextLine=item.line;eof=false;break;}
      output.push(item); size+=cost;
    }
    nextLine=item.line+1;
    if(scanned>=100000) {eof=false;break;}
  }
  return {root:args.root,path:args.path,version,lines:output,nextLine,eof,omitted,
    coverage:`Scanned from line ${args.startLine}. Continue with nextLine and version until eof to read/search the entire file.`};
}

export function emulatorPath(value) {
  if(typeof value!=='string' || !value.startsWith('/') || value.length>2000 || /[\0\r\n]/.test(value) || privatePath.test(value)) throw new Error('Use an absolute emulator path without credentials.');
  return `'${value.replaceAll("'", "'\\''")}'`;
}
export async function emulatorFileTool(name,args,adb,signal) {
  const target=emulatorPath(args.path);
  if(name==='android_list_files') {
    const text=await adb(['shell',`ls -la -- ${target}`],signal);
    return {path:args.path,text:text.slice(0,50000),truncated:text.length>50000};
  }
  if(name==='android_search_file') {
    const query=`'${args.query.replaceAll("'","'\\''")}'`;
    const text=await adb(['shell',`grep -n -F -i -- ${query} ${target}; result=$?; if [ "$result" = 1 ]; then exit 0; else exit "$result"; fi`],signal);
    return {path:args.path,matches:text.slice(0,50000),truncated:text.length>50000,coverage:'Whole-file literal search. Narrow the query or use line paging if matches exceed the result limit.'};
  }
  // sed's program consists only of validated integers; the path is shell quoted.
  const end=args.startLine+args.lineCount-1;
  const text=await adb(['shell',`sed -n '${args.startLine},${end}p' ${target}`],signal);
  if(text.includes('\0')) throw new Error('Binary file; inspect metadata instead.');
  const lineCount=text.split('\n').length-(text.endsWith('\n')?1:0);
  return {path:args.path,text:text.slice(0,50000),truncated:text.length>50000,nextLine:end+1,eof:!text || lineCount<args.lineCount,
    note:text.length>50000?'Reduce lineCount: output exceeded this page.':'Continue until eof to read the whole file. Live files can change during paging.'};
}

export function checkedAdbArgs(args) {
  if(!Array.isArray(args) || !args.length || args.length>80 || args.some(value=>typeof value!=='string'||value.length>8000||value.includes('\0'))) throw new Error('ADB arguments must be a bounded array of strings.');
  // First argument must be a device operation. Target/server flags cannot be
  // injected ahead of it. Never let the agent switch to the user's physical Quest.
  if(!['shell','exec-out','logcat','get-state','get-serialno','get-devpath','features','jdwp','install','install-multiple','install-multi-package','uninstall','push','pull','sync','root','unroot','remount','reboot','wait-for-device','forward','reverse','emu','bugreport','backup','restore','tcpip','usb'].includes(args[0])) throw new Error('Use a device operation; changing the ADB server or target is not allowed.');
  return args;
}
