import test from 'node:test';
import assert from 'node:assert/strict';
import { describePermissionFailure, parsePermissionPrompt, parseRuntimePermissions, permissionLabel, validPermission } from '../core/permissions.mjs';

// Captured from `adb shell dumpsys package com.vrchat.oculus.quest` on the
// Android 16 image AXRB provisions.
const DUMPSYS = `    requested permissions:
      android.permission.INTERNET
      android.permission.CAMERA
    install permissions:
      android.permission.MODIFY_AUDIO_SETTINGS: granted=true
      android.permission.FOREGROUND_SERVICE: granted=true
    User 0: ceDataInode=1 installed=true
      runtime permissions:
        android.permission.BLUETOOTH_CONNECT: granted=false, flags=[ USER_SENSITIVE_WHEN_GRANTED|USER_SENSITIVE_WHEN_DENIED]
        android.permission.READ_EXTERNAL_STORAGE: granted=false, flags=[ USER_SENSITIVE_WHEN_GRANTED|RESTRICTION_INSTALLER_EXEMPT]
        android.permission.CAMERA: granted=true, flags=[ USER_SENSITIVE_WHEN_GRANTED]
        android.permission.RECORD_AUDIO: granted=false, flags=[ USER_SENSITIVE_WHEN_DENIED]
`;

test('only the runtime permissions are offered, with their grant state', () => {
  const items = parseRuntimePermissions(DUMPSYS);
  assert.deepEqual(items.map(p => p.name).sort(), [
    'android.permission.BLUETOOTH_CONNECT', 'android.permission.CAMERA',
    'android.permission.READ_EXTERNAL_STORAGE', 'android.permission.RECORD_AUDIO'].sort());
  assert.equal(items.find(p => p.name.endsWith('CAMERA')).granted, true);
  assert.equal(items.find(p => p.name.endsWith('RECORD_AUDIO')).granted, false);
});

test('install-time permissions are excluded, because pm grant refuses them', () => {
  const names = parseRuntimePermissions(DUMPSYS).map(p => p.name);
  assert.ok(!names.includes('android.permission.MODIFY_AUDIO_SETTINGS'));
  assert.ok(!names.includes('android.permission.FOREGROUND_SERVICE'));
  assert.ok(!names.includes('android.permission.INTERNET'), 'a merely requested permission is not grantable');
});

test('a permission granted for any user counts as granted', () => {
  const twoUsers = DUMPSYS + `    User 10: ceDataInode=2 installed=true
      runtime permissions:
        android.permission.RECORD_AUDIO: granted=true, flags=[]
`;
  assert.equal(parseRuntimePermissions(twoUsers).find(p => p.name.endsWith('RECORD_AUDIO')).granted, true);
});

test('a package with no runtime permissions yields an empty list, not an error', () => {
  assert.deepEqual(parseRuntimePermissions('      runtime permissions:\n'), []);
  assert.deepEqual(parseRuntimePermissions(''), []);
  assert.deepEqual(parseRuntimePermissions(undefined), []);
});

test('permissions are labelled in the words a player would recognise', () => {
  assert.equal(permissionLabel('android.permission.RECORD_AUDIO'), 'Microphone');
  assert.equal(permissionLabel('com.oculus.permission.HAND_TRACKING'), 'Hand tracking');
  assert.equal(permissionLabel('com.example.custom.SOME_THING'), 'Some thing', 'an unknown name still reads as prose');
  assert.equal(parseRuntimePermissions(DUMPSYS)[0].label, 'Bluetooth devices', 'the list is ordered by label');
});

test('permission names that could reach the guest shell are rejected', () => {
  for (const bad of ['android.permission.X; rm -rf /', 'a b', '', '../x', 'nodot', 'android.permission.$(id)', null]) {
    assert.throws(() => validPermission(bad), /Invalid Android permission/, `${bad} must be rejected`);
  }
  assert.equal(validPermission('android.permission.RECORD_AUDIO'), 'android.permission.RECORD_AUDIO');
  assert.equal(validPermission('com.oculus.permission.EYE_TRACKING'), 'com.oculus.permission.EYE_TRACKING');
});

test('a waiting permission dialog is recognised from the resumed activity', () => {
  const prompting = '      topResumedActivity=ActivityRecord{2278 u0 com.google.android.permissioncontroller/com.android.permissioncontroller.permission.ui.GrantPermissionsActivity t9}';
  assert.deepEqual(parsePermissionPrompt(prompting), {
    package: 'com.google.android.permissioncontroller',
    activity: 'com.android.permissioncontroller.permission.ui.GrantPermissionsActivity',
  });
});

test('an ordinary foreground app is not mistaken for a permission dialog', () => {
  // Captured while the launcher was on top, with no dialog showing.
  const idle = '      topResumedActivity=ActivityRecord{227887118 u0 com.google.android.apps.nexuslauncher/.NexusLauncherActivity t2}';
  assert.equal(parsePermissionPrompt(idle), null);
  assert.equal(parsePermissionPrompt(''), null);
});

test('a refusal is reported as its one meaningful line, not a Java stack trace', () => {
  const trace = ['', "Exception occurred while executing 'grant':",
    'java.lang.SecurityException: Permission android.permission.INTERNET requested by package com.axrb.mathprobe is not a changeable permission type',
    '\tat com.android.server.permission.access.permission.PermissionService.setRuntimePermissionGranted(PermissionService.kt:1024)',
    '\tat android.os.Binder.execTransact(Binder.java:1365)'].join('\n');
  const message = describePermissionFailure(trace, 'android.permission.INTERNET');
  assert.match(message, /not a changeable permission type/);
  assert.doesNotMatch(message, /\tat |PermissionService\.kt/, 'the stack frames must not reach the user');
  assert.ok(message.length < 200, `kept short enough for a toast, got ${message.length}`);
});

test('an unrecognised refusal still names the permission', () => {
  assert.match(describePermissionFailure('', 'android.permission.CAMERA'), /Camera/);
  assert.match(describePermissionFailure('device offline', 'android.permission.CAMERA'), /Camera.*device offline/);
});
