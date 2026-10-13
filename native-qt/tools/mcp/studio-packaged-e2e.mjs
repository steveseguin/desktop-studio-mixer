// Real MCP client -> packaged stdio server -> running packaged native app.
import {Client} from '@modelcontextprotocol/sdk/client/index.js';
import {StdioClientTransport} from '@modelcontextprotocol/sdk/client/stdio.js';
import assert from 'node:assert/strict';
import {writeFile,stat} from 'node:fs/promises';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import path from 'node:path';
const [packageRoot,discovery,report]=process.argv.slice(2);
const client=new Client({name:'studio-packaged-e2e',version:'1.0'});
const transport=new StdioClientTransport({command:process.execPath,args:[path.join(packageRoot,'tools/mcp/studio-mcp.mjs')],stderr:'pipe'});
async function call(name,args={}){
 const r=await client.callTool({name:'desktop_studio_'+name,arguments:args},undefined,{timeout:45000});
 assert(!r.isError,JSON.stringify(r.content));return r;
}
async function waitState(check){
 for(let n=0;n<200;n++){
  const state=(await call('status')).structuredContent;if(check(state))return state;
  await new Promise(resolve=>setTimeout(resolve,100));
 }
 assert.fail('Timed out waiting for the packaged app');
}
try {
 await client.connect(transport);
 const listed=await client.listTools();assert(listed.tools.length>=30);
 await call('attach',{discovery});
 const state=(await call('status')).structuredContent;assert(state.headless);
 assert(!JSON.stringify(state).includes('Bearer '));
 assert(listed.tools.some(t=>t.name==='desktop_studio_monitor_set'));
 for(const name of ['production_set','guest_invite','guest_set','record_export','show_saveAs','show_collect','show_relink','local_set'])assert(listed.tools.some(t=>t.name==='desktop_studio_'+name));
 const recordTool=listed.tools.find(t=>t.name==='desktop_studio_record_start');
 assert.equal(recordTool.inputSchema.properties.audioSources.type,'array');
 assert.equal(recordTool.inputSchema.properties.isoSources.type,'array');
 await call('local_set',{spout:false,virtualCamera:false});
 assert(!(await call('status')).structuredContent.local_outputs.spout);
 await call('production_set',{privateBreak:true,microphonesMuted:true,privateTalkback:true});
 let production=(await call('status')).structuredContent;
 assert(production.private_break&&production.microphones_muted&&production.private_talkback);
 await call('production_set',{privateBreak:false,microphonesMuted:false,privateTalkback:false});
 const guest=(await call('guest_invite',{kind:'guest',name:'MCP private guest',returnMode:'none',programAudio:false})).structuredContent;
 assert(new URL(guest.invite).hostname==='vdo.ninja');
 await call('guest_set',{id:guest.id,admitted:true});
 await call('audio_set',{source:guest.id,programAudio:false,channelMode:2});
 production=(await call('status')).structuredContent;
 const caller=production.sources.find(s=>s.id===guest.id);assert(caller.admitted&&!caller.programAudio&&caller.channelMode===2);
 assert(!JSON.stringify(production).includes(guest.invite));
 const recording=path.join(report,'mcp-tracks.mkv');
 const invalidArray=await client.callTool({name:'desktop_studio_record_start',arguments:{path:recording,audioSources:[false]}});assert(invalidArray.isError);
 const duplicateArray=await client.callTool({name:'desktop_studio_record_start',arguments:{path:recording,audioSources:[guest.id,guest.id]}});assert(duplicateArray.isError);
 await call('record_start',{path:recording,audioSources:[guest.id],isoSources:[guest.id]});
 await waitState(s=>s.recording.status.startsWith('Recording:')&&s.recording.separate_videos[0].status.startsWith('Recording:'));
 await new Promise(resolve=>setTimeout(resolve,3000));await call('record_stop');
 const recorded=(await waitState(s=>!s.recording.busy)).recording;
 assert.deepEqual(recorded.audio_sources,[guest.id]);
 for(const file of [recording,recorded.separate_videos[0].path]){
  assert((await stat(file)).size>1000);
  const decoded=await promisify(execFile)(path.join(packageRoot,'ffmpeg/bin/ffmpeg.exe'),['-v','error','-i',file,'-map','0','-fps_mode:v','passthrough','-enc_time_base:v','1:1000000','-f','null','-'],{windowsHide:true,timeout:30000});
  assert.equal(decoded.stderr,'');
 }
 await call('guest_set',{id:guest.id,admitted:false});await call('source_remove',{id:guest.id});
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
 for(const [format,width,height,fps] of [[4,1080,1920,30],[5,720,1280,30],[6,720,1280,60],[7,1080,1920,60]]){
  await call('canvas_set',{format});
  const canvas=(await call('status')).structuredContent.canvas;
  assert.deepEqual([canvas.width,canvas.height,canvas.fps],[width,height,fps]);
 }
 const invalidFormat=await client.callTool({name:'desktop_studio_canvas_set',arguments:{format:8}});assert(invalidFormat.isError);
 await call('canvas_set',{format:5});
 const image=await call('screenshot',{target:'program',max_width:640});
 assert.equal(image.content[0].type,'image');assert.equal(image.content[0].mimeType,'image/png');
 const pixels=Buffer.from(image.content[0].data,'base64');assert.equal(pixels.subarray(1,4).toString(),'PNG');
 assert(pixels.readUInt32BE(20)>pixels.readUInt32BE(16),'Headless Program screenshot must be portrait');
 await writeFile(path.join(report,'mcp-program.png'),pixels);
 const resource=await client.readResource({uri:'studio://state'});assert(JSON.parse(resource.contents[0].text).scenes.some(s=>s.id===added.id));
 await call('scene_select',{id:state.preview});await call('scene_remove',{id:added.id});
 const invalid=await client.callTool({name:'desktop_studio_audio_set',arguments:{source:'missing',gain:100}});assert(invalid.isError);
 await writeFile(path.join(report,'mcp.json'),JSON.stringify({tools:listed.tools.length,imageBytes:pixels.length,resource:true,invalidRejected:true,monitorDeviceAndGain:true,monitorRefresh:true,missingMonitorRejected:true,portraitPresetsAndScreenshot:true,invalidFormatRejected:true,guestAndProductionControls:true,audioChannelRouting:true,typedRecordingArrays:true,separateRecordingsDecoded:true,invalidAndDuplicateArraysRejected:true,localOutputsControl:true},null,2));
} finally { await client.close(); }
