"""Packaged recording survives a real access violation; diagnostics survive restart.

Uses a private LOCALAPPDATA directory, separate show and synthetic credentials.
The injected fault targets only a process launched by this script.
"""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text(encoding='utf-8').split('\ntry:\n    target=')[0])

local = report/'local'
os.environ['LOCALAPPDATA'] = str(local)
root = None
results = {'executableSha256': hashlib.sha256((package/'desktop-studio.exe').read_bytes()).hexdigest()}

def start():
    global target, root
    target = cli_call('launch', '--headless', '--show', str(show))
    root = psutil.Process(target['pid'])
    assert Path(root.exe()).resolve() == (package/'desktop-studio.exe').resolve()
    owned.append(root)
    return Path(api('/logs/recent')['path'])

def quit_app():
    global target
    children = root.children(recursive=True)
    cli_call('quit'); root.wait(timeout=30); target = None
    _, alive = psutil.wait_procs(children, timeout=15)
    assert not alive, [p.pid for p in alive]

try:
    active_log = start()
    clip = report/'clock.mp4'
    made = run([ffmpeg, '-v', 'error', '-n', '-f', 'lavfi', '-i', 'testsrc2=s=640x360:r=30',
                '-f', 'lavfi', '-i', 'sine=frequency=731:sample_rate=48000', '-t', '12',
                '-c:v', 'libopenh264', '-c:a', 'aac', clip], timeout=45)
    assert made.returncode == 0, made.stderr
    sid = command('source.add', kind='media', name='Crash evidence', locator=str(clip))['id']
    command('monitor.set', enabled=False)
    until(lambda: state()['sources'][0]['frames'] > 20, 'media playing')
    recording = report/'access-violation.mkv'
    command('record.start', path=str(recording))
    until(lambda: state()['recording']['status'].startswith('Recording:'), 'recording')
    secret = 'FAKE_PRIVATE_STREAM_KEY_' + uuid.uuid4().hex
    with socket.socket() as reserve:
        reserve.bind(('127.0.0.1', 0)); unused = reserve.getsockname()[1]
    command('output.configure', protocol='rtmp', url=f'rtmp://127.0.0.1:{unused}/live/{secret}', encoder='auto')
    command('output.start'); time.sleep(6)
    issue = api('/commands', {'command': 'issue_report'}, expected=201)
    assert secret not in Path(issue['path']).read_text(encoding='utf-8')
    command('output.stop'); until(lambda: not state()['output']['active'], 'failed publisher stopped')
    assert state()['recording']['active']
    before = active_log.read_bytes()
    assert before and secret.encode() not in before
    children = root.children(recursive=True)
    # A null thread entry point raises an actual unhandled access violation in
    # this owned application, exercising its installed exception filter.
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = [ctypes.c_ulong, ctypes.c_bool, ctypes.c_ulong]
    kernel.OpenProcess.restype = ctypes.c_void_p
    kernel.CreateRemoteThread.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                         ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ulong, ctypes.c_void_p]
    kernel.CreateRemoteThread.restype = ctypes.c_void_p
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    handle = kernel.OpenProcess(0x0002 | 0x0400 | 0x0008 | 0x0010 | 0x0020, False, root.pid)
    assert handle, ctypes.get_last_error()
    try:
        thread = kernel.CreateRemoteThread(handle, None, 0, None, None, 0, None)
        assert thread, ctypes.get_last_error()
        kernel.CloseHandle(thread)
    finally: kernel.CloseHandle(handle)
    exit_code = root.wait(timeout=30); target = None
    _, alive = psutil.wait_procs(children, timeout=15)
    assert not alive, [p.pid for p in alive]
    reports = list((local/'VideoMixer/crashes').glob('crash-*.json'))
    assert len(reports) == 1, reports
    crash = json.loads(reports[0].read_text(encoding='utf-8'))
    assert crash['exception_code'] == '0xC0000005', crash
    assert crash['process_id'] == root.pid and crash['dump_written'], crash
    assert Path(crash['dump_path']).stat().st_size > 1024
    snapshot = Path(crash['log_snapshot_path'])
    assert snapshot.read_bytes().startswith(before)
    snapshot_hash = hashlib.sha256(snapshot.read_bytes()).hexdigest()
    results['accessViolation'] = {'exitCode': exit_code, 'crashReport': crash,
                                  'recording': decode(recording), 'ownedChildrenExited': True}
    log_path = start()
    assert log_path == active_log
    assert active_log.with_name(active_log.stem+'.1.log').read_bytes().startswith(before)
    assert not state()['recording']['active'] and not state()['output']['active']
    recovered = report/'after-restart.mkv'
    command('record.start', path=str(recovered)); time.sleep(5)
    command('record.stop'); until(lambda: not state()['recording']['busy'], 'restart recording saved')
    results['afterRestartRecording'] = decode(recovered)
    quit_app()
    # Populate closed logs close to the real size limit, then repeatedly launch
    # and record to verify rotation and archive retention without a test hook.
    sizes = []
    for number in range(5):
        active_log.write_bytes((f'closed-session-{number}\n'.encode()*400000)[:5*1024*1024])
        start(); assert state()['sources'][0]['id'] == sid; quit_app()
        files = list(active_log.parent.glob('video-mixer-debug*.log'))
        assert len(files) <= 4, files
        assert all(p.stat().st_size <= 5*1024*1024 for p in files)
        sizes.append(sum(p.stat().st_size for p in files))
    assert hashlib.sha256(snapshot.read_bytes()).hexdigest() == snapshot_hash
    assert not any(secret.encode() in p.read_bytes() for p in local.rglob('*.log'))
    results['rotation'] = {'sessions': 5, 'maxFiles': 4, 'totalBytesBySession': sizes,
                           'crashSnapshotSurvived': True, 'syntheticKeyAbsentFromLogsAndIssueReport': True}
    # Logging path unavailable: the studio must still start and record.
    os.environ['LOCALAPPDATA'] = str(report/'unwritable-log')
    blocker = Path(os.environ['LOCALAPPDATA'])/'VideoMixer/logs'
    blocker.parent.mkdir(parents=True); blocker.write_text('Deliberately blocks log directory creation.')
    start()
    path = report/'logging-unavailable.mkv'
    command('record.start', path=str(path)); time.sleep(5)
    command('record.stop'); until(lambda: not state()['recording']['busy'], 'recording without log saved')
    results['loggingUnavailableRecording'] = decode(path)
    quit_app(); results['passed'] = True
finally:
    if target:
        try: quit_app()
        except Exception:
            if root and root.is_running(): root.kill(); root.wait(timeout=10)
    for key, value in saved.items():
        if value is None:
            try: winreg.DeleteValue(settings, key)
            except FileNotFoundError: pass
        else: winreg.SetValueEx(settings, key, 0, value[1], value[0])
    winreg.CloseKey(settings)
    assert (hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None) == original_hash
    (report/'diagnostics-fault.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
print(json.dumps(results, indent=2), flush=True)
