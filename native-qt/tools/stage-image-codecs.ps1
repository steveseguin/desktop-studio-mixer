# Deploy the native chat avatar decoder with its non-Qt runtime dependencies.
param([Parameter(Mandatory=$true)][string]$VcpkgRoot,[Parameter(Mandatory=$true)][string]$PackageRoot)
$ErrorActionPreference='Stop'
$root=(Resolve-Path -LiteralPath $PackageRoot).Path
$installed=Join-Path $VcpkgRoot 'installed/x64-windows'
$webp=Join-Path $installed 'Qt6/plugins/imageformats/qwebp.dll'
if(!(Test-Path -LiteralPath $webp)){throw 'WebP avatars require qtimageformats[core,webp]:x64-windows in vcpkg before packaging.'}
function CopyCodecFile([string]$Source,[string]$Relative){
    $target=[IO.Path]::GetFullPath((Join-Path $root $Relative))
    if(!$target.StartsWith($root.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Codec destination escapes the package'}
    if(!(Test-Path -LiteralPath $Source)){throw "Missing image decoder dependency: $Source"}
    New-Item -ItemType Directory -Path (Split-Path $target) -Force|Out-Null
    if(Test-Path -LiteralPath $target){
        if((Get-FileHash -LiteralPath $Source).Hash -eq (Get-FileHash -LiteralPath $target).Hash){return}
        # Diagnostic packages can hard-link shared runtimes. Replace this file's
        # link, never overwrite bytes belonging to another qualified package.
        Remove-Item -LiteralPath $target
    }
    Copy-Item -LiteralPath $Source -Destination $target
}
CopyCodecFile $webp 'imageformats/qwebp.dll'
$libraries=@(Get-ChildItem -LiteralPath (Join-Path $installed 'bin') -File|Where-Object{$_.Name -match '^(lib)?(webp|sharpyuv).*\.dll$'})
if(!$libraries.Count){throw 'Missing libwebp runtime DLLs'}
foreach($file in $libraries){CopyCodecFile $file.FullName $file.Name}
CopyCodecFile (Join-Path $installed 'share/qtimageformats/copyright') 'licenses/qtimageformats.txt'
CopyCodecFile (Join-Path $installed 'share/libwebp/copyright') 'licenses/libwebp.txt'
"Staged WebP avatar support and $($libraries.Count) runtime libraries."
