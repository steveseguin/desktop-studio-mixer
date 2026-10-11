param([string]$BuildDirectory = 'build-mixer', [switch]$Package, [switch]$Installer)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'tools/restore-webview2.ps1')
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsRoot) { throw 'Visual Studio C++ build tools are required.' }
$buildRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot $BuildDirectory))
if (!$buildRoot.StartsWith(([IO.Path]::GetFullPath($PSScriptRoot) + [IO.Path]::DirectorySeparatorChar), [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The build directory must be inside native-qt.'
}
$configureArgs = @('-S', $PSScriptRoot, '-B', $buildRoot, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DVERSUS_BUILD_TESTS=OFF')
$vcpkgRoot = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { 'C:/vcpkg' }
if (Test-Path "$vcpkgRoot/scripts/buildsystems/vcpkg.cmake") {
    $configureArgs += "-DCMAKE_TOOLCHAIN_FILE=$vcpkgRoot/scripts/buildsystems/vcpkg.cmake"
    $configureArgs += '-DVCPKG_TARGET_TRIPLET=x64-windows'
}
foreach ($dependency in @('mbedtls', 'libdatachannel', 'spdlog', 'cxxopts', 'opus', 'spout2')) {
    $cachedSource = Join-Path $PSScriptRoot "third_party/cache/$dependency-src"
    if (Test-Path $cachedSource) { $configureArgs += "-DFETCHCONTENT_SOURCE_DIR_$($dependency.ToUpperInvariant())=$cachedSource" }
}
# Import the compiler environment; no file operations cross shells.
$compilerEnv = & cmd /d /s /c "`"$vsRoot/Common7/Tools/VsDevCmd.bat`" -arch=amd64 >nul && set"
foreach ($entry in $compilerEnv) {
    if ($entry -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw 'Configure failed.' }
& cmake --build $buildRoot --target video-mixer --parallel 8
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
if ($Package -or $Installer) {
    $packageRoot = Join-Path $PSScriptRoot 'dist/desktop-studio-portable'
    New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
    Get-ChildItem -LiteralPath "$buildRoot/bin" | Where-Object { $_.Name -ne "video-mixer.exe" } | Copy-Item -Destination $packageRoot -Recurse -Force
    $deploy = Join-Path $vcpkgRoot 'installed/x64-windows/tools/Qt6/bin/windeployqt.exe'
    & $deploy --release --no-translations --no-opengl-sw --compiler-runtime "$packageRoot/desktop-studio.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Qt packaging failed.' }
    # App-local VC runtime keeps the portable package and per-user setup usable
    # without an administrator installing the machine-wide redistributable.
    $crt = Join-Path $env:VCToolsRedistDir 'x64/Microsoft.VC143.CRT'
    if (!(Test-Path $crt)) { throw 'Cannot locate the Visual C++ x64 redistributable DLLs.' }
    Copy-Item -Path (Join-Path $crt '*.dll') -Destination $packageRoot -Force
    Copy-Item (Join-Path $PSScriptRoot '../LICENSE') $packageRoot
    Copy-Item (Join-Path $PSScriptRoot '../README.md') $packageRoot
    Copy-Item (Join-Path $PSScriptRoot 'resources/vdoninja.ico') $packageRoot
    $packageDocs = Join-Path $packageRoot 'docs'
    New-Item -ItemType Directory -Force -Path $packageDocs | Out-Null
    Copy-Item -Path (Join-Path $PSScriptRoot '../docs/mixer*.md') -Destination $packageDocs -Force
    Copy-Item (Join-Path $PSScriptRoot '../docs/game-capture-base.md') $packageDocs -Force
    Copy-Item (Join-Path $PSScriptRoot 'third_party/webview2/LICENSE.txt') (Join-Path $packageRoot 'WebView2-LICENSE.txt')
    Write-Output "Portable app: $packageRoot/desktop-studio.exe"
    $project = Get-Content (Join-Path $PSScriptRoot 'CMakeLists.txt') -Raw
    if ($project -notmatch 'project\(video-mixer-native-qt VERSION ([0-9.]+)') { throw 'Cannot read app version.' }
    $appVersion = $matches[1]
    $zipPath = Join-Path $PSScriptRoot "dist/desktop-studio-$appVersion-windows-x64.zip"
    & tar.exe -a -c -f $zipPath -C (Join-Path $PSScriptRoot 'dist') desktop-studio-portable
    if ($LASTEXITCODE -ne 0) { throw 'Portable archive failed.' }
    if ($Installer) {
        $nsis = "${env:ProgramFiles(x86)}/NSIS/makensis.exe"
        if (!(Test-Path $nsis)) { throw 'Install NSIS 3 to build the installer.' }
        $setupPath = Join-Path $PSScriptRoot "dist/desktop-studio-$appVersion-setup.exe"
        & $nsis /V2 "/DVERSION=$appVersion" "/DBUILD_BIN_DIR=$packageRoot" "/DOUTFILE=$setupPath" (Join-Path $PSScriptRoot 'installer-mixer.nsi')
        if ($LASTEXITCODE -ne 0) { throw 'Installer packaging failed.' }
        Write-Output "Installer: $setupPath"
    }
}
