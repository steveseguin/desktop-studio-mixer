"""Select seven sources, record and export using the packaged application's GUI.

Run with mixer-ui-workflows-e2e.py. API access is read-only observation; actions
use Windows UI Automation and keyboard/pointer input. Analyze the actual files.
"""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers = Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import hashlib
import re
import urllib.request
import numpy as np
import psutil

executable = Path(psutil.Process(args.pid).exe())
ffmpeg = executable.parent/'ffmpeg/bin/ffmpeg.exe'
results = {'passed': False, 'executableSha256': hashlib.sha256(executable.read_bytes()).hexdigest()}


def state():
    discovery = json.loads(Path(os.environ['STUDIO_E2E_DISCOVERY']).read_text())
    request = urllib.request.Request(discovery['base_url']+'/studio/state',
                                    headers={'Authorization': 'Bearer '+discovery['token']})
    with urllib.request.urlopen(request, timeout=15) as response:
        return json.load(response)


def wait(check, label):
    deadline = time.monotonic()+40
    while time.monotonic() < deadline:
        if check(): return
        time.sleep(.2)
    raise AssertionError(label)


def run(*argv):
    return subprocess.run([str(ffmpeg), *map(str, argv)], capture_output=True,
                          creationflags=subprocess.CREATE_NO_WINDOW, timeout=90)


def menu(title):
    # Current releases put recordings under Tools; older packages expose a button.
    if any((c.element_info.automation_id or '').endswith('.studioTools') for c in window.descendants()):
        press('studioTools')
        submenu = next(item for top in app.windows() for item in top.descendants(control_type='MenuItem')
                       if item.window_text() == 'Recordings')
        submenu.click_input(); time.sleep(.25)
    else:
        press('recordingFiles')
    item = next(item for top in app.windows() for item in top.descendants(control_type='MenuItem')
                if item.window_text() == title)
    item.click_input()


def filename(title, path):
    dialog = window.child_window(title=title, control_type='Window')
    dialog.wait('visible')
    field = dialog.child_window(title='File name:', control_type='Edit')
    if not field.exists(timeout=.5): field = control('fileNameEdit', dialog)
    field.set_edit_text(str(path)); field.set_focus(); field.type_keys('{ENTER}')


def names(path):
    info = run('-hide_banner', '-i', path).stderr.decode('utf-8', errors='replace')
    (report/(path.name+'.metadata.txt')).write_text(info, encoding='utf-8')
    blocks = re.split(r'\n\s*Stream #0:', info)[1:]
    audio = [block for block in blocks if 'Audio:' in block.splitlines()[0]]
    labels = [re.search(r'^\s*(?:title|name)\s*:\s*(.*?)\s*$', block, re.MULTILINE) for block in audio]
    assert all(labels), info
    return [label.group(1) for label in labels]


try:
    window.set_focus(); window.child_window(title='Studio', control_type='TabItem').click_input()
    studio_mode(False); scene('Main'); enabled(control('monitorEnable'), False)
    ids = []
    for index in range(7):
        clip = report/f'voice-{index+1}.wav'
        made = run('-v', 'error', '-n', '-f', 'lavfi', '-i',
                   f'sine=frequency={400+50*index}:sample_rate=48000', '-t', '180', clip)
        assert made.returncode == 0, made.stderr
        name = f'Voice {index+1}'
        add_source('Video / audio file', name, str(clip))
        ids.append(next(source['id'] for source in state()['sources'] if source['name'] == name))
    wait(lambda: all(source['peak'] > .02 for source in state()['sources'] if source['id'] in ids), 'Seven audible sources')
    menu('Recording tracks...')
    dialog = window.child_window(title='Recording tracks', control_type='Window'); dialog.wait('visible')
    for sid in ids:
        box = control('record_audio_'+sid, dialog)
        box.set_focus()
        if not box.get_toggle_state(): box.type_keys('{SPACE}')
        assert box.get_toggle_state()
    dialog.capture_as_image().save(str(report/'selected-seven-tracks.png'))
    dialog.child_window(title='Save', control_type='Button').click_input()
    wait(lambda: json.loads(show_path.read_text()).get('recordAudioSources') == ids, 'Saved track selection')
    assert not json.loads(show_path.read_text()).get('recordIsoSources')
    menu('Recording tracks...')
    dialog = window.child_window(title='Recording tracks', control_type='Window'); dialog.wait('visible')
    assert all(control('record_audio_'+sid, dialog).get_toggle_state() for sid in ids)
    dialog.child_window(title='Cancel', control_type='Button').click_input()
    results['sevenSourceSelectionSavedAndReopened'] = True

    recording = report/'Seven source recording.mkv'
    press('studioRecord'); filename('Record Program', recording); wait_recording(True)
    time.sleep(8)
    press('studioRecord'); wait(lambda: not state()['recording']['busy'], 'Recording finalizes')
    assert state()['recording']['status'].startswith('Recording saved:'), state()['recording']
    original_hash = hashlib.sha256(recording.read_bytes()).hexdigest()
    menu('Export recording as MP4...'); filename('Choose recording to export', recording)
    exported = report/'Seven source export.mp4'
    filename('Export MP4 (original recording preserved)', exported)
    wait(lambda: not state()['recording']['exporting'], 'MP4 export finishes')
    assert state()['recording']['export_status'].startswith('MP4 saved:'), state()['recording']
    assert hashlib.sha256(recording.read_bytes()).hexdigest() == original_hash
    expected = ['Program']+[f'Voice {n+1}' for n in range(7)]
    reference = np.exp(-2j*np.pi*(400+50*np.arange(7))[:, None]*np.arange(48000)[None, :]/48000)
    results['files'] = []
    for path in (recording, exported):
        labels = names(path); assert labels == expected, labels
        decoded = run('-v', 'error', '-i', path, '-map', '0', '-fps_mode:v', 'passthrough',
                      '-enc_time_base:v', '1:1000000', '-f', 'null', '-')
        assert decoded.returncode == 0 and not decoded.stderr, decoded.stderr
        rows = []
        for track in range(1, 8):
            raw = run('-v', 'error', '-i', path, '-ss', '3', '-map', f'0:a:{track}', '-t', '1',
                      '-ac', '1', '-ar', '48000', '-f', 'f32le', '-')
            assert raw.returncode == 0 and len(raw.stdout) == 48000*4, (track, raw.stderr, len(raw.stdout))
            row = (abs(reference@np.frombuffer(raw.stdout, np.float32))*2/48000).tolist(); rows.append(row)
            assert row[track-1] > .1 and max(value for n, value in enumerate(row) if n != track-1) < row[track-1]*.01, (track, row)
        results['files'].append({'file': path.name, 'trackNames': labels, 'fullDecode': 'clean',
                                 'sourceFrequencyAmplitudes': rows, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    window.capture_as_image().save(str(report/'export-complete.png'))
    results['originalPreserved'] = True
    results['passed'] = True
    print(json.dumps({'passed': True, 'audioStreams': len(expected), 'guiRecordAndExport': True}), flush=True)
except BaseException as error:
    results['error'] = str(error)
    raise
finally:
    (report/'multitrack-ui.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
