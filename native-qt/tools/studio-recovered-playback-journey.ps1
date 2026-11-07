# Requires an isolated QA profile; operates the actual packaged application.
param([Parameter(Mandatory=$true)][string]$SessionFile)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/studio-ui-workflows.ps1" -SessionFile "$SessionFile" -FunctionsOnly -LeaveOpen
$record=Get-Content (Join-Path $Out 'latest-record-crash.json') -Raw|ConvertFrom-Json
$files=Get-Content (Join-Path $record.evidence 'recordings.json') -Raw|ConvertFrom-Json
$Evidence=Join-Path $Out 'recovered-playback';New-Item -ItemType Directory $Evidence|Out-Null
try{
    Focus 'Add scene';Click 'Add scene';SetText 'Scene name' 'Recovered video';Press 'OK'
    Click '+ Add source';ChooseSourceType 'Video / audio file';SetText 'Name' 'Recovered recording';SetText 'URL / file / text / color' $files.recovered;Press 'OK'
    Check (WaitFor {@((State).sources|Where-Object{$_.name -eq 'Recovered recording' -and $_.frames -gt 15 -and $_.durationMs -gt 15000}).Count -eq 1} 20) 'Recovered MP4 opens with picture and a usable duration'
    if((State).mode -eq 'studio'){Press 'Take / Cut'}
    Focus 'Playback';Click 'Playback';(Find 'Paused').GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle();Focus 'Playback position';Keys '{HOME}{RIGHT 10}'
    Check (WaitFor {@((State).sources|Where-Object{$_.name -eq 'Recovered recording' -and $_.paused -and [math]::Abs($_.positionMs-10000) -lt 1500}).Count -eq 1} 10) 'Keyboard seeking reaches ten seconds in the recovered recording'
    Shot 'seek-ten-seconds';ProgramShot 'recovered-frame-at-ten-seconds'
    (Find 'Paused').GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle()
    Check (WaitFor {@((State).sources|Where-Object{$_.name -eq 'Recovered recording' -and !$_.paused -and $_.positionMs -gt 11000 -and $_.peak -gt .01}).Count -eq 1} 8) 'Resuming the recovered recording restores moving video and audible audio'
    Press 'Close';Focus 'Chat accounts';Shot 'playing-recovered-video'
    $before=@((State).scenes.id);node "$PSScriptRoot/mcp/studio-cli.mjs" --discovery $control quit|Out-Null;if(!$app.WaitForExit(15000)){throw 'Normal restart did not close'}
    $env:VIDEO_MIXER_DATA_DIR=$Out;$app=Start-Process $Exe -PassThru -ArgumentList '--local-control-port=0',"--local-control-discovery=$control"
    $session.pid=$app.Id;$session|ConvertTo-Json|Set-Content "$SessionFile"
    if(!(WaitFor {try{$d=Get-Content $control -Raw|ConvertFrom-Json;return $d.pid -eq $app.Id -and @(Roots).Count -gt 0}catch{return $false}} 30)){throw 'Normal restart did not reopen'}
    $discovery=Get-Content $control -Raw|ConvertFrom-Json
    $prompt=try{!(Find 'Recover recording...').Current.IsOffscreen}catch{$false}
    Check (!$prompt) 'A completed recovery stays dismissed after restarting'
    Check (@(Compare-Object $before @((State).scenes.id)).Count -eq 0 -and (State).mode -eq 'studio') 'Normal restart keeps the new scene and Studio preference'
    Check (!(State).output.active -and !(State).recording.active) 'Playback and restart leave production idle'
}catch{Check $false 'Recovered playback journey completed' $_.Exception.Message}
finally{@{exe=$Exe;sha256=(Get-FileHash $Exe).Hash;checks=$checks}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $Evidence 'results.json') -Encoding utf8;Write-Output "Evidence: $Evidence"}
if(@($checks|Where-Object{!$_.ok}).Count){exit 1}
