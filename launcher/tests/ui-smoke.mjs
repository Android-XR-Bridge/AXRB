import fs from 'node:fs/promises';
import path from 'node:path';
import { clipboard } from 'electron';
import { QuestStore } from '../core/meta.mjs';

// This must exercise the button and OS clipboard together: a renderer-only
// clipboard call can appear wired correctly but fail under Electron permissions.
export async function diagnosticsSmoke(window) {
  const js = code => window.webContents.executeJavaScript(code);
  const wait = async predicate => {
    const deadline = Date.now() + 6000;
    while (!await predicate()) {
      if (Date.now() > deadline) throw new Error('Diagnostics clipboard action did not complete.');
      await new Promise(resolve => setTimeout(resolve, 30));
    }
  };
  await wait(() => js(`Boolean(document.querySelector('button[aria-controls="debug-content"]'))`));
  await js(`document.querySelector('button[aria-controls="debug-content"]').click()`);
  await wait(() => js(`Boolean(document.querySelector('[data-log-source]'))`));
  const expected = await js(`document.querySelector('[data-log-source]').lastElementChild.textContent`);
  await clipboard.writeText('AXRB clipboard regression: waiting for the Copy visible button');
  await js(`Array.from(document.querySelectorAll('button')).find(button => button.textContent === 'Copy visible').click()`);
  await wait(async () => (await clipboard.readText()).includes(expected));
  await js(`document.querySelector('button[aria-controls="debug-content"]').click()`);
  console.log('AXRB diagnostics smoke passed: visible log copied through the native clipboard.');
}

// Exercise the production bundle in sandboxed Electron. Uses a separate profile
// and a local store fixture; no account, network search, installs or game launches.
export async function uiSmoke(window, directory, snapshot, errors) {
  const wc = window.webContents;
  const js = code => wc.executeJavaScript(code);
  const tick = () => new Promise(resolve => setTimeout(resolve, 60));
  async function check(code, message) {
    for (let i = 0; i < 100; i++) { if (await js(code)) return; await tick(); }
    throw new Error(message);
  }
  async function click(selector) { await js(`document.querySelector(${JSON.stringify(selector)}).focus(); document.querySelector(${JSON.stringify(selector)}).click()`); await tick(); }
  async function input(selector, text) {
    await js(`{
      const el = document.querySelector(${JSON.stringify(selector)});
      el.focus();
      Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set.call(el, ${JSON.stringify(text)});
      el.dispatchEvent(new Event('input', { bubbles: true }));
    }`);
    await tick();
  }
  async function pointer(selector) {
    const bounds = await js(`(() => { const r = document.querySelector(${JSON.stringify(selector)}).getBoundingClientRect(); return { x: Math.round(r.x+r.width/2), y: Math.round(r.y+r.height/2) }; })()`);
    wc.sendInputEvent({ type: 'mouseDown', button: 'left', clickCount: 1, ...bounds });
    wc.sendInputEvent({ type: 'mouseUp', button: 'left', clickCount: 1, ...bounds });
    await tick();
  }
  async function escape() { wc.sendInputEvent({ type: 'keyDown', keyCode: 'Escape' }); wc.sendInputEvent({ type: 'keyUp', keyCode: 'Escape' }); await tick(); }
  async function capture(name) { await tick(); await fs.writeFile(path.join(directory, `${name}.png`), (await wc.capturePage()).toPNG()); }
  const originalSearch = QuestStore.prototype.search;
  QuestStore.prototype.search = async () => [{ id: '123456', name: 'Pinball smoke fixture', source: 'meta', owned: false }];
  try {

  await check(`Boolean(document.querySelector('#library-search'))`, 'Library did not load');
  await diagnosticsSmoke(window);
  await check(`Array.from(document.querySelectorAll('button')).some(b => b.textContent === 'Install ZIP')`, 'ZIP install action missing');
  if (await js(`Boolean(document.querySelector('h1, footer'))`)) throw new Error('Unexpected decorative heading/footer');
  await click('[data-nav="quest"]');
  await check(`Boolean(document.querySelector('[data-quest]'))`, 'Quest view did not render');
  await check(`Boolean(document.querySelector('[aria-label="Quest device"]')) && Boolean(document.querySelector('[aria-label="Refresh Quest"]'))`, 'Quest controls missing');
  await capture('quest');
  await click('[data-nav="settings"]');
  await check(`Boolean(document.querySelector('#settings-form'))`, 'Settings did not render');
  await input('#downloadDir', 'C:\\AXRB UI draft');
  wc.send('axrb:changed', snapshot());
  await tick();
  await check(`document.querySelector('#downloadDir').value === 'C:\\\\AXRB UI draft'`, 'State updates erased settings draft');
  await click('[data-runtime-settings] summary');
  await check(`document.querySelector('[data-runtime-settings]').open`, 'Runtime settings did not expand');
  const originalHud = await js(`document.querySelector('#fps-hud').checked`);
  await click('#fps-hud');
  await check(`document.querySelector('#fps-hud').checked !== ${originalHud}`, 'FPS HUD toggle did not update');
  await click('#fps-hud');
  await check(`document.querySelector('#fps-hud').checked === ${originalHud}`, 'FPS HUD toggle did not restore');
  await capture('settings');
  await click('[data-nav="downloads"]');
  await check(`Boolean(document.querySelector('.jobs'))`, 'Downloads did not render');
  await click('[data-nav="store"]');
  await input('#store-query', 'Pinball');
  await js(`document.querySelector('#store-search').requestSubmit()`);
  await check(`Boolean(document.querySelector('[data-store="true"]'))`, 'Store fixture did not render');
  const count = await js(`document.querySelectorAll('[data-store="true"]').length`);
  await new Promise(r => setTimeout(r, 1200));
  await capture('store');
  await click('[data-store="true"]');
  await check(`Boolean(document.querySelector('#details[role="dialog"]'))`, 'Game dialog did not open');
  await capture('details');
  // Add only to the isolated smoke profile. This never purchases/downloads a game.
  await js(`Array.from(document.querySelectorAll('#details button')).find(b => b.textContent.includes('Add to library'))?.click()`);
  await check(`Boolean(document.querySelector('[aria-label="Game actions"]'))`, 'Library add did not update open dialog');
  await pointer('[aria-label="Game actions"]');
  await check(`Boolean(document.querySelector('[role="menu"]'))`, 'Game actions menu did not open');
  await escape();
  await escape();
  await check(`!document.querySelector('#details')`, 'Escape did not close dialog');
  await check(`document.activeElement.matches('[data-store="true"]')`, 'Dialog did not restore keyboard focus');
  await click('[data-nav="library"]');
  await check(`Boolean(document.querySelector('[data-game]'))`, 'Added game missing from library');
  await capture('window');
  await input('#library-search', 'no-match-axrb-test');
  wc.send('axrb:changed', snapshot());
  await tick();
  await check(`document.activeElement.id === 'library-search' && document.querySelector('#library-search').value === 'no-match-axrb-test' && !document.querySelector('[data-game]')`, 'Library filter/focus lost during state update');
  await input('#library-search', '');
  await pointer('[aria-label="Filter library"]');
  await check(`Boolean(document.querySelector('[role="listbox"]'))`, 'Filter did not open');
  wc.sendInputEvent({ type: 'keyDown', keyCode: 'Down' });
  wc.sendInputEvent({ type: 'keyUp', keyCode: 'Down' });
  await tick();
  await check(`document.activeElement.textContent === 'Installed'`, 'Keyboard did not focus Installed filter');
  wc.sendInputEvent({ type: 'keyDown', keyCode: 'Enter' });
  wc.sendInputEvent({ type: 'keyUp', keyCode: 'Enter' });
  await tick();
  await check(`!document.querySelector('[data-game]')`, 'Installed filter did not hide uninstalled game');

  const fixture = snapshot();
  fixture.jobs = [{ id: 'ui-download', gameId: '123456', name: 'Download test', status: 'downloading', stage: 'base.apk', completed: 100, total: 200 }];
  wc.send('axrb:changed', fixture);
  await click('[data-nav="downloads"]');
  await check(`document.querySelector('progress')?.value === 100`, 'Download progress did not render');
  await capture('downloads');
  window.setSize(920, 640);
  await tick();
  await click('[data-nav="library"]');
  await check(`document.documentElement.scrollWidth <= window.innerWidth`, 'Library import controls overflow minimum window width');
  await capture('library-small');
  await click('[data-nav="store"]');
  await check(`document.documentElement.scrollWidth <= window.innerWidth`, 'UI overflows minimum window width');
  await capture('store-small');
  wc.send('axrb:launch-error', 'Game launch test failed');
  await check(`document.querySelector('[role="alert"]')?.textContent.includes('Game launch test failed')`, 'Launch failure was not shown');
  const setup = { phase: 'hypervisor', directory: 'C:\\AXRB Runtime', active: false };
  wc.send('axrb:changed', { ...snapshot(), setup });
  await check(`document.body.textContent.includes('Enable Windows Hypervisor Platform')`, 'Virtualization instructions missing');
  await capture('setup-hypervisor');
  setup.phase = 'install';
  wc.send('axrb:changed', { ...snapshot(), setup });
  await check(`Boolean(document.querySelector('#runtime-folder'))`, 'Setup folder missing');
  await check(`document.querySelector('[data-setup-start]')?.disabled`, 'Setup did not require license acceptance');
  await click('#setup-license');
  await check(`!document.querySelector('[data-setup-start]')?.disabled`, 'License acceptance did not enable setup');
  await check(`Boolean(document.querySelector('[data-setup-archives] button'))`, 'New setup does not offer downloaded archives');
  await capture('setup-install');
  setup.current = { directory: 'C:\\Previous AXRB\\AXRB Runtime', storageGB: 64 };
  setup.directory = setup.current.directory;
  wc.send('axrb:changed', { ...snapshot(), setup });
  await check(`document.querySelector('#use-current-installation')?.checked && !document.querySelector('#runtime-folder')`, 'Discovered installation did not default to reuse');
  await check(`document.querySelector('#current-installation').textContent.includes(${JSON.stringify(setup.current.directory)}) && document.querySelector('#current-installation').textContent.includes('64 GB')`, 'Current installation path and disk size are missing');
  await check(`!document.querySelector('[data-setup-archives]')`, 'Existing disk reuse exposes local archive selection');
  await capture('setup-current');
  await click('#use-current-installation');
  await check(`document.querySelector('#runtime-folder')?.value === ''`, 'New installation silently inherited the current folder');
  await input('#runtime-folder', 'D:\\AXRB-Install');
  await input('#android-storage', '16');
  wc.send('axrb:changed', { ...snapshot(), setup });
  await check(`!document.querySelector('#use-current-installation').checked && document.querySelector('#runtime-folder').value === 'D:\\\\AXRB-Install'`, 'Setup refresh erased the new-install choice');
  await click('#use-current-installation');
  await check(`!document.querySelector('#android-storage')`, 'Current installation exposes an editable disk size');
  await click('#use-current-installation');
  await check(`document.querySelector('#runtime-folder').value === 'D:\\\\AXRB-Install' && document.querySelector('#android-storage').value === '16'`, 'Toggling reuse erased the new-install draft');
  await capture('setup-new');
  await click('#use-current-installation');
  setup.current = { ...setup.current, storageGB: null };
  wc.send('axrb:changed', { ...snapshot(), setup });
  await check(`document.querySelector('[data-setup-start]')?.disabled`, 'Setup allowed reuse with an unknown disk size');
  setup.current = { ...setup.current, storageGB: 64 };
  setup.currentNeeds = { directory: setup.current.directory, components: [], downloadBytes: 0, avd: false, moved: true, runtime: true, android: true, licensed: true, ready: false, fresh: false };
  wc.send('axrb:changed', { ...snapshot(), setup });
  await check(`!document.querySelector('#setup-license') && !document.querySelector('[data-setup-start]')?.disabled`, 'Runtime-only repair unnecessarily requires the SDK license again');
  await check(`Boolean(document.querySelector('[data-setup-needs]'))`, 'Runtime repair does not describe the work needed');
  await capture('setup-repair');
  await click('#use-current-installation');
  await check(`Boolean(document.querySelector('#setup-license'))`, 'A different new target inherited the old runtime license');
  Object.assign(setup, { phase: 'download', active: true, component: 'Android 16 with ARM64 translation', completed: 512, total: 1024 });
  wc.send('axrb:changed', { ...snapshot(), setup });
  await check(`document.querySelector('progress')?.value === 512 && document.body.textContent.includes('50%')`, 'Setup progress missing');
  await capture('setup-download');
  Object.assign(setup, { phase: 'error', active: false, error: 'Download interrupted' });
  wc.send('axrb:changed', { ...snapshot(), setup });
  await check(`document.querySelector('[role="alert"]')?.textContent.includes('Download interrupted') && document.body.textContent.includes('Retry setup')`, 'Setup retry missing');
  await check(`document.documentElement.scrollWidth <= window.innerWidth`, 'Setup overflows minimum window width');
  await js(`document.querySelector('[data-setup-start]').scrollIntoView({ block: 'center' })`);
  await check(`(() => { const box = document.querySelector('[data-setup-start]').getBoundingClientRect(); return box.top >= 0 && box.bottom <= window.innerHeight; })()`, 'Setup action is unreachable at minimum window size');
  await capture('setup-small');
  if (errors.length) throw new Error(`Renderer errors: ${errors.join('; ')}`);
  console.log(`AXRB React smoke passed: navigation, settings draft, ${count} local store fixture, library add/filter, dialog/menu keyboard focus, download progress, 920px layout`);
  } finally { QuestStore.prototype.search = originalSearch; }
}
