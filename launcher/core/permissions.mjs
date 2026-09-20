// Android asks for runtime permissions with a dialog inside the guest. AXRB
// runs the emulator headless, so nobody can see or tap that dialog and the game
// waits forever. Granting from the launcher settles the request up front.

// `pm grant` reaches the guest through a shell, so a permission name is only
// ever allowed to look like a permission name.
const NAME = /^[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z0-9_]+)+$/;
export function validPermission(value) {
  if (!NAME.test(value || '')) throw new Error('Invalid Android permission name.');
  return value;
}
const LABELS = {
  'android.permission.RECORD_AUDIO': 'Microphone',
  'android.permission.CAMERA': 'Camera',
  'android.permission.READ_EXTERNAL_STORAGE': 'Read files',
  'android.permission.WRITE_EXTERNAL_STORAGE': 'Modify files',
  'android.permission.READ_MEDIA_IMAGES': 'Photos',
  'android.permission.READ_MEDIA_VIDEO': 'Videos',
  'android.permission.READ_MEDIA_AUDIO': 'Music and audio',
  'android.permission.BLUETOOTH_CONNECT': 'Bluetooth devices',
  'android.permission.POST_NOTIFICATIONS': 'Notifications',
  'android.permission.ACCESS_FINE_LOCATION': 'Precise location',
  'android.permission.ACCESS_COARSE_LOCATION': 'Approximate location',
  'android.permission.BODY_SENSORS': 'Body sensors',
  'android.permission.ACTIVITY_RECOGNITION': 'Physical activity',
  'android.permission.READ_CONTACTS': 'Contacts',
  'com.oculus.permission.HAND_TRACKING': 'Hand tracking',
  'com.oculus.permission.EYE_TRACKING': 'Eye tracking',
  'com.oculus.permission.FACE_TRACKING': 'Face tracking',
  'com.oculus.permission.BODY_TRACKING': 'Body tracking',
  'com.oculus.permission.USE_SCENE': 'Room layout',
  'com.oculus.permission.USE_ANCHOR_API': 'Spatial anchors',
};
export function permissionLabel(name) {
  if (LABELS[name]) return LABELS[name];
  const tail = String(name ?? '').split('.').pop() || String(name ?? '');
  return tail.replaceAll('_', ' ').toLowerCase().replace(/^./, c => c.toUpperCase());
}
// Only the runtime block is offered: install-time permissions are already
// granted and `pm grant` refuses them, and a permission the package never
// declared is accepted in silence without doing anything.
export function parseRuntimePermissions(dumpsys) {
  const found = new Map();
  let owner = false, runtime = false;
  for (const line of String(dumpsys ?? '').split(/\r?\n/)) {
    const user = /^\s*User (\d+):/.exec(line);
    if (user) { owner = user[1] === '0'; runtime = false; continue; }
    if (/^\s*runtime permissions:\s*$/.test(line)) { runtime = owner; continue; }
    if (!runtime) continue;
    const entry = /^\s+([A-Za-z][A-Za-z0-9_.]*):\s*granted=(true|false)/.exec(line);
    if (!entry) { runtime = false; continue; }
    // Match the owner user explicitly targeted by the managed guest's pm calls.
    found.set(entry[1], entry[2] === 'true');
  }
  return [...found].map(([name, granted]) => ({ name, granted, label: permissionLabel(name) }))
    .sort((a, b) => a.label.localeCompare(b.label));
}
// This is a device-wide notice, not attribution to the selected game.
// Permission-management settings in the same package are not grant dialogs.
export function parsePermissionPrompt(activities) {
  const line = /^.*topResumedActivity=.*$/m.exec(String(activities ?? ''))?.[0] ?? '';
  const match = /\bu\d+\s+([A-Za-z][A-Za-z0-9_.]*)\/(\S+?)[\s}]/.exec(line);
  if (!match || !/(?:^|\.)permissioncontroller$/i.test(match[1]) ||
      !/(?:^|\.)GrantPermissionsActivity$/.test(match[2])) return null;
  return { package: match[1], activity: match[2] };
}
// `pm grant` reports refusals as a Java stack trace; only its first line says
// anything a person can act on.
export function describePermissionFailure(output, permission) {
  const exception = /^\s*(?:java|android)\.[\w.]*(?:Exception|Error):\s*(.+)$/m.exec(String(output ?? ''));
  const detail = (exception?.[1] ?? String(output ?? '').split(/\r?\n/).find(l => l.trim()) ?? '').trim();
  return detail ? `${permissionLabel(permission)}: ${detail}` : `Android refused to change ${permissionLabel(permission)}.`;
}
