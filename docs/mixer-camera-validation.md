# Video Mixer 0.3.0 - camera controls

Date: 2026-10-11. Windows x64 build 26200, physical Logitech HD Pro Webcam C920, Release portable app.

Executable SHA-256: `e6dfe1b502c25340bd2473bc30c37018d3ca43a75f45a9e303b1f989272e85be`.

## Use

Add a **Camera / webcam** source, select it, and choose **Camera settings...**. The panel has a live source preview, the camera's reported capture modes, image controls, and the actual negotiated capture mode. Choose **Apply capture mode** to restart that source in a new mode. The mixer output stays 1280x720/30.

Manual sliders and numeric fields follow driver ranges and step sizes. Auto toggles are enabled only for controls that support both automatic and manual operation. Reset uses the individual control's driver default and automatic mode when supported. Unsupported controls are omitted. Changes apply immediately to the physical device, including Program, and may affect other applications using it.

Only settings explicitly changed in the panel become saved overrides. They and the requested capture mode persist in the show and are reapplied on reconnect or show reload. Choosing a different physical camera clears these overrides. Opening the panel alone does not write to the device.

## Hardware reported by the C920

| Control | Driver range | Auto/manual selection |
| --- | --- | --- |
| Zoom | 100-500 | Manual |
| Exposure | -11 to -2 | Both |
| Focus | 0-250 | Both |
| Brightness / contrast / saturation / sharpness / gain | 0-255 | Manual |
| White balance | 2000-6500 | Both |
| Backlight compensation | 0-1 | Manual |
| Pan / tilt | -10 to 10 | Manual |

Values are driver units. A reported control does not imply a motorized camera. Gamma and hue were not exposed by this device. Exposure and lighting can reduce delivered frame rate below the requested capture rate.

## Actual application workflows

The packaged executable was operated through Windows UI Automation while capturing the physical C920:

- Brightness 64 versus 192 changed the actual preview's mean RGB intensity from 29.56 to 115.56. Reset returned the driver value to 128.
- Zoom 100 to 150 was read back as 150 and changed the actual image, with mean absolute pixel difference 50.97 on the 0-255 scale.
- Manual exposure -6, focus 25, and white balance 4500 were accepted and read back. Each returned successfully to Auto.
- Switched between 1920x1080/30, 640x480/15, and 1280x720/30 while recording. Decoded 42 one-second sample frames from the resulting H.264 recording; output stayed 1280x720 at 30 fps throughout.
- Brightness 176 and zoom 150 survived reconnect and a complete application restart, with values read back in the reopened panel.
- Saved 640x480 at approximately 7.5 fps, restarted the app, verified that nondefault mode and a live image, then returned to 1280x720/30. Fractional frame rates retain the driver's native ratio rather than rounding to whole fps.
- Restored original brightness/zoom and the original Auto modes for exposure, focus, and white balance. Removed the temporary source, restored the starter scene, and quit normally to release the camera.

The driver calls run on the existing Media Foundation capture thread. GUI writes are queued, validated against current ranges/capabilities, and read back after application. Capture remains Media Foundation; the source exposes Windows camera/image control interfaces. See Microsoft's [camera control range contract](https://learn.microsoft.com/en-us/windows/win32/api/strmif/nf-strmif-iamcameracontrol-getrange) and [Media Foundation camera-control example](https://github.com/microsoft/psi/blob/master/Sources/Media/Microsoft.Psi.Media_Interop.Windows.x64/MediaCaptureDevice.cpp).

## Reproduce and limits

Install `native-qt/e2e/mixer-requirements.txt`, connect a C920, and launch the packaged app against a disposable starter show. From `native-qt`:

```powershell
python -B e2e/mixer-camera-e2e.py --pid <app-process-id> --report e2e/reports/mixer-camera
```

The helper opens the physical camera, changes its controls, records locally, and restarts the portable app. It restores the exercised controls' original manual values or Auto mode at the end. Reports and images remain in the ignored report folder; recordings can contain private camera imagery. Build/syntax/diff gates supplement these workflows and are not substitutes for application testing.

This validates one C920 and its installed driver. Other camera models, virtual cameras without hardware controls, physical hot-unplug, Windows 10, high-DPI displays, and driver-specific failures remain unverified. The earlier transport, guest, monitoring, and chat results are in [mixer-0.2-validation.md](mixer-0.2-validation.md); those broader workflows were not all repeated for this camera-specific change.
