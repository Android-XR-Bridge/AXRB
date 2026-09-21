import { useState } from 'react';
import { ChevronDown } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { activeStatuses, call } from './common';

export function Settings({ state, run, pending, notify }) {
  const [draft, setDraft] = useState({ ...state.settings });
  const [dirty, setDirty] = useState(false);
  const [report, setReport] = useState(null);
  const [storage, setStorage] = useState(state.settings.storageGB ?? 32);
  const edit = (key, value) => { setDraft(d => ({ ...d, [key]: value })); setDirty(true); };
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
    <div className="flex items-center justify-between border-y py-4"><span>Meta{state.signedIn && <span className="ml-3 text-muted-foreground">Connected</span>}</span>
      <Button type="button" variant="outline" disabled={pending.has('account')} onClick={() => run('account', () => call(state.signedIn ? 'logout' : 'login'))}>{state.signedIn ? 'Sign out' : 'Connect'}</Button>
    </div>
    {state.portable && <p className="text-sm text-muted-foreground">This portable install stores your Meta session unencrypted in data/meta-session.txt inside its folder. Anyone with a copy can use your Meta session for the store and downloads until it expires. Sign out before sharing or discarding the folder. Signing out does not revoke earlier copies; revoke the session in Meta account settings if the folder or drive is lost.</p>}
    <details className="group" data-runtime-settings><summary className="flex cursor-pointer list-none items-center justify-between py-1">Android runtime<ChevronDown className="size-4 transition-transform group-open:rotate-180" /></summary>
      <div className="mt-5 space-y-5">{!state.settings.managedDirectory && <>{field('sdk', 'Android SDK', { required: true })}{field('avd', 'Virtual device', { required: true, pattern: '[a-zA-Z0-9_-]+' })}</>}
        <div className="grid grid-cols-2 gap-4">{field('port', 'Port', { type: 'number', min: 5554, max: 5682, step: 2, required: true })}{field('memoryMB', 'Memory (MB)', { type: 'number', min: 2048, max: 16384, step: 1024, required: true })}</div>
        <div className="space-y-2">
          <label htmlFor="storageGB">Android storage (GB)</label>
          <div className="flex gap-2">
            <Input id="storageGB" name="storageGB" type="number" min="8" max="256" step="8"
              value={storage} onChange={e => setStorage(Number(e.target.value))} />
            <Button type="button" variant="outline" disabled={pending.has('storage') || storage === (state.settings.storageGB ?? 32)}
              onClick={() => run('storage', async () => {
                const result = await call('storage', Number(storage));
                notify(result.changed ? `Android storage set to ${result.storageGB} GB. It grows on the next Android start.` : 'Android storage is already that size.');
              })}>Resize</Button>
          </div>
          <p className="text-xs text-muted-foreground">Storage can only grow, and only while Android is stopped. The space is claimed as Android fills it, not up front.</p>
        </div>
        <div className="space-y-2">
          <div className="flex items-center justify-between"><label htmlFor="cpuCores">vCPUs</label><output htmlFor="cpuCores">{draft.cpuCores ?? 4}</output></div>
          <input id="cpuCores" name="cpuCores" type="range" min="2" max="6" step="1" value={draft.cpuCores ?? 4} onChange={e => edit('cpuCores', Number(e.target.value))} className="w-full accent-primary" aria-describedby="cpu-restart" />
          <p id="cpu-restart" className="text-xs text-muted-foreground">Applies after restarting Android.</p>
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
