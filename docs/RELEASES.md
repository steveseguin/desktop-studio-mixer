# Desktop Studio Windows releases

Use Studio's packaging workflow below. `native-qt/qa/release-and-publish.ps1`
and `native-qt/qa/build-release.ps1` are inherited Game Capture tooling;
they do not create or publish Desktop Studio releases.

## Prepare and freeze

1. Update the version in `native-qt/CMakeLists.txt` and
   `native-qt/installer-mixer.nsi`, then commit the intended source changes.
2. Validate the pinned LGPL FFmpeg bundle, its manifest, runtime DLLs, notices,
   and corresponding-source information. Never bundle a research GPL/nonfree
   FFmpeg executable.
3. From the repository root, run locally:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File native-qt/build-mixer.ps1 -Installer
   ```

Outputs are `native-qt/dist/desktop-studio-portable`, the versioned Windows ZIP,
and the versioned setup EXE. Use a clean staging directory; the copy allowlist
excludes developer test executables and Qt6Test.dll but does not remove stale
files from an existing stage. Preserve previous versioned artifacts and user data.

The application is signed before archiving; setup is signed separately. Require
the expected signer and timestamp and reject missing or mismatched signatures.
Record any local certificate-chain trust limitation. Never print signing secrets.
Record the source commit and final package hashes before qualification, and do
not modify a qualified payload afterwards.

## Qualify the exact signed package

Builds and unit checks are gates. Release testing means using the packaged app
and verifying the actual media output. Run workflows sequentially when comparing
CPU/GPU measurements, retain failed attempts and focused reruns, and restore user
preferences afterwards.

- Check fresh defaults, saved settings, and persistence after restart.
- Publish real moving video and audio using Auto, available NVIDIA/AMD/Intel
  encoders, and software. Include landscape, portrait, and concurrent recording.
- Verify CLI/API/MCP control, headless screenshots, browser audio/transparency,
  and recovery after browser or publishing-worker failure.
- Install into an isolated QA directory, compare installed/portable identity,
  publish and record, then uninstall. Never replace an existing user installation.

Useful runners in `native-qt/e2e` include `mixer-automation-e2e.py`,
`mixer-encoder-defaults-e2e.py`, `mixer-amd-codecs-e2e.py`,
`mixer-fault-quality-e2e.py`, and `mixer-installed-e2e.py`. Pass explicit package
and report paths where supported. Run Python runners from `native-qt`.
Record hardware, source/package hashes, results, and limitations in versioned
release notes. Keep credentials and unsanitized recordings/logs private.

## Publish

Create a draft `v<version>` release in `steveseguin/desktop-studio-mixer`, targeting
the frozen source commit. Upload these versioned files and identical stable aliases:

| Versioned file | Stable alias |
| --- | --- |
| `desktop-studio-<version>-setup.exe` | `desktop-studio-setup.exe` |
| `desktop-studio-<version>-windows-x64.zip` | `desktop-studio-windows-x64.zip` |
| `desktop-studio-<version>-ffmpeg-source-info.zip` | `desktop-studio-ffmpeg-source-info.zip` |

Include `SHA256SUMS.txt`, release notes, a validation report link, and the encoder
guide link. Verify every uploaded file's SHA-256 and size against the local
artifact and confirm stable/versioned identity before publishing as latest.
Verify `releases/latest/download/...` links afterwards.

The website is served from `main:/root`; `index.html` is its landing page.
Keep download links on stable aliases. Update release-sensitive guide wording
without relabeling historical measurements as results from the new binaries.
Build and qualify locally; no hosted build or additional GitHub Actions run is
required for this workflow.
