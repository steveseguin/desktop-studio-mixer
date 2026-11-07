param([Parameter(Mandatory=$true)][string]$SessionFile)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/studio-ui-workflows.ps1" -SessionFile $SessionFile -FunctionsOnly -LeaveOpen
$Evidence=Join-Path $Out ('appearance-'+(Get-Date -Format yyyyMMdd-HHmmss));New-Item -ItemType Directory $Evidence|Out-Null
$originalTheme='Original'
try{
    Resize 1280 720;Focus 'Chat accounts';Click 'Appearance' ([Windows.Automation.ControlType]::MenuItem)
    foreach($name in @('Original','Graphite','Rose','Neon','Warm sand')){
        $item=Find $name ([Windows.Automation.ControlType]::MenuItem)
        try{if($item.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Current.ToggleState -eq [Windows.Automation.ToggleState]::On){$originalTheme=$name}}catch{}
    }
    Keys '{ESC}'
    foreach($name in @('Graphite','Rose','Warm sand')){
        Click 'Appearance' ([Windows.Automation.ControlType]::MenuItem);Click $name ([Windows.Automation.ControlType]::MenuItem)
        Focus 'Search comments';SetText 'Search comments' '';Press 'All';SelectFirst;Focus 'Show on screen'
        Check (!(Status).on_screen) ($name+' preserves private selection')
        Shot ($name.Replace(' ','-')+'-private');Press 'Show on screen';Check (WaitFor {(Status).on_screen} 15) ($name+' keeps Show functional');ProgramShot ($name.Replace(' ','-')+'-program');Focus 'Hide';Press 'Hide'
    }
    Click 'Appearance' ([Windows.Automation.ControlType]::MenuItem);Click $originalTheme ([Windows.Automation.ControlType]::MenuItem)
    foreach($layout in @('Simple','Podcast - larger audio mixer','Production - larger source panel','Balanced')){
        Click 'Appearance' ([Windows.Automation.ControlType]::MenuItem);Click 'Workspace layout' ([Windows.Automation.ControlType]::MenuItem);Click $layout ([Windows.Automation.ControlType]::MenuItem)
        Focus '+ Add source';Check (!(Find '+ Add source').Current.IsOffscreen) ($layout+' keeps source creation reachable');Shot ($layout.Split(' ')[0]+'-workspace')
    }
    Focus 'Chat accounts';Click 'Video settings';Keys '{ESC}'
    Check (!(Find 'Video settings').Current.IsOffscreen) 'Escape returns from Video settings'
    Check (!(State).output.active -and !(State).recording.active) 'Appearance exploration starts no production output'
}catch{Check $false 'Appearance walkthrough completed' $_.Exception.Message}
finally{
    try{Keys '{ESC}';Click 'Appearance' ([Windows.Automation.ControlType]::MenuItem);Click $originalTheme ([Windows.Automation.ControlType]::MenuItem);Api @{command='chat.hide'}|Out-Null;Api @{command='chat.select';id=''}|Out-Null;Resize 1280 900}catch{}
    @{exe=$Exe;sha256=(Get-FileHash $Exe).Hash;checks=$checks}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $Evidence 'results.json') -Encoding utf8;"Evidence: $Evidence"
}
if(@($checks|Where-Object{!$_.ok}).Count){exit 1}
