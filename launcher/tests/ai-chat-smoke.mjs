import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import { clipboard } from 'electron';

// Real IPC, backend streaming parser and assistant-ui. Only the remote service
// and account status are fixtures; no account tokens or real inference needed.
export async function aiChatSmoke(window, ai, auth, directory) {
  const js = async code => {
    try { return await window.webContents.executeJavaScript(code); }
    catch (error) { throw new Error(`AI UI fixture failed: ${code.slice(0, 250)}: ${error.message}`); }
  };
  const wait = ms => new Promise(r => setTimeout(r, ms));
  async function check(code, label) {
    for (let i = 0; i < 180; i++) { if (await js(code)) return; await wait(40); }
    throw new Error(`AI chat: ${label}`);
  }
  const button = label => js(`Array.from(document.querySelectorAll('[aria-label="AI diagnostics"] button')).find(b => b.textContent === ${JSON.stringify(label)} || b.getAttribute('aria-label') === ${JSON.stringify(label)}).click()`);
  async function compose(value) {
    await js(`{const e=document.querySelector('[aria-label="Problem or follow-up question"]'); Object.getOwnPropertyDescriptor(HTMLTextAreaElement.prototype,'value').set.call(e,${JSON.stringify(value)});e.dispatchEvent(new Event('input',{bubbles:true}));}`);
  }
  const originals = { fetch: ai.fetch, access: auth.access, status: auth.status, current: auth.current };
  const consoleError = details => { if (details.level === 'error') console.log(`AI renderer: ${details.message}`); };
  window.webContents.on('console-message', consoleError);
  let writer, mode = 'success';
  const emit = item => writer.enqueue(new TextEncoder().encode(`data: ${JSON.stringify(item)}\n\n`));
  try {
    ai.reset();
    const launcherState=await js('window.axrb.state().then(result=>result.value)');
    window.webContents.send('axrb:changed', {...launcherState,setup:{...launcherState.setup,phase:'ready',checks:launcherState.setup?.checks || []}});
    auth.status = async () => ({ signedIn: true, accounts: [], active: 'fixture', signingIn: false, error: '' });
    auth.access = async () => 'fixture-not-a-token';
    auth.current = () => ({ client_id: 'fixture' });
    ai.fetch = async (url, options) => {
      if (url.endsWith('/models')) return Response.json({ models: [{ slug: 'fixture-model', display_name: 'Test model', visibility: 'list' }] });
      if (mode === 'failure') return new Response('', { status: 503 });
      return new Response(new ReadableStream({ start(controller) {
        writer = controller;
        options.signal.addEventListener('abort', () => { try { controller.error(new DOMException('Aborted', 'AbortError')); } catch {} }, { once: true });
      } }));
    };
    await check(`!!document.querySelector('[data-nav="ai"]')`, 'navigation missing');
    await js(`document.querySelector('[data-nav="ai"]').click()`);
    await check(`!!document.querySelector('[data-ai-thread]')`, 'thread missing');
    await check(`document.querySelector('button[aria-label="AI model"]')?.textContent.includes('Test model')`, 'models did not load automatically');
    assert.equal(await js(`!!document.querySelector('[data-ai-composer] [aria-label="AI model"]')`),true);
    assert.equal(await js(`!!document.querySelector('[aria-label="Game to diagnose"], [aria-label="Diagnostics to share"]')`),false);
    await compose('Explain the runtime error.');
    await check(`document.querySelector('[aria-label="Send message"]')?.disabled && document.querySelector('[data-ai-send-blocked]')?.textContent.includes('permission')`, 'missing consent did not explain blocked Send');
    await js(`document.querySelector('[aria-label="AI diagnostics"] input[type="checkbox"]').click()`);
    await check(`!document.querySelector('[aria-label="Send message"]')?.disabled`, 'composer did not enable');
    await button('Web search');
    assert.equal(await js(`document.querySelectorAll('[data-ai-message="user"]').length`),0,'Web toggle submitted the draft');
    await button('Send message');
    await check(`document.querySelector('[data-ai-message="user"]')?.textContent.includes('Explain the runtime error.')`, 'user message missing');
    for (let i = 0; !writer && i < 100; i++) await wait(20);
    assert.ok(writer);
    const firstWriter = writer;
    emit({ type: 'response.completed', response: { status: 'completed', output: [{ type: 'function_call', namespace: 'axrb', name: 'runtime_status', arguments: '{}', call_id: 'smoke-runtime' }] } });
    firstWriter.close();
    for (let i = 0; writer === firstWriter && i < 150; i++) await wait(20);
    assert.notEqual(writer, firstWriter, 'agent did not request a follow-up after the real runtime status tool');
    await check(`document.querySelector('[data-ai-tool="runtime_status"]')?.textContent.includes('Complete')`, 'real tool execution missing');
    emit({ type: 'response.output_text.delta', delta: '## Evidence\n\n**Device lost**\n\n| Check | Result |\n| --- | --- |\n| GPU | Error |\n\n```log\nVK_ERROR_DEVICE_LOST' });
    await check(`document.querySelector('[data-ai-message="assistant"] strong')?.textContent==='Device lost'`, 'streamed Markdown not rendered');
    await check(`!!document.querySelector('[data-ai-message="assistant"] table')`, 'GFM table missing');
    emit({ type: 'response.output_text.delta', delta: '\n```\n\n<img src="https://example.invalid/track" onerror="alert(1)">\n\n![tracking](https://example.invalid/track)\n\n[bad](javascript:alert(1))' });
    emit({ type: 'response.completed', response: { status: 'completed' } }); writer.close();
    await check(`!Array.from(document.querySelectorAll('[data-ai-message] [role="status"]')).some(e=>e.textContent.includes('Analyzing'))`, 'stream never completed');
    await check(`document.querySelectorAll('[data-ai-message="assistant"]').length===1`, 'stream duplicated message');
    assert.equal(await js(`document.querySelectorAll('[data-ai-thread] img, [data-ai-thread] script, [data-ai-thread] a[href^="javascript:"]').length`), 0);
    await button('Copy code');
    assert.match(await clipboard.readText(), /VK_ERROR_DEVICE_LOST/);
    await fs.writeFile(path.join(directory, 'ai-chat-markdown.png'), (await window.webContents.capturePage()).toPNG());
    // The generic renderer also handles future backend tool parts, without
    // pretending this fixture executed an actual diagnostic command.
    ai.messages = [...ai.messages, { id: 'tool-fixture', role: 'assistant', status: { type: 'complete', reason: 'stop' }, content: [{ type: 'tool-call', toolCallId: 'fixture-call', toolName: 'read_log', args: { source: 'emulator' }, argsText: '{"source":"emulator"}', result: { lines: ['Fixture only'] } }] }]; ai.onUpdate();
    await check(`document.querySelector('[data-ai-tool="read_log"]')?.textContent.includes('Complete')`, 'tool result renderer missing');
    await js(`document.querySelector('[data-ai-tool="read_log"] summary').click()`);
    await check(`document.querySelector('[data-ai-tool="read_log"]').open`, 'tool result cannot expand');
    const groupedCalls=[...Array.from({length:3},(_,i)=>({type:'tool-call',toolCallId:`web-${i}`,toolName:'web_search',args:{query:'Android emulator WHPX'},result:{status:'completed'}})),...Array.from({length:3},(_,i)=>({type:'tool-call',toolCallId:`file-${i}`,toolName:'read_file',args:{path:'logs/emulator.log'},result:{text:'Diagnostic fixture'}}))];
    ai.messages=[{id:'bubble-fixture',role:'user',content:[{type:'text',text:'Can you check why the emulator is taking so long to start?'}]},{id:'group-fixture',role:'assistant',status:{type:'complete',reason:'stop'},content:[...groupedCalls,{type:'text',text:'The emulator is reachable. The logs show that hardware acceleration is active.'}]}];ai.onUpdate();
    await check(`document.querySelector('[data-ai-tool-group] > summary')?.textContent.includes('3 web searches and 3 tool calls')`, 'tool counts missing');
    assert.equal(await js(`document.querySelector('[data-ai-tool-group]').open`),false);
    await js(`document.querySelector('[data-ai-tool-group] > summary').click()`);
    await check(`document.querySelector('[data-ai-tool-group]').open && document.querySelectorAll('[data-ai-tool-group] [data-ai-tool]').length===6`, 'group drawer did not expand');
    await wait(200);
    await fs.writeFile(path.join(directory,'ai-chat-tools-expanded.png'),(await window.webContents.capturePage()).toPNG());
    await js(`document.querySelector('[data-ai-tool-group] > summary').click()`);
    await wait(200);
    await fs.writeFile(path.join(directory,'ai-chat-bubbles.png'),(await window.webContents.capturePage()).toPNG());
    writer = null; await compose('Another check'); await button('Send message');
    await check(`!!document.querySelector('[aria-label="Stop"]')&&!document.querySelector('[aria-label="Stop"]').disabled`, 'stop not enabled');
    await button('Stop');
    await check(`document.querySelector('[data-ai-thread]')?.textContent.includes('Stopped. This response is incomplete.')`, 'cancelled state missing');
    mode = 'failure'; await compose('Retry the check'); await button('Send message');
    await check(`document.querySelector('[data-ai-thread]')?.textContent.includes('503')`, 'error state missing');
    await button('New chat');
    await check(`document.querySelectorAll('[data-ai-message]').length===0`, 'new conversation did not clear messages');
    console.log('AI chat smoke passed: assistant-ui composer, IPC streaming, Markdown/table/code, native copy, unsafe content, tool results, cancellation, failure, reset.');
  } catch (error) {
    console.log('AI fixture state:', JSON.stringify(ai.status()));
    await fs.writeFile(path.join(directory, 'ai-chat-failure.png'), (await window.webContents.capturePage()).toPNG());
    throw error;
  } finally {
    window.webContents.removeListener('console-message', consoleError);
    ai.cancel(); ai.fetch = originals.fetch; auth.access = originals.access; auth.status = originals.status; auth.current = originals.current;
    for (let i = 0; ai.controller && i < 100; i++) await wait(20);
    ai.reset(); ai.models = [];
  }
}
