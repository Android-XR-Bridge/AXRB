import { useEffect, useState } from 'react';
import { Loader2 } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { call } from './common';

export function SetupScreen({ setup }) {
  const current = setup.current;
  const [newDirectory, setDirectory] = useState(null);
  const normalizeDirectory = value => String(value || '').replaceAll('/', '\\').replace(/[\\]+$/, '').toLowerCase();
  // Resume a selected new disk after failure; keep an explicit reuse choice
  // through status refreshes for the same current disk.
  const [reuseChoice, setReuseChoice] = useState(null);
  const useCurrent = Boolean(current) && (reuseChoice?.directory === current.directory
    ? reuseChoice.enabled : normalizeDirectory(current.directory) === normalizeDirectory(setup.directory));
  const directory = newDirectory ?? (current && normalizeDirectory(current.directory) === normalizeDirectory(setup.directory)
    ? '' : setup.directory.replace(/[\\/]AXRB Runtime$/, '').replace(/^([A-Za-z]:)$/, '$1\\'));
  const [accepted, setAccepted] = useState(false);
  const [storageGB, setStorageGB] = useState(setup.storageGB ?? 32);
  const selectedStorageGB = useCurrent ? current.storageGB : storageGB;
  const validStorage = Number.isInteger(selectedStorageGB) && selectedStorageGB >= 8 && selectedStorageGB <= 256;
  const [error, setError] = useState('');
  const [archives, setArchives] = useState([]);
  const [selectingArchives, setSelectingArchives] = useState(false);
  const [starting, setStarting] = useState(false);
  const [now, setNow] = useState(Date.now());
  useEffect(() => {
    if (!setup.active || !setup.startedAt) return undefined;
    const timer = setInterval(() => setNow(Date.now()), 1000);
    return () => clearInterval(timer);
  }, [setup.active, setup.startedAt]);
  const invoke = async (name, value) => { try { setError(''); return await call(name, value); } catch (e) { setError(e.message); } };
  const percent = setup.total ? Math.min(100, Math.floor(setup.completed / setup.total * 100)) : 0;
  const elapsed = setup.active && setup.startedAt ? Math.max(0, Math.floor((now - setup.startedAt) / 1000)) : 0;
  const elapsedText = `${Math.floor(elapsed / 60)}:${String(elapsed % 60).padStart(2, '0')}`;
  const labels = { checking: 'Checking your PC', download: 'Downloading', verify: 'Verifying download', extract: 'Extracting', boot: 'Preparing Android' };
  const target = useCurrent ? current.directory : `${directory.replace(/[\\/]+$/, '')}\\AXRB Runtime`;
  const inspected = useCurrent ? setup.currentNeeds ?? setup.needs : setup.needs;
  const needs = inspected && normalizeDirectory(inspected.directory) === normalizeDirectory(target) ? inspected : null;
  const missing = needs?.components.filter(component => useCurrent || !archives.some(archive => archive.id === component.id));
  const downloadBytes = missing?.reduce((bytes, component) => bytes + component.size, 0);
  const sizeText = bytes => bytes >= 1024 ** 3 ? `${(bytes / 1024 ** 3).toFixed(1)} GB` : `${Math.ceil(bytes / 1024 ** 2)} MB`;
  const licenseNeeded = !needs?.licensed || Boolean(needs.components.length);
  const steps = !needs ? [] : [
    ...(missing.length ? [`Download ${missing.map(component => component.name).join(', ')} (${sizeText(downloadBytes)})`] : []),
    ...(!useCurrent && archives.length ? [`Use ${archives.length} verified local setup archive${archives.length === 1 ? '' : 's'}`] : []),
    ...(needs.avd ? ['Create the Android virtual device configuration'] : []),
    ...(!needs.android ? ['Create Android storage'] : []),
    ...(needs.moved ? ['Update the moved virtual device paths and discard its old quick-boot snapshot'] : []),
    ...(needs.runtime ? ['Install this build’s AXRB runtime app in Android (no download)'] : []),
  ];
  const selectArchives = async () => {
    setSelectingArchives(true);
    try { const selected = await invoke('chooseSetupArchives'); if (selected) setArchives(selected); }
    finally { setSelectingArchives(false); }
  };
  const start = async () => {
    setStarting(true);
    try { await invoke('setupStart', { directory, accepted: licenseNeeded ? accepted : true,
      storageGB: selectedStorageGB, useCurrent, archives: useCurrent ? [] : archives.map(archive => archive.path) }); }
    finally { setStarting(false); }
  };
  return <main className="flex min-h-screen items-center justify-center p-6"><section className="w-full max-w-4xl space-y-4" aria-label="Runtime setup">
    <h1 className="text-xl font-semibold">AXRB</h1>
    {setup.phase === 'unsupported' ? <>
      <p>This build requires a Windows x64 PC, an AMD or NVIDIA GPU, and at least 12 GB RAM.</p>
      <p className="text-sm text-muted-foreground">{setup.hardware?.gpu} · {setup.hardware?.memoryGB} GB RAM</p>
      <Button variant="outline" onClick={() => invoke('setupCheck')}>Check again</Button>
    </> : setup.phase === 'hypervisor' ? <>
      <p>Enable Windows Hypervisor Platform to run Android.</p>
      <ol className="list-decimal space-y-2 pl-5 text-sm text-muted-foreground"><li>Open Windows Features and enable <strong>Windows Hypervisor Platform</strong>.</li><li>Restart your PC, then reopen AXRB.</li><li>If it remains unavailable, enable Intel VT-x or AMD SVM in your BIOS.</li></ol>
      <div className="flex gap-3"><Button onClick={() => invoke('setupFeatures')}>Windows Features</Button><Button variant="outline" onClick={() => invoke('setupCheck')}>Check again</Button></div>
    </> : setup.active || setup.phase === 'checking' ? <>
      <div className="flex items-center gap-3" role="status"><Loader2 className="size-4 animate-spin" /><span>{labels[setup.phase] || setup.phase}{setup.component ? ` · ${setup.component}` : ''}</span>{setup.active && <span className="ml-auto tabular-nums text-sm text-muted-foreground" aria-label="Elapsed time">{elapsedText}</span>}</div>
      {setup.total > 0 && <><progress aria-label="Setup progress" value={setup.completed} max={setup.total} className="h-2 w-full accent-primary" /><div className="flex justify-between text-sm text-muted-foreground"><span>{setup.phase === 'download' ? `${(setup.completed / 1024 ** 2).toFixed(0)} / ${(setup.total / 1024 ** 2).toFixed(0)} MB` : 'Files'}</span><span>{percent}%</span></div></>}
      {setup.phase === 'boot' && <p className="text-sm text-muted-foreground">First boot can take a few minutes.</p>}
      {setup.phase === 'boot' && setup.logs?.length > 0 && <pre aria-label="Android startup log" className="max-h-56 overflow-auto rounded-md bg-muted p-3 text-[11px] leading-4 text-muted-foreground whitespace-pre-wrap">{setup.logs.join('\n')}</pre>}
      {setup.active && <Button variant="outline" disabled={setup.cancelling} onClick={() => invoke('setupCancel')}>{setup.cancelling ? 'Stopping setup?' : 'Cancel'}</Button>}
    </> : <>
      <p>{useCurrent ? 'Set up AXRB using your existing Android disk.' : 'Set up Android 16 and the emulator.'}</p>
      <div className="grid gap-6 md:grid-cols-2"><div className="space-y-4">
      {current && <div className="space-y-3 rounded-lg border p-4">
        <label className="flex items-start gap-3 text-sm"><input id="use-current-installation" type="checkbox" checked={useCurrent} onChange={e => { setReuseChoice({ directory: current.directory, enabled: e.target.checked }); setError(''); }} aria-describedby="current-installation" className="mt-1" /><span>Use current Android installation</span></label>
        <div id="current-installation" className="space-y-1 text-sm">
          <p className="font-medium">Current installation</p>
          <p className="break-all text-muted-foreground">{current.directory}</p>
          <p className="text-muted-foreground">{current.storageGB === null ? 'Android disk size is unknown.' : `${current.storageGB} GB Android storage`}</p>
        </div>
      </div>}
      {useCurrent ? <p className="text-sm text-muted-foreground">{current.storageGB === null
        ? 'The disk configuration is missing or unreadable. Restore config.ini, or uncheck the box to install in a new folder.'
        : 'The current disk size, installed games and saves are preserved. Storage can be grown later in Settings.'}</p> : <>
        <div className="space-y-2"><label htmlFor="runtime-folder" className="text-sm">{current ? 'New install folder' : 'Install folder'}</label><div className="flex gap-2"><Input id="runtime-folder" value={directory} onChange={e => setDirectory(e.target.value)} /><Button variant="outline" onClick={async () => { const value = await invoke('chooseFolder'); if (value) setDirectory(value); }}>Browse</Button></div></div>
        <p className="text-xs text-muted-foreground">Setup creates an AXRB Runtime subfolder.{current && ' The old installation stays untouched; its games and saves are not copied.'}</p>
        {setup.portableRoot && <p className="text-xs text-muted-foreground">Portable mode keeps Android inside {setup.portableRoot}. Choose this folder or a subfolder.</p>}
        <div className="space-y-2"><label htmlFor="android-storage" className="text-sm">Android storage (GB)</label><Input id="android-storage" type="number" min="8" max="256" step="8" value={storageGB} onChange={e => setStorageGB(Number(e.target.value))} /></div>
        <p className="text-sm text-muted-foreground">{downloadBytes !== undefined ? `${sizeText(downloadBytes)} still to download. ` : 'Only missing components are downloaded. '}Allow {Math.ceil(storageGB * 1.2 + 14)} GB free for setup. Android checks this space before creating its disk. Downloaded game APKs need additional space.</p>
      </>}
      </div><div className="space-y-4">
        {!useCurrent && <div className="space-y-2" data-setup-archives>
          <div className="flex flex-wrap gap-2">
            <Button variant="outline" disabled={selectingArchives || starting} onClick={selectArchives}>{selectingArchives ? 'Checking selected files…' : 'Use downloaded setup files'}</Button>
            {archives.length > 0 && <Button variant="ghost" disabled={selectingArchives || starting} onClick={() => setArchives([])}>Clear selection</Button>}
          </div>
          {archives.length > 0 && <div role="status" className="text-sm">
            <p>{archives.length} recognized archive{archives.length === 1 ? '' : 's'} selected.</p>
            <ul className="list-disc pl-5">{archives.map(archive => <li key={archive.id} className="break-words">{archive.name} — {archive.path.split(/[\\/]/).at(-1)}</li>)}</ul>
          </div>}
        </div>}
      {steps.length > 0 && <div className="space-y-2 text-sm" data-setup-needs>
        <p>Setup needs to complete:</p>
        <ul className="list-disc space-y-1 pl-5">{steps.map(step => <li key={step}>{step}</li>)}</ul>
        {useCurrent && needs.android && <p className="text-muted-foreground">Installed games and Android storage are kept. A changed AXRB runtime app does not require downloading Android again.</p>}
      </div>}
      </div></div>
      <div className="flex flex-wrap items-center justify-between gap-3">
      {licenseNeeded
        ? <label className="flex items-start gap-3 text-sm"><input id="setup-license" type="checkbox" checked={accepted} onChange={e => setAccepted(e.target.checked)} className="mt-1" /><span>I accept the <button className="underline" onClick={e => { e.preventDefault(); invoke('setupLicense'); }}>Android SDK license</button>.</span></label>
        : <p className="text-xs text-muted-foreground">The Android SDK license was accepted for this runtime, and no SDK components need installing.</p>}
      <Button data-setup-start disabled={starting || selectingArchives || (licenseNeeded && !accepted) || (!useCurrent && !directory) || !validStorage} onClick={start}>{starting ? 'Checking setup files…' : ['error', 'cancelled'].includes(setup.phase) ? 'Retry setup' : useCurrent ? needs ? 'Finish setup' : 'Set up current installation' : archives.length ? 'Set up Android' : 'Download and set up'}</Button>
      {setup.phase === 'error' && <Button variant="outline" className="ml-3" onClick={() => invoke('setupCheck')}>Check again</Button>}
      </div>
      <p className="text-xs text-muted-foreground">SteamVR or another active OpenXR runtime is required to play. ovrport is not included; import APKs you have patched separately.</p>
    </>}
    {(error || setup.error) && <p role="alert" className="break-words text-sm text-destructive">{error || setup.error}</p>}
  </section></main>;
}
