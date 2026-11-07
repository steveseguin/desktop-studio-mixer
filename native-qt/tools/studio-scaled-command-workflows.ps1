# Forced Qt scaling exercises the packaged UI; it is not an OS multi-monitor DPI test.
param([Parameter(Mandatory=$true)][string]$SessionFile)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/studio-ui-workflows.ps1" -SessionFile $SessionFile -FunctionsOnly -LeaveOpen
$Evidence=Join-Path $Out ('scaled-command-'+(Get-Date -Format yyyyMMdd-HHmmss));New-Item -ItemType Directory $Evidence|Out-Null
$originalLanguage=(State).preferences.language;$originalScale=$env:QT_SCALE_FACTOR
$ids=@('studioStartOutput','studioRecord','privateBreak','muteMicrophones','inviteGuest','inviteCamera','socialChat','guestTalkback','mixerOutput','studioVideoSettings','studioTools')
function ById($id){$hits=@(foreach($root in Roots){$root.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)|Where-Object{$_.Current.AutomationId.EndsWith('.'+$id)}});if(!$hits.Count){throw "Missing control: $id"};return $hits[0]}
function Relaunch($scale,$language){
    Api @{command='preferences.set';language=$language}|Out-Null
    node "$PSScriptRoot/mcp/studio-cli.mjs" --discovery $control quit|Out-Null
    if(!$app.WaitForExit(15000)){throw 'QA app did not close'}
    $env:VIDEO_MIXER_DATA_DIR=$Out;$env:QT_SCALE_FACTOR=$scale
    $script:app=Start-Process $Exe -PassThru -ArgumentList "--studio-show=$Out/show.mixer.json",'--local-control-port=0',"--local-control-discovery=$control"
    $session.pid=$app.Id;$session|ConvertTo-Json|Set-Content $SessionFile
    if(!(WaitFor {try{$candidate=Get-Content $control -Raw|ConvertFrom-Json;return $candidate.pid -eq $app.Id -and @(Roots).Count -gt 0}catch{return $false}} 30)){throw 'Scaled app did not become visible'}
    $script:discovery=Get-Content $control -Raw|ConvertFrom-Json
}
try{
    if((State).output.active -or (State).recording.active){throw 'Preserving active output'}
    foreach($case in @(@{scale='1.25';lang='en';w=1280;h=900},@{scale='1.5';lang='en';w=1600;h=1000},@{scale='1';lang='fr';w=1280;h=720},@{scale='1';lang='es';w=1280;h=720})){
        Relaunch $case.scale $case.lang;Resize $case.w $case.h
        $window=@(Roots)[0];[void][StudioUI]::SetForegroundWindow([IntPtr]$window.Current.NativeWindowHandle)
        Check ((State).preferences.language -eq $case.lang) "Language $($case.lang) loads at scale $($case.scale)"
        $bar=(ById 'studioCommandBar').Current.BoundingRectangle;$buttons=@($ids|ForEach-Object{ById $_});$bounds=$window.Current.BoundingRectangle
        Check (@($buttons|Where-Object{$r=$_.Current.BoundingRectangle;$_.Current.IsOffscreen -or $r.Left -lt $bar.Left -or $r.Right -gt $bar.Right -or $r.Bottom -gt $bar.Bottom -or $r.Right -gt $bounds.Right}).Count -eq 0) 'All translated and scaled command labels stay inside the bar'
        $overlap=$false;for($i=0;$i -lt $buttons.Count;$i++){for($j=$i+1;$j -lt $buttons.Count;$j++){if($buttons[$i].Current.BoundingRectangle.IntersectsWith($buttons[$j].Current.BoundingRectangle)){$overlap=$true}}};Check (!$overlap) 'Scaled command buttons do not overlap'
        (ById 'studioStartOutput').SetFocus();$order=$true
        foreach($id in $ids){if(![Windows.Automation.AutomationElement]::FocusedElement.Current.AutomationId.EndsWith('.'+$id)){$order=$false};Keys '{TAB}'}
        Check $order 'Scaled and translated keyboard order follows the buttons'
        $sideways=@(foreach($root in Roots){$root.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::ScrollBar))|Where-Object{!$_.Current.IsOffscreen -and $_.Current.BoundingRectangle.Width -gt $_.Current.BoundingRectangle.Height -and $_.Current.AutomationId -match '(scenesPanelScroll|sourcesPanelScroll|audioPanelScroll)'}})
        Check (!$sideways.Count) 'Scaled and translated inspectors avoid sideways scrolling'
        $prefix=$case.lang+'-'+$case.scale;Shot ($prefix+'-workspace')
        (ById 'studioMode').GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle();Check ((State).mode -eq 'studio') 'Studio mode still exposes Preview and Program';Shot ($prefix+'-studio-mode');(ById 'studioMode').GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle()
        $video=ById 'studioVideoSettings';$b=$video.Current.BoundingRectangle;[void][StudioUI]::SetCursorPos([int]($b.X+$b.Width/2),[int]($b.Y+$b.Height/2));[StudioUI]::mouse_event(2,0,0,0,[UIntPtr]::Zero);[StudioUI]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
        Check (WaitFor {try{return !(ById 'canvasFormat').Current.IsOffscreen}catch{return $false}}) 'Video settings opens at this scale and language';Shot ($prefix+'-video');Keys '{ESC}'
        Check (!(State).output.active -and !(State).recording.active) 'Scale exploration starts no output'
    }
}catch{Check $false 'Scaled walkthrough completed' $_.Exception.Message}
finally{
    try{Relaunch $originalScale $originalLanguage;Resize 1280 900;Focus 'Chat accounts'}catch{Check $false 'Original scale and language restored' $_.Exception.Message}
    @{exe=$Exe;sha256=(Get-FileHash $Exe).Hash;checks=$checks}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $Evidence 'results.json') -Encoding utf8;"Evidence: $Evidence"
}
if(@($checks|Where-Object{!$_.ok}).Count){exit 1}
