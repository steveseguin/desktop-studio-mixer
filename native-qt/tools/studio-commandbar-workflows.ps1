# Visible packaged workflows; never starts a public output.
param([Parameter(Mandatory=$true)][string]$SessionFile)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/studio-ui-workflows.ps1" -SessionFile $SessionFile -FunctionsOnly -LeaveOpen
$Evidence=Join-Path $Out ('commandbar-'+(Get-Date -Format yyyyMMdd-HHmmss));New-Item -ItemType Directory $Evidence|Out-Null
$ids=@('studioStartOutput','studioRecord','privateBreak','muteMicrophones','inviteGuest','inviteCamera','socialChat','guestTalkback','mixerOutput','studioVideoSettings','studioTools')
function ById($id){$hits=@(foreach($root in Roots){$root.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)|Where-Object{$_.Current.AutomationId.EndsWith('.'+$id)}});if(!$hits.Count){throw "Missing control: $id"};return $hits[0]}
function Geometry($label){
    $bar=(ById 'studioCommandBar').Current.BoundingRectangle;$window=@(Roots)[0].Current.BoundingRectangle
    $buttons=@($ids|ForEach-Object{ById $_});$bad=@($buttons|Where-Object{$b=$_.Current.BoundingRectangle;$_.Current.IsOffscreen -or $b.Left -lt $bar.Left -or $b.Right -gt $bar.Right -or $b.Top -lt $bar.Top -or $b.Bottom -gt $bar.Bottom -or $b.Right -gt $window.Right})
    Check (!$bad.Count) ($label+' keeps every action inside the command bar')
    $overlap=$false;for($a=0;$a -lt $buttons.Count;$a++){for($b=$a+1;$b -lt $buttons.Count;$b++){if($buttons[$a].Current.BoundingRectangle.IntersectsWith($buttons[$b].Current.BoundingRectangle)){$overlap=$true}}}
    Check (!$overlap) ($label+' has no overlapping actions')
    return @($buttons|ForEach-Object{$_.Current.BoundingRectangle.Y}|Select-Object -Unique).Count
}
try{
    if((State).output.active -or (State).recording.active){throw 'Preserving active output'}
    foreach($size in @(@(1024,768),@(1280,720),@(1366,768),@(1920,1080))){
        Resize $size[0] $size[1];Focus 'Chat accounts';$rows=Geometry "$($size[0])x$($size[1])"
        # Geometry emits checks and returns its row count last.
        $rows=@($rows)[-1]
        Check ($rows -eq $(if($size[0] -eq 1024){2}else{1})) 'Actions use one row when space permits' "$rows rows at $($size[0])px"
        $branding=@(foreach($root in Roots){$root.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,'Desktop Studio'))|Where-Object{!$_.Current.IsOffscreen}})
        Check (!$branding.Count) 'The workspace has no repeated Desktop Studio heading';Shot "$($size[0])x$($size[1])"
    }
    Resize 1280 720;Focus 'GO LIVE'
    $route=@();foreach($id in $ids){$f=[Windows.Automation.AutomationElement]::FocusedElement;$route+=@{expected=$id;actual=$f.Current.AutomationId};Keys '{TAB}'}
    Check (@($route|Where-Object{!$_.actual.EndsWith('.'+$_.expected)}).Count -eq 0) 'Tab follows the visible command order'
    Check ([Windows.Automation.AutomationElement]::FocusedElement.Current.AutomationId.EndsWith('.studioMode')) 'Tab leaves the command bar at Studio mode'
    $route|ConvertTo-Json|Set-Content (Join-Path $Evidence 'keyboard-route.json')
    foreach($item in @(@{id='privateBreak';state='private_break'},@{id='muteMicrophones';state='microphones_muted'},@{id='guestTalkback';state='private_talkback'})){
        (ById $item.id).GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke();Check (WaitFor {(State).($item.state)}) ($item.id+' changes actual state');Geometry ($item.id+' enabled')|ForEach-Object{if($_ -is [string]){$_}};Shot ($item.id+'-enabled')
        (ById $item.id).GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke();Check (WaitFor {!(State).($item.state)}) ($item.id+' restores actual state')
    }
    Press 'Chat';Start-Sleep -Seconds 1;Geometry 'Chat hidden with unread count'|ForEach-Object{if($_ -is [string]){$_}};Shot 'chat-hidden';Press 'Chat'
    Check (!(State).output.active -and !(State).recording.active) 'Command exploration starts no stream or recording'
}catch{Check $false 'Command walkthrough completed' $_.Exception.Message}
finally{@{exe=$Exe;sha256=(Get-FileHash $Exe).Hash;checks=$checks}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $Evidence 'results.json') -Encoding utf8;"Evidence: $Evidence"}
if(@($checks|Where-Object{!$_.ok}).Count){exit 1}
