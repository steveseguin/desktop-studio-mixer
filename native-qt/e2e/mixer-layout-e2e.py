"""Verify usable control geometry and workspace scrolling at laptop sizes."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
import win32gui
import numpy as np
reveal_workspace = False

results = {}
original = win32gui.GetWindowRect(window.handle)
original_mode = bool(control('studioMode').get_toggle_state())
try:
    window.set_focus(); window.child_window(title='Studio', control_type='TabItem').click_input()
    for enabled in (False, True):
        studio_mode(enabled)
        win32gui.MoveWindow(window.handle, 10, 10, 1366, 768, True)
        time.sleep(.5)
        scroll = control('studioWorkspaceScroll')
        scroll.wheel_mouse_input(wheel_dist=100, coords=(8, 60)); time.sleep(.3)
        viewport = scroll.rectangle()
        start = control('studioStartOutput').rectangle()
        assert viewport.top < start.top < start.bottom < viewport.bottom
        tracks = control('audioTracksScroll').rectangle()
        gain = control('masterGain').rectangle()
        master = control('masterAudioMeter').rectangle()
        headphones = control('monitorEnable').rectangle()
        assert tracks.height() >= 170, tracks
        assert tracks.bottom <= gain.top < gain.bottom <= master.top, (tracks, gain, master)
        assert master.height() == 58 and master.bottom <= headphones.top, (master, headphones)
        # The top and bottom regions remain accessible through an outlined viewport.
        mode = 'studio' if enabled else 'single'
        window.capture_as_image().save(str(report/(mode+'-top.png')))
        image = np.asarray(scroll.capture_as_image().convert('RGB'))
        parent = np.asarray(window.capture_as_image().convert('RGB'))
        border = np.sum(np.all(np.abs(image.astype(int)-(80,102,123)) < 4, axis=2)) > 100
        contrast = (np.sum(np.all(image == (16,27,39), axis=2)) > 500
                    and np.max(np.abs(parent[5,5].astype(int)-(16,27,39))) >= 8)
        assert border or contrast, 'Workspace needs a border or contrasting background'
        # Each tool panel scrolls independently in 0.5. Reveal the requested
        # control through its actual nested viewport before checking geometry.
        reveal_workspace = True
        for name in ('addSource', 'editSource', 'layout-Full', 'layerBack'):
            rect = control(name).rectangle()
            assert rect.height() >= 30, (name, rect)
            assert viewport.top < rect.top < rect.bottom < viewport.bottom, (name, rect, viewport)
            panel=control('sourcesPanelScroll').rectangle()
            assert panel.top < rect.top < rect.bottom <= panel.bottom, (name,rect,panel)
        for name in ('scenesPanelScroll','sourcesPanelScroll','audioPanelScroll','audioTracksScroll'):
            panel=control(name)
            pixels=np.asarray(panel.capture_as_image().convert('RGB')).astype(int)
            distinct=any(np.any(np.all(pixels==color,axis=2)) for color in ((80,102,123),(8,15,23),(11,20,30)))
            assert distinct, (name,'No border or distinct background')
        reveal_workspace = False
        window.capture_as_image().save(str(report/(mode+'-bottom.png')))
        results[mode+'LaptopLayout'] = 'readable meters/buttons, no overlaps, distinct scroll area reaches both ends'
        # Restore a large workspace before switching modes so its control is visible.
        win32gui.MoveWindow(window.handle, original[0], original[1], original[2]-original[0], original[3]-original[1], True)
        scroll.wheel_mouse_input(wheel_dist=100, coords=(8, 60)); time.sleep(.3)
    results['passed'] = True
finally:
    win32gui.MoveWindow(window.handle, original[0], original[1], original[2]-original[0], original[3]-original[1], True)
    studio_mode(original_mode)
    control('studioWorkspaceScroll').wheel_mouse_input(wheel_dist=100, coords=(8,60))
    (report/'layout-workflow.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    print(json.dumps(results, indent=2), flush=True)
