import fs from 'node:fs';
import {execFileSync} from 'node:child_process';
import {createRequire} from 'node:module';
const require=createRequire(new URL('../../launcher/package.json',import.meta.url));
const semver=require('semver');
const event=JSON.parse(fs.readFileSync(process.env.GITHUB_EVENT_PATH,'utf8'));
const current=JSON.parse(fs.readFileSync('launcher/package.json','utf8')).version;
if(!semver.valid(current))throw new Error('launcher/package.json must contain a valid semantic version');
const lock=JSON.parse(fs.readFileSync('launcher/package-lock.json','utf8'));
if(lock.version!==current || lock.packages[''].version!==current)throw new Error('Bump package-lock.json alongside package.json (npm version).');
const base=event.pull_request?.base.sha || event.before;
let previous;
if(base && !/^0+$/.test(base)) {
  if(!/^[a-f0-9]{40}$/.test(base))throw new Error('Invalid base SHA');
  previous=JSON.parse(execFileSync('git',['show',`${base}:launcher/package.json`],{encoding:'utf8'})).version;
}
const changed=process.env.GITHUB_EVENT_NAME==='workflow_dispatch' || Boolean(previous && semver.gt(current,previous));
if(previous && semver.lt(current,previous))throw new Error('Version must not decrease');
fs.appendFileSync(process.env.GITHUB_OUTPUT,`changed=${changed}\nversion=${current}\nprerelease=${Boolean(semver.prerelease(current))}\n`);
console.log(`${previous || '(no baseline)'} -> ${current}: ${changed?'build':'skip'}`);
