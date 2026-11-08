# Actual packaged recording while the user minimizes or covers Studio.
param([Parameter(Mandatory=$true)][string]$SessionFile,[int]$PhaseSeconds=45)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/studio-ui-workflows.ps1" -SessionFile $SessionFile -FunctionsOnly -LeaveOpen
if((State).recording.busy -or (State).output.active){throw 'Use an idle isolated QA application'}
if(@((State).sources|Where-Object{$_.frames -gt 30 -and $_.peak -gt .01}).Count -eq 0){throw 'Select a scene with a continuously moving picture and pulse audio first'}
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class BackgroundStudio {
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int cmd);
 [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
}
'@
$Evidence=Join-Path $Out ('background-recording-'+(Get-Date -Format yyyyMMdd-HHmmss));New-Item -ItemType Directory $Evidence|Out-Null
$handle=$app.MainWindowHandle;$cover=$null;$phases=[Collections.ArrayList]::new()
function ObservePhase($name){
    Start-Sleep -Seconds 2
    $before=(State).recording;$until=(Get-Date).AddSeconds($PhaseSeconds)
    while((Get-Date) -lt $until){[Windows.Forms.Application]::DoEvents();Start-Sleep -Milliseconds 100}
    $after=(State).recording
    $phase=@{name=$name;seconds=$PhaseSeconds;repeated=$after.repeated_frames-$before.repeated_frames;dropped=$after.dropped_frames-$before.dropped_frames;active=$after.active}
    [void]$phases.Add($phase)
    Check ($after.active) "$name keeps recording active"
    Check ($phase.repeated -eq 0) "$name introduces no repeated recorder frames during the observed window" ($phase|ConvertTo-Json -Compress)
}
try{
    Resize 1280 900;Focus 'Record...'
    $file=Join-Path $Evidence 'background-recording.mkv';Click 'Record...';SetText 'File name:' $file;Click 'Save' ([Windows.Automation.ControlType]::Button)
    Check (WaitFor {(State).recording.active} 15) 'The recording starts through the visible UI'
    ObservePhase 'Visible Studio';Shot 'visible-recording'
    [void][BackgroundStudio]::ShowWindow($handle,6)
    Check ([BackgroundStudio]::IsIconic($handle)) 'Studio is actually minimized'
    ObservePhase 'Minimized Studio'
    [void][BackgroundStudio]::ShowWindow($handle,9);Focus 'Stop recording'
    $bounds=[StudioUI+Rect]::new();[void][StudioUI]::GetWindowRect($handle,[ref]$bounds)
    $cover=[Windows.Forms.Form]::new();$cover.Text='Background recording check';$cover.StartPosition='Manual';$cover.Location=[Drawing.Point]::new($bounds.left,$bounds.top);$cover.Size=[Drawing.Size]::new($bounds.right-$bounds.left,$bounds.bottom-$bounds.top);$cover.BackColor=[Drawing.Color]::DarkSlateGray;$cover.Show();$cover.Activate();$cover.Refresh()
    ObservePhase 'Studio covered by another window'
    $cover.Close();$cover.Dispose();$cover=$null;Focus 'Stop recording';Shot 'returned-to-recording'
    ObservePhase 'Restored Studio'
    Press 'Stop recording';Check (WaitFor {!(State).recording.busy -and (State).recording.status -like 'Recording saved:*'} 30) 'The background recording finalizes normally'
    Shot 'saved';ProgramShot 'saved-program'
}catch{Check $false 'Background recording journey completes' $_.Exception.Message}
finally{
    if($cover){$cover.Close();$cover.Dispose()}
    [void][BackgroundStudio]::ShowWindow($handle,9)
    if((State).recording.active){Api @{command='record.stop'}|Out-Null;[void](WaitFor {!(State).recording.busy} 30)}
    @{exe=$Exe;sha256=(Get-FileHash $Exe).Hash;checks=$checks;phases=$phases}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $Evidence 'results.json')
    "Evidence: $Evidence"
}
if(@($checks|Where-Object{!$_.ok}).Count){exit 1}
