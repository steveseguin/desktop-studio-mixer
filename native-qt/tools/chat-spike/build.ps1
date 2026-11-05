$ErrorActionPreference = 'Stop'
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsRoot) { throw 'Visual Studio C++ build tools required' }
$env:Path = (Split-Path $vswhere) + ';' + $env:Path
$compilerEnv = & cmd /d /s /c "`"$vsRoot/VC/Auxiliary/Build/vcvars64.bat`" >nul && set"
foreach ($entry in $compilerEnv) {
    if ($entry -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$buildRoot = Join-Path $PSScriptRoot 'build'
& cmake -S $PSScriptRoot -B $buildRoot -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
if ($LASTEXITCODE) { throw 'Configure failed' }
& cmake --build $buildRoot --parallel 4
if ($LASTEXITCODE) { throw 'Build failed' }
& C:/vcpkg/installed/x64-windows/tools/Qt6/bin/windeployqt.exe --release --no-translations --compiler-runtime "$buildRoot/chat-spike.exe"
if ($LASTEXITCODE) { throw 'Qt deployment failed' }
