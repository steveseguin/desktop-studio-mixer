# Requires an isolated QA profile; operates the actual packaged application.
param([Parameter(Mandatory=$true)][string]$SessionFile)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/studio-ui-workflows.ps1" -SessionFile "$SessionFile" -FunctionsOnly -LeaveOpen
$Evidence=Join-Path $Out 'create-show';New-Item -ItemType Directory $Evidence|Out-Null
function SelectScene($name){$list=Find 'Scenes' ([Windows.Automation.ControlType]::List);$row=$list.FindFirst([Windows.Automation.TreeScope]::Children,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,$name));$row.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select();Start-Sleep -Milliseconds 250}
try{
    Focus '+ Add source';Click '+ Add source'
    ChooseSourceType 'Camera / webcam'
    Check (!(Find 'No camera found. Connect or enable a camera, then choose Refresh devices.').Current.IsOffscreen) 'Missing camera is explained before attempting to add it'
    SetText 'Name' 'My camera';Press 'Refresh devices'
    Check ((Find 'Name' ([Windows.Automation.ControlType]::Edit)).GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).Current.Value -eq 'My camera') 'Refreshing devices preserves the entered source name'
    ChooseSourceType 'Camera / webcam';Shot 'camera-no-device';Press 'OK'
    Check (WaitFor {try{return !(Find 'No source found. Connect a device or open a window first.').Current.IsOffscreen}catch{return $false}}) 'Missing camera explains failure without closing the dialog'
    Shot 'camera-error';Press 'Cancel';Check (@((State).sources|Where-Object{$_.kind -eq 'camera'}).Count -eq 0) 'Missing camera creates no broken source'
    Click 'Add scene';SetText 'Scene name' 'Presentation';Press 'OK'
    Check (@((State).scenes|Where-Object{$_.name -eq 'Presentation'}).Count -eq 1) 'Create a Presentation scene'
    Click '+ Add source';ChooseSourceType 'Video / audio file';SetText 'Name' 'Moving picture and synchronized pulse';SetText 'URL / file / text / color' (Join-Path $Out 'journey-reference.mp4');Press 'OK'
    Check (WaitFor {@((State).sources|Where-Object{$_.kind -eq 'media' -and $_.frames -gt 20 -and $_.has_audio}).Count -eq 1} 20) 'Imported video produces real picture frames and audio'
    Check (WaitFor {@((State).sources|Where-Object{$_.kind -eq 'media' -and $_.peak -gt .01}).Count -eq 1} 5) 'Audio meter responds to the source pulse'
    ProgramShot 'presentation-program';Shot 'presentation-workspace'
    Click 'Add scene';SetText 'Scene name' 'Closing';Press 'OK'
    Click '+ Add source';ChooseSourceType 'Title / slate';SetText 'Name' 'Thanks';SetText 'URL / file / text / color' 'Thanks for watching';Press 'OK'
    Check (@((State).scenes|Where-Object{$_.name -eq 'Closing'}).Count -eq 1) 'Create a Closing scene with a title'
    SelectScene 'Opening';(Find 'Studio mode').GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle();$before=(State).program
    SelectScene 'Presentation';Check ((State).preview -ne $before -and (State).program -eq $before) 'Stage the presentation privately in Studio mode';Shot 'preview-before-take'
    Press 'Take / Cut';Check ((State).program -eq (State).preview) 'Take sends the presentation to Program';Shot 'taken-to-program'
    Check (!(Status).practice -and @((Status).channels).Count -eq 0) 'Fresh profile starts without practice or signed-in chat channels'
    Check (!(State).output.active -and !(State).recording.active) 'Creating the show starts no output'
}catch{Check $false 'Create-show journey completed' $_.Exception.Message}
finally{@{exe=$Exe;sha256=(Get-FileHash $Exe).Hash;checks=$checks}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $Evidence 'results.json') -Encoding utf8;Write-Output "Evidence: $Evidence"}
if(@($checks|Where-Object{!$_.ok}).Count){exit 1}
