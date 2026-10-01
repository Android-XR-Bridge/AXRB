import { useState } from 'react';
import { AssistantRuntimeProvider, useExternalStoreRuntime, ThreadPrimitive, MessagePrimitive,
  ComposerPrimitive, ActionBarPrimitive, useAui, useAuiState } from '@assistant-ui/react';
import { MarkdownTextPrimitive } from '@assistant-ui/react-markdown';
import remarkGfm from 'remark-gfm';
import { Button } from '@/components/ui/button';
import { call } from './common';
import { ArrowUp, Square, ChevronRight, Check, CircleAlert, Globe, Wrench, LoaderCircle } from 'lucide-react';

function Copy({ text, label = 'Copy' }) {
  const [copied, setCopied] = useState(false), [error, setError] = useState('');
  return <><Button size="sm" variant="ghost" type="button" aria-label={label} onClick={async () => {
    try { await call('copyText', text); setCopied(true); setError(''); } catch (e) { setError(e.message); }
  }}>{copied ? 'Copied' : label}</Button>{error && <span role="alert">{error}</span>}</>;
}
function CodeHeader({ language, code }) {
  return <div className="flex items-center justify-between border-b px-3 text-xs text-muted-foreground"><span>{language || 'text'}</span><Copy text={code} label="Copy code" /></div>;
}
const plugins = [remarkGfm];
const markdownComponents = {
  CodeHeader,
  // Reports may contain untrusted URLs and images. Display links without
  // automatic network requests or navigation; users can copy a URL to inspect.
  a: ({ children, href }) => <span className="underline decoration-muted-foreground" title={href}>{children}{href && <span className="text-xs text-muted-foreground"> ({href})</span>}</span>,
  img: ({ alt }) => <span className="text-muted-foreground">[Image: {alt || 'omitted'}]</span>,
};
function Markdown() { return <MarkdownTextPrimitive className="ai-markdown" remarkPlugins={plugins} components={markdownComponents} skipHtml smooth={false} />; }

export function DiagnosticTool({ toolName, args, result, isError, status }) {
  const state = isError ? 'Failed' : result !== undefined ? 'Complete' : status?.type === 'incomplete' ? 'Stopped' : 'Running';
  const Icon = isError ? CircleAlert : state==='Running' ? LoaderCircle : toolName==='web_search' ? Globe : Check;
  const label = toolName==='web_search' ? 'Web search' : toolName.replaceAll('_',' ').replace(/^./,c=>c.toUpperCase());
  const hint = args?.query || args?.queries?.join(', ') || args?.path || args?.check || '';
  return <details className="group/tool min-w-0 text-xs" data-ai-tool={toolName}>
    <summary className="flex cursor-pointer list-none items-center gap-2 rounded-lg px-2 py-2 text-muted-foreground transition-colors hover:bg-muted/60 hover:text-foreground [&::-webkit-details-marker]:hidden">
      <Icon className={`size-3.5 shrink-0 ${isError?'text-destructive':state==='Running'?'animate-spin':''}`} />
      <span className="shrink-0">{label}</span><span className="min-w-0 flex-1 truncate opacity-65" title={hint}>{hint}</span>
      <span className={isError?'text-destructive':'sr-only'}>{state}</span>
      <ChevronRight className="size-3 shrink-0 transition-transform group-open/tool:rotate-90" />
    </summary>
    <pre className="mx-2 mb-2 max-h-64 overflow-auto whitespace-pre-wrap break-all rounded-lg bg-background/70 p-3 text-[11px] leading-relaxed text-muted-foreground">{JSON.stringify({ arguments: args, ...(result !== undefined ? { result } : {}) }, null, 2)}</pre>
  </details>;
}
function ToolActivity({ calls, status }) {
  if (!calls.length) return null;
  if (calls.length===1) return <DiagnosticTool {...calls[0]} status={status} />;
  const web=calls.filter(c=>c.toolName==='web_search').length, other=calls.length-web;
  const label=[web && `${web} web ${web===1?'search':'searches'}`,other && `${other} tool ${other===1?'call':'calls'}`].filter(Boolean).join(' and ');
  const failures=calls.filter(c=>c.isError).length;
  const running=status?.type==='running' && calls.some(c=>c.result===undefined);
  const Icon=running?LoaderCircle:failures?CircleAlert:web===calls.length?Globe:Wrench;
  return <details className="group/activity mb-3 min-w-0" data-ai-tool-group>
    <summary className="flex w-fit cursor-pointer list-none items-center gap-2 rounded-lg px-2 py-2 text-xs text-muted-foreground hover:bg-muted/60 hover:text-foreground [&::-webkit-details-marker]:hidden">
      <Icon className={`size-3.5 ${running?'animate-spin':failures?'text-destructive':''}`} />
      <span>{label}</span>{failures>0 && <span className="text-destructive">{failures} failed</span>}
      <ChevronRight className="size-3 transition-transform group-open/activity:rotate-90" />
    </summary>
    <div className="ml-3 mt-1 max-h-80 space-y-0.5 overflow-auto border-l pl-3">
      {calls.map(c=><DiagnosticTool key={c.toolCallId} {...c} status={status} />)}
    </div>
  </details>;
}
const HiddenTool = () => null;
const parts = { Text: Markdown, tools: { Fallback: HiddenTool } };
function ChatMessage() {
  const message = useAuiState(s => s.message);
  const text = message.content.filter(p => p.type === 'text').map(p => p.text).join('\n');
  const user=message.role==='user';
  return <MessagePrimitive.Root className={`flex min-w-0 flex-col py-3 ${user?'items-end':'items-start'}`} data-ai-message={message.role} aria-label={user?'Your message':'Assistant message'}>
    <div className={`min-w-0 space-y-2 rounded-2xl px-4 py-3 ${user?'max-w-[85%] rounded-br-sm bg-secondary':'w-full max-w-[94%] rounded-bl-sm border border-border/40 bg-muted/15'}`} data-ai-bubble>
    {!user && <ToolActivity calls={message.content.filter(p=>p.type==='tool-call')} status={message.status} />}
    <MessagePrimitive.Parts components={parts} />
    {message.status?.type === 'running' && <p className="text-xs text-muted-foreground" role="status">Analyzing…</p>}
    {message.status?.type === 'incomplete' && <p className="text-sm text-destructive" role="alert">{message.status.reason === 'cancelled' ? 'Stopped. This response is incomplete.' : String(message.status.error || 'This response is incomplete.')}</p>}
    </div>
    {!user && text && <ActionBarPrimitive.Root className="mt-1 opacity-60 transition-opacity hover:opacity-100"><Copy text={text} label="Copy response" /></ActionBarPrimitive.Root>}
  </MessagePrimitive.Root>;
}
function ChatComposer({ disabled, busy, running, onDraft, hasMessages, controls, blockedReason }) {
  const aui = useAui();
  const text = useAuiState(s => s.composer.text);
  return <ComposerPrimitive.Root className="rounded-3xl border bg-muted/20 p-3 shadow-sm" data-ai-composer>
    <ComposerPrimitive.Input aria-label="Problem or follow-up question" className="w-full resize-none bg-transparent px-3 py-2 text-base outline-none" minRows={2} maxRows={8} maxLength={5500}
      placeholder="Ask anything about AXRB, or describe what isn't working…" />
    <div className="mt-2 flex flex-wrap items-center gap-1">
      {controls}
      {hasMessages && <Button size="sm" type="button" variant="ghost" disabled={disabled || busy} onClick={async () => { if (await onDraft(text)) aui.composer.setText(''); }}>Draft report</Button>}
      <span className="flex-1" />
      {running ? <ComposerPrimitive.Cancel asChild><Button variant="outline" type="button" aria-label="Stop" className="h-9 w-9 rounded-full p-0"><Square className="size-3" /></Button></ComposerPrimitive.Cancel>
        : <ComposerPrimitive.Send asChild><Button type="button" aria-label="Send message" className="h-9 w-9 rounded-full p-0"><ArrowUp className="size-4" /></Button></ComposerPrimitive.Send>}
    </div>
    {text.trim() && blockedReason && <p role="status" className="px-3 pt-2 text-xs text-muted-foreground" data-ai-send-blocked>{blockedReason}</p>}
  </ComposerPrimitive.Root>;
}
const empty = [];
const convertMessage = message => message;
export function AiChat({ messages = empty, running, disabled, busy, onSend, onCancel, onDraft, controls, consentControl, blockedReason }) {
  const runtime = useExternalStoreRuntime({ messages, convertMessage, isRunning: running, isSendDisabled: disabled || busy,
    onNew: async message => { await onSend(message.content.filter(p => p.type === 'text').map(p => p.text).join('\n')); }, onCancel });
  return <AssistantRuntimeProvider runtime={runtime}>
    <ThreadPrimitive.Root className={`flex min-w-0 flex-1 flex-col gap-3 ${messages.length?'':'justify-center pb-[12vh]'}`} aria-label="Analysis conversation" data-ai-thread>
      <ThreadPrimitive.Viewport className={messages.length?'max-h-[calc(100vh-22rem)] min-h-20 flex-1 overflow-y-auto':'hidden'} autoScroll>
        <ThreadPrimitive.Messages components={{ UserMessage: ChatMessage, AssistantMessage: ChatMessage }} />
        <ThreadPrimitive.ScrollToBottom asChild><Button variant="outline" size="sm" type="button" className="mb-3">Latest message</Button></ThreadPrimitive.ScrollToBottom>
      </ThreadPrimitive.Viewport>
      <ChatComposer disabled={disabled} busy={busy || running} running={running} onDraft={onDraft} hasMessages={messages.length > 0} controls={controls} blockedReason={blockedReason} />
      {consentControl}
    </ThreadPrimitive.Root>
  </AssistantRuntimeProvider>;
}
