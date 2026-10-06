import { useState } from 'react';
import { Button } from '@/components/ui/button';
import { Dialog, DialogContent, DialogHeader, DialogTitle, DialogDescription } from '@/components/ui/dialog';
import { call } from './common';

export function GameExport({ games, busy, onClose, run, pending, setPage }) {
  const available = games.filter(game => game.apk);
  const [selected, setSelected] = useState(new Set());
  const ids = available.filter(game => selected.has(game.id)).map(game => game.id);
  return <Dialog open onOpenChange={open => { if (!open) onClose(); }}>
    <DialogContent>
      <DialogHeader><DialogTitle>Export games</DialogTitle><DialogDescription>Choose games to save in one ZIP. Use Install ZIP to import it again. Includes local APKs and assets, without pulling saves from Android.</DialogDescription></DialogHeader>
      {available.length ? <>
        <div className="flex items-center justify-between"><span className="text-sm text-muted-foreground">{ids.length} selected</span><Button variant="ghost" size="sm" onClick={() => setSelected(ids.length === available.length ? new Set() : new Set(available.map(game => game.id)))}>{ids.length === available.length ? 'Clear selection' : 'Select all'}</Button></div>
        <div className="max-h-80 overflow-y-auto">{available.map(game => <label key={game.id} className="flex cursor-pointer items-center gap-3 rounded-md px-2 py-3 hover:bg-secondary">
          <input type="checkbox" checked={selected.has(game.id)} onChange={event => setSelected(previous => { const next = new Set(previous); if (event.target.checked) next.add(game.id); else next.delete(game.id); return next; })} />
          <span className="min-w-0 truncate text-sm">{game.name}</span>
        </label>)}</div>
      </> : <p className="text-sm text-muted-foreground">Download or import a game APK first.</p>}
      <div className="flex justify-end gap-2"><Button variant="outline" onClick={onClose}>Cancel</Button><Button disabled={!ids.length || ids.length > 1000 || busy || pending.has('export-games')} onClick={() => run('export-games', async () => {
        if (await call('exportGames', ids)) { onClose(); setPage('downloads'); }
      })}>Export {ids.length || ''} {ids.length === 1 ? 'game' : 'games'}</Button></div>
    </DialogContent>
  </Dialog>;
}
