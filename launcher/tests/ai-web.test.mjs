import test from 'node:test';
import assert from 'node:assert/strict';
import { AiDiagnostics, citedText, readResponse } from '../core/ai-diagnostics.mjs';
const event = output => new Response(`data: ${JSON.stringify({type:'response.completed',response:{status:'completed',output}})}\n\n`);
test('web citations preserve claims, replace markers and reject unsafe URLs',()=>{
 assert.equal(citedText({text:'Claim.',annotations:[{type:'url_citation',start_index:0,end_index:6,url:'https://example.com',title:'Source'}]}),'Claim. [Source](<https://example.com/>)');
 assert.equal(citedText({text:'Claim.',annotations:[{type:'url_citation',start_index:0,end_index:6,url:'javascript:alert(1)',title:'Bad'}]}),'Claim.');
 assert.equal(citedText({text:'citex',annotations:[{type:'url_citation',start_index:0,end_index:8,url:'https://example.com',title:'Source'}]}),'[Source](<https://example.com/>)');
});
test('web calls survive replay, citations reach chat and search can be disabled',async()=>{
 const requests=[];
 const auth={access:async()=> 'fixture'};
 const ai=new AiDiagnostics(auth,async(url,options)=>{
  requests.push(JSON.parse(options.body));
  return event([{type:'web_search_call',id:'ws_1',status:'completed',action:{type:'search',query:'Android emulator WHPX'}},{type:'message',role:'assistant',content:[{type:'output_text',text:'Found it.',annotations:[{type:'url_citation',start_index:0,end_index:9,title:'Android',url:'https://developer.android.com/studio/run/emulator-acceleration'}]}]}]);
 });
 ai.models=[{id:'fixture'}];
 const answer=await ai.analyze({model:'fixture',question:'Search the web',consent:true});
 assert.ok(requests[0].tools.some(t=>t.type==='web_search'));
 assert.match(answer,/\[Android\]\(<https:/);
 assert.equal(ai.messages.at(-1).content[0].toolName,'web_search');
 await ai.analyze({model:'fixture',question:'Follow up without web',consent:true,webSearch:false});
 assert.equal(requests[1].tools.some(t=>t.type==='web_search'),false);
 assert.ok(requests[1].input.some(t=>t.type==='web_search_call'));
});
test('unsupported web search explains how to recover',async()=>{
 const ai=new AiDiagnostics({access:async()=> 'fixture'},async()=>Response.json({error:{message:'web_search is not supported'}},{status:400}));
 ai.models=[{id:'fixture'}];
 await assert.rejects(ai.analyze({model:'fixture',question:'Search',consent:true}),/Turn off Web/);
});
