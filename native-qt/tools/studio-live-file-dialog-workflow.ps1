# Exercise the packaged file chooser while real recording/publication continue.
# Analyze the saved local and received media to qualify uninterrupted picture/audio.
param([Parameter(Mandatory=$true)][string]$SessionFile,[switch]$OtherPickers,[switch]$RecordingOnly)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/studio-ui-workflows.ps1" -SessionFile $SessionFile -FunctionsOnly -LeaveOpen
$Evidence=Join-Path $Out ('live-file-dialog-'+(Get-Date -Format yyyyMMdd-HHmmss));New-Item -ItemType Directory $Evidence|Out-Null
try{
 $before=State
 if(!$before.recording.active -or ($RecordingOnly -and $before.output.active) -or (!$RecordingOnly -and !$before.output.live)){throw 'Use an isolated synthetic recording with the requested output state'}
 function ProductionPreserved {$s=State;return $s.recording.active -and $s.output.active -eq $before.output.active -and $s.output.live -eq $before.output.live -and $s.project_path -eq $before.project_path}
 $opened=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()/1000.0
 Focus 'Save a copy';Click 'Save a copy'
 Check (WaitFor {try{return !(Find 'File name:' ([Windows.Automation.ControlType]::Edit)).Current.IsOffscreen}catch{return $false}} 10) 'Save a copy opens its file chooser during live recording'
 Start-Sleep -Seconds 8;Shot 'file-dialog'
 $copy=Join-Path $Evidence 'copied-show.mixer.json';SetText 'File name:' $copy;Click 'Save' ([Windows.Automation.ControlType]::Button)
 $closed=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()/1000.0
 Check (Test-Path -LiteralPath $copy) 'The chosen show copy is saved'
 Check (ProductionPreserved) 'Saving a copy preserves the active show, recording and output state'
 if($OtherPickers){
  $sources=@((State).sources.id)
  Focus '+ Add source';Click '+ Add source';ChooseSourceType 'Video / audio file';Click 'Choose file / color'
  Check (WaitFor {try{return !(Find 'File name:' ([Windows.Automation.ControlType]::Edit)).Current.IsOffscreen -and !(Find 'Open' ([Windows.Automation.ControlType]::Button)).Current.IsOffscreen}catch{return $false}} 10) 'Adding a source opens its file browser during live recording'
  Start-Sleep -Seconds 8;Shot 'source-file-browser';Keys '{ESC}';Keys '{ESC}'
  Check (@(Compare-Object $sources @((State).sources.id)).Count -eq 0) 'Cancelling source browsing creates no source'
  Focus 'Save a copy';$menu=(Find 'Save a copy').Current.BoundingRectangle
  [void][StudioUI]::SetCursorPos([int]($menu.Right-6),[int]($menu.Y+$menu.Height/2));[StudioUI]::mouse_event(2,0,0,0,[UIntPtr]::Zero);[StudioUI]::mouse_event(4,0,0,0,[UIntPtr]::Zero);Start-Sleep -Milliseconds 250
  Click 'Relink missing assets from folder...' ([Windows.Automation.ControlType]::MenuItem)
  # Qt exposes this folder action as Open to UI Automation, although its
  # painted label is Choose. The directory editor identifies this dialog.
  Check (WaitFor {try{return !(Find 'Directory:' ([Windows.Automation.ControlType]::Edit)).Current.IsOffscreen}catch{return $false}} 10) 'Relink opens its directory chooser during live recording'
  Start-Sleep -Seconds 8;Shot 'directory-chooser';Keys '{ESC}'
  Check (ProductionPreserved) 'Cancelling both browsers preserves recording, output state and show'
 }
 Focus 'Chat accounts';Shot 'returned'
 @{openedEpoch=$opened;closedEpoch=$closed;copy=$copy}|ConvertTo-Json|Set-Content "$Evidence/dialog-window.json"
}catch{Check $false 'Live file-dialog workflow completes' $_.Exception.Message}
@{exe=$Exe;sha256=(Get-FileHash $Exe).Hash;checks=$checks;meaning='UI state checks only; inspect both saved recordings for freezes and audio gaps during the dialog.'}|ConvertTo-Json -Depth 8|Set-Content "$Evidence/results.json"
"Evidence: $Evidence"
if(@($checks|Where-Object{!$_.ok}).Count){exit 1}
