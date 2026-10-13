"""Select encrypted SRT listener through the visible controls and decode its output."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-outputs-050-e2e.py').read_text().split('\ntry:\n    scene')[0])
try:
    scene('Main');add_source('Video / audio file','QA SRT listener',str(media_fixture()))
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    secret='private-studio-listener-fixture'
    configure('SRT',f'srt://127.0.0.1:{port}');press('mixerOutput')
    choose_dialog(window,'outputSrtMode','listener');assert 'mode=listener' in control('outputUrlInput').window_text()
    control('outputSecretInput').set_edit_text(secret);control('outputStreamIdInput').set_edit_text('')
    window.capture_as_image().save(str(report/'listener-settings.png'))
    window.child_window(title='Studio',control_type='TabItem').click_input()
    independent=report/'listener-independent.mkv';record(independent)
    press('studioStartOutput');wait_output(True)
    received=report/'listener-received.mkv';log=(report/'receiver.log').open('wb')
    receiver=subprocess.Popen([str(ffmpeg),'-v','error','-y','-i',f'srt://127.0.0.1:{port}?mode=caller&passphrase={secret}&pbkeylen=16&connect_timeout=15000','-t','6','-map','0','-c','copy',str(received)],stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
    assert receiver.wait(timeout=45)==0;log.close()
    press('studioStartOutput');wait_output(False);assert control('studioRecord').window_text()=='Stop recording'
    press('studioRecord');recording(False)
    results={'passed':True,'receiver':decode(received),'independentRecording':decode(independent),'listenerSelectedInGui':True}
finally:
    if receiver and receiver.poll() is None:receiver.terminate();receiver.wait(timeout=10)
    (report/'srt-listener-ui.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2),flush=True)
