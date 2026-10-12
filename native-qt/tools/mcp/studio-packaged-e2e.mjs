// Real MCP client -> packaged stdio server -> running packaged native app.
import {Client} from '@modelcontextprotocol/sdk/client/index.js';
import {StdioClientTransport} from '@modelcontextprotocol/sdk/client/stdio.js';
import assert from 'node:assert/strict';
import {writeFile} from 'node:fs/promises';
import path from 'node:path';
const [packageRoot,discovery,report]=process.argv.slice(2);
const client=new Client({name:'studio-packaged-e2e',version:'1.0'});
const transport=new StdioClientTransport({command:process.execPath,args:[path.join(packageRoot,'tools/mcp/studio-mcp.mjs')],stderr:'pipe'});
async function call(name,args={}){
 const r=await client.callTool({name:'desktop_studio_'+name,arguments:args},undefined,{timeout:45000});
 assert(!r.isError,JSON.stringify(r.content));return r;
}
try {
 await client.connect(transport);
 const listed=await client.listTools();assert(listed.tools.length>=30);
 await call('attach',{discovery});
 const state=(await call('status')).structuredContent;assert(state.headless);
 assert(!JSON.stringify(state).includes('Bearer '));
 assert(listed.tools.some(t=>t.name==='desktop_studio_monitor_set'));
 await call('monitor_set',{refresh:true});
 let monitoring;
 for(let attempt=0;attempt<50;attempt++){
  monitoring=(await call('status')).structuredContent.monitor;
  if(!monitoring.refreshing)break;
  await new Promise(resolve=>setTimeout(resolve,100));
 }
 assert(!monitoring.refreshing);assert(monitoring.devices.some(d=>d.id==='default'&&d.available));
 const missingOutput=await client.callTool({name:'desktop_studio_monitor_set',arguments:{device:'qa-missing-endpoint'}});assert(missingOutput.isError);
 const invalidMonitorGain=await client.callTool({name:'desktop_studio_monitor_set',arguments:{gain:2}});assert(invalidMonitorGain.isError);
 await call('monitor_set',{device:'default',gain:.37,enabled:true});
 await new Promise(resolve=>setTimeout(resolve,1200));
 monitoring=(await call('status')).structuredContent.monitor;
 assert(monitoring.enabled);assert.equal(monitoring.device,'default');assert(Math.abs(monitoring.gain-.37)<.001);
 await call('monitor_set',{enabled:false});assert(!(await call('status')).structuredContent.monitor.enabled);
 const added=(await call('scene_add',{name:'MCP blue',background:'#1234d0'})).structuredContent;
 assert(added.id);
 await call('scene_select',{id:added.id});
 const image=await call('screenshot',{target:'program',max_width:640});
 assert.equal(image.content[0].type,'image');assert.equal(image.content[0].mimeType,'image/png');
 const pixels=Buffer.from(image.content[0].data,'base64');assert.equal(pixels.subarray(1,4).toString(),'PNG');
 await writeFile(path.join(report,'mcp-program.png'),pixels);
 const resource=await client.readResource({uri:'studio://state'});assert(JSON.parse(resource.contents[0].text).scenes.some(s=>s.id===added.id));
 await call('scene_select',{id:state.preview});await call('scene_remove',{id:added.id});
 const invalid=await client.callTool({name:'desktop_studio_audio_set',arguments:{source:'missing',gain:100}});assert(invalid.isError);
 await writeFile(path.join(report,'mcp.json'),JSON.stringify({tools:listed.tools.length,imageBytes:pixels.length,resource:true,invalidRejected:true,monitorDeviceAndGain:true,monitorRefresh:true,missingMonitorRejected:true},null,2));
} finally { await client.close(); }
