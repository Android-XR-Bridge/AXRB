import { useEffect, useRef, useState } from 'react';
import { ChevronDown, RefreshCw } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { activeStatuses, bytes, call, EmulatorStatus } from './common';

export function Settings({ state, run, pending, notify }) {
  const [draft, setDraft] = useState({ ...state.settings });
  const [dirty, setDirty] = useState(false);
  const [report, setReport] = useState(null);
  const [scan, setScan] = useState(null);
  const [storage, setStorage] = useState(state.settings.storageGB ?? 32);
  const [storageInfo, setStorageInfo] = useState(null);
  const [storageError, setStorageError] = useState('');
  const [loadingStorage, setLoadingStorage] = useState(false);
  const storageRequest = useRef(false);
  const hostCapacity = storageInfo?.host.totalBytes || 0;
  const hostEnvironment = Math.min(storageInfo?.host.usedBytes || 0, hostCapacity);
  const hostUsed = Math.max(0, Math.min(hostCapacity, hostCapacity - (storageInfo?.host.freeBytes || 0)));
  const hostOther = Math.min(Math.max(0, hostUsed - hostEnvironment), hostCapacity - hostEnvironment);
  const hostFree = Math.max(0, hostCapacity - hostEnvironment - hostOther);
  const hostPercent = value => hostCapacity ? `${value / hostCapacity * 100}%` : '0%';
  const androidPercent = value => storageInfo?.android?.totalBytes
    ? `${Math.min(100, value / storageInfo.android.totalBytes * 100)}%` : '0%';
  const edit = (key, value) => { setDraft(d => ({ ...d, [key]: value })); setDirty(true); };
  const refreshStorageInfo = async () => {
    if (storageRequest.current) return;
    storageRequest.current = true; setLoadingStorage(true); setStorageError('');
    try { setStorageInfo(await call('storageInfo')); }
    catch (error) { setStorageError(error.message); }
    finally { storageRequest.current = false; setLoadingStorage(false); }
  };
  useEffect(() => {
    void refreshStorageInfo();
    const timer = setInterval(() => void refreshStorageInfo(), 30000);
    return () => clearInterval(timer);
  }, []);
  const browse = key => run(`choose-${key}`, async () => {
    const value = await call(key === 'ovrportCli' ? 'chooseCli' : 'chooseFolder');
    if (value) edit(key, value);
  });
  const field = (key, label, options = {}) => <div className="space-y-2">
    <label htmlFor={key}>{label}</label>
    <div className="flex gap-2"><Input id={key} name={key} value={draft[key] ?? ''} onChange={e => edit(key, e.target.value)} {...options} />
      {['downloadDir', 'ovrportCli'].includes(key) && <Button type="button" variant="outline" disabled={pending.has(`choose-${key}`)} onClick={() => browse(key)}>Browse</Button>}
    </div>
  </div>;
  return <form id="settings-form" className="max-w-2xl space-y-7" onSubmit={e => {
    e.preventDefault(); run('settings', async () => {
      await call('settings', { ...draft, port: Number(draft.port), memoryMB: Number(draft.memoryMB), cpuCores: Number(draft.cpuCores ?? 4) });
      setDirty(false); notify('Saved');
    });
  }}>
    {field('downloadDir', 'Download folder', { required: true })}
    {field('ovrportCli', 'ovrport CLI', { placeholder: 'Optional .exe or .jar' })}
    {state.portable && <p className="text-sm text-muted-foreground">Portable mode keeps downloads, imported files and patch output inside its folder. Put the complete optional ovrport CLI distribution inside that folder before selecting it. Java, when required by the CLI, remains a system prerequisite.</p>}
    <div className="flex items-center justify-between border-y py-4"><span>Meta{state.signedIn && <span className="ml-3 text-muted-foreground">Connected</span>}</span>
      <Button type="button" variant="outline" disabled={pending.has('account')} onClick={() => run('account', () => call(state.signedIn ? 'logout' : 'login'))}>{state.signedIn ? 'Sign out' : 'Connect'}</Button>
    </div>
    {state.portable && <p className="text-sm text-muted-foreground">This portable install stores your Meta session unencrypted in data/meta-session.txt inside its folder. Anyone with a copy can use your Meta session for the store and downloads until it expires. Sign out before sharing or discarding the folder. Signing out does not revoke earlier copies; revoke the session in Meta account settings if the folder or drive is lost.</p>}
    <details className="group" data-runtime-settings><summary className="flex cursor-pointer list-none items-center justify-between py-1">Android runtime<ChevronDown className="size-4 transition-transform group-open:rotate-180" /></summary>
      <div className="mt-5 space-y-5">
        <div className="flex items-center justify-between gap-4 rounded-md border p-3">
          <EmulatorStatus emulator={state.emulator} />
          <div className="flex shrink-0 gap-2">
            <Button type="button" variant="outline" disabled={pending.has('start-android') || state.busy || ['online', 'starting'].includes(state.emulator?.phase)}
              onClick={() => run('start-android', async () => { await call('startAndroid'); await refreshStorageInfo(); notify('Android started'); })}>Start</Button>
            <Button type="button" variant="outline" disabled={pending.has('stop-android') || state.busy || Boolean(state.running) || !['online', 'starting'].includes(state.emulator?.phase)}
              onClick={() => run('stop-android', async () => { await call('stopAndroid'); await refreshStorageInfo(); notify('Android stopped'); })}>Stop</Button>
          </div>
        </div>
        {/* Escaped so the class stays valid under the stricter v-mode regex engine some Chromium builds use for pattern validation; an unescaped trailing hyphen used to pass unnoticed. */}
        {!state.settings.managedDirectory && <>{field('sdk', 'Android SDK', { required: true })}{field('avd', 'Virtual device', { required: true, pattern: '[a-zA-Z0-9_\\-]+' })}</>}
        <div className="grid grid-cols-2 gap-4">{field('port', 'Port', { type: 'number', min: 5554, max: 5682, step: 2, required: true })}{field('memoryMB', 'Memory (MB)', { type: 'number', min: 2048, max: 16384, step: 1024, required: true })}</div>
        <div className="space-y-2">
          <label htmlFor="storageGB">Android storage (GB)</label>
          <div className="flex gap-2">
            <Input id="storageGB" name="storageGB" type="number" min="8" max="256" step="8"
              value={storage} onChange={e => setStorage(Number(e.target.value))} />
            <Button type="button" variant="outline" disabled={pending.has('storage') || storage === (state.settings.storageGB ?? 32)}
              onClick={() => run('storage', async () => {
                const result = await call('storage', Number(storage));
                await refreshStorageInfo();
                notify(result.changed ? `Android storage set to ${result.storageGB} GB. It grows on the next Android start.` : 'Android storage is already that size.');
              })}>Resize</Button>
          </div>
          <p className="text-xs text-muted-foreground">Storage can only grow, and only while Android is stopped. The space is claimed as Android fills it, not up front.</p>
        </div>
        <section className="space-y-3 rounded-md border p-4" aria-label="Storage usage" aria-live="polite">
          <div className="flex items-center justify-between gap-3">
            <h3 className="font-medium">Storage usage</h3>
            <Button type="button" variant="outline" size="sm" disabled={loadingStorage} onClick={() => void refreshStorageInfo()}>
              <RefreshCw className={loadingStorage ? 'animate-spin' : ''} />Refresh
            </Button>
          </div>
          {storageError && <p className="text-sm text-destructive">{storageError}</p>}
          {storageInfo && <>
            <div className="space-y-2">
              <div className="flex h-4 w-full overflow-hidden rounded-sm border bg-background" role="img"
                aria-label={`${storageInfo.host.drive} drive, ${bytes(storageInfo.host.totalBytes)} total: Android environment ${bytes(storageInfo.host.usedBytes)}, other data ${bytes(hostOther)}, free ${bytes(storageInfo.host.freeBytes)}`}>
                <span className="h-full bg-muted-foreground/50" style={{ width: hostPercent(hostOther) }} title={`Other drive usage: ${bytes(hostOther)}`} />
                <span className="h-full bg-primary" style={{ width: hostPercent(hostEnvironment) }} title={`Android environment: ${bytes(hostEnvironment)}`} />
                <span className="h-full" style={{ width: hostPercent(hostFree) }} title={`Free space: ${bytes(hostFree)}`} />
              </div>
              <div className="flex flex-wrap gap-x-5 gap-y-1 text-xs text-muted-foreground">
                <span className="flex items-center gap-1.5"><span className="size-2.5 rounded-sm bg-muted-foreground/50" />Other drive usage · {bytes(hostOther)}</span>
                <span className="flex items-center gap-1.5"><span className="size-2.5 rounded-sm bg-primary" />Android environment · {bytes(hostEnvironment)}</span>
                <span className="flex items-center gap-1.5"><span className="size-2.5 rounded-sm border" />Free · {bytes(hostFree)}</span>
                <span className="ml-auto">{storageInfo.host.drive} · {bytes(storageInfo.host.totalBytes)} total</span>
              </div>
            </div>
            {storageInfo.android
              ? <div className="space-y-2">
                <div className="flex items-baseline justify-between gap-3 text-sm">
                  <span>Android internal storage</span>
                  <span className="text-right tabular-nums text-muted-foreground">{bytes(storageInfo.android.usedBytes)} used · {bytes(storageInfo.android.freeBytes)} free of {bytes(storageInfo.android.totalBytes)}</span>
                </div>
                <div className="h-3 w-full overflow-hidden rounded-sm border bg-background" role="img"
                  aria-label={`Android internal storage: ${bytes(storageInfo.android.usedBytes)} used, ${bytes(storageInfo.android.freeBytes)} free, ${bytes(storageInfo.android.totalBytes)} total`}>
                  <div className="h-full bg-primary" style={{ width: androidPercent(storageInfo.android.usedBytes) }} />
                </div>
              </div>
              : <p className="text-xs text-muted-foreground">Start Android to see internal storage usage. Host usage is the allocated size of the managed runtime folder, or the AVD folder for a custom SDK setup. Values refresh every 30 seconds.</p>}
          </>}
          {!storageInfo && !storageError && <p className="text-sm text-muted-foreground">Loading storage usage…</p>}
        </section>
        <div className="space-y-2">
          <div className="flex items-center justify-between"><label htmlFor="cpuCores">vCPUs</label><output htmlFor="cpuCores">{draft.cpuCores ?? 4}</output></div>
          <input id="cpuCores" name="cpuCores" type="range" min="2" max="6" step="1" value={draft.cpuCores ?? 4} onChange={e => edit('cpuCores', Number(e.target.value))} className="w-full accent-primary" aria-describedby="cpu-restart" />
          <p id="cpu-restart" className="text-xs text-muted-foreground">Applies after restarting Android. Six is the emulator's own maximum. Leave some of the PC's cores free: past that, Android competes with the headset compositor and frames get worse, not better.</p>
        </div>
      </div>
    </details>
    <div className="flex items-center justify-between gap-4">
      <div>
        <label htmlFor="precompose-projection-layers">Precompose projection layers</label>
        <p id="precompose-projection-layers-help" className="text-xs text-muted-foreground">Off by default. Enable for compatibility with games such as The Climb 2 that use multiple projection layers. This adds GPU cost and applies on the next game launch.</p>
      </div>
      <input id="precompose-projection-layers" type="checkbox" role="switch" checked={draft.precomposeProjectionLayers === true}
        className="size-4 shrink-0 accent-primary" aria-describedby="precompose-projection-layers-help"
        onChange={e => edit('precomposeProjectionLayers', e.target.checked)} />
    </div>
    <label className="flex items-center justify-between gap-4" htmlFor="fps-hud">
      <span>Show FPS in headset <span className="text-xs text-muted-foreground">(debug)</span></span>
      <input id="fps-hud" type="checkbox" role="switch" checked={state.settings.fpsHud === true} disabled={pending.has('fpsHud')}
        className="size-4 accent-primary" onChange={e => { const enabled = e.target.checked; run('fpsHud', () => call('fpsHud', enabled)); }} />
    </label>
    {/* A scan samples a session that is running, so there is nothing to offer
        between games and the control stays out of the way until there is. */}
    {state.running && <div className="space-y-3 border-t pt-5">
      <div className="flex items-center justify-between gap-4">
        <div>
          <div>Performance scan</div>
          <p className="text-xs text-muted-foreground">Watches the running game for ten seconds — Windows processes, GPU engines, Android threads and the bridge's own timings — and says where the frame time went. Keep playing while it runs. Same redaction as below.</p>
        </div>
        <div className="flex shrink-0 gap-2">
          <Button type="button" variant="outline" disabled={pending.has('scan')}
            onClick={() => run('scan', async () => setScan({ ...await call('performanceScan', {}), url: '' }))}>{pending.has('scan') ? 'Scanning…' : 'Run scan'}</Button>
          <Button type="button" variant="outline" disabled={pending.has('scan')}
            onClick={() => run('scan', async () => {
              const result = await call('performanceScan', { upload: true });
              setScan(result);
              try { await navigator.clipboard.writeText(result.url); notify('Scan link copied'); } catch { notify('Scan uploaded'); }
            })}>Run &amp; upload</Button>
        </div>
      </div>
      {scan?.url && <p className="text-sm break-all">Share this link: <a href={scan.url} target="_blank" rel="noreferrer" className="underline">{scan.url}</a></p>}
      {scan && <textarea readOnly value={scan.bundle} rows={16} aria-label="Performance scan"
        className="w-full rounded-md border bg-secondary/40 p-3 font-mono text-xs" />}
    </div>}
    <div className="space-y-3 border-t pt-5">
      <div className="flex items-center justify-between gap-4">
        <div>
          <div>Diagnostics</div>
          <p className="text-xs text-muted-foreground">Android, emulator, OpenXR host and launcher logs. Preview before sharing: known credentials and Windows identity are redacted, but game output may still contain personal information. Uploads are public and expire after 30 days.</p>
        </div>
        <div className="flex shrink-0 gap-2">
          <Button type="button" variant="outline" disabled={pending.has('diagnostics')}
            onClick={() => run('diagnostics', async () => setReport({ ...await call('diagnostics', { upload: false }), url: '' }))}>Preview</Button>
          <Button type="button" variant="outline" disabled={pending.has('diagnostics') || !report?.bundle || Boolean(report.url)}
            onClick={() => run('diagnostics', async () => {
              const result = await call('diagnostics', { upload: true });
              setReport(result);
              try { await call('copyText', result.url); notify('Log link copied'); } catch { notify('Log uploaded'); }
            })}>Upload &amp; copy link</Button>
        </div>
      </div>
      {field('diagnosticsEndpoint', 'Upload endpoint', { placeholder: 'Optional https:// URL — blank uses the public paste service' })}
      {report?.url && <p className="text-sm break-all">Share this link: <a href={report.url} target="_blank" rel="noreferrer" className="underline">{report.url}</a></p>}
      {report && !report.url && <>
        <p className="text-xs text-muted-foreground">Nothing has been uploaded. Review the bundle, then use Upload.</p>
        <textarea readOnly value={report.bundle} rows={14} aria-label="Diagnostics bundle"
          className="w-full rounded-md border bg-secondary/40 p-3 font-mono text-xs" />
      </>}
    </div>
    <Button type="submit" disabled={!dirty || state.busy || Boolean(state.running) || pending.has('settings') || state.jobs.some(j => activeStatuses.includes(j.status))}>Save</Button>
  </form>;
}
