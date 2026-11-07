# Requires an isolated QA profile; operates the actual packaged application.
param([Parameter(Mandatory=$true)][string]$SessionFile)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/studio-ui-workflows.ps1" -SessionFile "$SessionFile" -FunctionsOnly -LeaveOpen
$Evidence=Join-Path $Out 'desktop-audio';New-Item -ItemType Directory $Evidence|Out-Null;$player=$null
$ffmpeg=Join-Path (Split-Path $Exe) 'ffmpeg/bin/ffmpeg.exe'
try{
    if((State).output.active -or (State).recording.active){throw 'Stop the live journey before checking desktop audio'}
    Focus 'Chat accounts'
    foreach($source in @((State).sources|Where-Object{$_.has_audio -and !$_.muted})){
        (Find ($source.name+' - Mute') ([Windows.Automation.ControlType]::CheckBox)).GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle()
    }
    Focus '+ Add source';Click '+ Add source';ChooseSourceType 'Desktop audio';SetText 'Name' 'Windows desktop audio';Press 'OK'
    $wave=Join-Path $Evidence 'quiet-tone.wav'
    & $ffmpeg -hide_banner -loglevel error -f lavfi -i 'sine=frequency=660:sample_rate=48000' -af 'volume=0.12' -t 10 -c:a pcm_s16le $wave
    if($LASTEXITCODE){throw 'Tone fixture failed'}
    $player=Start-Process powershell -WindowStyle Hidden -PassThru -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File',"$PSScriptRoot/studio-journey-tone.ps1",'-Path',$wave -RedirectStandardOutput (Join-Path $Evidence 'player-stdout.txt') -RedirectStandardError (Join-Path $Evidence 'player-stderr.txt')
    Check (WaitFor {@((State).sources|Where-Object{$_.name -eq 'Windows desktop audio' -and $_.has_audio -and $_.peak -gt .001}).Count -eq 1} 20) 'Windows desktop audio captures sound from another application'
    $file=Join-Path $Evidence 'desktop-audio.mkv';Focus 'Record...';Click 'Record...';SetText 'File name:' $file;Click 'Save' ([Windows.Automation.ControlType]::Button)
    if(!(WaitFor {(State).recording.active} 20)){throw 'Audio recording did not start'}
    [IO.File]::WriteAllText((Join-Path $Out 'metrics/phase.txt'),'desktop-audio-recording')
    $clock=[Diagnostics.Stopwatch]::StartNew();Start-Sleep -Seconds 6
    $mute=Find 'Windows desktop audio - Mute' ([Windows.Automation.ControlType]::CheckBox);$mute.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle();$mutedAt=$clock.Elapsed.TotalSeconds;Start-Sleep -Seconds 5
    $mute.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle();$unmutedAt=$clock.Elapsed.TotalSeconds;Start-Sleep -Seconds 5
    Focus 'Stop recording';Shot 'audio-recording';Press 'Stop recording';Check (WaitFor {!(State).recording.busy} 30) 'Desktop-audio recording finalizes'
    & $ffmpeg -v error -i $file -f null - 2> (Join-Path $Evidence 'decode-errors.txt');Check ($LASTEXITCODE -eq 0) 'Desktop-audio recording decodes completely'
    foreach($segment in @(@{name='audible';time=2},@{name='muted';time=$mutedAt+1},@{name='restored';time=$unmutedAt+1})){
        $start=$segment.time.ToString('F2',[Globalization.CultureInfo]::InvariantCulture);$log=Join-Path $Evidence ($segment.name+'-volume.txt')
        $measure=Start-Process $ffmpeg -WindowStyle Hidden -Wait -PassThru -ArgumentList '-hide_banner','-nostats','-ss',$start,'-i',$file,'-t','2','-vn','-af','volumedetect','-f','null','-' -RedirectStandardError $log -RedirectStandardOutput (Join-Path $Evidence ($segment.name+'-stdout.txt'))
        $text=Get-Content $log -Raw;if($measure.ExitCode -ne 0 -or $text -notmatch 'max_volume: (-?inf|[-\d.]+) dB'){throw 'Could not measure captured desktop audio'}
        $db=if($Matches[1] -eq '-inf'){[double]::NegativeInfinity}else{[double]::Parse($Matches[1],[Globalization.CultureInfo]::InvariantCulture)}
        Check ($(if($segment.name -eq 'muted'){$db -lt -65}else{$db -gt -60})) ($segment.name+' desktop audio is correct in the saved file') "$db dB"
    }
}catch{Check $false 'Desktop-audio journey completed' $_.Exception.Message}
finally{
    if($player -and !$player.HasExited){Stop-Process -Id $player.Id -Force}
    try{if((State).recording.active){Press 'Stop recording';[void](WaitFor {!(State).recording.busy} 30)}}catch{}
    @{exe=$Exe;sha256=(Get-FileHash $Exe).Hash;checks=$checks}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $Evidence 'results.json') -Encoding utf8
    Write-Output "Evidence: $Evidence"
}
if(@($checks|Where-Object{!$_.ok}).Count){exit 1}
