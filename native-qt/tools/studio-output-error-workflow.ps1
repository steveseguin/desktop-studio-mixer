param([Parameter(Mandatory=$true)][string]$SessionFile)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/studio-ui-workflows.ps1" -SessionFile $SessionFile -FunctionsOnly -LeaveOpen
$Evidence=Join-Path $Out ('output-error-'+(Get-Date -Format yyyyMMdd-HHmmss));New-Item -ItemType Directory $Evidence|Out-Null
function ChooseDestination($label){$combo=Find 'Output destination' ([Windows.Automation.ControlType]::ComboBox);$combo.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern).Expand();if(!(WaitFor {try{$null=Find $label ([Windows.Automation.ControlType]::ListItem);return $true}catch{return $false}})){throw 'Output destination list did not open'};Click $label ([Windows.Automation.ControlType]::ListItem)}
try{
    if((State).output.active -or (State).recording.active){throw 'Preserving active output'}
    # An isolated --studio-show launch starts with default output settings.
    if((State).output.protocol -ne 'vdo'){throw 'This walkthrough requires a disposable profile with the default output'}
    [void][StudioUI]::SetForegroundWindow([IntPtr](@(Roots)[0].Current.NativeWindowHandle));Keys '{ESC}';Click 'Studio' ([Windows.Automation.ControlType]::TabItem);Resize 1280 720;Focus 'Output settings';Press 'Output settings'
    ChooseDestination 'RTMP / RTMPS';SetText 'Output URL' 'not-a-stream-url'
    Click 'Studio' ([Windows.Automation.ControlType]::TabItem);Focus 'GO LIVE';Click 'GO LIVE'
    if((Status).practice){$yes=try{Find 'Yes' ([Windows.Automation.ControlType]::Button)}catch{$null};if($yes){Click 'Yes' ([Windows.Automation.ControlType]::Button)}}
    Check (WaitFor {(State).output.state -eq 'error'}) 'Malformed output URL produces an error without connecting'
    $label=@(foreach($root in Roots){$root.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)|Where-Object{$_.Current.AutomationId.EndsWith('.studioOutputStatus')}})[0]
    Check (!$label.Current.IsOffscreen -and $label.Current.Name.Contains('complete output URL')) 'The compact command bar keeps the full failure visible'
    Check (!(State).output.active -and !(State).output.starting -and !(State).recording.active) 'Rejected settings start no stream or recording';Shot 'visible-error'
    Focus 'Output settings';Press 'Output settings';ChooseDestination 'VDO.Ninja (default)';Click 'Studio' ([Windows.Automation.ControlType]::TabItem)
    Check (!(Find 'Output settings').Current.IsOffscreen) 'Output settings stays reachable after the error'
    Click 'Language' ([Windows.Automation.ControlType]::MenuItem);Click 'English' ([Windows.Automation.ControlType]::MenuItem)
    Check (WaitFor {!$label.Current.IsOffscreen -and $label.Current.Name.Contains('Language saved')}) 'Explicit settings confirmations remain visible in the compact bar';Shot 'settings-confirmation'
}catch{Check $false 'Output error walkthrough completed' $_.Exception.Message}
finally{@{exe=$Exe;sha256=(Get-FileHash $Exe).Hash;checks=$checks}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $Evidence 'results.json') -Encoding utf8;"Evidence: $Evidence"}
if(@($checks|Where-Object{!$_.ok}).Count){exit 1}
