# Record a continuously moving reference through the packaged application's UI.
param([Parameter(Mandatory=$true)][string]$Exe,[Parameter(Mandatory=$true)][string]$Out,[Parameter(Mandatory=$true)][string]$ReferenceFile,[int]$Seconds=180,[switch]$Chat,[switch]$Live,[ValidatePattern('^[a-zA-Z0-9_]+$')][string]$TwitchChannel='esl_dota2',[ValidateRange(0,3)][int]$BackgroundClips=0,[ValidateSet(30,60)][int]$OutputFps=30,[string]$ReuseProfile)
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $Out){throw 'Use a fresh isolated profile'}
New-Item -ItemType Directory $Out|Out-Null;$Out=(Resolve-Path $Out).Path;$Exe=(Resolve-Path $Exe).Path
$dataRoot=$Out
if($ReuseProfile){
    # Reuse only disposable browser data to avoid another large QA cache. The
    # show, recordings and evidence still belong to the fresh Out directory.
    $dataRoot=(Resolve-Path -LiteralPath $ReuseProfile).Path
    if($dataRoot -notmatch '[\\/]build[^\\/]*[\\/]'){throw 'ReuseProfile must be an isolated build-directory profile'}
    $chatSettings=Get-Content -LiteralPath (Join-Path $dataRoot 'chat.json') -Raw|ConvertFrom-Json
    if(@($chatSettings.channels).Count -or $chatSettings.practice){throw 'Disconnect QA channels and stop practice through the UI before reusing this profile'}
}
$control=Join-Path $Out 'control.json';$env:VIDEO_MIXER_DATA_DIR=$dataRoot
$owned=Start-Process $Exe -PassThru -ArgumentList "--studio-show=$Out/show.mixer.json",'--local-control-port=0',"--local-control-discovery=$control"
$deadline=(Get-Date).AddSeconds(30)
do{Start-Sleep -Milliseconds 100;$owned.Refresh();$ready=try{(Get-Content $control -Raw|ConvertFrom-Json).pid -eq $owned.Id -and $owned.MainWindowHandle -ne [IntPtr]::Zero}catch{$false}}while(!$ready -and (Get-Date) -lt $deadline)
if(!$ready){throw 'Owned app did not appear'}
$sessionFile=Join-Path $Out 'ui-session.json';@{exe=$Exe;out=$Out;pid=$owned.Id;control=$control}|ConvertTo-Json|Set-Content $sessionFile
. "$PSScriptRoot/studio-ui-workflows.ps1" -SessionFile $sessionFile -FunctionsOnly -LeaveOpen
$Evidence=Join-Path $Out 'cadence';New-Item -ItemType Directory $Evidence|Out-Null
$metrics=Join-Path $Evidence 'metrics';New-Item -ItemType Directory $metrics|Out-Null
'recording-moving-reference'|Set-Content (Join-Path $metrics 'phase.txt')
$sampler=Start-Process python -WindowStyle Hidden -PassThru -ArgumentList "$PSScriptRoot/studio-journey-metrics.py",'--session',$sessionFile,'--output',$metrics -RedirectStandardOutput (Join-Path $metrics 'stdout.log') -RedirectStandardError (Join-Path $metrics 'stderr.log')
try{
    Resize 1280 900
    if($OutputFps -eq 60){
        Focus 'Video settings';Click 'Video settings';Focus 'Canvas format';Keys '{HOME}{DOWN 3}{ENTER}';Keys '{ESC}'
        Check (WaitFor {(State).canvas.fps -eq 60 -and (State).canvas.width -eq 1280} 10) 'Video settings select the 720p sixty-frame canvas'
    }
    Focus 'Add scene';Click 'Add scene';SetText 'Scene name' 'Smooth playback';Press 'OK'
    Click '+ Add source';ChooseSourceType 'Video / audio file';SetText 'Name' 'Moving reference';SetText 'URL / file / text / color' $ReferenceFile;Press 'OK'
    Check (WaitFor {@((State).sources|Where-Object{$_.name -eq 'Moving reference' -and $_.frames -gt 30 -and $_.peak -gt .01}).Count -eq 1} 30) 'Moving reference has picture and audio'
    for($n=0;$n -lt $BackgroundClips;$n++){
        Click 'Add scene';SetText 'Scene name' "Other clip $n";Press 'OK'
        Click '+ Add source';ChooseSourceType 'Video / audio file';SetText 'Name' "Other reference $n";SetText 'URL / file / text / color' $ReferenceFile;Press 'OK'
    }
    Click 'Smooth playback' ([Windows.Automation.ControlType]::ListItem)
    if($Chat){
        SetText 'Your channel link or name' 'https://www.youtube.com/@LofiGirl';Press 'Add this channel';Click '+ Add';SetText 'Your channel link or name' ('https://www.twitch.tv/'+$TwitchChannel);Press 'Add this channel'
        Check (WaitFor {(Status).ready -and (Status).count -gt 2} 90) 'Anonymous live comments reach the panel'
        Press 'All';SelectFirst;Press 'Show on screen';Check (WaitFor {(Status).on_screen} 15) 'A real comment is featured';Press 'Keep up'
    }
    if($Live){
        Focus 'Output settings';Press 'Output settings';$stream='studiocadence'+[guid]::NewGuid().ToString('N')
        SetText 'Stream ID or VDO.Ninja URL' $stream;SetText 'Stream password' 'false'
        Click 'Studio' ([Windows.Automation.ControlType]::TabItem);Focus 'GO LIVE';Click 'GO LIVE'
        Check (WaitFor {$ok=try{Find 'OK' ([Windows.Automation.ControlType]::Button)}catch{$null};if($ok -and !$ok.Current.IsOffscreen){Press 'OK'};return (State).output.live} 60) 'The reference is published to an unlisted VDO.Ninja stream'
        @{viewer=('https://vdo.ninja/?view='+$stream+'&password=false&autostart&cleanoutput&stereo=1')}|ConvertTo-Json|Set-Content (Join-Path $Evidence 'destination.json')
    }
    $file=Join-Path $Evidence 'recording.mkv';Focus 'Record...';Click 'Record...';SetText 'File name:' $file;Click 'Save' ([Windows.Automation.ControlType]::Button)
    Check (WaitFor {(State).recording.active} 20) 'Reference recording starts'
    $begin=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()/1000.0
    @{startedEpoch=$begin;targetSeconds=$Seconds;file=$file;exe=$Exe;dataRoot=$dataRoot;sha256=(Get-FileHash $Exe).Hash}|ConvertTo-Json|Set-Content (Join-Path $Evidence 'run.json')
    Shot 'recording';$until=(Get-Date).AddSeconds($Seconds)
    $chatSamples=[Collections.ArrayList]::new()
    while((Get-Date) -lt $until){
        if(!(State).recording.active){throw 'Recording stopped early'}
        if($Live -and !(State).output.live){throw 'Publication stopped early'}
        if((Get-PSDrive C).Free -lt 750MB){throw 'Disk guard: less than 750 MiB remains free'}
        if($Chat){$chatState=Status;$sample=@{epoch=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()/1000.0;resources=$chatState.resources;count=$chatState.count;on_screen=$chatState.on_screen;timing=$chatState.timing};[void]$chatSamples.Add($sample);$sample|ConvertTo-Json -Depth 8 -Compress|Add-Content (Join-Path $Evidence 'chat-samples.jsonl')}
        Start-Sleep -Seconds 2
    }
    Focus 'Stop recording';Press 'Stop recording';Check (WaitFor {!(State).recording.busy -and (State).recording.status -like 'Recording saved:*'} 30) 'Reference recording finalizes'
    @{file=$file;startedEpoch=$begin;finishedEpoch=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()/1000.0;recording=(State).recording}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $Evidence 'capture.json')
    Shot 'saved';ProgramShot 'saved-program'
    if($Chat){Press 'Hide'}
    if($Live){Focus 'STOP';Click 'STOP';Check (WaitFor {!(State).output.active} 30) 'Publication stops normally'}
}catch{Check $false 'Cadence journey completes' $_.Exception.Message}
finally{
    if($Chat -and $chatSamples){$chatSamples|ConvertTo-Json -Depth 8|Set-Content (Join-Path $Evidence 'chat-samples.json')}
    'complete'|Set-Content (Join-Path $metrics 'stop')
    if((State).recording.active){Api @{command='record.stop'}|Out-Null;[void](WaitFor {!(State).recording.busy} 30)}
    if((State).output.active){Api @{command='output.stop'}|Out-Null;[void](WaitFor {!(State).output.active} 30)}
    @{exe=$Exe;sha256=(Get-FileHash $Exe).Hash;checks=$checks}|ConvertTo-Json -Depth 6|Set-Content (Join-Path $Evidence 'results.json')
    node "$PSScriptRoot/mcp/studio-cli.mjs" --discovery $control quit|Out-Null
    if(!$owned.WaitForExit(15000)){throw 'Owned app did not quit'}
    "Evidence: $Evidence"
}
if(@($checks|Where-Object{!$_.ok}).Count){exit 1}
