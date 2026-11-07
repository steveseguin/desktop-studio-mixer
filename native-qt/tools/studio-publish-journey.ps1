# Requires an isolated QA profile; operates the actual packaged application.
param([Parameter(Mandatory=$true)][string]$SessionFile)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/studio-ui-workflows.ps1" -SessionFile "$SessionFile" -FunctionsOnly -LeaveOpen
$Evidence=Join-Path $Out 'live';New-Item -ItemType Directory $Evidence|Out-Null
try{
    $list=Find 'Scenes' ([Windows.Automation.ControlType]::List);$row=$list.FindFirst([Windows.Automation.TreeScope]::Children,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,'Presentation'));$row.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
    if((State).mode -eq 'studio'){Press 'Take / Cut'}
    SetText 'Your channel link or name' 'https://www.twitch.tv/gaules';Press 'Add this channel'
    Click '+ Add';SetText 'Your channel link or name' 'https://www.youtube.com/@LofiGirl';Press 'Add this channel'
    Check (WaitFor {@((Status).channels).Count -eq 2}) 'Add Twitch and YouTube through the channel dialog without sign-in'
    Check (!(Status).practice) 'The feed uses real channels, with practice disabled'
    Focus 'Output settings';Press 'Output settings'
    $stream='studiojourney'+[guid]::NewGuid().ToString('N');SetText 'Stream ID or VDO.Ninja URL' $stream;SetText 'Stream password' 'false'
    Shot 'destination-configured';Click 'Studio' ([Windows.Automation.ControlType]::TabItem);Focus 'GO LIVE';Click 'GO LIVE'
    Check (WaitFor {
        $ok=try{Find 'OK' ([Windows.Automation.ControlType]::Button)}catch{$null}
        if($ok -and !$ok.Current.IsOffscreen){Shot 'first-launch-notice';Press 'OK'}
        return (State).output.live
    } 60) 'GO LIVE connects Program to VDO.Ninja after its first-use notice'
    [IO.File]::WriteAllText((Join-Path $Out 'metrics/phase.txt'),'streaming-and-two-chat-channels')
    @{stream=$stream;viewer=('https://vdo.ninja/?view='+$stream+'&password=false&autostart&cleanoutput&stereo=1') }|ConvertTo-Json|Set-Content (Join-Path $Evidence 'destination.json') -Encoding utf8
    Shot 'live-workspace'
    (State).output|ConvertTo-Json -Depth 5|Set-Content (Join-Path $Evidence 'output-start.json') -Encoding utf8
}catch{Check $false 'Live-start journey completed' $_.Exception.Message}
finally{@{exe=$Exe;sha256=(Get-FileHash $Exe).Hash;checks=$checks}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $Evidence 'start-results.json') -Encoding utf8;Write-Output "Evidence: $Evidence"}
if(@($checks|Where-Object{!$_.ok}).Count){exit 1}
