"""Private Chromium WHIP receiver: actual browser decode and audio RED stats."""
import argparse, asyncio, json, os, sys, time, uuid
from pathlib import Path
sys.path.insert(0, str(Path('.cache/mixer-receiver').resolve()))
sys.path.insert(0, str(Path('.cache/mixer-qa').resolve()))
from aiohttp import web
from playwright.async_api import async_playwright

parser=argparse.ArgumentParser(); parser.add_argument('--port',required=True,type=int); parser.add_argument('--report',required=True)
args=parser.parse_args(); report=Path(args.report).resolve(); report.mkdir(parents=True,exist_ok=True)
page=None; browser=None; playwright=None; current=None; closed=True

async def offer(request):
    global current,closed
    if current: await close_session()
    # A fresh document prevents callbacks/stats from a previous session from
    # contaminating the next one while using the same running browser process.
    await page.goto(f'http://127.0.0.1:{args.port}/')
    await page.evaluate('value => window.combined=value',bool(os.environ.get('STUDIO_BROWSER_COMBINED')))
    current=str(uuid.uuid4());closed=False
    sdp=await page.evaluate('''async sdp => {
        window.framesSeen=[]; window.audioLevels=[];
        window.pc=new RTCPeerConnection({iceServers:[]});
        window.v=document.createElement('video'); v.autoplay=true; v.muted=!combined; v.volume=0; document.body.replaceChildren(v);
        window.received=new MediaStream();if(combined)v.srcObject=received;
        window.canvas=document.createElement('canvas'); canvas.width=640; canvas.height=360;
        const g=canvas.getContext('2d',{willReadFrequently:true});
        function frame(now,meta){
            if(v.videoWidth){g.drawImage(v,0,0,640,360); const p=g.getImageData(0,0,320,24).data;
                let id=0; for(let bit=0;bit<16;bit++)if(p[(12*320+bit*20+10)*4]>128)id+=2**bit;
                framesSeen.push({pts:meta.mediaTime,wall:performance.timeOrigin+performance.now(),id,presentedFrames:meta.presentedFrames,rtpTimestamp:meta.rtpTimestamp});}
            window.frameCallback=v.requestVideoFrameCallback(frame);
        }
        window.frameCallback=v.requestVideoFrameCallback(frame);
        pc.ontrack=event=>{
            if(combined){received.addTrack(event.track);v.play();}
            if(event.track.kind==='video'){if(!combined){v.srcObject=new MediaStream([event.track]);v.play();}}
            else {
                window.context=new AudioContext({sampleRate:48000});
                if(combined)window.a=v;
                else {window.a=document.createElement('audio');a.autoplay=true;a.volume=0;
                    a.srcObject=new MediaStream([event.track]);document.body.append(a);a.play().catch(e=>window.playError=String(e));}
                const source=context.createMediaStreamSource(new MediaStream([event.track]));
                window.analyzer=context.createAnalyser(); analyzer.fftSize=2048; source.connect(analyzer);
                const silence=context.createGain();silence.gain.value=0;analyzer.connect(silence).connect(context.destination);context.resume();
                window.audioTimer=setInterval(()=>{const a=new Float32Array(2048);analyzer.getFloatTimeDomainData(a);
                    let energy=0;for(const x of a)energy+=x*x;audioLevels.push({wall:performance.timeOrigin+performance.now(),rms:Math.sqrt(energy/a.length)});},20);
            }
        };
        await pc.setRemoteDescription({type:'offer',sdp});
        await pc.setLocalDescription(await pc.createAnswer());
        if(pc.iceGatheringState!=='complete')await new Promise(resolve=>pc.addEventListener('icegatheringstatechange',()=>{if(pc.iceGatheringState==='complete')resolve();}));
        return pc.localDescription.sdp;
    }''',await request.text())
    (report/(current+'.sdp')).write_text(sdp,encoding='utf-8')
    return web.Response(status=201,text=sdp,content_type='application/sdp',headers={'Location':'/session/'+current})

async def snapshot():
    value=await page.evaluate('''async()=>{const q=v.getVideoPlaybackQuality();return {contextState:window.context?.state,playError:window.playError,audioTime:window.a?.currentTime,playbackQuality:{totalVideoFrames:q.totalVideoFrames,droppedVideoFrames:q.droppedVideoFrames},frames:framesSeen,audio:audioLevels,stats:[...await pc.getStats()].map(x=>x[1])};}''')
    value['id']=current;value['combinedPlayback']=bool(os.environ.get('STUDIO_BROWSER_COMBINED'));return value

async def close_session():
    global closed
    if not current or closed:return
    closed=True
    value=await snapshot(); (report/(current+'.json')).write_text(json.dumps(value),encoding='utf-8')
    await page.screenshot(path=str(report/(current+'.png')))
    await page.evaluate('''async()=>{v.cancelVideoFrameCallback(window.frameCallback);pc.close();clearInterval(window.audioTimer);if(window.context && context.state!=="closed")await context.close();}''')

async def delete(request):
    await close_session(); return web.Response(status=200)

async def status(request):
    if closed and current:return web.json_response(json.loads((report/(current+'.json')).read_text(encoding='utf-8')))
    return web.json_response(await snapshot())

async def startup(app):
    global page,browser,playwright
    playwright=await async_playwright().start()
    browser=await playwright.chromium.launch(channel='msedge',headless=True,args=[
        '--autoplay-policy=no-user-gesture-required','--disable-features=WebRtcHideLocalIpsWithMdns',
        '--allow-loopback-in-peer-connection'])
    page=await browser.new_page(); await page.goto(f'http://127.0.0.1:{args.port}/')

async def cleanup(app):
    if browser:await browser.close()
    if playwright:await playwright.stop()

async def main():
    app=web.Application(client_max_size=1024*1024)
    app.router.add_get('/',lambda request:web.Response(text='<title>Private WHIP receiver</title>',content_type='text/html'))
    app.router.add_post('/whip/whip',offer); app.router.add_options('/whip/whip',lambda request:web.Response(status=204))
    app.router.add_delete('/session/{id}',delete); app.router.add_get('/status',status)
    app.on_cleanup.append(cleanup)
    runner=web.AppRunner(app); await runner.setup(); await web.TCPSite(runner,'127.0.0.1',args.port).start()
    try:await startup(app); await asyncio.Event().wait()
    finally:await runner.cleanup()

asyncio.run(main())
