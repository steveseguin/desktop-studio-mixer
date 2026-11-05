param([Parameter(Mandatory=$true)][string]$Destination, [string]$Source = $env:SSN_ROOT)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (!$Source) { $Source = Join-Path $repo '../social_stream' }
$Source = [IO.Path]::GetFullPath($Source)
$Destination = [IO.Path]::GetFullPath($Destination)
if (!$Destination.StartsWith($repo + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or (Split-Path $Destination -Leaf) -ne 'ssn') { throw 'SSN destination must be an ssn folder inside this worktree.' }
if (!(Test-Path -LiteralPath (Join-Path $Source 'manifest.json'))) {
    if (Test-Path -LiteralPath $Destination) { Remove-Item -LiteralPath $Destination -Recurse -Force }
    Write-Warning "Chat is unavailable: SSN pack source is missing. Set SSN_ROOT to a Social Stream Ninja checkout and rebuild. Expected: $Source"
    return
}
$manifest = Get-Content -LiteralPath (Join-Path $Source 'manifest.json') -Raw | ConvertFrom-Json
$files = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$queue = [Collections.Generic.Queue[string]]::new()
function Add-Asset([string]$relative) {
    $relative = ($relative -split '[?#]')[0].Replace('\','/')
    if ($relative -match '(^|/)thirdparty/(kokoro|kitten|neural-tts|piper|espeak|transformers|ort[.-]|tf[.-]|metadata\.json|model\.json)') { return }
    if (!$relative -or $relative -match '(^|/)(docs?|tests?|node_modules|\.git)/' -or $relative -match '://|^data:') { return }
    $full = [IO.Path]::GetFullPath((Join-Path $Source $relative))
    if (!$full.StartsWith($Source + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or !(Test-Path -LiteralPath $full -PathType Leaf)) { return }
    $relative = $full.Substring($Source.Length+1).Replace('\','/')
    if ($files.Add($relative)) { $queue.Enqueue($relative) }
}
foreach ($name in @('manifest.json','background.html','featured.html','loader.js','LICENSE','LICENSE.txt','LICENCE','favicon.ico','translations/en.json','translations/es.json','translations/fr.json')) { Add-Asset $name }
foreach ($entry in $manifest.content_scripts) { foreach ($name in $entry.js) { Add-Asset $name } }
# Follow static local dependencies, including loader.js's ordered module list.
while ($queue.Count) {
    $relative = $queue.Dequeue()
    if ([IO.Path]::GetExtension($relative) -notin @('.html','.js','.css')) { continue }
    $content = [IO.File]::ReadAllText((Join-Path $Source $relative))
    $pattern = '["''(]((?:\./|\.\./)?[a-zA-Z0-9_./-]+\.(?:js|css|json|png|svg|webp|ico|woff2?)(?:\?[^"'')\s]*)?)["'')]'
    foreach ($match in [regex]::Matches($content,$pattern)) {
        $asset = ($match.Groups[1].Value -split '\?')[0]
        $parent = Split-Path $relative -Parent
        if ($parent) { Add-Asset (Join-Path $parent $asset) }
        Add-Asset $asset
    }
}
foreach ($license in Get-ChildItem -LiteralPath $Source -File | Where-Object Name -Match '^(LICENSE|LICENCE|COPYING|NOTICE)') { Add-Asset $license.Name }
if (!(($files | Where-Object { $_ -match '^(LICENSE|LICENCE|COPYING)' }).Count)) { throw 'SSN licence is missing; refusing to package chat.' }
if (Test-Path -LiteralPath $Destination) { Remove-Item -LiteralPath $Destination -Recurse -Force }
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
$hashes = @()
foreach ($relative in ($files | Sort-Object)) {
    $target = Join-Path $Destination $relative
    New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $Source $relative) -Destination $target
    $hashes += "$((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant())  $relative"
}
[IO.File]::WriteAllLines((Join-Path $Destination 'SHA256SUMS'),$hashes,[Text.UTF8Encoding]::new($false))
$bytes = (Get-ChildItem -LiteralPath $Destination -Recurse -File | Measure-Object Length -Sum).Sum
@{ version=$manifest.version; files=$files.Count; bytes=$bytes; manifestSha256=(Get-FileHash -LiteralPath (Join-Path $Destination 'manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant() } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Destination 'pack-version.json') -Encoding UTF8
Write-Output ('SSN pack: {0} files, {1:N2} MiB, version {2}' -f $files.Count,($bytes/1MB),$manifest.version)
