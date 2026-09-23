import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {
  loadCompatibilityProfiles,
  resolveCompatibility,
  compatibilityPatchArgs,
  compatibilityRuntimeOptions,
} from '../core/compatibility.mjs';

const bundled = new URL('../core/game-compatibility.json', import.meta.url);

async function writeProfiles(t, doc) {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-compat-'));
  t.after(() => fs.rm(dir, { recursive: true, force: true }));
  const file = path.join(dir, 'profiles.json');
  await fs.writeFile(file, typeof doc === 'string' ? doc : JSON.stringify(doc));
  return file;
}

function profile(overrides = {}) {
  return {
    id: 'test-game-1.0',
    label: 'Test Game 1.0',
    package: 'com.example.game',
    versions: [{ versionName: '1.0' }],
    ovrport: { extraPatches: ['patch_vrapi_openxr'] },
    runtime: { precomposeProjectionLayers: true },
    summary: 'Verified adapter and precomposition',
    ...overrides,
  };
}

const doc = (...profiles) => ({ schemaVersion: 1, profiles });

test('bundled database matches The Climb 2 2.2 with patch and runtime actions', async () => {
  const profiles = await loadCompatibilityProfiles(bundled);
  const resolution = resolveCompatibility(profiles, { package: 'com.crytek.climb2', version: '2.2' });
  assert.equal(resolution.status, 'matched');
  assert.deepEqual(compatibilityPatchArgs(resolution), ['--extra-patches=patch_vrapi_openxr']);
  assert.deepEqual(compatibilityRuntimeOptions(resolution), { precomposeProjectionLayers: true });
});

test('bundled database patches Batman with the recommended set and North Star with its audio fix', async () => {
  const profiles = await loadCompatibilityProfiles(bundled);
  const batman = resolveCompatibility(profiles, { package: 'com.camouflaj.manta', version: '1.4.1-350961', versionCode: '350961' });
  assert.equal(batman.status, 'matched');
  assert.deepEqual(compatibilityPatchArgs(batman), [], 'no extra patches: OVRPort applies its recommended set');
  const northStar = resolveCompatibility(profiles, { package: 'com.meta.samples.NorthStar', version: '1.0.1', versionCode: '101' });
  assert.equal(northStar.status, 'matched');
  assert.deepEqual(compatibilityPatchArgs(northStar), ['--extra-patches=patch_disable_meta_xr_audio_telemetry']);
  for (const resolution of [batman, northStar]) {
    assert.deepEqual(compatibilityRuntimeOptions(resolution), {});
    assert.match(resolution.summary, /before installing/);
  }
  assert.equal(resolveCompatibility(profiles, { package: 'com.camouflaj.manta', version: '1.4.2-360000', versionCode: '360000' }).status, 'mismatch');
});

test('a profile may record that the recommended patches alone were verified', async t => {
  const recommendedOnly = profile({ id: 'recommended', ovrport: { recommended: true } });
  delete recommendedOnly.runtime;
  const both = profile({ id: 'both', package: 'com.example.both', ovrport: { recommended: true, extraPatches: ['patch_a'] } });
  const profiles = await loadCompatibilityProfiles(await writeProfiles(t, doc(recommendedOnly, both)));
  assert.deepEqual(profiles[0].ovrport, { recommended: true });
  const resolution = resolveCompatibility(profiles, { package: 'com.example.game', version: '1.0' });
  assert.equal(resolution.status, 'matched');
  assert.deepEqual(compatibilityPatchArgs(resolution), []);
  assert.deepEqual(compatibilityPatchArgs(resolveCompatibility(profiles, { package: 'com.example.both', version: '1.0' })), ['--extra-patches=patch_a']);
});

test('version codes match as exact strings, including large codes and combined selectors', async t => {
  const file = await writeProfiles(t, doc(profile({
    id: 'code-game',
    package: 'com.example.code',
    versions: [{ versionCode: '9007199254740993' }, { versionCode: '42', versionName: '1.0' }],
  })));
  const profiles = await loadCompatibilityProfiles(file);
  assert.equal(resolveCompatibility(profiles, { package: 'com.example.code', versionCode: '9007199254740993' }).status, 'matched');
  assert.equal(resolveCompatibility(profiles, { package: 'com.example.code', versionCode: '42', version: '1.0' }).status, 'matched');
  // A large code passed as a JavaScript number loses integer precision before
  // comparison, so it must not match the exact string selector.
  assert.equal(resolveCompatibility(profiles, { package: 'com.example.code', versionCode: 9007199254740993 }).status, 'mismatch');
  assert.equal(resolveCompatibility(profiles, { package: 'com.example.code', versionCode: '42', version: '2.0' }).status, 'mismatch');
  assert.equal(resolveCompatibility(profiles, { package: 'com.example.code', versionCode: '43', version: '1.0' }).status, 'mismatch');
  assert.equal(resolveCompatibility(profiles, { package: 'com.example.code', version: '1.0' }).status, 'mismatch');
});

test('version names compare exactly and never equal a missing field', async t => {
  const file = await writeProfiles(t, doc(profile()));
  const profiles = await loadCompatibilityProfiles(file);
  assert.equal(resolveCompatibility(profiles, { package: 'com.example.game', version: '1.0' }).status, 'matched');
  assert.equal(resolveCompatibility(profiles, { package: 'com.example.game', version: '1.0.0' }).status, 'mismatch');
  assert.equal(resolveCompatibility(profiles, { package: 'com.example.game', version: ' 1.0 ' }).status, 'mismatch');
  for (const game of [{ package: 'com.example.game' }, { package: 'com.example.game', version: undefined },
    { package: 'com.example.game', version: null }, { package: 'com.example.game', versionCode: '7' }]) {
    assert.equal(resolveCompatibility(profiles, game).status, 'mismatch');
  }
  // A selector spelling out 'undefined' must not match a game without a version.
  const literal = await writeProfiles(t, doc(profile({ id: 'literal', versions: [{ versionName: 'undefined' }] })));
  const literalProfiles = await loadCompatibilityProfiles(literal);
  assert.equal(resolveCompatibility(literalProfiles, { package: 'com.example.game' }).status, 'mismatch');
});

test('unknown packages and missing identities resolve to none with status only', async t => {
  const file = await writeProfiles(t, doc(profile()));
  const profiles = await loadCompatibilityProfiles(file);
  for (const game of [{ package: 'com.other.game', version: '1.0' }, {}, null, undefined, 'com.example.game']) {
    assert.deepEqual(resolveCompatibility(profiles, game), { status: 'none' });
  }
  assert.deepEqual(compatibilityPatchArgs({ status: 'none' }), []);
  assert.deepEqual(compatibilityRuntimeOptions({ status: 'none' }), {});
});

test('version mismatches disclose the verified version and apply no actions', async t => {
  const file = await writeProfiles(t, doc(profile()));
  const profiles = await loadCompatibilityProfiles(file);
  const resolution = resolveCompatibility(profiles, { package: 'com.example.game', version: '2.0' });
  assert.equal(resolution.status, 'mismatch');
  assert.match(resolution.summary, /1\.0/);
  assert.match(resolution.summary, /not applied/);
  assert.deepEqual(resolution.verifiedVersions, ['1.0']);
  assert.ok(!('profile' in resolution));
  assert.ok(!('label' in resolution));
  assert.deepEqual(compatibilityPatchArgs(resolution), []);
  assert.deepEqual(compatibilityRuntimeOptions(resolution), {});
});

test('package-wide profiles match any version, including unknown ones', async t => {
  const wide = profile({ id: 'wide', versions: undefined });
  delete wide.versions;
  const file = await writeProfiles(t, doc(wide));
  const profiles = await loadCompatibilityProfiles(file);
  for (const game of [{ package: 'com.example.game' }, { package: 'com.example.game', version: '9.9' },
    { package: 'com.example.game', versionCode: '123' }]) {
    const resolution = resolveCompatibility(profiles, game);
    assert.equal(resolution.status, 'matched');
    assert.deepEqual(compatibilityPatchArgs(resolution), ['--extra-patches=patch_vrapi_openxr']);
    assert.deepEqual(compatibilityRuntimeOptions(resolution), { precomposeProjectionLayers: true });
  }
});

test('malformed envelopes, identities and unknown keys are rejected', async t => {
  const cases = [
    ['missing file is reported', null],
    ['schema array', []],
    ['bad schema version', { schemaVersion: 2, profiles: [] }],
    ['missing profiles', { schemaVersion: 1 }],
    ['profiles not array', { schemaVersion: 1, profiles: {} }],
    ['unknown top key', { schemaVersion: 1, profiles: [], extra: true }],
    ['profile not object', { schemaVersion: 1, profiles: [null] }],
    ['unknown profile key', doc({ ...profile(), extra: true })],
    ['empty id', doc(profile({ id: '' }))],
    ['duplicate ids', doc(profile({ id: 'dup' }), profile({ id: 'dup', package: 'com.other.game' }))],
    ['bad package', doc(profile({ package: 'not a package!' }))],
    ['missing package', (() => { const p = profile(); delete p.package; return doc(p); })()],
    ['empty label', doc(profile({ label: '  ' }))],
    ['empty summary', doc(profile({ summary: '' }))],
    ['unknown version key', doc(profile({ versions: [{ versionName: '1.0', build: 'a' }] }))],
    ['version rule empty', doc(profile({ versions: [{}] }))],
    ['versions empty array', doc(profile({ versions: [] }))],
    ['numeric versionCode', doc(profile({ versions: [{ versionCode: 42 }] }))],
    ['empty versionName', doc(profile({ versions: [{ versionName: '' }] }))],
    ['unknown ovrport key', doc(profile({ ovrport: { extraPatches: ['patch_vrapi_openxr'], extra: true } }))],
    ['ovrport not object', doc(profile({ ovrport: [] }))],
    ['ovrport empty patches', doc(profile({ ovrport: { extraPatches: [] } }))],
    ['ovrport with no action', doc(profile({ ovrport: {} }))],
    ['ovrport recommended false', doc(profile({ ovrport: { recommended: false } }))],
    ['ovrport recommended not boolean', doc(profile({ ovrport: { recommended: 'yes' } }))],
    ['patch semicolon injection', doc(profile({ ovrport: { extraPatches: ['patch_a;patch_b'] } }))],
    ['patch argument injection', doc(profile({ ovrport: { extraPatches: ['patch_a=1'] } }))],
    ['patch empty name', doc(profile({ ovrport: { extraPatches: [''] } }))],
    ['patch non-string', doc(profile({ ovrport: { extraPatches: [42] } }))],
    ['unknown runtime key', doc(profile({ runtime: { precomposeProjectionLayers: true, extra: true } }))],
    ['runtime not boolean', doc(profile({ runtime: { precomposeProjectionLayers: 'yes' } }))],
    ['runtime empty', doc(profile({ runtime: {} }))],
    ['no actions', doc((() => { const p = profile(); delete p.ovrport; delete p.runtime; return p; })())],
    ['inert runtime false alone', doc((() => { const p = profile(); delete p.ovrport; p.runtime = { precomposeProjectionLayers: false }; return p; })())],
  ];
  for (const [name, body] of cases) {
    if (body === null) {
      await assert.rejects(loadCompatibilityProfiles(path.join(os.tmpdir(), 'axrb-compat-missing.json')), /could not be read/, name);
    } else {
      const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-compat-'));
      try {
        const file = path.join(dir, 'profiles.json');
        await fs.writeFile(file, JSON.stringify(body));
        await assert.rejects(loadCompatibilityProfiles(file), /.+/, name);
      } finally {
        await fs.rm(dir, { recursive: true, force: true });
      }
    }
  }
  await assert.rejects(loadCompatibilityProfiles(await writeProfiles(t, '{not json')), /could not be read/);
});

test('ambiguous matches fail naming every colliding profile', async t => {
  const first = await writeProfiles(t, doc(profile({ id: 'first' }), profile({ id: 'second' })));
  const profiles = await loadCompatibilityProfiles(first);
  assert.throws(() => resolveCompatibility(profiles, { package: 'com.example.game', version: '1.0' }), /first.*second|second.*first/);
  const split = await writeProfiles(t, doc(
    profile({ id: 'by-name', versions: [{ versionName: '1.0' }] }),
    profile({ id: 'by-code', package: 'com.example.game', versions: [{ versionCode: '7' }] }),
  ));
  const splitProfiles = await loadCompatibilityProfiles(split);
  assert.equal(resolveCompatibility(splitProfiles, { package: 'com.example.game', version: '1.0' }).status, 'matched');
  assert.equal(resolveCompatibility(splitProfiles, { package: 'com.example.game', versionCode: '7' }).status, 'matched');
  assert.throws(() => resolveCompatibility(splitProfiles, { package: 'com.example.game', version: '1.0', versionCode: '7' }), /by-name.*by-code|by-code.*by-name/);
  assert.equal(resolveCompatibility(splitProfiles, { package: 'com.example.game', version: '2.0' }).status, 'mismatch');
});

test('patch and runtime helpers act only on matched profiles', () => {
  const patchesOnly = { status: 'matched', profile: { ovrport: { extraPatches: ['patch_a', 'patch_b'] }, runtime: {} } };
  assert.deepEqual(compatibilityPatchArgs(patchesOnly), ['--extra-patches=patch_a;patch_b']);
  assert.deepEqual(compatibilityRuntimeOptions(patchesOnly), {});
  const runtimeOnly = { status: 'matched', profile: { runtime: { precomposeProjectionLayers: true } } };
  assert.deepEqual(compatibilityPatchArgs(runtimeOnly), []);
  assert.deepEqual(compatibilityRuntimeOptions(runtimeOnly), { precomposeProjectionLayers: true });
  const disabled = { status: 'matched', profile: { runtime: { precomposeProjectionLayers: false } } };
  assert.deepEqual(compatibilityRuntimeOptions(disabled), {});
  for (const resolution of [{ status: 'mismatch' }, { status: 'none' }, null, undefined, {}]) {
    assert.deepEqual(compatibilityPatchArgs(resolution), []);
    assert.deepEqual(compatibilityRuntimeOptions(resolution), {});
  }
});
