import { useRef, useState } from 'react';
import { Download, ExternalLink, Loader2, MoreHorizontal, Plus } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { Dialog, DialogContent, DialogTitle } from '@/components/ui/dialog';
import { DropdownMenu, DropdownMenuContent, DropdownMenuItem, DropdownMenuSeparator, DropdownMenuTrigger } from '@/components/ui/dropdown-menu';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { activeStatuses, bytes, call, Cover, IconButton, InstallProgress } from './common';

export function GameDetails({ game, state, local, onClose, run, pending, setPage, notify }) {
  const returnFocus = useRef(document.activeElement);
  const [extra, setExtra] = useState(null);
  const [build, setBuild] = useState('');
  const busy = state.busy || pending.has(`game-${game.id}`);
  const activeJob = state.jobs.find(j => j.gameId === game.id && activeStatuses.includes(j.status));
  const running = state.running === game.id;
  const operate = fn => run(`game-${game.id}`, fn);
  const download = () => operate(async () => { await call('download', game.id, build || undefined); onClose(); setPage('downloads'); });
  const install = () => operate(async () => { await call('install', game.id); notify('Installed'); });
  const patch = selected => operate(async () => {
    const result = await call('patch', game.id, selected);
    if (result?.patches) {
      setExtra({ kind: 'patches', items: result.patches.map(item => ({ ...item, selected: item.recommended, arguments: '' })) });
      return;
    }
    notify(result?.profileLabel ? `Patched with ${result.profileLabel} compatibility profile` : 'Patched');
    setExtra(null);
  });
  const editPatch = (name, values) => setExtra(current => ({ ...current, items: current.items.map(item => item.name === name ? { ...item, ...values } : item) }));
  const loadExtra = kind => operate(async () => {
    const result = await call(kind, game.id);
    if (kind === 'permissions') { setExtra({ kind, ...result }); return; }
    setExtra({ kind, items: result });
    if (kind === 'builds') setBuild(result[0]?.id || '');
  });
  const setPermission = async (name, granted) => {
    try { setExtra({ kind: 'permissions', ...await call('setPermission', game.id, name, granted) }); }
    catch (error) {
      try { setExtra({ kind: 'permissions', ...await call('permissions', game.id) }); } catch {}
      throw error;
    }
  };
  return <Dialog open onOpenChange={open => { if (!open) onClose(); }}>
    <DialogContent id="details" aria-describedby={undefined} onCloseAutoFocus={event => { event.preventDefault(); if (returnFocus.current?.isConnected) returnFocus.current.focus(); }} className="max-h-[85vh] gap-0 overflow-y-auto p-0 sm:max-w-[520px]">
      <div className="px-6 pt-6 pr-14"><DialogTitle className="leading-snug">{game.name}</DialogTitle>{game.version && <div className="mt-1 text-xs text-muted-foreground">{game.version}</div>}</div>
      {game.installed && <p className="mx-6 mt-3 text-xs text-muted-foreground">Play checks the version installed in Android.{game.apk && ' Patching checks the local APK separately.'}</p>}
      {game.apk && game.compatibility?.status !== 'none' && game.compatibility && <div data-compatibility={game.compatibility.status} className="mx-6 mt-4 rounded-md border p-3 text-xs">
        {game.compatibility.status === 'matched' && <div className="mb-1 font-medium">APK compatibility profile: {game.compatibility.label}</div>}
        <p className="text-muted-foreground">{game.compatibility.summary}</p>
      </div>}
      <div className="space-y-5 p-6">
        {extra?.kind !== 'patches' && <Cover game={game} className="aspect-video" />}
        <div className="flex items-center gap-2">
          {activeJob ? <Button variant="secondary" onClick={() => { onClose(); setPage('downloads'); }}><Loader2 className="animate-spin" />{{ downloading: 'Downloading', installing: 'Installing', importing: 'Importing', uninstalling: 'Uninstalling' }[activeJob.status] || 'Queued'}</Button>
            : running ? <Button disabled={busy} onClick={() => operate(async () => { await call('stop'); notify('Closing game'); })}>Stop</Button>
            : game.installed ? <Button className="min-w-24" disabled={busy || Boolean(state.running)} onClick={() => operate(async () => { const result = await call('play', game.id); if (result?.profileLabel) notify(`Using ${result.profileLabel} compatibility profile`); else if (result?.compatibilityNotice) notify(result.compatibilityNotice); onClose(); })}>Play</Button>
            : game.apk ? <Button disabled={busy} onClick={install}>Install</Button>
            : !local ? <Button disabled={busy} onClick={() => operate(() => call('add', game.id))}><Plus />Add to library</Button>
            : game.source !== 'meta' ? <Button disabled={busy} onClick={() => run('import', () => call('import'))}>Import APK</Button>
            : !state.signedIn ? <Button disabled={pending.has('account')} onClick={() => run('account', () => call('login'))}>Connect Meta</Button>
            : <Button disabled={busy} onClick={download}><Download />Download</Button>}
          <div className="flex-1" />
          {game.source === 'meta' && <IconButton label="View on Meta" onClick={() => operate(() => call('openStore', game.id))}><ExternalLink /></IconButton>}
          {local && <DropdownMenu><DropdownMenuTrigger asChild><Button variant="ghost" size="icon" aria-label="Game actions" title="Game actions"><MoreHorizontal /></Button></DropdownMenuTrigger>
            <DropdownMenuContent align="end">
              {game.source === 'meta' && <><DropdownMenuItem disabled={busy || !state.signedIn} onSelect={() => loadExtra('builds')}>Versions</DropdownMenuItem><DropdownMenuItem disabled={busy || !state.signedIn} onSelect={() => loadExtra('dlc')}>Add-ons</DropdownMenuItem></>}
              {game.apk && <>{game.source === 'meta' && <DropdownMenuSeparator />}<DropdownMenuItem disabled={busy} onSelect={() => patch()}>Patch with ovrport</DropdownMenuItem><DropdownMenuItem disabled={busy} onSelect={() => operate(async () => { await call('importAssets', game.id); notify('Install to apply content files'); })}>Add content files</DropdownMenuItem><DropdownMenuItem onSelect={() => operate(() => call('openFolder', game.id))}>Open folder</DropdownMenuItem>{game.installed && <DropdownMenuItem disabled={busy} onSelect={install}>Update installation</DropdownMenuItem>}</>}
              {!game.apk && game.source !== 'meta' && <DropdownMenuItem disabled={busy} onSelect={() => run('import', () => call('import'))}>Import APK</DropdownMenuItem>}
              {game.installed && <DropdownMenuItem disabled={busy} onSelect={() => loadExtra('permissions')}>Android permissions</DropdownMenuItem>}
              {game.installed && <><DropdownMenuSeparator /><DropdownMenuItem className="text-destructive" disabled={busy || Boolean(state.running) || Boolean(activeJob)} onSelect={() => operate(() => call('uninstall', game.id))}>Uninstall</DropdownMenuItem></>}
            </DropdownMenuContent>
          </DropdownMenu>}
        </div>
        {activeJob?.status === 'installing' ? <div role="status"><div className="break-words text-xs text-muted-foreground">{activeJob.stage}</div><InstallProgress job={activeJob} /></div>
          : pending.has(`game-${game.id}`) && <div role="status" className="flex items-center gap-2 text-xs text-muted-foreground"><Loader2 className="size-3 animate-spin" />Working…</div>}
        {extra?.kind === 'patches' && <section data-patch-options className="space-y-3 border-t pt-4" aria-label="Patch options">
          <div className="text-sm font-medium">Patch options</div>
          <p className="text-xs text-muted-foreground">No verified profile for this game version. Recommended CLI patches are selected; review optional patches before applying. Patching writes a separate APK. Install or update it afterward.</p>
          <Button size="sm" variant="outline" disabled={busy} onClick={() => setExtra(current => ({ ...current, items: current.items.map(item => ({ ...item, selected: item.recommended, arguments: '' })) }))}>Reset to recommended</Button>
          {extra.items.map(item => <div key={item.name} className="space-y-2">
            <label className="flex items-start gap-3 text-xs">
              <input type="checkbox" className="mt-0.5 size-4 shrink-0 accent-primary" checked={item.selected} disabled={busy} data-patch={item.name} onChange={e => editPatch(item.name, { selected: e.target.checked })} />
              <span><span className="block">{item.name.replace(/^patch_/, '').replaceAll('_', ' ')}{item.recommended ? ' (recommended)' : ''}</span><code className="text-muted-foreground">{item.name}</code></span>
            </label>
            {item.selected && <details className="ml-7 text-xs text-muted-foreground"><summary className="cursor-pointer">Arguments (advanced)</summary><Input className="mt-2" aria-label={`Arguments for ${item.name}`} placeholder="Comma-separated arguments" value={item.arguments} disabled={busy} onChange={e => editPatch(item.name, { arguments: e.target.value })} /></details>}
          </div>)}
          <div className="flex gap-2"><Button disabled={busy || !extra.items.some(item => item.selected)} onClick={() => patch(extra.items.filter(item => item.selected).map(item => ({ name: item.name, arguments: item.arguments.trim() ? item.arguments.split(',').map(value => value.trim()) : [] })))}>Apply selected patches</Button><Button variant="ghost" disabled={busy} onClick={() => setExtra(null)}>Cancel</Button></div>
        </section>}
        {extra?.kind === 'builds' && <div className="flex items-center gap-2 border-t pt-4">{extra.items.length ? <><Select value={build} onValueChange={setBuild}><SelectTrigger className="min-w-0 flex-1" aria-label="Quest build"><SelectValue /></SelectTrigger><SelectContent>{extra.items.map(b => <SelectItem key={b.id} value={b.id}>{b.version || b.code}</SelectItem>)}</SelectContent></Select><Button disabled={busy || Boolean(activeJob)} onClick={download}>Download</Button></> : <span className="text-muted-foreground">No builds available</span>}</div>}
        {extra?.kind === 'permissions' && <div className="border-t pt-4">
          {extra.prompt && <p role="alert" className="mb-3 rounded-md bg-secondary p-3 text-xs">This Android device has a pending permission dialog, which may belong to another app. Relaunch the requesting app after changing its permissions.</p>}
          {extra.items.length ? <>
            <div className="mb-3 flex items-center justify-between gap-3">
              <p className="text-xs text-muted-foreground">Manage this game's runtime permissions for Android's main user. Relaunch the game after making changes.</p>
              <Button size="sm" variant="outline" disabled={busy || extra.items.every(p => p.granted)}
                onClick={() => operate(async () => {
                  for (const item of extra.items.filter(p => !p.granted)) await setPermission(item.name, true);
                  notify('Permissions allowed');
                })}>Allow all</Button>
            </div>
            {extra.items.map(item => <div key={item.name} className="flex items-center justify-between gap-3 py-2">
              <div className="min-w-0"><div className="text-sm">{item.label}</div><div className="mt-0.5 truncate text-xs text-muted-foreground" title={item.name}>{item.name}</div></div>
              <Button size="sm" variant={item.granted ? 'ghost' : 'outline'} disabled={busy} onClick={() => operate(() => setPermission(item.name, !item.granted))}>{item.granted ? 'Allowed' : 'Allow'}</Button>
            </div>)}
          </> : <p className="text-muted-foreground">This game asks for no permissions.</p>}
        </div>}
        {extra?.kind === 'dlc' && <div className="border-t pt-4">{extra.items.length ? <><p className="mb-3 text-xs text-muted-foreground">Install after downloading to apply add-ons.</p>{extra.items.map(dlc => <div key={dlc.id} className="flex items-center justify-between gap-3 py-2"><div className="min-w-0"><div className="text-sm">{dlc.name}</div><div className="mt-1 text-xs text-muted-foreground">{!dlc.owned ? 'Ownership not confirmed' : !dlc.fileCount ? 'No separate download' : bytes(dlc.bytes)}</div></div><Button size="sm" variant="outline" disabled={!dlc.owned || !dlc.fileCount || busy || Boolean(activeJob)} onClick={() => operate(async () => { await call('downloadDlc', game.id, dlc.id); onClose(); setPage('downloads'); })}>Download</Button></div>)}</> : <p className="text-muted-foreground">No downloadable add-ons</p>}</div>}
      </div>
    </DialogContent>
  </Dialog>;
}
