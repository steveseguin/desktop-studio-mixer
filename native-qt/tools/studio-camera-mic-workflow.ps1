# Local-only hardware recording. No publication, account access or audio monitoring.
param([Parameter(Mandatory=$true)][string]$Exe,[Parameter(Mandatory=$true)][string]$Out,[string]$Microphone='Microphone (Logitech BRIO)')
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $Out){throw 'Use a new isolated QA directory'}
New-Item -ItemType Directory $Out|Out-Null;$Out=(Resolve-Path $Out).Path;$Exe=(Resolve-Path $Exe).Path
$control=Join-Path $Out 'control.json';$env:VIDEO_MIXER_DATA_DIR=$Out
$owned=Start-Process $Exe -PassThru -ArgumentList "--studio-show=$Out/show.mixer.json",'--local-control-port=0',"--local-control-discovery=$control"
$deadline=(Get-Date).AddSeconds(30)
do{Start-Sleep -Milliseconds 100;$ready=try{(Get-Content -LiteralPath $control -Raw|ConvertFrom-Json).pid -eq $owned.Id}catch{$false}}while(!$ready -and (Get-Date) -lt $deadline)
if(!$ready){throw 'Owned QA application did not start'}
$sessionPath=Join-Path $Out 'ui-session.json';@{exe=$Exe;out=$Out;pid=$owned.Id;control=$control}|ConvertTo-Json|Set-Content $sessionPath
. "$PSScriptRoot/studio-ui-workflows.ps1" -SessionFile $sessionPath -FunctionsOnly -LeaveOpen
$Evidence=Join-Path $Out 'hardware';New-Item -ItemType Directory $Evidence|Out-Null
$peaks=[Collections.ArrayList]::new()
function Observe($seconds){$until=(Get-Date).AddSeconds($seconds);while((Get-Date) -lt $until){$mic=@((State).sources|Where-Object{$_.kind -eq 'microphone'})[0];[void]$peaks.Add(@{utc=[DateTimeOffset]::UtcNow.ToString('o');peak=$mic.peak});Start-Sleep -Milliseconds 200}}
try{
    if(!(WaitFor {try{return !(Find '+ Add source').Current.IsOffscreen}catch{return $false}} 30)){throw 'QA window did not appear'}
    Resize 1280 900;Focus 'Add scene';Click 'Add scene';SetText 'Scene name' 'Camera recording';Press 'OK'
    Click '+ Add source';ChooseSourceType 'Camera / webcam';SetText 'Name' 'BRIO camera';Press 'Refresh devices';Shot 'camera-picker';Press 'OK'
    Check (WaitFor {@((State).sources|Where-Object{$_.kind -eq 'camera' -and $_.frames -gt 30 -and !$_.error}).Count -eq 1} 30) 'The connected camera produces real picture frames'
    ProgramShot 'camera-before';Shot 'camera-workspace'
    Focus 'Reconnect';Press 'Reconnect'
    Check (WaitFor {@((State).sources|Where-Object{$_.kind -eq 'camera' -and $_.frames -gt 30 -and !$_.error}).Count -eq 1} 30) 'Reconnect reopens the camera successfully'
    Focus 'Camera settings...';Click 'Camera settings...'
    Check (WaitFor {try{return !(Find 'Capture mode' ([Windows.Automation.ControlType]::ComboBox)).Current.IsOffscreen}catch{return $false}} 15) 'Camera capture mode has an accessible name'
    $cameraControls=@(foreach($root in Roots){$root.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)|Where-Object{$_.Current.AutomationId -match 'camera(Slider_|Value_|Auto_|Reset_)' -and $_.Current.ControlType -in @([Windows.Automation.ControlType]::Slider,[Windows.Automation.ControlType]::Spinner,[Windows.Automation.ControlType]::CheckBox,[Windows.Automation.ControlType]::Button)}})
    $names=@($cameraControls|ForEach-Object{$_.Current.Name})
    Check ($names.Count -gt 8 -and @($names|Where-Object{[string]::IsNullOrWhiteSpace($_)}).Count -eq 0) 'Physical camera adjustment controls expose meaningful accessible names'
    Check (@($names|Group-Object|Where-Object Count -gt 1).Count -eq 0) 'Auto and Reset controls identify which camera setting they affect'
    (Find 'Capture mode' ([Windows.Automation.ControlType]::ComboBox)).SetFocus();Keys '{TAB}';Check ([Windows.Automation.AutomationElement]::FocusedElement.Current.Name -eq 'Apply capture mode') 'Keyboard navigation reaches Apply capture mode from its named picker'
    Shot 'camera-settings-accessible';Keys '{ESC}'
    Check (WaitFor {try{return (Find 'Capture mode' ([Windows.Automation.ControlType]::ComboBox)).Current.IsOffscreen}catch{return $true}}) 'Escape closes camera settings and returns to the workspace'
    Focus '+ Add source';Click '+ Add source';ChooseSourceType 'Microphone / audio input';SetText 'Name' 'BRIO microphone'
    $combo=Find 'Device / window' ([Windows.Automation.ControlType]::ComboBox);$combo.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern).Expand()
    if(!(WaitFor {try{$null=Find $Microphone ([Windows.Automation.ControlType]::ListItem);return $true}catch{return $false}})){throw 'Named microphone is not available'}
    Click $Microphone ([Windows.Automation.ControlType]::ListItem);Shot 'microphone-picker';Press 'OK'
    Check (WaitFor {@((State).sources|Where-Object{$_.kind -eq 'microphone' -and $_.has_audio -and !$_.error}).Count -eq 1} 20) 'The explicitly selected physical microphone starts capture'
    Check (!(State).output.active) 'Hardware capture remains local with no stream active'
    $file=Join-Path $Evidence 'camera-and-microphone.mkv';Focus 'Record...';Click 'Record...';SetText 'File name:' $file;Click 'Save' ([Windows.Automation.ControlType]::Button)
    if(!(WaitFor {(State).recording.active} 20)){throw 'Hardware recording failed to start'}
    $timer=[Diagnostics.Stopwatch]::StartNew();Observe 15
    Focus 'Mute mics';Press 'Mute mics';$muted=$timer.Elapsed.TotalSeconds;Observe 5;Shot 'microphone-muted'
    Press 'Unmute mics';$unmuted=$timer.Elapsed.TotalSeconds;Observe 15
    Focus 'Stop recording';Shot 'hardware-recording';ProgramShot 'camera-after';Press 'Stop recording'
    Check (WaitFor {!(State).recording.busy -and (State).recording.status -like 'Recording saved:*'} 30) 'Camera and microphone recording finalizes successfully'
    @{file=$file;mutedAt=$muted;unmutedAt=$unmuted;sourcePeaks=$peaks;sources=(State).sources}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $Evidence 'capture.json')
    $max=($peaks|ForEach-Object{[double]$_.peak}|Measure-Object -Maximum).Maximum
    Check ($max -gt .0001) 'Physical microphone delivers nonzero room audio' ('peak='+$max)
    Press 'Mute mics';Click 'Add scene';SetText 'Scene name' 'Review camera recording';Press 'OK'
    Click '+ Add source';ChooseSourceType 'Video / audio file';SetText 'Name' 'Recorded camera';SetText 'URL / file / text / color' $file;Press 'OK'
    Check (WaitFor {@((State).sources|Where-Object{$_.name -eq 'Recorded camera' -and $_.frames -gt 20 -and $_.durationMs -gt 30000}).Count -eq 1} 20) 'The actual camera recording plays back with a usable duration'
    Shot 'review-recording';ProgramShot 'review-program'
    Check (!(State).output.active -and !(State).recording.active) 'Review leaves production idle'
    @{file=$file;mutedAt=$muted;unmutedAt=$unmuted;sourcePeaks=$peaks;sources=(State).sources}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $Evidence 'capture.json')
}catch{Check $false 'Hardware journey completes' $_.Exception.Message}
finally{
    if((State).recording.active){Focus 'Stop recording';Press 'Stop recording';[void](WaitFor {!(State).recording.busy} 30)}
    @{exe=$Exe;sha256=(Get-FileHash $Exe).Hash;checks=$checks}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $Evidence 'results.json') -Encoding utf8
    node "$PSScriptRoot/mcp/studio-cli.mjs" --discovery $control quit|Out-Null
    "Evidence: $Evidence"
}
if(@($checks|Where-Object{!$_.ok}).Count){exit 1}
