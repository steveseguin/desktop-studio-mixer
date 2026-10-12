"""Publish native 1080p60 Program through private RTMP and WHIP receivers."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import socket,os,numpy as np
results={};children=[]
ffmpeg=Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve()
server_exe=Path(os.environ.get('MEDIAMTX_PATH','.cache/mediamtx/mediamtx.exe')).resolve()
def port(kind=socket.SOCK_STREAM):
    with socket.socket(type=kind) as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
rtsp,rtmp,whip,udp=port(),port(),port(),port(socket.SOCK_DGRAM)
config=report/'mediamtx.yml';config.write_text(f'''logLevel: info
rtspAddress: 127.0.0.1:{rtsp}
rtspTransports: [tcp]
rtmpAddress: 127.0.0.1:{rtmp}
webrtcAddress: 127.0.0.1:{whip}
webrtcLocalUDPAddress: 127.0.0.1:{udp}
webrtcIPsFromInterfaces: false
webrtcAdditionalHosts: [127.0.0.1]
webrtcICEServers2: []
srt: false
hls: false
moq: false
paths:
  all_others:
''')
log=(report/'server.log').open('w');server=subprocess.Popen([str(server_exe),str(config)],cwd=str(report),stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW);children.append(server)
window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input();studio_mode(False);scene('Main');remove_sources(('QA ',))
enabled(control('guestProgramReturn'),False)
choose_dialog(window,'canvasFormat','1920x1080 / 60 fps')
add_source('Video / audio file','QA protocol',str(media_fixture()))
try:
    for label,url,name in [('RTMP / RTMPS',f'rtmp://127.0.0.1:{rtmp}/qa-rtmp','qa-rtmp'),('WHIP',f'http://127.0.0.1:{whip}/qa-whip/whip','qa-whip')]:
        configure(label,url);press('studioStartOutput');wait_output(True)
        deadline=time.time()+30
        while time.time()<deadline:
            lines=(report/'server.log').read_text()
            if any('is publishing' in line and name in line for line in lines.splitlines()):break
            time.sleep(.3)
        else:raise AssertionError(control('studioOutputStatus').window_text()+'\n'+lines[-2000:])
        path=report/(name+'.mkv')
        receiver=subprocess.run([str(ffmpeg),'-hide_banner','-y','-rtsp_transport','tcp','-i',f'rtsp://127.0.0.1:{rtsp}/{name}','-t','8','-map','0','-c','copy',str(path)],capture_output=True,text=True,timeout=30)
        assert receiver.returncode==0,receiver.stderr
        press('studioStartOutput');wait_output(False)
        decoded=subprocess.run([str(ffmpeg),'-hide_banner','-i',str(path),'-vf','scale=64:36','-an','-f','framemd5','-'],capture_output=True,text=True,timeout=40)
        assert decoded.returncode==0 and '1920x1080' in decoded.stderr and 'Video: h264' in decoded.stderr,decoded.stderr
        frames=[line for line in decoded.stdout.splitlines() if line and not line.startswith('#')]
        assert len(frames)>400,len(frames)
        pcm=np.frombuffer(subprocess.check_output([str(ffmpeg),'-v','error','-i',str(path),'-vn','-f','f32le','-ac','1','-ar','48000','pipe:1']),np.float32)
        rms=float(np.sqrt(np.mean(pcm*pcm)));assert rms>.01,rms
        results[label]={'decodedFrames':len(frames),'dimensions':'1920x1080','audioRms':rms};print(label,results[label],flush=True)
finally:
    if control('studioStartOutput').window_text()=='STOP':press('studioStartOutput');wait_output(False)
    for child in children:
        if child.poll() is None:child.terminate();child.wait(timeout=8)
    log.close();(report/'protocols.json').write_text(json.dumps(results,indent=2))
