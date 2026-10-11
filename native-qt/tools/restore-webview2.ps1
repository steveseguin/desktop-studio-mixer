param([ValidatePattern('^\d+\.\d+\.\d+\.\d+$')][string]$Version = '1.0.2903.40')
$ErrorActionPreference = 'Stop'
$sdkRoot = Join-Path $PSScriptRoot '../third_party/webview2'
if (Test-Path (Join-Path $sdkRoot 'build/native/include/WebView2.h')) { return }
New-Item -ItemType Directory -Force -Path $sdkRoot | Out-Null
$sdkArchive = Join-Path $sdkRoot 'sdk.zip'
Invoke-WebRequest -UseBasicParsing -Uri "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/$Version/microsoft.web.webview2.$Version.nupkg" -OutFile $sdkArchive
Expand-Archive -LiteralPath $sdkArchive -DestinationPath $sdkRoot -Force
Remove-Item -LiteralPath $sdkArchive
Write-Output "Restored WebView2 SDK $Version. The Evergreen Runtime is installed separately."
