// Change emulatorEnabled to true before building to restore the legacy backend.
// This is deliberately independent of user settings and environment variables.
export const buildProfile = Object.freeze({
  productName: 'AXRB-Emulatorless',
  runtimeDirectory: 'AXRB-Emulatorless Runtime',
  emulatorEnabled: false,
  updatesEnabled: false,
  minimumMemoryGB: 6,
});
export function requireEmulator(enabled) {
  if (!enabled) throw new Error('The Android Emulator backend is disabled in this build.');
}
export function checkBackendRequest(name, args, enabled) {
  if (enabled) return;
  if (['install', 'uninstall', 'startAndroid', 'stopAndroid', 'permissions', 'setPermission', 'storage', 'setupResume', 'chooseSetupArchives'].includes(name) ||
      (name === 'play' && args[1] && args[1] !== 'direct')) requireEmulator(false);
}
