import argparse,json,re,subprocess
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser();p.add_argument('--ffmpeg',required=True);p.add_argument('files',nargs='+');a=p.parse_args()
failed=False
def run(args):return subprocess.run([a.ffmpeg,'-hide_banner','-nostdin',*args],capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=120)
for filename in a.files:
    # Preserve the demuxer's precision: rounding millisecond timestamps onto a
    # 1/fps output grid can manufacture duplicate DTS in the null muxer.
    f=Path(filename);r=run(['-v','error','-xerror','-i',str(f),'-map','0','-fps_mode','passthrough','-enc_time_base:v','demux','-f','null','-']);decode=r.stderr.decode(errors='replace');(f.parent/(f.name+'.decode.log')).write_text(decode)
    v=run(['-i',str(f),'-map','0:v:0','-vf','crop=64:64:32:32,scale=1:1:flags=area,showinfo','-fps_mode','passthrough','-pix_fmt','rgb24','-f','rawvideo','-'])
    times=np.array([float(x) for x in re.findall(r'\bn:\s*\d+\s+pts:\s*-?\d+\s+pts_time:([-\d.]+)',v.stderr.decode(errors='replace'))]);rgb=np.frombuffer(v.stdout,np.uint8).reshape(-1,3)
    au=run(['-i',str(f),'-map','0:a:0','-af','ashowinfo','-ac','1','-ar','48000','-f','f32le','-']);wave=np.frombuffer(au.stdout,np.float32);at=re.search(r'pts_time:([-\d.]+)',au.stderr.decode(errors='replace'));start=float(at.group(1)) if at else 0
    n=len(wave)//480;levels=np.max(np.abs(wave[:n*480].reshape(-1,480)),axis=1);audible=levels>.04;audio_on=np.where(audible & ~np.r_[False,audible[:-1]])[0]/100+start
    white=np.mean(rgb,axis=1)>200;video_on=times[np.where(white & ~np.r_[False,white[:-1]])[0]] if len(times)==len(rgb) else np.array([])
    offsets=[float(t-video_on[np.argmin(abs(video_on-t))]) for t in audio_on[1:-1]] if len(video_on) else []
    gaps=np.diff(times)
    failed=failed or r.returncode!=0 or bool(decode.strip()) or v.returncode!=0 or au.returncode!=0 or not len(times) or len(times)!=len(rgb) or bool(np.any(gaps<=0)) or not len(wave)
    result={'file':f.name,'bytes':f.stat().st_size,'fullDecodeExit':r.returncode,'decodeLog':decode[-800:],'frames':len(rgb),'videoFirstPts':float(times[0]) if len(times) else None,'videoLastPts':float(times[-1]) if len(times) else None,'audioSeconds':len(wave)/48000,'audioPeak':float(np.max(abs(wave))) if len(wave) else 0,'videoFlashes':len(video_on),'audioPulses':len(audio_on),'medianAudioMinusVideoMs':float(np.median(offsets)*1000) if offsets else None,'maxAbsSyncOffsetMs':float(np.max(np.abs(offsets))*1000) if offsets else None,'maxFrameGapMs':float(np.max(gaps)*1000) if len(gaps) else None,'frameGapsOver100ms':int(np.sum(gaps>.1001))}
    (f.parent/(f.name+'.analysis.json')).write_text(json.dumps(result,indent=2));print(json.dumps(result))
raise SystemExit(1 if failed else 0)
