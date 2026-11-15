"""Packaged recording cycles: 7, 16, then reordered 7 sources, with MP4 export.

Verifies actual files, source identity, export packet identity and overwrite
protection. Uses a temporary show and restores the existing UI preferences.
Supplying --mediamtx also keeps a private stream and receiver running across
all recording/export cycles, checking the captured output for interruptions.
Editor import remains a separate user workflow.
"""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text(encoding='utf-8').split('\ntry:\n    target=')[0])
import re

results = {'passed': False, 'cycles': [],
           'executableSha256': hashlib.sha256((package/'desktop-studio.exe').read_bytes()).hexdigest()}
root = None
mtx = receiver = receiver_log = mtx_log = None
stream_path = None
stream_workers = set()


def file_hash(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def metadata(path):
    info = run([ffmpeg, '-hide_banner', '-i', path], timeout=30).stderr.decode('utf-8', errors='replace')
    (report/(path.name+'.metadata.txt')).write_text(info, encoding='utf-8')
    blocks = re.split(r'\n\s*Stream #0:', info)[1:]
    audio = [block for block in blocks if 'Audio:' in block.splitlines()[0]]
    names = []
    for block in audio:
        title = re.search(r'^\s*title\s*:\s*(.*?)\s*$', block, re.MULTILINE)
        name = re.search(r'^\s*name\s*:\s*(.*?)\s*$', block, re.MULTILINE)
        handler = re.search(r'^\s*handler_name\s*:\s*(.*?)\s*$', block, re.MULTILINE)
        # MKV exposes the source label as title; MP4 exposes its track udta as name.
        names.append(title.group(1) if title else name.group(1) if name else handler.group(1) if handler else None)
    return names


def decode_all(path):
    decoded = run([ffmpeg, '-v', 'error', '-i', path, '-map', '0', '-fps_mode:v', 'passthrough',
                   '-enc_time_base:v', '1:1000000', '-f', 'null', '-'], timeout=90)
    assert decoded.returncode == 0 and not decoded.stderr, (path, decoded.stderr)


def audio_packet_hashes(path):
    encoded = run([ffmpeg, '-v', 'error', '-i', path, '-map', '0:a', '-c', 'copy',
                   '-f', 'streamhash', '-hash', 'sha256', '-'], timeout=30)
    assert encoded.returncode == 0 and not encoded.stderr, (path, encoded.stderr)
    return [line for line in encoded.stdout.decode().splitlines() if line and not line.startswith('#')]


def free_port():
    with socket.socket() as reserve:
        reserve.bind(('127.0.0.1', 0))
        return reserve.getsockname()[1]


def finish_receiver():
    if receiver and receiver.poll() is None:
        receiver.communicate(input=b'q\n', timeout=30)


def verify_continuous_stream(path):
    decode_all(path)
    packets = run([ffmpeg, '-v', 'error', '-i', path, '-map', '0:v:0', '-c', 'copy',
                   '-f', 'framecrc', '-'], text=True, encoding='utf-8', timeout=90)
    assert packets.returncode == 0 and not packets.stderr, packets.stderr
    rows = [line.split(',') for line in packets.stdout.splitlines() if line and not line.startswith('#')]
    timebase = re.search(r'#tb 0: (\d+)/(\d+)', packets.stdout)
    assert timebase and rows
    scale = int(timebase[1])/int(timebase[2])
    pts = np.array([int(row[2])*scale for row in rows]); gaps = np.diff(pts)
    assert len(rows) > 24*30 and min(gaps) > 0 and max(gaps) < .2, (len(rows), min(gaps), max(gaps))
    raw = run([ffmpeg, '-v', 'error', '-i', path, '-map', '0:a:0', '-ac', '1', '-ar', '48000',
               '-f', 'f32le', '-'], timeout=90)
    assert raw.returncode == 0 and not raw.stderr, raw.stderr
    audio = np.frombuffer(raw.stdout, np.float32)
    windows = [float(np.sqrt(np.mean(audio[n:n+24000]**2))) for n in range(48000, len(audio)-48000, 24000)]
    assert windows and min(windows) > .005, 'Stream went silent while recording/exporting'
    tone_rows = []
    for start in (48000, len(audio)-96000):
        row = (abs(reference@audio[start:start+48000])*2/48000).tolist(); tone_rows.append(row)
        assert min(row) > .003, ('Stream lost a source', row)
    return {'file': path.name, 'fullDecode': 'clean', 'videoPackets': len(rows),
            'durationSeconds': len(audio)/48000, 'maxVideoGapMs': float(max(gaps)*1000),
            'minimumHalfSecondAudioRms': min(windows), 'startAndEndSourceTones': tone_rows,
            'sha256': file_hash(path)}


try:
    target = cli_call('launch', '--headless', '--show', str(show))
    root = psutil.Process(target['pid']); owned.append(root)
    command('monitor.set', enabled=False)
    ids = []
    for n in range(16):
        clip = report/f'source-{n+1}.wav'
        made = run([ffmpeg, '-v', 'error', '-n', '-f', 'lavfi', '-i',
                    f'sine=frequency={400+50*n}:sample_rate=48000', '-t', '40', clip], timeout=30)
        assert made.returncode == 0, made.stderr
        ids.append(command('source.add', kind='media', name=f'Voice {n+1}', locator=str(clip))['id'])
    until(lambda: all(s['peak'] > .02 for s in state()['sources']), 'all 16 sources audible')
    frequencies = 400+50*np.arange(16)
    reference = np.exp(-2j*np.pi*frequencies[:, None]*np.arange(48000)[None, :]/48000)
    if args.mediamtx:
        rtmp, rtsp = free_port(), free_port()
        assert rtmp != rtsp
        config = report/'receiver.yml'
        config.write_text(f'logLevel: warn\nrtspAddress: 127.0.0.1:{rtsp}\nrtspTransports: [tcp]\nrtmpAddress: 127.0.0.1:{rtmp}\nwebrtc: false\nsrt: false\nhls: false\nmoq: false\npaths:\n  all_others:\n')
        mtx_log = (report/'mediamtx.log').open('wb')
        mtx = subprocess.Popen([args.mediamtx, str(config)], stdout=mtx_log, stderr=mtx_log, creationflags=subprocess.CREATE_NO_WINDOW)
        time.sleep(.7); assert mtx.poll() is None
        command('output.configure', protocol='rtmp', url=f'rtmp://127.0.0.1:{rtmp}/cycles', encoder='auto', bitrate_kbps=3000)
        command('output.start'); until(lambda: state()['output']['live'], 'Private RTMP streaming')
        for attempt in range(10):
            stream_path = report/f'continuous-stream-{attempt}.mkv'
            receiver_log = (report/f'continuous-stream-{attempt}.log').open('wb')
            receiver = subprocess.Popen([str(ffmpeg), '-v', 'error', '-n', '-rtsp_transport', 'tcp',
                '-i', f'rtsp://127.0.0.1:{rtsp}/cycles', '-map', '0', '-c', 'copy', '-t', '240', str(stream_path)],
                stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=receiver_log, creationflags=subprocess.CREATE_NO_WINDOW)
            time.sleep(1)
            if receiver.poll() is None: break
            receiver_log.close(); receiver_log = None
            assert b'404' in (report/f'continuous-stream-{attempt}.log').read_bytes(), 'Stream receiver failed'
        assert receiver.poll() is None
        until(lambda: stream_path.exists() and stream_path.stat().st_size > 0, 'Receiver captures stream')
        stream_workers = {process.pid for process in root.children()}
        results['streamingAcrossCycles'] = True
    for cycle, selection in enumerate((list(range(7)), list(range(16)), list(reversed(range(7)))), 1):
        recording, exported = report/f'cycle-{cycle}.mkv', report/f'cycle-{cycle}.mp4'
        command('record.start', path=str(recording), audioSources=[ids[n] for n in selection], isoSources=[])
        until(lambda: state()['recording']['status'].startswith('Recording:'), 'recording starts')
        time.sleep(8)
        assert state()['recording']['program_active']
        command('record.stop')
        until(lambda: not state()['recording']['busy'], 'recording finalizes', 60)
        assert state()['recording']['status'].startswith('Recording saved:'), state()['recording']
        decode_all(recording)
        names = metadata(recording)
        expected = ['Program']+[f'Voice {n+1}' for n in selection]
        assert names == expected, (names, expected)
        rows = []
        for track, source_index in enumerate(selection, 1):
            raw = run([ffmpeg, '-v', 'error', '-i', recording, '-ss', '3', '-map', f'0:a:{track}',
                       '-t', '1', '-ac', '1', '-ar', '48000', '-f', 'f32le', '-'], timeout=30)
            assert raw.returncode == 0 and len(raw.stdout) == 48000*4, (track, raw.stderr, len(raw.stdout))
            samples = np.frombuffer(raw.stdout, np.float32)
            row = (abs(reference@samples)*2/48000).tolist(); rows.append(row)
            assert row[source_index] > .1, (track, source_index, row)
            assert max(v for n, v in enumerate(row) if n != source_index) < row[source_index]*.01, (track, row)
        before = file_hash(recording)
        command('record.export', input=str(recording), path=str(exported))
        until(lambda: not state()['recording']['exporting'], 'MP4 export finishes', 60)
        assert state()['recording']['export_status'].startswith('MP4 saved:'), state()['recording']
        decode_all(exported)
        exported_names = metadata(exported)
        assert exported_names == expected, (exported_names, expected)
        original_packets, exported_packets = audio_packet_hashes(recording), audio_packet_hashes(exported)
        assert len(original_packets) == len(expected) and original_packets == exported_packets, (original_packets, exported_packets)
        assert file_hash(recording) == before, 'Export changed the original MKV'
        exported_hash = file_hash(exported)
        api('/studio/command', {'command':'record.export', 'input':str(recording), 'path':str(exported)}, 400)
        assert file_hash(exported) == exported_hash, 'Rejected export overwrote the MP4'
        api('/studio/command', {'command':'record.start', 'path':str(recording)}, 400)
        assert file_hash(recording) == before and not state()['recording']['active'], 'Existing recording was overwritten'
        if args.mediamtx:
            assert state()['output']['live'] and receiver.poll() is None, 'Stream stopped during a recording cycle'
            assert stream_workers <= {process.pid for process in root.children()}, 'A streaming/source worker restarted'
        result = {'cycle': cycle, 'sourceIndices': selection, 'audioStreams': len(expected), 'trackNames': names,
                  'exportedTrackNames': exported_names, 'exportedTrackNamesPreserved': names == exported_names,
                  'sourceFrequencyAmplitudes': rows, 'allStreamsDecoded': True,
                  'audioPacketsUnchangedByExport': True, 'originalPreserved': True, 'overwriteRejected': True,
                  'recordingSha256': before, 'exportSha256': exported_hash}
        results['cycles'].append(result)
        print(json.dumps({k: result[k] for k in ('cycle', 'audioStreams', 'exportedTrackNamesPreserved', 'audioPacketsUnchangedByExport')}), flush=True)
        (report/'multitrack-roundtrip.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    if args.mediamtx:
        finish_receiver(); assert receiver.returncode == 0
        receiver_log.close(); receiver_log = None
        command('output.stop'); until(lambda: not state()['output']['active'], 'Streaming stops')
        results['continuousStream'] = verify_continuous_stream(stream_path)
        results['streamingWorkerRestarts'] = 0
    descendants = root.children(recursive=True)
    cli_call('quit'); root.wait(timeout=30); target = None
    _, alive = psutil.wait_procs(descendants, timeout=15)
    assert not alive, [p.pid for p in alive]
    results['cleanExit'] = True
    results['passed'] = True
except BaseException as error:
    results['error'] = str(error)
    raise
finally:
    if receiver:
        try: finish_receiver()
        except Exception as error:
            results['receiverCleanupError'] = str(error)
            results['passed'] = False
            receiver.kill(); receiver.wait(timeout=10)
    if receiver_log: receiver_log.close()
    if target:
        try:
            cli_call('quit'); root.wait(timeout=30)
        except Exception as error:
            results['cleanupError'] = str(error)
            results['passed'] = False
    if mtx:
        mtx.terminate(); mtx.wait(timeout=10)
    if mtx_log: mtx_log.close()
    for key, value in saved.items():
        if value is None:
            try: winreg.DeleteValue(settings, key)
            except FileNotFoundError: pass
        else: winreg.SetValueEx(settings, key, 0, value[1], value[0])
    winreg.CloseKey(settings)
    results['userShowUnchanged'] = (hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None) == original_hash
    if not results['userShowUnchanged']: results['passed'] = False
    (report/'multitrack-roundtrip.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    assert results['userShowUnchanged'], 'User show changed'
