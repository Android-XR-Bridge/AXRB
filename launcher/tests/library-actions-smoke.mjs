import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';

// Only used by --smoke-test in its isolated profile. Native dialogs and Android
// mutations are replaced here; production IPC and React run unchanged.
export async function libraryActionsSmoke(window, { state, runtime, dialog, persist, publicState, ovrport }) {
  const wc = window.webContents, js = code => wc.executeJavaScript(code);
  const wait = () => new Promise(r => setTimeout(r, 50));
  async function check(code, message) {
    for (let n = 0; n < 100; n++) { if (await js(code)) return; await wait(); }
    throw Error(message);
  }
  const original = { open: dialog.showOpenDialog, confirm: dialog.showMessageBox, inspect: runtime.inspect, prepare: runtime.prepareLaunch, uninstall: runtime.uninstall, launch: runtime.launch, execute: ovrport.execute, cli: state.data.settings.ovrportCli };
  const cli = path.join(state.directory, 'smoke-ovrport.jar');
  const climb = { id: 'smoke-climb', package: 'com.crytek.climb2', name: 'The Climb 2', version: '2.2', versionCode: '22', activity: 'com.crytek.climb2/.Main', installed: true, source: 'local', apk: path.join(state.directory, 'climb-base.apk') };
  const patchCalls = [], launchCalls = [];
  const gameApk = path.join(state.directory, 'smoke-base.apk');
  const game = { id: 'local:com.axrbtest.game', package: 'com.axrbtest.game', name: 'Uninstall test', installed: true, source: 'local', apk: gameApk, files: [{ path: 'C:/test/content.obb' }] };
  let removed = 0, confirmation;
  try {
    await fs.writeFile(gameApk, 'isolated APK fixture');
    await fs.writeFile(climb.apk, 'isolated APK fixture');
    state.put(game); await persist();
    await js(`document.querySelector('[data-nav="library"]').click()`);
    // Remove the previous smoke filter by opening the dialog through a fresh state.
    await js(`document.querySelector('[aria-label="Filter library"]').click()`);
    await check(`Boolean(document.querySelector('[role="option"]'))`, 'Filter did not open');
    await js(`Array.from(document.querySelectorAll('[role="option"]')).find(e => e.textContent === 'All games').click()`);
    await check(`Boolean(document.querySelector('[data-game="${game.id}"]'))`, 'Installed game missing');
    await js(`document.querySelector('[data-game="${game.id}"]').click()`);
    await check(`Boolean(document.querySelector('[aria-label="Game actions"]'))`, 'Actions missing');
    // Dispatch pointerdown as required by the Radix dropdown trigger.
    await js(`document.querySelector('[aria-label="Game actions"]').dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, button: 0, pointerType: 'mouse', ctrlKey: false }))`);
    await check(`Array.from(document.querySelectorAll('[role="menuitem"]')).some(e => e.textContent === 'Uninstall')`, 'Uninstall action missing');
    dialog.showMessageBox = async (_window, options) => { confirmation = options; return { response: 0 }; };
    runtime.uninstall = async () => { removed++; };
    await js(`Array.from(document.querySelectorAll('[role="menuitem"]')).find(e => e.textContent === 'Uninstall').click()`);
    await check(`!document.querySelector('[role="menu"]')`, 'Uninstall menu did not close');
    await wait(); assert.equal(removed, 0); assert.equal(game.installed, true);
    assert.match(confirmation.detail, /saved data/); assert.equal(confirmation.defaultId, 0);
    dialog.showMessageBox = async () => ({ response: 1 });
    const result = await js(`window.axrb.uninstall('${game.id}')`);
    assert.equal(result.ok, true); assert.equal(result.value, true); assert.equal(removed, 1);
    assert.equal(game.installed, false); assert.equal(game.apk, gameApk); assert.equal(game.files.length, 1);
    await check(`Array.from(document.querySelectorAll('#details button')).some(e => e.textContent === 'Install')`, 'Uninstall did not update library');
    await check(`document.querySelector('[data-job-toast]')?.textContent.includes('uninstalled')`, 'Uninstall completion toast missing');
    game.installed = true; runtime.uninstall = async () => { throw Error('Android uninstall rejected'); }; await persist();
    await js(`window.axrb.uninstall('${game.id}')`); assert.equal(game.installed, true);
    await check(`Array.from(document.querySelectorAll('[data-job-toast]')).some(e => e.textContent.includes('Android uninstall rejected'))`, 'Uninstall failure toast missing');
    // An unrelated game's active download must never block this one (it used to,
    // via a global controllers.size check instead of scoping to this game's jobs).
    runtime.uninstall = async () => { removed++; };
    const unrelatedJob = { id: 'unrelated-download', gameId: 'some-other-game', name: 'Unrelated', status: 'downloading' };
    state.data.jobs.unshift(unrelatedJob); await persist();
    const beforeUnrelated = removed;
    let uninstallResult = await js(`window.axrb.uninstall('${game.id}')`);
    assert.equal(uninstallResult.ok, true, uninstallResult.error);
    assert.equal(removed, beforeUnrelated + 1, 'An unrelated active job must not block uninstalling this game');
    game.installed = true; await persist();
    // This game's own active job must still block it.
    const ownJob = { id: 'own-transfer', gameId: game.id, name: game.name, status: 'downloading' };
    state.data.jobs.unshift(ownJob); await persist();
    uninstallResult = await js(`window.axrb.uninstall('${game.id}')`);
    assert.equal(uninstallResult.ok, false);
    assert.match(uninstallResult.error, /Finish this game.s transfer/);
    state.data.jobs = state.data.jobs.filter(j => j !== unrelatedJob && j !== ownJob); await persist();
    dialog.showOpenDialog = async () => ({ canceled: false, filePaths: ['C:/test/import.apk'] });
    runtime.inspect = async () => { await new Promise(r => setTimeout(r, 250)); return { id: 'local:com.axrbtest.imported', package: 'com.axrbtest.imported', name: 'APK toast test', source: 'local', apk: 'C:/test/import.apk' }; };
    await js(`window.axrb.import()`);
    await check(`Array.from(document.querySelectorAll('[data-job-toast]')).some(e => e.textContent.includes('Reading APK'))`, 'APK progress toast missing');
    await check(`Array.from(document.querySelectorAll('[data-job-toast]')).some(e => e.textContent.includes('APK toast test: imported'))`, 'APK completion toast missing');
    const importedId = 'local:com.axrbtest.imported';
    await check(`Boolean(document.querySelector('[data-game="${importedId}"]'))`, 'Imported APK missing from library');
    await js(`document.querySelector('[data-game="${importedId}"]').click()`);
    await check(`Boolean(document.querySelector('[aria-label="Game actions"]'))`, 'Imported app actions missing');
    await js(`document.querySelector('[aria-label="Game actions"]').dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, button: 0, pointerType: 'mouse', ctrlKey: false }))`);
    await check(`Array.from(document.querySelectorAll('[role="menuitem"]')).some(e => e.textContent === 'Delete imported app')`, 'Delete imported app action missing');
    dialog.showMessageBox = async (_window, options) => { confirmation = options; return { response: 0 }; };
    await js(`Array.from(document.querySelectorAll('[role="menuitem"]')).find(e => e.textContent === 'Delete imported app').click()`);
    await check(`!document.querySelector('[role="menu"]')`, 'Delete menu did not close');
    await wait();
    assert.match(confirmation.detail, /Android app data and saves will be lost/);
    assert.ok(state.data.games.some(item => item.id === importedId), 'Cancel must keep the imported app');
    dialog.showMessageBox = async () => ({ response: 1 });
    await js(`document.querySelector('[aria-label="Game actions"]').dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, button: 0, pointerType: 'mouse', ctrlKey: false }))`);
    await check(`Array.from(document.querySelectorAll('[role="menuitem"]')).some(e => e.textContent === 'Delete imported app')`, 'Delete action missing after cancellation');
    await js(`Array.from(document.querySelectorAll('[role="menuitem"]')).find(e => e.textContent === 'Delete imported app').click()`);
    assert.equal(state.data.games.some(item => item.id === importedId), false);
    await check(`!document.querySelector('[data-game="${importedId}"]')`, 'Deleted imported app remains in library');
    await check(`document.body.textContent.includes('Imported app deleted')`, 'Delete completion notification missing');
    dialog.showMessageBox = original.confirm;
    runtime.inspect = async () => { throw Error('Invalid APK fixture'); };
    await js(`window.axrb.import()`);
    await check(`Array.from(document.querySelectorAll('[data-job-toast]')).some(e => e.textContent.includes('Invalid APK fixture'))`, 'APK failure toast missing');
    const snapshot = publicState(), zip = { id: 'zip-toast', kind: 'zip', name: 'ZIP test', status: 'downloading', stage: 'Extracting ZIP', total: 10, completed: 4, progressUnit: 'files' };
    snapshot.jobs = [zip, ...snapshot.jobs]; wc.send('axrb:changed', snapshot);
    await check(`document.querySelector('[data-job-toast="zip-toast"] progress')?.value === 4`, 'ZIP progress toast missing');
    zip.status = 'installing'; zip.stage = 'Installing APK'; wc.send('axrb:changed', snapshot);
    await check(`document.querySelector('[data-job-toast="zip-toast"]')?.textContent.includes('Installing APK')`, 'ZIP install stage missing');
    await js(`document.querySelector('[data-job-toast="zip-toast"] button').click()`);
    wc.send('axrb:changed', snapshot); await wait();
    assert.equal(await js(`Boolean(document.querySelector('[data-job-toast="zip-toast"]'))`), false);
    zip.status = 'complete'; wc.send('axrb:changed', snapshot);
    await check(`document.querySelector('[data-job-toast="zip-toast"]')?.textContent.includes('ZIP test: installed')`, 'ZIP completion missing after dismissed progress');
    await js(`document.querySelectorAll('button[aria-label^="Dismiss "]').forEach(button => button.click())`);
    // Keep the actual IPC, profile resolver and UI; replace only process/APK boundaries.
    // Local APK identity (runtime.inspect) and installed Android identity
    // (runtime.prepareLaunch) are tracked separately so Play authority and Patch
    // re-inspection can diverge from the stored library record.
    await fs.writeFile(cli, 'isolated CLI fixture');
    state.data.settings.ovrportCli = cli;
    ovrport.execute = async (_executable, args) => {
      const command = args.slice(2);
      if (command[0] === 'patches') return JSON.stringify([
        { name: 'patch_copy_libraries', recommended: true },
        { name: 'patch_vrapi_openxr', recommended: false },
      ]);
      if (command.length === 1 && command[0] === 'patch') return '--extra-patches=<value>';
      patchCalls.push(command);
      const output = command.find(arg => arg.startsWith('--output=')).slice('--output='.length);
      const input = command.find(arg => arg.startsWith('--input=')).slice('--input='.length);
      await fs.mkdir(output, { recursive: true });
      await fs.writeFile(path.join(output, `${path.basename(input, '.apk')}-axrb.apk`), 'isolated patched APK fixture');
      return 'Patching successful.';
    };
    const actual = { package: climb.package, version: '2.2', versionCode: '22', activity: climb.activity };
    const installed = { package: climb.package, version: '2.2', versionCode: '22', activity: climb.activity };
    runtime.inspect = async apk => {
      if (apk === game.apk) return { package: game.package, version: '1.0', versionCode: '1', activity: 'com.axrbtest.game/.Main', patched: false };
      return { package: actual.package, version: actual.version, versionCode: actual.versionCode, activity: actual.activity, patched: false };
    };
    runtime.prepareLaunch = async request => {
      if (request.package === climb.package) return { game: { ...request, package: installed.package, version: installed.version, versionCode: installed.versionCode, activity: installed.activity }, ownsEmulator: false };
      return { game: { ...request }, ownsEmulator: false };
    };
    runtime.launch = (launched, _onExit, compatibility, options) => { launchCalls.push({ package: launched.package, version: launched.version, versionCode: launched.versionCode, activity: launched.activity, compatibility, options }); };
    state.put(climb); await persist();
    const storedClimb = () => state.data.games.find(g => g.id === climb.id);
    async function dismissNotices() { await js(`document.querySelectorAll('button[aria-label="Dismiss notification"]').forEach(button => button.click())`); }
    async function closeDetails() {
      wc.sendInputEvent({ type: 'keyDown', keyCode: 'Escape' }); wc.sendInputEvent({ type: 'keyUp', keyCode: 'Escape' });
      await check(`!document.querySelector('#details')`, 'Details did not close');
    }
    async function openDetails(id) {
      await check(`Boolean(document.querySelector('[data-game="${id}"]'))`, 'Profile fixture missing');
      await js(`document.querySelector('[data-game="${id}"]').click()`);
      await check(`Boolean(document.querySelector('#details'))`, 'Profile details missing');
    }
    async function choosePatch() {
      await check(`!Array.from(document.querySelectorAll('#details [role="status"]')).some(e => e.textContent.includes('Working'))`, 'Prior action did not finish');
      await js(`document.querySelector('[aria-label="Game actions"]').dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, button: 0, pointerType: 'mouse', ctrlKey: false }))`);
      await check(`Array.from(document.querySelectorAll('[role="menuitem"]')).some(e => e.textContent === 'Patch with ovrport')`, 'Patch action missing');
      await js(`Array.from(document.querySelectorAll('[role="menuitem"]')).find(e => e.textContent === 'Patch with ovrport').click()`);
    }
    async function capture(name) { await fs.writeFile(path.join(state.directory, `${name}.png`), (await wc.capturePage()).toPNG()); }
    await dismissNotices();
    await closeDetails();
    // Matched local APK plus matched installed build: automatic preset and Play profile.
    await openDetails(climb.id);
    await check(`document.querySelector('[data-compatibility="matched"]')?.textContent.includes('APK compatibility profile')`, 'Local APK profile label missing');
    await check(`document.querySelector('[data-compatibility="matched"]')?.textContent.includes('The Climb 2 2.2')`, 'Matched profile disclosure missing');
    await check(`document.body.textContent.includes('Play checks the version installed in Android')`, 'Installed Play check note missing');
    await capture('compatibility-matched');
    const patchBaseMatched = patchCalls.length;
    await choosePatch();
    await check(`document.body.textContent.includes('Patched with The Climb 2 2.2 compatibility profile')`, 'Profile patch notification missing');
    assert.ok(patchCalls[patchBaseMatched].includes('--extra-patches=patch_vrapi_openxr'), 'Matched patch must apply the verified preset');
    assert.equal(await js(`Boolean(document.querySelector('[data-patch-options]'))`), false);
    const launchBaseMatched = launchCalls.length;
    await js(`Array.from(document.querySelectorAll('#details button')).find(e => e.textContent === 'Play').click()`);
    await check(`document.body.textContent.includes('Using The Climb 2 2.2 compatibility profile')`, 'Profile play notification missing');
    assert.deepEqual(launchCalls[launchBaseMatched].compatibility, { precomposeProjectionLayers: true });
    assert.equal(launchCalls[launchBaseMatched].version, '2.2');
    await check(`!document.querySelector('#details')`, 'Play did not close details');
    // Local 2.3 with installed 2.2: Play still follows the installed build.
    await dismissNotices();
    Object.assign(storedClimb(), { version: '2.3', versionCode: '23' }); await persist();
    actual.version = '2.3'; actual.versionCode = '23';
    installed.version = '2.2'; installed.versionCode = '22';
    await openDetails(climb.id);
    await check(`document.querySelector('[data-compatibility="mismatch"]')?.textContent.includes('not applied')`, 'Version mismatch disclosure missing');
    await capture('compatibility-mismatch');
    const launchBaseInstalledMatch = launchCalls.length;
    await js(`Array.from(document.querySelectorAll('#details button')).find(e => e.textContent === 'Play').click()`);
    await check(`document.body.textContent.includes('Using The Climb 2 2.2 compatibility profile')`, 'Installed 2.2 build must still use its profile when local is 2.3');
    assert.deepEqual(launchCalls[launchBaseInstalledMatch].compatibility, { precomposeProjectionLayers: true });
    assert.equal(launchCalls[launchBaseInstalledMatch].version, '2.2', 'Play must launch the installed version, not the local APK');
    await check(`!document.querySelector('#details')`, 'Installed-profile play did not close details');
    // Local 2.2 with installed 2.3: no profile actions or toast from Play.
    await dismissNotices();
    Object.assign(storedClimb(), { version: '2.2', versionCode: '22' }); await persist();
    actual.version = '2.2'; actual.versionCode = '22';
    installed.version = '2.3'; installed.versionCode = '23';
    await openDetails(climb.id);
    await check(`document.querySelector('[data-compatibility="matched"]')?.textContent.includes('The Climb 2 2.2')`, 'Local matched disclosure missing before unprofiled play');
    const launchBaseInstalledMismatch = launchCalls.length;
    await js(`Array.from(document.querySelectorAll('#details button')).find(e => e.textContent === 'Play').click()`);
    await check(`!document.querySelector('#details')`, 'Unprofiled play did not close details');
    assert.deepEqual(launchCalls[launchBaseInstalledMismatch].compatibility, {});
    assert.equal(launchCalls[launchBaseInstalledMismatch].version, '2.3', 'Play must launch the installed 2.3 build');
    assert.equal(await js(`document.body.textContent.includes('Using The Climb 2 2.2 compatibility profile')`), false, 'Unverified installed build must not toast a profile');
    // Stored 2.2 with actual input 2.3: Patch re-inspects, refreshes the saved
    // identity, returns the catalog without invoking the CLI.
    await dismissNotices();
    Object.assign(storedClimb(), { version: '2.2', versionCode: '22' }); await persist();
    actual.version = '2.3'; actual.versionCode = '23'; actual.package = climb.package;
    installed.version = '2.3'; installed.versionCode = '23';
    await openDetails(climb.id);
    await check(`document.querySelector('[data-compatibility="matched"]')?.textContent.includes('The Climb 2 2.2')`, 'Stale stored disclosure missing before re-inspection');
    const patchBaseRefresh = patchCalls.length;
    await choosePatch();
    await check(`Boolean(document.querySelector('[data-patch-options]'))`, 'Unverified version did not offer patch options');
    assert.equal(patchCalls.length, patchBaseRefresh, 'Opening options must not patch');
    assert.equal(await js(`document.body.textContent.includes('Patched with')`), false, 'Catalog response must not toast a patch');
    assert.equal(storedClimb().version, '2.3', 'Patch must refresh the persisted local version from the actual input');
    assert.equal(storedClimb().versionCode, '23');
    const savedClimb = JSON.parse(await fs.readFile(path.join(state.directory, 'library.json'), 'utf8')).games.find(g => g.id === climb.id);
    assert.equal(savedClimb.version, '2.3');
    assert.equal(savedClimb.versionCode, '23');
    await check(`document.querySelector('[data-compatibility="mismatch"]')?.textContent.includes('not applied')`, 'Refreshed mismatch disclosure missing');
    await capture('compatibility-patch-options');
    await closeDetails();
    // Stale stored 2.3 with actual input 2.2: automatic preset applies again.
    Object.assign(storedClimb(), { version: '2.3', versionCode: '23' }); await persist();
    actual.version = '2.2'; actual.versionCode = '22';
    await openDetails(climb.id);
    await check(`document.querySelector('[data-compatibility="mismatch"]')?.textContent.includes('not applied')`, 'Stale mismatch disclosure missing before auto preset');
    await dismissNotices();
    const patchBaseStaleAuto = patchCalls.length, apkBeforeStaleAuto = storedClimb().apk;
    await choosePatch();
    await check(`document.body.textContent.includes('Patched with The Climb 2 2.2 compatibility profile')`, 'Stale stored 2.3 with actual 2.2 must auto apply the preset');
    assert.ok(patchCalls[patchBaseStaleAuto].includes('--extra-patches=patch_vrapi_openxr'));
    assert.equal(await js(`Boolean(document.querySelector('[data-patch-options]'))`), false);
    assert.equal(storedClimb().version, '2.2');
    assert.equal(storedClimb().versionCode, '22');
    assert.notEqual(storedClimb().apk, apkBeforeStaleAuto, 'Successful preset must write a separate APK');
    // Different-package replacement: reject before any CLI call or metadata overwrite.
    await dismissNotices();
    const storedBeforePackage = { package: storedClimb().package, version: storedClimb().version, versionCode: storedClimb().versionCode, apk: storedClimb().apk };
    actual.package = 'com.other.game'; actual.version = '9.9'; actual.versionCode = '99';
    const patchBasePackage = patchCalls.length;
    await check(`Boolean(document.querySelector('#details'))`, 'Details must stay open for package rejection');
    await choosePatch();
    await check(`document.body.textContent.includes('APK package no longer matches this game')`, 'Package replacement error missing');
    assert.equal(patchCalls.length, patchBasePackage, 'Package mismatch must not invoke the CLI');
    assert.equal(storedClimb().package, storedBeforePackage.package, 'Package mismatch must not overwrite saved metadata');
    assert.equal(storedClimb().version, storedBeforePackage.version);
    assert.equal(storedClimb().versionCode, storedBeforePackage.versionCode);
    assert.equal(storedClimb().apk, storedBeforePackage.apk);
    assert.equal(await js(`Boolean(document.querySelector('[data-patch-options]'))`), false);
    actual.package = climb.package; actual.version = '2.2'; actual.versionCode = '22';
    await dismissNotices();
    // Picker opened for unmatched input cannot apply after the input becomes matched.
    Object.assign(storedClimb(), { version: '2.3', versionCode: '23' }); await persist();
    actual.version = '2.3'; actual.versionCode = '23';
    await check(`document.querySelector('[data-compatibility="mismatch"]')?.textContent.includes('not applied')`, 'Mismatch disclosure missing before stale picker race');
    const patchBaseRace = patchCalls.length;
    await choosePatch();
    await check(`Boolean(document.querySelector('[data-patch-options]'))`, 'Race setup did not open patch options');
    assert.equal(patchCalls.length, patchBaseRace);
    await js(`document.querySelector('[data-patch="patch_vrapi_openxr"]').click()`);
    actual.version = '2.2'; actual.versionCode = '22';
    const apkBeforeRace = storedClimb().apk;
    await js(`Array.from(document.querySelectorAll('#details button')).find(e => e.textContent === 'Apply selected patches').click()`);
    await check(`document.body.textContent.includes('This game now has a verified compatibility profile')`, 'Stale picker race must be rejected');
    assert.equal(patchCalls.length, patchBaseRace, 'Stale picker must not invoke the CLI after the input became matched');
    assert.equal(storedClimb().apk, apkBeforeRace, 'Stale picker must not mutate the APK');
    assert.equal(await js(`Boolean(document.querySelector('[data-patch-options]'))`), true, 'Rejected stale picker must stay open');
    await dismissNotices();
    await js(`Array.from(document.querySelectorAll('#details button')).find(e => e.textContent === 'Cancel').click()`);
    await check(`!document.querySelector('[data-patch-options]')`, 'Stale picker did not cancel');
    await closeDetails();
    // Unmatched manual selection still patches through the picker.
    Object.assign(storedClimb(), { version: '2.3', versionCode: '23' }); await persist();
    actual.version = '2.3'; actual.versionCode = '23';
    await openDetails(climb.id);
    await check(`document.querySelector('[data-compatibility="mismatch"]')?.textContent.includes('not applied')`, 'Manual patch setup disclosure missing');
    await dismissNotices();
    const patchBaseManual = patchCalls.length, apkBeforeManual = storedClimb().apk;
    await choosePatch();
    await check(`Boolean(document.querySelector('[data-patch-options]'))`, 'Manual patch options missing');
    assert.equal(patchCalls.length, patchBaseManual, 'Opening manual options must not patch');
    await js(`document.querySelector('[data-patch="patch_vrapi_openxr"]').click()`);
    await js(`Array.from(document.querySelectorAll('#details button')).find(e => e.textContent === 'Apply selected patches').click()`);
    await check(`!document.querySelector('[data-patch-options]')`, 'Manual patch did not finish');
    assert.ok(patchCalls[patchBaseManual].includes('--patches=patch_copy_libraries;patch_vrapi_openxr'));
    assert.ok(!patchCalls[patchBaseManual].some(arg => arg.startsWith('--extra-patches')));
    assert.notEqual(storedClimb().apk, apkBeforeManual);
    await check(`document.body.textContent.includes('Patched')`, 'Manual patch notification missing');
    assert.equal(await js(`document.body.textContent.includes('Patched with')`), false, 'Manual patch of an unverified version must not claim a profile');
    await dismissNotices();
    await closeDetails();
    await openDetails(game.id);
    await choosePatch();
    await check(`Boolean(document.querySelector('[data-patch-options]'))`, 'Undocumented game did not offer patch options');
    const patchBaseUndocumented = patchCalls.length;
    await js(`Array.from(document.querySelectorAll('#details button')).find(e => e.textContent === 'Cancel').click()`);
    await check(`!document.querySelector('[data-patch-options]')`, 'Undocumented cancel did not close options');
    assert.equal(patchCalls.length, patchBaseUndocumented, 'Cancel must not patch');
    await closeDetails();
    assert.equal(state.data.games.some(g => Object.hasOwn(g, 'compatibility')), false, 'Derived profiles must not be persisted');
    console.log('Library actions smoke passed: uninstall/import lifecycle, installed-vs-local Play authority, Patch re-inspection refresh, stale auto preset, package rejection, stale picker race, manual patch options and cancellation.');
  } finally {
    dialog.showOpenDialog = original.open; dialog.showMessageBox = original.confirm; runtime.inspect = original.inspect; runtime.prepareLaunch = original.prepare; runtime.uninstall = original.uninstall;
    runtime.launch = original.launch; ovrport.execute = original.execute; state.data.settings.ovrportCli = original.cli;
    ovrport.help.delete(cli); await fs.rm(cli, { force: true });
    await fs.rm(gameApk, { force: true });
    await fs.rm(path.join(state.directory, 'climb-base.apk'), { force: true });
    state.data.games = state.data.games.filter(g => g.id !== climb.id);
    state.data.games = state.data.games.filter(g => !g.package?.startsWith('com.axrbtest.'));
    state.data.jobs = state.data.jobs.filter(j => !['apk', 'uninstall'].includes(j.kind)); await persist();
  }
}
