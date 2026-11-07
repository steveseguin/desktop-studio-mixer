# Requires an isolated QA profile; operates the actual packaged application.
param([Parameter(Mandatory=$true)][string]$SessionFile)
$ErrorActionPreference='Stop'
$s=Get-Content "$SessionFile" -Raw|ConvertFrom-Json
$ffmpeg=Join-Path (Split-Path $s.exe) 'ffmpeg/bin/ffmpeg.exe'
$asset=Join-Path $s.out 'journey-reference.mp4'
& $ffmpeg -hide_banner -loglevel error -f lavfi -i 'testsrc2=size=1280x720:rate=30' -f lavfi -i 'aevalsrc=if(lt(mod(t\,1)\,0.1)\,0.2*sin(2*PI*880*t)\,0):s=48000' -vf "drawbox=x=0:y=0:w=128:h=128:color=white:t=fill:enable='lt(mod(t,1),0.1)'" -t 60 -c:v mpeg4 -q:v 4 -c:a aac -b:a 128k $asset
if($LASTEXITCODE){throw 'Could not generate synchronized local fixture'}
Write-Output 'Created moving video with a white flash and 880 Hz pulse each second.'
