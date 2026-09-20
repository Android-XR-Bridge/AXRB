import { useEffect, useMemo, useRef, useState } from 'react';
import { ArrowDown, ChevronDown, ChevronUp, Copy, Download, Pause, Play, Terminal } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { call } from './common';

const ROW_HEIGHT = 22;
const SOURCES = ['android', 'emulator', 'host', 'launcher'];
const LEVELS = ['V', 'D', 'I', 'W', 'E', 'F'];
const empty = { entries: [], lastId: 0, firstId: 1, dropped: 0, android: { state: 'waiting', detail: 'Waiting for diagnostics.', serial: '', lastReceivedAt: null } };
const lineText = entry => `${entry.receivedAt} [${entry.source}/${entry.level}]${entry.guestTime ? ` [guest ${entry.guestTime}]` : ''}${entry.pid ? ` pid=${entry.pid}` : ''}${entry.tag ? ` ${entry.tag}:` : ''} ${entry.text}`;
const clampHeight = value => Math.max(200, Math.min(value, window.innerHeight - 180));

export function DebugPanel() {
  const [open, setOpen] = useState(false);
  const [height, setHeight] = useState(() => clampHeight(320));
  const [capture, setCapture] = useState(empty);
  const latest = useRef(empty);
  const [frozen, setFrozen] = useState(null);
  const [query, setQuery] = useState('');
  const [source, setSource] = useState('all');
  const [level, setLevel] = useState('all');
  const [following, setFollowing] = useState(true);
  const [scrollTop, setScrollTop] = useState(0);
  const [viewportHeight, setViewportHeight] = useState(160);
  const [notice, setNotice] = useState('');
  const [saving, setSaving] = useState(false);
  const viewport = useRef(null);
  const drag = useRef(null);

  useEffect(() => {
    document.documentElement.style.setProperty('--diagnostics-height', `${open ? height : 40}px`);
    return () => document.documentElement.style.removeProperty('--diagnostics-height');
  }, [open, height]);
  useEffect(() => {
    const resize = () => setHeight(value => clampHeight(value));
    window.addEventListener('resize', resize);
    return () => window.removeEventListener('resize', resize);
  }, []);
  useEffect(() => {
    if (!open) return;
    let active = true, reading = false, again = false;
    const read = async () => {
      if (reading) { again = true; return; }
      reading = true;
      do {
        again = false;
        try {
          const next = await call('diagnosticsRead', latest.current.lastId);
          if (!active) return;
          const entries = [...latest.current.entries.filter(entry => entry.id >= next.firstId), ...next.entries].slice(-5000);
          latest.current = { ...next, entries };
          setCapture(latest.current);
        } catch (error) { if (active) setNotice(error.message); }
      } while (again && active);
      reading = false;
    };
    const off = window.axrb.onDiagnostics(read);
    read();
    return () => { active = false; off(); };
  }, [open]);
  useEffect(() => {
    if (!open || !viewport.current) return;
    const observer = new ResizeObserver(([entry]) => setViewportHeight(entry.contentRect.height));
    observer.observe(viewport.current);
    return () => observer.disconnect();
  }, [open]);

  const shown = frozen || capture;
  const entries = useMemo(() => {
    const text = query.trim().toLowerCase(), minimum = LEVELS.indexOf(level);
    return shown.entries.filter(entry => (source === 'all' || entry.source === source)
      && (level === 'all' || LEVELS.indexOf(entry.level) >= minimum)
      && (!text || `${entry.text} ${entry.tag} ${entry.pid || ''} ${entry.guestTime || ''}`.toLowerCase().includes(text)));
  }, [shown.entries, query, source, level]);
  useEffect(() => {
    if (open && following && !frozen && viewport.current) viewport.current.scrollTop = viewport.current.scrollHeight;
  }, [open, following, frozen, entries, viewportHeight]);
  const first = Math.max(0, Math.min(Math.floor(scrollTop / ROW_HEIGHT) - 12, Math.max(0, entries.length - 1)));
  const visible = entries.slice(first, first + Math.ceil(viewportHeight / ROW_HEIGHT) + 25);
  const status = capture.android;
  const resize = event => {
    if (!drag.current) return;
    setHeight(clampHeight(drag.current.height + drag.current.y - event.clientY));
  };
  const finishResize = event => {
    drag.current = null;
    if (event.currentTarget.hasPointerCapture(event.pointerId)) event.currentTarget.releasePointerCapture(event.pointerId);
  };
  const copy = async () => {
    try { await call('copyText', entries.map(lineText).join('\n')); setNotice(`Copied ${entries.length} filtered log lines.`); }
    catch (error) { setNotice(`Could not copy logs: ${error.message}`); }
  };
  const save = async () => {
    setSaving(true); setNotice('');
    try { const result = await call('diagnostics', { save: true }); if (result.path) setNotice('Saved the full diagnostics report. Nothing was uploaded.'); }
    catch (error) { setNotice(error.message); }
    finally { setSaving(false); }
  };
  return <section aria-label="Debug console" className="fixed inset-x-0 bottom-0 z-40 flex flex-col border-t bg-background shadow-[0_-8px_24px_#0004]" style={{ height: open ? height : 40 }}>
    {open && <div role="separator" aria-label="Resize debug panel" aria-orientation="horizontal" aria-valuemin={200} aria-valuemax={Math.max(200, window.innerHeight - 180)} aria-valuenow={height} tabIndex={0}
      className="h-1.5 shrink-0 cursor-row-resize touch-none bg-secondary hover:bg-muted-foreground"
      onPointerDown={event => { drag.current = { y: event.clientY, height }; event.currentTarget.setPointerCapture(event.pointerId); }}
      onPointerMove={resize} onPointerUp={finishResize} onPointerCancel={finishResize}
      onKeyDown={event => { if (event.key === 'ArrowUp' || event.key === 'ArrowDown') { event.preventDefault(); setHeight(value => clampHeight(value + (event.key === 'ArrowUp' ? 20 : -20))); } }} />}
    <div className="flex h-10 shrink-0 items-center gap-3 px-4">
      <button type="button" aria-expanded={open} aria-controls="debug-content" onClick={() => setOpen(value => !value)} className="flex items-center gap-2 text-sm font-medium"><Terminal className="size-4" />Debug {open ? <ChevronDown className="size-4" /> : <ChevronUp className="size-4" />}</button>
      {open ? <><span className={`size-1.5 shrink-0 rounded-full ${status.state === 'streaming' ? 'bg-emerald-400' : status.state === 'error' ? 'bg-red-400' : 'bg-amber-400'}`} />
        <span className="min-w-0 truncate text-xs text-muted-foreground" title={status.detail} role="status">{status.serial} · {status.state} — {status.detail}</span>
        <span className="ml-auto shrink-0 text-xs text-muted-foreground">{frozen ? `Display paused · ${Math.max(0, capture.lastId - frozen.lastId)} new entries` : 'Capture continues when closed'}</span></>
        : null}
    </div>
    {open && <div id="debug-content" className="flex min-h-0 flex-1 flex-col">
      <div className="flex shrink-0 items-center gap-2 border-y px-4 py-2">
        <Input type="search" aria-label="Search debug logs" placeholder="Search messages, tags or PID" value={query} onChange={event => setQuery(event.target.value)} className="h-8 min-w-32 flex-1" />
        <Select value={source} onValueChange={setSource}><SelectTrigger aria-label="Log source" className="h-8 w-32"><SelectValue /></SelectTrigger><SelectContent><SelectItem value="all">All sources</SelectItem>{SOURCES.map(value => <SelectItem key={value} value={value}>{value[0].toUpperCase() + value.slice(1)}</SelectItem>)}</SelectContent></Select>
        <Select value={level} onValueChange={setLevel}><SelectTrigger aria-label="Log severity" className="h-8 w-28"><SelectValue /></SelectTrigger><SelectContent>{[['all', 'All levels'], ['D', 'Debug+'], ['I', 'Info+'], ['W', 'Warning+'], ['E', 'Error+'], ['F', 'Fatal']].map(([value, label]) => <SelectItem key={value} value={value}>{label}</SelectItem>)}</SelectContent></Select>
        <Button size="sm" variant="outline" onClick={() => setFrozen(value => value ? null : latest.current)} aria-pressed={Boolean(frozen)}>{frozen ? <Play /> : <Pause />}{frozen ? 'Resume' : 'Pause'}</Button>
        <Button size="sm" variant="ghost" aria-label="Follow latest logs" aria-pressed={following} disabled={Boolean(frozen)} onClick={() => { setFollowing(true); if (viewport.current) viewport.current.scrollTop = viewport.current.scrollHeight; }}><ArrowDown /></Button>
        <Button size="sm" variant="outline" disabled={!entries.length} onClick={copy}><Copy />Copy visible</Button>
        <Button size="sm" variant="outline" disabled={saving} onClick={save}><Download />{saving ? 'Saving…' : 'Save report'}</Button>
      </div>
      <div ref={viewport} role="log" aria-label="Live diagnostic entries" aria-live="off" tabIndex={0} className="min-h-0 flex-1 overflow-auto font-mono text-xs"
        onScroll={event => { const element = event.currentTarget; setScrollTop(element.scrollTop); setFollowing(element.scrollHeight - element.scrollTop - element.clientHeight < ROW_HEIGHT * 2); }}>
        {!entries.length ? <p className="p-4 font-sans text-muted-foreground">{shown.entries.length ? 'No logs match these filters.' : status.detail} Host and emulator capture does not require Android to be online.</p>
          : <div style={{ height: entries.length * ROW_HEIGHT, minWidth: '100%', width: 'max-content', position: 'relative' }}>
            <div style={{ paddingTop: first * ROW_HEIGHT }}>
              {visible.map(entry => <div key={entry.id} data-log-source={entry.source} className={`grid items-center gap-3 px-4 ${entry.level === 'E' || entry.level === 'F' ? 'text-red-300' : entry.level === 'W' ? 'text-amber-200' : 'text-foreground'}`} style={{ height: ROW_HEIGHT, gridTemplateColumns: '100px 70px 16px 160px minmax(300px, 1fr)' }}>
                <time className="text-muted-foreground" dateTime={entry.receivedAt} title={`Host received: ${entry.receivedAt}`}>{entry.receivedAt.slice(11, 23)}</time>
                <span className="text-muted-foreground">{entry.source}</span><span>{entry.level}</span>
                <span className="truncate text-muted-foreground" title={`${entry.tag}${entry.pid ? ` · PID ${entry.pid}` : ''}`}>{entry.tag}{entry.pid ? ` · ${entry.pid}` : ''}</span>
                <span className="whitespace-pre">{entry.guestTime && <span className="text-muted-foreground">[guest {entry.guestTime}] </span>}{entry.text}</span>
              </div>)}
            </div>
          </div>}
      </div>
      <div className="flex shrink-0 items-center gap-3 border-t px-4 py-1 text-[11px] text-muted-foreground">
        <span>{entries.length} matching / {shown.entries.length} retained · {capture.dropped} older omitted</span>
        <span className="min-w-0 flex-1 truncate" role="status" title={notice}>{notice}</span>
        <span className="shrink-0">Save includes all sources</span>
      </div>
    </div>}
  </section>;
}
