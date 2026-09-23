import fs from 'node:fs/promises';
import { validPackage } from './runtime.mjs';

const TOP_KEYS = new Set(['schemaVersion', 'profiles']);
const PROFILE_KEYS = new Set(['id', 'label', 'package', 'versions', 'ovrport', 'runtime', 'summary']);
const VERSION_KEYS = new Set(['versionCode', 'versionName']);
const OVRPORT_KEYS = new Set(['recommended', 'extraPatches']);
const RUNTIME_KEYS = new Set(['precomposeProjectionLayers']);
// Patch names travel inside a semicolon-separated `--extra-patches` value where
// `=` introduces per-patch arguments, so anything outside a plain identifier
// would either split the value or inject arguments.
const PATCH_NAME = /^[A-Za-z][A-Za-z0-9_]*$/;

function isRecord(value) {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function assertKnownKeys(record, allowed, context) {
  for (const key of Object.keys(record)) {
    if (!allowed.has(key)) throw new Error(`${context} has unknown key '${key}'.`);
  }
}

function nonemptyString(value, context) {
  if (typeof value !== 'string' || value.trim().length === 0) throw new Error(`${context} must be a nonempty string.`);
  return value;
}

function checkPatchName(name, context) {
  if (typeof name !== 'string' || !PATCH_NAME.test(name)) {
    throw new Error(`${context} has invalid patch name '${String(name)}'.`);
  }
  return name;
}

function checkVersionValue(value, field, context) {
  // Android version codes exceed the integer precision JavaScript numbers
  // preserve, so selectors are stored and compared as exact strings.
  if (typeof value === 'number') throw new Error(`${context} field '${field}' must be a string, not a number.`);
  return nonemptyString(value, `${context} field '${field}'`);
}

function checkVersionRule(rule, context) {
  if (!isRecord(rule)) throw new Error(`${context} must be an object.`);
  assertKnownKeys(rule, VERSION_KEYS, context);
  const hasCode = Object.hasOwn(rule, 'versionCode');
  const hasName = Object.hasOwn(rule, 'versionName');
  if (!hasCode && !hasName) throw new Error(`${context} needs versionCode, versionName, or both.`);
  if (hasCode) checkVersionValue(rule.versionCode, 'versionCode', context);
  if (hasName) checkVersionValue(rule.versionName, 'versionName', context);
  return { ...(hasCode ? { versionCode: rule.versionCode } : {}), ...(hasName ? { versionName: rule.versionName } : {}) };
}

function checkProfile(profile, seenIds) {
  if (!isRecord(profile)) throw new Error('Compatibility profile must be an object.');
  assertKnownKeys(profile, PROFILE_KEYS, 'Compatibility profile');
  const id = nonemptyString(profile.id, 'Compatibility profile id');
  if (seenIds.has(id)) throw new Error(`Duplicate compatibility profile id '${id}'.`);
  seenIds.add(id);
  const context = `Compatibility profile '${id}'`;
  const label = nonemptyString(profile.label, `${context} label`);
  if (typeof profile.package !== 'string') throw new Error(`${context} needs an Android package name.`);
  try {
    validPackage(profile.package);
  } catch {
    throw new Error(`${context} has invalid package '${profile.package}'.`);
  }
  const summary = nonemptyString(profile.summary, `${context} summary`);

  let versions;
  if (Object.hasOwn(profile, 'versions') && profile.versions !== undefined) {
    if (!Array.isArray(profile.versions) || profile.versions.length === 0) {
      throw new Error(`${context} versions must be a nonempty array when present.`);
    }
    versions = profile.versions.map((rule, index) => checkVersionRule(rule, `${context} versions[${index}]`));
  }

  let ovrport;
  if (Object.hasOwn(profile, 'ovrport') && profile.ovrport !== undefined) {
    if (!isRecord(profile.ovrport)) throw new Error(`${context} ovrport must be an object.`);
    assertKnownKeys(profile.ovrport, OVRPORT_KEYS, `${context} ovrport`);
    // OVRPort always applies its recommended set; extraPatches add to it.
    // `recommended: true` records that the set alone was verified, which is an
    // action in its own right: a matched profile patches without asking.
    const recommended = Object.hasOwn(profile.ovrport, 'recommended');
    if (recommended && profile.ovrport.recommended !== true) throw new Error(`${context} ovrport.recommended must be true when present.`);
    const extra = Object.hasOwn(profile.ovrport, 'extraPatches');
    if (!recommended && !extra) throw new Error(`${context} ovrport needs extraPatches or recommended.`);
    if (extra && (!Array.isArray(profile.ovrport.extraPatches) || profile.ovrport.extraPatches.length === 0)) {
      throw new Error(`${context} ovrport.extraPatches must be a nonempty array.`);
    }
    ovrport = {
      ...(recommended ? { recommended: true } : {}),
      ...(extra ? { extraPatches: profile.ovrport.extraPatches.map((name, index) =>
        checkPatchName(name, `${context} ovrport.extraPatches[${index}]`)) } : {}),
    };
  }

  let runtime;
  if (Object.hasOwn(profile, 'runtime') && profile.runtime !== undefined) {
    if (!isRecord(profile.runtime)) throw new Error(`${context} runtime must be an object.`);
    assertKnownKeys(profile.runtime, RUNTIME_KEYS, `${context} runtime`);
    if (!Object.hasOwn(profile.runtime, 'precomposeProjectionLayers')) {
      throw new Error(`${context} runtime needs precomposeProjectionLayers.`);
    }
    if (typeof profile.runtime.precomposeProjectionLayers !== 'boolean') {
      throw new Error(`${context} runtime.precomposeProjectionLayers must be a boolean.`);
    }
    runtime = { precomposeProjectionLayers: profile.runtime.precomposeProjectionLayers };
  }

  if (!ovrport && runtime?.precomposeProjectionLayers !== true) {
    throw new Error(`${context} has no compatibility actions.`);
  }

  return {
    id, label, package: profile.package, summary,
    ...(versions ? { versions } : {}),
    ...(ovrport ? { ovrport } : {}),
    ...(runtime ? { runtime } : {}),
  };
}

export async function loadCompatibilityProfiles(file) {
  let data;
  try {
    data = JSON.parse(await fs.readFile(file, 'utf8'));
  } catch (error) {
    throw new Error(`Compatibility profiles could not be read: ${error.message}`);
  }
  if (!isRecord(data)) throw new Error('Compatibility profiles must be an object.');
  assertKnownKeys(data, TOP_KEYS, 'Compatibility profiles');
  if (data.schemaVersion !== 1) throw new Error('Unsupported compatibility schema version.');
  if (!Array.isArray(data.profiles)) throw new Error('Compatibility profiles must be an array.');
  const seenIds = new Set();
  return data.profiles.map(profile => checkProfile(profile, seenIds));
}

function formatVersionRule(rule) {
  if (rule.versionName !== undefined && rule.versionCode !== undefined) {
    return `${rule.versionName} (versionCode ${rule.versionCode})`;
  }
  if (rule.versionName !== undefined) return rule.versionName;
  return `versionCode ${rule.versionCode}`;
}

function formatVerifiedVersions(profile) {
  return (profile.versions ?? []).map(formatVersionRule);
}

function ruleMatches(rule, game) {
  if (rule.versionCode !== undefined) {
    if (game?.versionCode === undefined || game?.versionCode === null) return false;
    if (String(game.versionCode) !== rule.versionCode) return false;
  }
  if (rule.versionName !== undefined) {
    if (game?.version === undefined || game?.version === null) return false;
    if (String(game.version) !== rule.versionName) return false;
  }
  return true;
}

function profileMatches(profile, game) {
  if (profile.versions === undefined) return true;
  return profile.versions.some(rule => ruleMatches(rule, game));
}

export function resolveCompatibility(profiles, game) {
  if (!Array.isArray(profiles)) throw new Error('Compatibility profiles must be an array.');
  const pkg = isRecord(game) ? game.package : undefined;
  if (typeof pkg !== 'string' || pkg.length === 0) return { status: 'none' };
  const candidates = profiles.filter(profile => profile.package === pkg);
  if (candidates.length === 0) return { status: 'none' };
  const matched = candidates.filter(profile => profileMatches(profile, game));
  if (matched.length > 1) {
    throw new Error(`Ambiguous compatibility profiles for ${pkg}: ${matched.map(p => p.id).join(', ')}`);
  }
  if (matched.length === 1) {
    const profile = matched[0];
    return { status: 'matched', label: profile.label, summary: profile.summary, verifiedVersions: formatVerifiedVersions(profile), profile };
  }
  const verifiedVersions = [...new Set(candidates.flatMap(profile => formatVerifiedVersions(profile)))];
  const verified = verifiedVersions.length > 0 ? `Verified for ${verifiedVersions.join(', ')}.` : 'No verified version is recorded.';
  return {
    status: 'mismatch',
    summary: `${verified} This version is not verified; compatibility settings were not applied.`,
    verifiedVersions,
  };
}

export function compatibilityPatchArgs(resolution) {
  if (!resolution || resolution.status !== 'matched' || !resolution.profile) return [];
  const patches = resolution.profile.ovrport?.extraPatches;
  if (!Array.isArray(patches) || patches.length === 0) return [];
  return [`--extra-patches=${patches.join(';')}`];
}

export function compatibilityRuntimeOptions(resolution) {
  if (!resolution || resolution.status !== 'matched' || !resolution.profile) return {};
  if (resolution.profile.runtime?.precomposeProjectionLayers === true) return { precomposeProjectionLayers: true };
  return {};
}
