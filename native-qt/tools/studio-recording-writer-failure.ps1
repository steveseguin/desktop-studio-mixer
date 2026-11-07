# Run only with a new disposable profile. Terminates that app's recording muxer.
param([Parameter(Mandatory=$true)][string]$Exe,[Parameter(Mandatory=$true)][string]$Out,[Parameter(Mandatory=$true)][string]$ReferenceFile)
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $Out){throw 'Use a new QA directory'}
New-Item -ItemType Directory $Out|Out-Null;$Out=(Resolve-Path $Out).Path;$Exe=(Resolve-Path $Exe).Path
$control=Join-Path $Out 'control.json';$env:VIDEO_MIXER_DATA_DIR=$Out
$owned=Start-Process $Exe -PassThru -ArgumentList "--studio-show=$Out/show.mixer.json",'--local-control-port=0',"--local-control-discovery=$control"
$deadline=(Get-Date).AddSeconds(30)
do{Start-Sleep -Milliseconds 100;$ready=try{(Get-Content -LiteralPath $control -Raw|ConvertFrom-Json).pid -eq $owned.Id}catch{$false}}while(!$ready -and (Get-Date) -lt $deadline)
if(!$ready){throw 'Owned QA application did not start'}
$sessionPath=Join-Path $Out 'ui-session.json';@{exe=$Exe;out=$Out;pid=$owned.Id;control=$control}|ConvertTo-Json|Set-Content $sessionPath
. "$PSScriptRoot/studio-ui-workflows.ps1" -SessionFile $sessionPath -FunctionsOnly -LeaveOpen
$Evidence=Join-Path $Out 'writer-failure';New-Item -ItemType Directory $Evidence|Out-Null
function RecordFile($file){Focus 'Record...';Click 'Record...';SetText 'File name:' $file;Click 'Save' ([Windows.Automation.ControlType]::Button);if(!(WaitFor {(State).recording.active} 20)){throw 'Recording did not start'}}
try{
    if(!(WaitFor {try{return !(Find '+ Add source').Current.IsOffscreen}catch{return $false}} 30)){throw 'Owned QA window did not become usable'}
    Resize 1280 900;Focus 'Add scene';Click 'Add scene';SetText 'Scene name' 'Recording failure';Press 'OK'
    Click '+ Add source';ChooseSourceType 'Video / audio file';SetText 'Name' 'Moving reference';SetText 'URL / file / text / color' $ReferenceFile;Press 'OK'
    Check (WaitFor {@((State).sources|Where-Object{$_.kind -eq 'media' -and $_.frames -gt 20 -and $_.peak -gt .01}).Count -eq 1} 20) 'Real picture and audio are available before the writer fails'
    $file=Join-Path $Evidence 'interrupted-writer.mkv';RecordFile $file;Start-Sleep -Seconds 18
    $processes=Get-CimInstance Win32_Process;$ownedIds=@($app.Id)
    for($depth=0;$depth -lt 5;$depth++){$ownedIds+=@($processes|Where-Object{$_.ParentProcessId -in $ownedIds}|Select-Object -ExpandProperty ProcessId);$ownedIds=@($ownedIds|Select-Object -Unique)}
    $writer=$processes|Where-Object{$_.ProcessId -in $ownedIds -and $_.Name -eq 'ffmpeg.exe' -and $_.CommandLine.Replace('\','/').Contains($file.Replace('\','/'))}|Select-Object -First 1
    if(!$writer){throw 'Owned recording writer could not be verified'}
    $before=(State).sources|Where-Object{$_.kind -eq 'media'}|Select-Object -First 1
    Stop-Process -Id $writer.ProcessId -Force
    Check (WaitFor {!(State).recording.busy} 30) 'Studio detects the failed writer and leaves recording mode'
    Check ((State).recording.status -notmatch '^Recording saved' -and (State).recording.status -match 'error|failed|exit|stopped') 'A failed recording is not reported as successfully saved' (State).recording.status
    Check (WaitFor {try{return !(Find 'Recover recording...').Current.IsOffscreen}catch{return $false}} 10) 'The failed recording exposes a visible recovery action'
    Check (@((State).sources|Where-Object{$_.kind -eq 'media' -and $_.frames -gt $before.frames}).Count -eq 1) 'Program keeps receiving moving picture after its writer fails'
    Shot 'writer-failure';ProgramShot 'program-after-failure'
    $hash=(Get-FileHash -LiteralPath $file).Hash;$recovered=Join-Path $Evidence 'recovered.mp4'
    Click 'Recover recording...';SetText 'File name:' $recovered;Click 'Save' ([Windows.Automation.ControlType]::Button)
    Check (WaitFor {(Test-Path -LiteralPath $recovered) -and !(State).recording.exporting} 30) 'Recover the failed writer file to MP4 through the interface'
    Check ((State).recording.export_status -like 'MP4 saved:*') 'Recovery reports successful export'
    Check ((Get-FileHash -LiteralPath $file).Hash -eq $hash) 'Recovery preserves the interrupted original byte for byte'
    $prompt=try{!(Find 'Recover recording...').Current.IsOffscreen}catch{$false};Check (!$prompt) 'Successful export clears the recovery reminder'
    Shot 'recovered'
    RecordFile (Join-Path $Evidence 'next-recording.mkv');Start-Sleep -Seconds 10;Press 'Stop recording'
    Check (WaitFor {!(State).recording.busy -and (State).recording.status -like 'Recording saved:*'} 30) 'A new recording succeeds after the writer failure without restarting Studio'
}catch{Check $false 'Writer-failure journey completes' $_.Exception.Message}
finally{
    if((State).recording.active){Focus 'Stop recording';Press 'Stop recording';[void](WaitFor {!(State).recording.busy} 30)}
    @{exe=$Exe;sha256=(Get-FileHash $Exe).Hash;checks=$checks}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $Evidence 'results.json') -Encoding utf8
    node "$PSScriptRoot/mcp/studio-cli.mjs" --discovery $control quit|Out-Null
    "Evidence: $Evidence"
}
if(@($checks|Where-Object{!$_.ok}).Count){exit 1}
