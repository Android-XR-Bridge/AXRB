import test from 'node:test';
import assert from 'node:assert/strict';
import { Ovrport, selectedPatchArgs } from '../core/ovrport.mjs';

const catalog = [
  { name: 'patch_copy_libraries', recommended: true },
  { name: 'patch_vrapi_openxr', recommended: false },
  { name: 'patch_remove_vrapi', recommended: false },
  { name: 'patch_ac_nexus_no_appsw_72', recommended: false },
  { name: 'patch_ac_nexus_no_appsw_90', recommended: false },
];

test('a legacy CLI cannot claim profiled patch support', async () => {
  const cli = new Ovrport(async () => 'usage: overport patch --patches=<value>');
  await assert.rejects(cli.requireProfiles('legacy.jar'), /newer ovrport CLI/);
  await assert.rejects(cli.patches('legacy.jar'), /Update the ovrport CLI/);
});

test('profile capabilities belong to the configured CLI path', async () => {
  const cli = new Ovrport(async (_executable, args) => args.includes('new.jar') ? '--extra-patches=<value>' : '--patches=<value>');
  await cli.requireProfiles('new.jar');
  await assert.rejects(cli.requireProfiles('old.jar'), /newer ovrport CLI/);
});

test('a profile patch the configured CLI lacks is named before patching starts', async () => {
  const answer = patches => async (_executable, args) => args.includes('--json') ? JSON.stringify(patches) : '--extra-patches=<value>';
  const old = new Ovrport(answer(catalog));
  await old.requireProfiles('old.jar');
  await assert.rejects(old.requireProfiles('old.jar', ['patch_disable_meta_xr_audio_telemetry']), /needs patch_disable_meta_xr_audio_telemetry, which the configured ovrport CLI does not have/);
  const current = new Ovrport(answer([...catalog, { name: 'patch_disable_meta_xr_audio_telemetry', recommended: false }]));
  await current.requireProfiles('new.jar', ['patch_disable_meta_xr_audio_telemetry']);
});

test('manual selections reject unknown patches, duplicates, conflicts and argument injection', () => {
  assert.throws(() => selectedPatchArgs(catalog, []), /at least one/);
  assert.throws(() => selectedPatchArgs(catalog, [{ name: 'patch_typo', arguments: [] }]), /Invalid/);
  assert.throws(() => selectedPatchArgs(catalog, [{ name: 'patch_copy_libraries', arguments: [] }, { name: 'patch_copy_libraries', arguments: [] }]), /duplicate/);
  assert.throws(() => selectedPatchArgs(catalog, [{ name: 'patch_vrapi_openxr', arguments: [] }, { name: 'patch_remove_vrapi', arguments: [] }]), /conflicts/);
  assert.throws(() => selectedPatchArgs(catalog, [{ name: 'patch_ac_nexus_no_appsw_72', arguments: [] }, { name: 'patch_ac_nexus_no_appsw_90', arguments: [] }]), /conflict/);
  assert.throws(() => selectedPatchArgs(catalog, [{ name: 'patch_copy_libraries', arguments: ['x;patch_remove_vrapi'] }]), /Invalid arguments/);
  assert.deepEqual(selectedPatchArgs(catalog, [{ name: 'patch_copy_libraries', arguments: ['first', 'second'] }, { name: 'patch_vrapi_openxr', arguments: [] }]), ['--patches=patch_copy_libraries=first,second;patch_vrapi_openxr']);
});

test('CLI catalog rejects malformed or ambiguous patch choices', async () => {
  for (const data of [null, [], [...catalog, catalog[0]], [{ name: 'patch_bad;injected', recommended: true }], [{ name: 'patch_copy_libraries', recommended: 'true' }]]) {
    await assert.rejects(new Ovrport(async () => JSON.stringify(data)).patches('cli.jar'), /invalid patch catalog/);
  }
});
