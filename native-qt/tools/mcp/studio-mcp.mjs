import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import { z } from 'zod';
import { readFile } from 'node:fs/promises';
import { discovery, defaultDiscovery, request, launch, screenshot } from './studio-client.mjs';

const server=new McpServer({name:'desktop-studio',version:'0.7.3'});
let selected=defaultDiscovery;
let queue=Promise.resolve();
const text=value=>({content:[{type:'text',text:JSON.stringify(value)}],structuredContent:value});
function register(name,description,inputSchema,readOnly,handler) {
  server.registerTool('desktop_studio_'+name,{description,inputSchema,
    annotations:{readOnlyHint:readOnly,destructiveHint:!readOnly,openWorldHint:!readOnly}},async args=>{
    const run=async()=>handler(args);
    try {
      if(readOnly)return await run();
      const pending=queue.then(run);queue=pending.catch(()=>{});return await pending;
    } catch(error){return {isError:true,content:[{type:'text',text:error.message}]};}
  });
}
const pathText=z.string().min(1).max(4096);
register('attach','Select an existing instance by its private discovery file. Never returns the bearer token.',{discovery:pathText},false,async args=>{
  const target=await discovery(args.discovery);await request(target,'/studio/state');selected=args.discovery;return text({attached:true,pid:target.pid});
});
register('launch','Launch Desktop Studio, windowless by default. Does not start output. The app continues running when MCP disconnects; quit explicitly after stopping outputs.',{
  executable:pathText.optional(),show:pathText.optional(),headless:z.boolean().default(true)
},false,async args=>{const value=await launch(args);selected=value.result.discovery;return text(value.result);});
register('status','Read scenes, stable source IDs, audio levels, active outputs and asynchronous recording/publishing status. Source URLs and credentials are omitted.',{},true,
  async()=>text(await request(await discovery(selected),'/studio/state')));
register('schema','Read the complete versioned command contract. Use this before generic commands.',{},true,
  async()=>text(await request(await discovery(selected),'/studio/schema')));
register('sources','List capture device IDs for adding sources.',{kind:z.enum(['cameras','windows','spout','audio-inputs'])},true,
  async({kind})=>text(await request(await discovery(selected),'/sources/'+kind)));
register('screenshot','Get a PNG image of Program, Preview, or the app window, including when windowless. Captures app-rendered pixels only.',{
  target:z.enum(['program','preview','window']).default('program'),max_width:z.number().int().min(160).max(3840).default(1280)
},true,async args=>{const img=await screenshot(await discovery(selected),args);return {content:[{type:'image',mimeType:img.mime_type,data:img.data}]};});
register('quit','Quit the selected app and finalize recording. This also stops publishing.',{},false,
  async()=>text(await request(await discovery(selected),'/commands',{command:'quit'})));

// Generate named, typed tools from the same checked-in contract embedded in the
// native application. The native server validates again before any side effects.
let contractFile;
try { contractFile=await readFile(new URL('./studio-control.json',import.meta.url),'utf8'); }
catch(error) { if(error.code!=='ENOENT')throw error; contractFile=await readFile(new URL('../../resources/studio-control.json',import.meta.url),'utf8'); }
const contract=JSON.parse(contractFile);
function field(rule){
  if(rule.enum)return z.enum(rule.enum);
  if(rule.type==='boolean')return z.boolean();
  if(rule.type==='number' || rule.type==='integer'){
    let v=z.number();if(rule.type==='integer')v=v.int();return v.min(rule.minimum).max(rule.maximum);
  }
  let v=z.string();if(rule.minLength)v=v.min(rule.minLength);return v.max(rule.maxLength || 8192);
}
for(const [command,spec] of Object.entries(contract.commands)){
  const shape={};
  for(const [key,rule] of Object.entries(spec.properties))if(key!=='command'){
    const value=field(rule);shape[key]=spec.required.includes(key)?value:value.optional();
  }
  register(command.replaceAll('.','_'),spec.description,shape,false,
    async args=>text(await request(await discovery(selected),'/studio/command',{command,...args})));
}
server.registerResource('state','studio://state',{mimeType:'application/json'},async uri=>({contents:[{
  uri:uri.href,mimeType:'application/json',text:JSON.stringify(await request(await discovery(selected),'/studio/state'))
}]}));
await server.connect(new StdioServerTransport());
