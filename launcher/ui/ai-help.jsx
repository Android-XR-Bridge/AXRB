import { useEffect, useRef, useState } from 'react';
import { Button } from '@/components/ui/button';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { UserRound, Globe } from 'lucide-react';
import { call } from './common';
import { AiChat } from './ai-chat';

export function AiHelp({ active = true }) {
  const [status,setStatus]=useState(null), [error,setError]=useState(''), [busy,setBusy]=useState('');
  const [model,setModel]=useState(''), [consent,setConsent]=useState(false);
  const [report,setReport]=useState(''), [notice,setNotice]=useState('');
  const [webSearch,setWebSearch]=useState(true);
  const loadedAccount=useRef(null);
  useEffect(()=>{
    if(!active) return;
    let mounted=true,timer;
    const unsubscribe=window.axrb.onAiUpdate(next=>{if(mounted)setStatus(previous=>({...previous,...next}));});
    const poll=async()=>{
      try {const next=await call('aiStatus');if(mounted)setStatus(next);}
      catch(e){if(mounted)setError(e.message);}
      finally{if(mounted)timer=setTimeout(poll,1000);}
    };
    poll();return()=>{mounted=false;clearTimeout(timer);unsubscribe();};
  },[active]);
  useEffect(()=>{
    const auth=status?.auth;
    if(!active || !auth?.signedIn || auth.signingIn || loadedAccount.current===auth.active) return;
    let cancelled=false,settled=false;loadedAccount.current=auth.active;
    call('aiModels').then(models=>{
      if(cancelled)return;
      settled=true;
      setModel(previous=>models.some(m=>m.id===previous)?previous:models[0]?.id || '');
      setStatus(previous=>({...previous,models}));
    }).catch(e=>{if(!cancelled)setError(e.message);});
    return()=>{cancelled=true;if(!settled && loadedAccount.current===auth.active)loadedAccount.current=null;};
  },[active,status?.auth?.active,status?.auth?.signedIn,status?.auth?.signingIn]);
  const task=async(name,fn)=>{
    if(busy)return false;setBusy(name);setError('');setNotice('');
    try {await fn();setStatus(await call('aiStatus'));return true;}
    catch(e){setError(e.message);return false;}finally{setBusy('');}
  };
  const working=Boolean(busy || status?.working || status?.auth?.signingIn);
  const blockedReason = status?.auth?.signingIn ? 'Finish signing in to send a message.'
    : !status?.auth?.signedIn ? 'Sign in with ChatGPT to send a message.'
    : !model ? 'Choose a model to send a message.'
    : !consent ? 'Check the access and sharing permission below to enable Send.' : '';
  const account=(action,id)=>task('account',async()=>{
    await call('aiAccount',{action,id});loadedAccount.current=null;setModel('');setConsent(false);setReport('');
  });
  const analyze=(draft,question)=>task('analysis',async()=>{
    const answer=await call('aiAnalyze',{consent,model,webSearch,question:draft?`Prepare a bug report from this investigation. User observations: ${question || '(see conversation)'}`:question});
    if(draft)setReport(answer);
  });
  const controls=<>
    <Select value={model} disabled={working || !status?.auth?.signedIn} onValueChange={value=>{if(value)setModel(value);}}>
      <SelectTrigger aria-label="AI model" size="sm" className="max-w-64 rounded-full border-transparent px-3 font-medium shadow-none hover:bg-accent data-[state=open]:bg-accent dark:bg-transparent dark:hover:bg-accent">
        <SelectValue placeholder={status?.auth?.signedIn?'Choose model':'Sign in to choose a model'}>{(status?.models || []).find(m=>m.id===model)?.name}</SelectValue>
      </SelectTrigger>
      <SelectContent position="popper" side="top" align="start" sideOffset={8} className="min-w-60 max-w-[min(24rem,calc(100vw-2rem))] rounded-xl border-border/60 p-1 shadow-xl">
        {(status?.models || []).map(m=><SelectItem key={m.id} value={m.id} className="cursor-pointer rounded-lg py-2.5 pr-9 pl-3">{m.name}</SelectItem>)}
      </SelectContent>
    </Select>
    <Button type="button" size="sm" variant="ghost" className={webSearch?'rounded-full bg-accent':'rounded-full text-muted-foreground'} aria-label="Web search" aria-pressed={webSearch} title={webSearch?'Web search enabled':'Web search disabled'} disabled={working} onClick={()=>setWebSearch(value=>!value)}><Globe className="size-3.5" />Web</Button>
    {status?.auth?.signedIn && !model && <Button type="button" size="sm" variant="ghost" disabled={working} onClick={()=>task('models',async()=>{const models=await call('aiModels');setModel(models[0]?.id || '');})}>Retry models</Button>}
  </>;
  return <section className="mx-auto flex min-h-[calc(100vh-9rem)] max-w-4xl flex-col gap-4" aria-label="AI diagnostics">
    <div className="flex flex-wrap items-center justify-end gap-2">
      {status?.auth?.accounts.length>0 && <Select value={status.auth.active || ''} disabled={working} onValueChange={id=>account('select',id)}>
        <SelectTrigger aria-label="ChatGPT account" size="sm" className="max-w-72 gap-2.5 rounded-full border-border/60 bg-muted/30 px-3 text-xs shadow-none hover:bg-accent data-[state=open]:bg-accent dark:bg-muted/30 dark:hover:bg-accent">
          <UserRound className="size-3.5" /><SelectValue placeholder="Choose account">{status.auth.accounts.find(a=>a.id===status.auth.active)?.label}</SelectValue>
        </SelectTrigger>
        <SelectContent position="popper" align="end" sideOffset={8} className="min-w-64 max-w-[min(24rem,calc(100vw-2rem))] rounded-xl border-border/60 p-1 shadow-xl">
          {status.auth.accounts.map(a=><SelectItem key={a.id} value={a.id} textValue={a.label} className="cursor-pointer rounded-lg py-2.5 pr-9 pl-3">
            <span className="min-w-0 truncate">{a.label}</span>{!a.signedIn && <span className="shrink-0 text-xs text-muted-foreground">Signed out</span>}
          </SelectItem>)}
        </SelectContent>
      </Select>}
      {!status?.auth?.signedIn && <Button disabled={working} onClick={()=>account('login',status?.auth?.active)}>Continue with ChatGPT</Button>}
      {status?.auth?.signedIn && <Button size="sm" variant="ghost" disabled={working} onClick={()=>account('logout')}>Sign out</Button>}
      {status?.auth?.accounts.length>0 && <Button size="sm" variant="ghost" disabled={working} onClick={()=>account('login')}>Add account</Button>}
      {status?.messages?.length>0 && <Button size="sm" variant="ghost" disabled={working} onClick={()=>task('reset',async()=>{await call('aiReset');setReport('');})}>New chat</Button>}
    </div>
    {(error || status?.error || status?.auth?.error) && <p role="alert" className="text-sm text-destructive">{error || status?.error || status?.auth?.error}</p>}
    {notice && <p role="status" className="text-sm text-muted-foreground">{notice}</p>}
    {status?.auth?.signingIn && <div className="flex items-center gap-2 text-sm">Finish signing in in your browser.<Button variant="ghost" onClick={()=>call('aiCancel').catch(e=>setError(e.message))}>Cancel sign-in</Button></div>}
    <AiChat messages={status?.messages} running={Boolean(status?.working)} busy={Boolean(busy)} controls={controls}
      blockedReason={blockedReason}
      disabled={!consent || !model || !status?.auth?.signedIn || Boolean(status?.auth?.signingIn)}
      onSend={question=>analyze(false,question)} onDraft={question=>analyze(true,question)}
      onCancel={async()=>{try{await call('aiCancel');}catch(e){setError(e.message);}}}
      onReset={()=>task('reset',async()=>{await call('aiReset');setReport('');})}
      consentControl={<label className="flex cursor-pointer items-start gap-3 rounded-xl border border-border/60 bg-muted/20 px-4 py-3 text-sm leading-6 text-foreground"><input type="checkbox" className="mt-1 size-4 shrink-0 accent-neutral-200" checked={consent} disabled={working} onChange={e=>setConsent(e.target.checked)} />Allow diagnostic file and emulator access, and share redacted results with OpenAI. General ADB commands require approval.</label>} />
    {report && <div className="space-y-3 border-t pt-4">
      <textarea aria-label="Bug report draft" value={report} onChange={e=>setReport(e.target.value)} rows={12} maxLength={180000} className="w-full rounded-lg border bg-background p-3 font-mono text-xs" />
      <div className="flex gap-2"><Button variant="outline" disabled={working} onClick={()=>task('save',async()=>{const result=await call('aiReport',{action:'save',text:report});if(result.path)setNotice('Report saved.');})}>Save report</Button>
        <Button disabled={working} onClick={()=>task('issue',async()=>{await call('aiReport',{action:'issue',text:report});setNotice('Report copied. Review it before submitting on GitHub.');})}>Copy & open GitHub</Button></div>
    </div>}
  </section>;
}
