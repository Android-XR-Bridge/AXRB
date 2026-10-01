import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { hostFileTool, resolveDiagnosticFile, emulatorFileTool, emulatorPath, checkedAdbArgs } from '../core/ai-files.mjs';
import { DiagnosticHarness } from '../core/ai-tools.mjs';
const signal=()=>new AbortController().signal;
async function fixture(t) {const directory=await fs.mkdtemp(path.join(os.tmpdir(),'axrb-files-'));t.after(()=>fs.rm(directory,{recursive:true,force:true}));return directory;}

test('host file reader pages to EOF without losing lines and searches old content beyond log tails',async t=>{
  const directory=await fixture(t), context={dataHome:directory};
  const content=Array.from({length:1800},(_,i)=>`${i===1?'old-error-needle':'entry'} ${i} ${'x'.repeat(170)}`).join('\n');
  await fs.writeFile(path.join(directory,'host.log'),content);
  let startLine=1,version='',eof=false,all=[];
  while(!eof) {const page=await hostFileTool('read_file',{root:'output',path:'host.log',startLine,version},context,signal());all.push(...page.lines.map(l=>l.text));assert.ok(page.nextLine>startLine);startLine=page.nextLine;version=page.version;eof=page.eof;}
  assert.equal(all.join('\n'),content);
  const found=await hostFileTool('search_file',{root:'output',path:'host.log',startLine:1,version:'',query:'old-error-needle'},context,signal());
  assert.equal(found.lines[0].line,2);assert.equal(found.eof,true);
  await fs.appendFile(path.join(directory,'host.log'),'changed');
  await assert.rejects(hostFileTool('read_file',{root:'output',path:'host.log',startLine:1,version},context,signal()),/changed/);
});
test('host roots block traversal, credential reads and binary files; listings are pageable',async t=>{
  const directory=await fixture(t), context={dataHome:directory};
  await fs.writeFile(path.join(directory,'credentials.json'),'secret');
  await fs.writeFile(path.join(directory,'binary.bin'),Buffer.from([0,1,2]));
  const roots=[{id:'output',directory}];
  for(const file of ['../outside','credentials.json','C:\\Windows\\x','file:stream','.git/config']) await assert.rejects(resolveDiagnosticFile(roots,'output',file));
  const list=await hostFileTool('list_files',{root:'output',path:'',offset:0},context,signal());
  assert.equal(list.items.some(e=>e.name==='credentials.json'),false);assert.equal(list.eof,true);
  await assert.rejects(hostFileTool('read_file',{root:'output',path:'binary.bin',startLine:1,version:''},context,signal()),/Binary/);
});
test('file tools redact complete lines and honor cancellation',async t=>{
  const directory=await fixture(t),context={dataHome:directory};
  await fs.writeFile(path.join(directory,'safe.log'),'Authorization: Bearer private-value\nHello\n');
  const page=await hostFileTool('read_file',{root:'output',path:'safe.log',startLine:1,version:''},context,signal());
  assert.equal(JSON.stringify(page).includes('private-value'),false);
  const controller=new AbortController();controller.abort();
  await assert.rejects(hostFileTool('read_file',{root:'output',path:'safe.log',startLine:1,version:''},context,controller.signal));
});
test('emulator file paths and literal search cannot inject shell commands',async()=>{
  const commands=[];const adb=async args=>{commands.push(args);return 'line 1\nline 2\n';};
  const page=await emulatorFileTool('android_read_file',{path:"/sdcard/test'; reboot; 'file",startLine:1,lineCount:200},adb,signal());
  assert.equal(page.eof,true);assert.match(commands[0][1],/sed -n '1,200p'/);
  assert.ok(commands[0][1].includes("'\\''"));
  await emulatorFileTool('android_search_file',{path:'/sdcard/test',query:"'; reboot; '"},adb,signal());
  assert.ok(commands[1][1].includes("grep -n -F -i -- ''\\''; reboot; '\\'''"));
  assert.throws(()=>emulatorPath('/data/auth.json'));assert.throws(()=>emulatorPath('/data/a\nreboot'));
});
test('full ADB is pinned, approval-gated, decline is respected, global flags rejected',async()=>{
  const commands=[];let approvals=0,allow=false;
  const context={sdk:'C:\\sdk',port:5584,dataHome:'C:\\output'};
  const harness=new DiagnosticHarness({context:()=>context,approveAdb:async()=>{approvals++;return allow;},execute:async(exe,args)=>{commands.push(args);return 'done';}});
  const session=harness.begin();
  const args={args:['shell','getprop'],reason:'Inspect Android properties'};
  await session.execute('adb_command',args,signal());assert.equal(commands.length,0);assert.equal(approvals,1);
  await assert.rejects(session.execute('adb_command',args,signal()),/declined/);assert.equal(approvals,1);
  allow=true;await harness.begin().execute('adb_command',args,signal());
  assert.deepEqual(commands[0],['-P','5038','-s','emulator-5584','shell','getprop']);
  for(const cmd of [['-s','other','shell'],['kill-server'],['connect','1.2.3.4'],['shell','x\0y']]) assert.throws(()=>checkedAdbArgs(cmd));
});
