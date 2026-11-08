# Receive a synthetic VDO.Ninja sender through the packaged application's browser source.
param([Parameter(Mandatory=$true)][string]$Exe,[Parameter(Mandatory=$true)][string]$Out,[Parameter(Mandatory=$true)][string]$DestinationFile,[int]$Seconds=120,[ValidateSet(30,60)][int]$OutputFps=30,[switch]$ExerciseRecovery,[switch]$ExercisePlayback,[string]$ObserveUntilRunFile)
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $Out){throw 'Use a fresh isolated receiver profile'}
New-Item -ItemType Directory $Out|Out-Null;$Out=(Resolve-Path $Out).Path;$Exe=(Resolve-Path $Exe).Path
$destination=Get-Content $DestinationFile -Raw|ConvertFrom-Json
$control=Join-Path $Out 'control.json';$env:VIDEO_MIXER_DATA_DIR=$Out
$owned=Start-Process $Exe -PassThru -ArgumentList "--studio-show=$Out/show.mixer.json",'--local-control-port=0',"--local-control-discovery=$control"
$deadline=(Get-Date).AddSeconds(30)
do{Start-Sleep -Milliseconds 100;$owned.Refresh();$ready=try{(Get-Content $control -Raw|ConvertFrom-Json).pid -eq $owned.Id -and $owned.MainWindowHandle -ne [IntPtr]::Zero}catch{$false}}while(!$ready -and (Get-Date) -lt $deadline)
if(!$ready){throw 'Owned receiver did not appear'}
$sessionFile=Join-Path $Out 'ui-session.json';@{exe=$Exe;out=$Out;pid=$owned.Id;control=$control}|ConvertTo-Json|Set-Content $sessionFile
. "$PSScriptRoot/studio-ui-workflows.ps1" -SessionFile $sessionFile -FunctionsOnly -LeaveOpen
$Evidence=Join-Path $Out 'receive';New-Item -ItemType Directory $Evidence|Out-Null
$metrics=Join-Path $Evidence 'metrics';New-Item -ItemType Directory $metrics|Out-Null
'received-video'|Set-Content (Join-Path $metrics 'phase.txt')
$sampler=Start-Process python -WindowStyle Hidden -PassThru -ArgumentList "$PSScriptRoot/studio-journey-metrics.py",'--session',$sessionFile,'--output',$metrics -RedirectStandardOutput (Join-Path $metrics 'stdout.log') -RedirectStandardError (Join-Path $metrics 'stderr.log')
$events=@{}
function BrowserState { @((State).sources|Where-Object{$_.kind -eq 'browser'})[0] }
function BrowserWorker {
    $workers=@(Get-CimInstance Win32_Process|Where-Object{$_.ParentProcessId -eq $owned.Id -and $_.Name -eq 'desktop-studio.exe' -and $_.CommandLine -match '--studio-worker browser'})
    if($workers.Count -ne 1 -or $workers[0].ExecutablePath -ne $Exe){throw 'Cannot verify the owned browser worker'}
    $workers[0]
}
try{
    Resize 1280 900
    if($OutputFps -eq 60){Focus 'Video settings';Click 'Video settings';Focus 'Canvas format';Keys '{HOME}{DOWN 3}{ENTER}';Keys '{ESC}';Check ((State).canvas.fps -eq 60) 'Receiver canvas is sixty fps'}
    Focus '+ Add source';Click '+ Add source';ChooseSourceType 'Browser / remote VDO.Ninja feed';SetText 'Name' 'Received from VDO.Ninja';SetText 'URL / file / text / color' $destination.viewer
    $transparent=Find 'Transparent overlay background (up to 15 fps)';Check ($transparent.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Current.ToggleState -eq [Windows.Automation.ToggleState]::Off) 'Browser video starts in opaque video mode'
    Press 'OK';Check (WaitFor {@((State).sources|Where-Object{$_.kind -eq 'browser' -and $_.frames -gt 60}).Count -eq 1} 45) 'The independent receiver renders the viewing page'
    Check (WaitFor {@((State).sources|Where-Object{$_.kind -eq 'browser' -and $_.has_audio -and $_.peak -gt .01}).Count -eq 1} 45) 'Received video has audible pulse audio'
    @{startedEpoch=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()/1000.0}|ConvertTo-Json|Set-Content (Join-Path $Evidence 'monitor-run.json')
    Start-Sleep -Seconds 5;Shot 'receiver-workspace';ProgramShot 'received-program'
    $file=Join-Path $Evidence 'received-vdo.mkv';Focus 'Record...';Click 'Record...';SetText 'File name:' $file;Click 'Save' ([Windows.Automation.ControlType]::Button)
    Check (WaitFor {(State).recording.active} 20) 'Receiver recording starts'
    $begin=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()/1000.0;$until=(Get-Date).AddSeconds($Seconds)
    while((Get-Date) -lt $until){if(!(State).recording.active){throw 'Receiver recording stopped early'};Start-Sleep -Seconds 2}
    if($ExercisePlayback){
        $studioApp=$app;Focus 'Open / Invite';Press 'Open / Invite';$worker=BrowserWorker;$app=Get-Process -Id $worker.ProcessId
        Check (WaitFor {try{return !(Find 'Pause video' ([Windows.Automation.ControlType]::Button)).Current.IsOffscreen}catch{return $false}} 15) 'The browser source exposes its real playback controls'
        Click 'Pause video' ([Windows.Automation.ControlType]::Button);$events.pauseEpoch=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()/1000.0
        Start-Sleep -Seconds 1;ProgramShot 'paused-before';Start-Sleep -Seconds 2;ProgramShot 'paused-after'
        Check ((Get-FileHash (Join-Path $Evidence 'paused-before.png')).Hash -eq (Get-FileHash (Join-Path $Evidence 'paused-after.png')).Hash) 'Paused browser picture stays still after buffered frames drain'
        Click 'Resume video' ([Windows.Automation.ControlType]::Button);$events.resumeEpoch=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()/1000.0
        Check (WaitFor {(BrowserState).peak -gt .01} 10) 'Browser audio resumes after a pause'
        Keys '{ESC}';$app=$studioApp;Focus 'Stop recording';Start-Sleep -Seconds 12;ProgramShot 'resumed-picture'
        Check ((Get-FileHash (Join-Path $Evidence 'resumed-picture.png')).Hash -ne (Get-FileHash (Join-Path $Evidence 'paused-after.png')).Hash) 'Browser picture resumes after a pause'
    }
    if($ExerciseRecovery){
        $sourceId=(BrowserState).id;$worker=BrowserWorker;$dead=$worker.ProcessId
        $events.workerKilledEpoch=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()/1000.0
        Stop-Process -Id $dead -Force
        Check (WaitFor {(BrowserState).error -match 'Browser|reconnect|stopped'} 15) 'A failed browser worker is reported visibly'
        Shot 'worker-failed'
        Check (WaitFor {try{return (BrowserWorker).ProcessId -ne $dead -and !(BrowserState).error -and (BrowserState).frames -gt 30 -and (BrowserState).peak -gt .01}catch{return $false}} 60) 'Picture and audio recover automatically after browser-worker failure'
        $events.workerRecoveredEpoch=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()/1000.0
        Check ((State).recording.active -and (BrowserState).id -eq $sourceId) 'Browser recovery preserves recording and source identity'
        ProgramShot 'recovered-picture';$prior=(BrowserWorker).ProcessId;Focus 'Reconnect';Press 'Reconnect'
        Check (WaitFor {try{return (BrowserWorker).ProcessId -ne $prior -and !(BrowserState).error -and (BrowserState).frames -gt 30 -and (BrowserState).peak -gt .01}catch{return $false}} 60) 'The visible Reconnect action restores picture and audio'
        $events.reconnectedEpoch=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()/1000.0
        Start-Sleep -Seconds 15
    }
    Focus 'Stop recording';Press 'Stop recording';Check (WaitFor {!(State).recording.busy -and (State).recording.status -like 'Recording saved:*'} 30) 'Received recording finalizes'
    @{file=$file;startedEpoch=$begin;finishedEpoch=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()/1000.0;events=$events;recording=(State).recording;sources=(State).sources}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $Evidence 'capture.json')
    Shot 'receiver-saved';ProgramShot 'received-final'
    if($ObserveUntilRunFile){
        # Keep the same receiver and one-second sampler connected throughout
        # the sender's long run, saving another five-minute sample at its end.
        $senderRun=Get-Content -LiteralPath $ObserveUntilRunFile -Raw|ConvertFrom-Json
        $senderEnd=$senderRun.startedEpoch+$senderRun.targetSeconds
        if($senderEnd-[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()/1000.0 -lt 330){throw 'Not enough sender time remains for the final received sample'}
        while([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()/1000.0 -lt $senderEnd-300){
            if($owned.HasExited){throw 'The monitored receiver exited early'}
            Start-Sleep -Seconds 2
        }
        $file=Join-Path $Evidence 'received-end.mkv';Focus 'Record...';Click 'Record...';SetText 'File name:' $file;Click 'Save' ([Windows.Automation.ControlType]::Button)
        Check (WaitFor {(State).recording.active} 20) 'The final received sample starts recording'
        $begin=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()/1000.0
        while([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()/1000.0 -lt $senderEnd-10){if(!(State).recording.active){throw 'The final received sample stopped early'};Start-Sleep -Seconds 2}
        Focus 'Stop recording';Press 'Stop recording';Check (WaitFor {!(State).recording.busy -and (State).recording.status -like 'Recording saved:*'} 30) 'The final received sample finalizes'
        @{file=$file;startedEpoch=$begin;finishedEpoch=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()/1000.0;recording=(State).recording;sources=(State).sources}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $Evidence 'capture-end.json')
        Shot 'receiver-end-saved';ProgramShot 'received-end-program'
    }
}catch{Check $false 'Receiver journey completes' $_.Exception.Message}
finally{
    'complete'|Set-Content (Join-Path $metrics 'stop')
    if((State).recording.active){Api @{command='record.stop'}|Out-Null;[void](WaitFor {!(State).recording.busy} 30)}
    @{exe=$Exe;sha256=(Get-FileHash $Exe).Hash;checks=$checks}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $Evidence 'results.json')
    node "$PSScriptRoot/mcp/studio-cli.mjs" --discovery $control quit|Out-Null
    if(!$owned.WaitForExit(15000)){throw 'Receiver did not quit normally'}
    "Evidence: $Evidence"
}
if(@($checks|Where-Object{!$_.ok}).Count){exit 1}
