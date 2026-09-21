import path from 'node:path';
import { powershellArgs } from './runtime.mjs';

// The same scan the launcher runs is also a script people can run by hand when
// they are asked for one, so the launcher only supplies what it knows better
// than the script does: which package it launched, and which version it is.
// Everything the script can work out for itself is left to it.
export const SCAN_SCRIPT = 'scripts/run/performance_scan.ps1';
export function performanceScanArgs(root, settings = {}, { seconds = 10, version = '', packageName = '' } = {}) {
  if (!Number.isInteger(seconds) || seconds < 3 || seconds > 60) throw new Error('Choose a scan window between 3 and 60 seconds.');
  return powershellArgs(path.join(root, SCAN_SCRIPT), {
    ...(settings.sdk ? { Sdk: settings.sdk } : {}),
    ...(settings.port ? { Port: settings.port } : {}),
    Seconds: seconds,
    ...(packageName ? { Package: packageName } : {}),
    ...(version ? { Version: version } : {}),
  });
}
// The scan sleeps for its whole window before it prints anything, and the ADB
// round trips on either side of it are slow on a guest that is already
// struggling, which is the case it exists for. The margin is that slack, not a
// guess at how long the work takes.
export function performanceScanTimeout(seconds = 10) { return (seconds + 60) * 1000; }
