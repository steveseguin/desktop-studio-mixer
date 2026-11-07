# Visible transport controls and scene changes during a real local recording.
param([Parameter(Mandatory=$true)][string]$Exe,[Parameter(Mandatory=$true)][string]$Out,[Parameter(Mandatory=$true)][string]$ReferenceFile)
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $Out){throw 'Use a fresh isolated profile'}
New-Item -ItemType Directory $Out|Out-Null;$Out=(Resolve-Path $Out).Path;$Exe=(Resolve-Path $Exe).Path
$control=Join-Path $Out 'control.json';$env:VIDEO_MIXER_DATA_DIR=$Out
$owned=Start-Process $Exe -PassThru -ArgumentList "--studio-show=$Out/show.mixer.json",'--local-control-port=0',"--local-control-discovery=$control"
$deadline=(Get-Date).AddSeconds(30)
do{Start-Sleep -Milliseconds 100;$owned.Refresh();$ready=try{(Get-Content $control -Raw|ConvertFrom-Json).pid -eq $owned.Id -and $owned.MainWindowHandle -ne [IntPtr]::Zero}catch{$false}}while(!$ready -and (Get-Date) -lt $deadline)
if(!$ready){throw 'Owned app did not appear'}
$sessionFile=Join-Path $Out 'ui-session.json';@{exe=$Exe;out=$Out;pid=$owned.Id;control=$control}|ConvertTo-Json|Set-Content $sessionFile
. "$PSScriptRoot/studio-ui-workflows.ps1" -SessionFile $sessionFile -FunctionsOnly -LeaveOpen
$Evidence=Join-Path $Out 'transport';New-Item -ItemType Directory $Evidence|Out-Null
$events=@{};$timer=[Diagnostics.Stopwatch]::new()
function ClipState { @((State).sources|Where-Object {$_.name -eq 'Moving reference'})[0] }
function TogglePause { (Find 'Paused' ([Windows.Automation.ControlType]::CheckBox)).GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle() }
try{
    Resize 1280 900;Focus 'Add scene';Click 'Add scene';SetText 'Scene name' 'Playback controls';Press 'OK'
    Click '+ Add source';ChooseSourceType 'Video / audio file';SetText 'Name' 'Moving reference';SetText 'URL / file / text / color' $ReferenceFile;Press 'OK'
    Check (WaitFor {(ClipState).frames -gt 30 -and (ClipState).peak -gt .01} 30) 'The file produces picture and pulse audio'
    Check ([math]::Abs((ClipState).durationMs-60000) -lt 1000) 'The journey uses a sixty-second reference'
    $file=Join-Path $Evidence 'transport.mkv';Focus 'Record...';Click 'Record...';SetText 'File name:' $file;Click 'Save' ([Windows.Automation.ControlType]::Button)
    Check (WaitFor {(State).recording.active} 20) 'Transport recording starts';$timer.Start();Start-Sleep -Seconds 10
    Focus 'Playback';Click 'Playback';TogglePause;$events.pausedAt=$timer.Elapsed.TotalSeconds
    Check (WaitFor {(ClipState).paused} 5) 'Pause holds playback'
    Start-Sleep -Milliseconds 300;ProgramShot 'paused-before';Start-Sleep -Seconds 3;ProgramShot 'paused-after'
    Check ((Get-FileHash (Join-Path $Evidence 'paused-before.png')).Hash -eq (Get-FileHash (Join-Path $Evidence 'paused-after.png')).Hash) 'Paused Program retains the same picture'
    Focus 'Playback position';Keys '{HOME}{RIGHT 10}'
    Check (WaitFor {(ClipState).paused -and [math]::Abs((ClipState).positionMs-10000) -lt 200} 10) 'Seek while paused reaches the requested position'
    Start-Sleep -Milliseconds 500;ProgramShot 'seek-ten';Shot 'seek-controls'
    Check ((Get-FileHash (Join-Path $Evidence 'seek-ten.png')).Hash -ne (Get-FileHash (Join-Path $Evidence 'paused-before.png')).Hash) 'Paused seeking replaces the old Program picture'
    TogglePause;$events.resumedAt=$timer.Elapsed.TotalSeconds
    Check (WaitFor {!(ClipState).paused -and (ClipState).positionMs -gt 11000 -and (ClipState).peak -gt .01} 10) 'Resume restores motion and audio';Start-Sleep -Seconds 10
    Focus 'Playback position';Keys '{END}{LEFT 5}';$events.loopCueAt=$timer.Elapsed.TotalSeconds
    Check (WaitFor {(ClipState).positionMs -gt 54000} 8) 'Seek during playback reaches the end of the file'
    Check (WaitFor {(ClipState).positionMs -lt 12000 -and !(ClipState).paused -and (ClipState).peak -gt .01} 12) 'Playback loops with picture and audio';Start-Sleep -Seconds 8
    Keys '{ESC}';Focus 'Reconnect';Press 'Reconnect';$events.reconnectedAt=$timer.Elapsed.TotalSeconds
    Check (WaitFor {(ClipState).frames -gt 30 -and (ClipState).peak -gt .01 -and (ClipState).positionMs -lt 10000} 20) 'Reconnect reopens the file without stale queued pictures';Start-Sleep -Seconds 8
    Click 'Break' ([Windows.Automation.ControlType]::ListItem);$events.slateAt=$timer.Elapsed.TotalSeconds;Start-Sleep -Seconds 3;ProgramShot 'break-scene'
    Click 'Playback controls' ([Windows.Automation.ControlType]::ListItem);$events.returnedAt=$timer.Elapsed.TotalSeconds;Start-Sleep -Seconds 8;ProgramShot 'returned-scene'
    Check ((State).recording.active -and !(State).output.active) 'Transport and scene changes preserve local recording'
    Focus 'Stop recording';Press 'Stop recording';Check (WaitFor {!(State).recording.busy -and (State).recording.status -like 'Recording saved:*'} 30) 'Transport recording finalizes'
    @{file=$file;events=$events;seconds=$timer.Elapsed.TotalSeconds}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $Evidence 'capture.json');Shot 'saved'
}catch{Check $false 'Transport journey completes' $_.Exception.Message}
finally{
    if((State).recording.active){Api @{command='record.stop'}|Out-Null;[void](WaitFor {!(State).recording.busy} 30)}
    @{exe=$Exe;sha256=(Get-FileHash $Exe).Hash;checks=$checks}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $Evidence 'results.json')
    node "$PSScriptRoot/mcp/studio-cli.mjs" --discovery $control quit|Out-Null
    if(!$owned.WaitForExit(15000)){throw 'Owned app did not quit'}
    "Evidence: $Evidence"
}
if(@($checks|Where-Object{!$_.ok}).Count){exit 1}
