# Requires an isolated QA profile; operates the actual packaged application.
param([Parameter(Mandatory=$true)][string]$SessionFile)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/studio-ui-workflows.ps1" -SessionFile "$SessionFile" -FunctionsOnly -LeaveOpen
$Evidence=Join-Path $Out ('record-crash-'+(Get-Date -Format yyyyMMdd-HHmmss));New-Item -ItemType Directory $Evidence|Out-Null
$metrics=Join-Path $Out 'metrics'
function Phase($name){[IO.File]::WriteAllText((Join-Path $metrics 'phase.txt'),$name)}
function StartRecording($path){Click 'Record...';if(!(WaitFor {try{$null=Find 'File name:' ([Windows.Automation.ControlType]::Edit);return $true}catch{return $false}})){throw 'No recording file dialog'};SetText 'File name:' $path;Click 'Save' ([Windows.Automation.ControlType]::Button);if(!(WaitFor {(State).recording.active} 20)){throw 'Recording did not start'}}
try{
    $list=Find 'Scenes' ([Windows.Automation.ControlType]::List);$row=$list.FindFirst([Windows.Automation.TreeScope]::Children,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,'Presentation'));$row.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select();if((State).mode -eq 'studio'){Press 'Take / Cut'}
    $normal=Join-Path $Evidence 'normal.mkv';StartRecording $normal;Phase 'recording-local';Shot 'recording'
    $activeShow=(State).project_path;$save=(Find 'Save a copy').Current.BoundingRectangle
    [void][StudioUI]::SetCursorPos([int]($save.Right-6),[int]($save.Y+$save.Height/2));[StudioUI]::mouse_event(2,0,0,0,[UIntPtr]::Zero);[StudioUI]::mouse_event(4,0,0,0,[UIntPtr]::Zero);Start-Sleep -Milliseconds 250
    Click 'Save As active show...' ([Windows.Automation.ControlType]::MenuItem);SetText 'File name:' (Join-Path $Evidence 'switch-during-recording.mixer.json');Click 'Save' ([Windows.Automation.ControlType]::Button)
    Check ((State).project_path -eq $activeShow -and (State).recording.active) 'Changing the active show cannot detach a running recording from its recovery journal'
    Check (!(Find 'Stop recording before changing the active show. Use Save a copy while recording.').Current.IsOffscreen) 'The active-show guard explains the available Save a copy route'
    Start-Sleep -Seconds 20
    Press 'Stop recording';Check (WaitFor {!(State).recording.busy} 30) 'A normal recording stops and finalizes';Phase 'media-preview';Start-Sleep -Seconds 2
    $crashed=Join-Path $Evidence 'interrupted.mkv';StartRecording $crashed;Phase 'recording-before-crash';Start-Sleep -Seconds 22;Shot 'before-forced-termination'
    $before=State;$sceneIds=@($before.scenes.id);$sourceIds=@($before.sources.id)
    if($app.Path -ne $Exe){throw 'Refusing to terminate an unexpected process'}
    $children=@(Get-CimInstance Win32_Process|Where-Object{$_.ParentProcessId -eq $app.Id}|Select-Object -ExpandProperty ProcessId)
    Phase 'forced-termination';Stop-Process -Id $app.Id -Force;[void]$app.WaitForExit(15000)
    Check (Test-Path -LiteralPath $crashed) 'Interrupted recording remains on disk after forced application termination'
    Start-Sleep -Seconds 3
    $remaining=@($children|Where-Object{Get-Process -Id $_ -ErrorAction SilentlyContinue});Check (!$remaining.Count) 'Owned workers close after their application is terminated'
    $env:VIDEO_MIXER_DATA_DIR=$Out;$app=Start-Process $Exe -PassThru -ArgumentList '--local-control-port=0',"--local-control-discovery=$control"
    $session.pid=$app.Id;$session|ConvertTo-Json|Set-Content "$SessionFile"
    if(!(WaitFor {try{$current=Get-Content $control -Raw|ConvertFrom-Json;return $current.pid -eq $app.Id -and @(Roots).Count -gt 0}catch{return $false}} 30)){throw 'Relaunch failed'}
    $discovery=Get-Content $control -Raw|ConvertFrom-Json;$after=State
    Check (@(Compare-Object $sceneIds @($after.scenes.id)).Count -eq 0 -and @(Compare-Object $sourceIds @($after.sources.id)).Count -eq 0) 'Relaunch restores the autosaved scenes and sources'
    Check (!$after.recording.active -and !$after.output.active) 'Relaunch does not restart recording or streaming unexpectedly'
    Check ($after.mode -eq 'studio') 'Studio mode survives abrupt termination in the isolated normal-launch profile'
    Focus 'Tools';Shot 'after-relaunch';Phase 'after-crash-relaunch'
    Check (!(Find 'Recover recording...').Current.IsOffscreen) 'Restart visibly offers recovery of the interrupted recording'
    Click 'Recover recording...';Click 'Cancel' ([Windows.Automation.ControlType]::Button)
    Check (!(Find 'Recover recording...').Current.IsOffscreen) 'Cancelling recovery keeps the recovery prompt'
    $originalHash=(Get-FileHash $crashed).Hash
    Click 'Recover recording...'
    $recovered=Join-Path $Evidence 'recovered.mp4';SetText 'File name:' $recovered;Click 'Save' ([Windows.Automation.ControlType]::Button)
    Check (WaitFor {(Test-Path $recovered) -and !(State).recording.exporting} 30) 'Recover recording exports the interrupted MKV to MP4'
    Check ((State).recording.export_status -notmatch 'failed|error') 'Recovery export completes without an error';Shot 'recovery-exported'
    $recoveryVisible=try{!(Find 'Recover recording...').Current.IsOffscreen}catch{$false}
    Check (!$recoveryVisible) 'Successful recovery clears the prompt'
    Check ((Get-FileHash $crashed).Hash -eq $originalHash) 'Recovering preserves the original interrupted MKV byte for byte'
    @{normal=$normal;interrupted=$crashed;recovered=$recovered;before=$before.recording;after=(State).recording}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $Evidence 'recordings.json') -Encoding utf8
}catch{Check $false 'Record/crash/recovery journey completed' $_.Exception.Message}
finally{@{exe=$Exe;sha256=(Get-FileHash $Exe).Hash;checks=$checks}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $Evidence 'results.json') -Encoding utf8;@{evidence=$Evidence}|ConvertTo-Json|Set-Content (Join-Path $Out 'latest-record-crash.json') -Encoding utf8;Write-Output "Evidence: $Evidence"}
if(@($checks|Where-Object{!$_.ok}).Count){exit 1}
