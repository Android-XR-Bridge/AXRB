import fs from 'node:fs/promises';
import path from 'node:path';
import { createHash } from 'node:crypto';

// Same supported emulator build as run_windows_game.ps1 and the clock helper.
const supportedQemu = 'dcec1cc23ac57ff04ec748cde7e42bfc713bf2ad532e49606a4a9332cfb94b56';

export function migrateClockPolicy(settings) {
  if (settings.clockPolicyVersion === 1) return;
  // Older launchers persisted their unoptimized default as an explicit choice.
  // Migrate once; a later deliberate Default override remains an override.
  if (!settings.guestClock || settings.guestClock === 'Default') settings.guestClock = 'Auto';
  settings.clockPolicyVersion = 1;
}

export async function selectGuestClock(root, settings) {
  const requested = settings.guestClock || 'Auto';
  if (requested !== 'Auto') return requested;
  try {
    const directory = path.join(root, 'out/clock/Release');
    await fs.access(path.join(directory, 'axrb_clock_launcher.exe'));
    await fs.access(path.join(directory, 'axrb_whpx_clock.dll'));
    const qemu = await fs.readFile(path.join(settings.sdk, 'emulator/qemu/windows-x86_64/qemu-system-x86_64-headless.exe'));
    return createHash('sha256').update(qemu).digest('hex') === supportedQemu ? 'TscCorrected' : 'Default';
  } catch { return 'Default'; }
}
