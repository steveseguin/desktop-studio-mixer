param([string]$BuildDirectory = 'build-mixer', [switch]$Package)
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
if ($Package) {
    $packageRoot = Join-Path $PSScriptRoot 'dist/video-mixer-portable'
    New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
    Copy-Item -Path "$buildRoot/bin/*" -Destination $packageRoot -Recurse -Force
    $deploy = Join-Path $vcpkgRoot 'installed/x64-windows/tools/Qt6/bin/windeployqt.exe'
    & $deploy --release --no-translations --no-opengl-sw --compiler-runtime "$packageRoot/video-mixer.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Qt packaging failed.' }
    Copy-Item (Join-Path $PSScriptRoot '../LICENSE') $packageRoot
    Copy-Item (Join-Path $PSScriptRoot 'third_party/webview2/LICENSE.txt') (Join-Path $packageRoot 'WebView2-LICENSE.txt')
    Write-Output "Portable app: $packageRoot/video-mixer.exe"
}
