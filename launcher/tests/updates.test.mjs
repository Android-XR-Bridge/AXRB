import test from 'node:test';
import assert from 'node:assert/strict';
import {EventEmitter} from 'node:events';
import {LauncherUpdates} from '../core/updates.mjs';
function fixture({choice=0,fail=false,canInstall=true}={}) {
 const calls=[];const updater=new EventEmitter();
 updater.checkForUpdates=async()=>calls.push('check');
 updater.downloadUpdate=async()=>{calls.push('download');if(fail)throw new Error('Checksum mismatch');};
 updater.quitAndInstall=(...args)=>calls.push(['install',...args]);
 const prompts=[];
 const updates=new LauncherUpdates({updater,prompt:async options=>{prompts.push(options);return {response:choice};},install:async()=>{calls.push('close');updates.finishInstall();},canInstall:()=>canInstall,progress:value=>calls.push(['progress',value])});
 return {updates,updater,calls,prompts};
}
test('startup check runs once; no download or implicit installation',async()=>{
 const f=fixture();await f.updates.check();await f.updates.check();assert.deepEqual(f.calls,['check']);
 assert.equal(f.updater.autoDownload,false);assert.equal(f.updater.autoInstallOnAppQuit,false);assert.equal(f.updater.allowDowngrade,false);
});
test('Later leaves installation untouched',async()=>{const f=fixture({choice:1});await f.updates.offer({version:'2.0.0'});assert.deepEqual(f.calls,[]);});
test('approved update downloads before shutdown and installation',async()=>{
 const f=fixture();await f.updates.offer({version:'2.0.0'});
 assert.deepEqual(f.calls,[['progress',0],'download',['progress',-1],'close',['install',false,true]]);
});
test('verification errors cannot reach installer',async()=>{
 const f=fixture({fail:true});await f.updates.offer({version:'2.0.0'});assert.equal(f.updates.ready,false);
 assert.equal(f.prompts.at(-1).type,'error');assert.ok(!f.calls.includes('close'));assert.throws(()=>f.updates.finishInstall(),/verified/);
});
test('active operations defer update installation',async()=>{
 const f=fixture({canInstall:false});await f.updates.offer({version:'2.0.0'});assert.equal(f.updates.ready,true);assert.ok(!f.calls.includes('close'));
});
test('duplicate available events share one offer',async()=>{
 const f=fixture();await Promise.all([f.updates.offer({version:'2.0.0'}),f.updates.offer({version:'2.0.0'})]);assert.equal(f.prompts.length,1);
});
