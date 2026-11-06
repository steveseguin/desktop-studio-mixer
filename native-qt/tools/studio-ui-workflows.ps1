# Packaged Studio workflows through Windows UI Automation. Control API reads verify
# outcomes; only practice-message fixtures are seeded through the API. No public posts.
param(
    [string]$Exe = $env:VIDEO_MIXER_CHAT_EXE,
    [string]$Out = (Join-Path $PSScriptRoot "../build-chat/visual-workflows/$(Get-Date -Format yyyyMMdd-HHmmss)"),
    [string]$SessionFile,
    [switch]$LeaveOpen,
    [switch]$FunctionsOnly
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Windows.Forms, System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class StudioUI {
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int w,int height,uint flags);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint flags,uint x,uint y,uint data,UIntPtr extra);
 [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left,top,right,bottom; }
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
}
'@
if ($SessionFile) {
    $session = Get-Content -LiteralPath $SessionFile -Raw | ConvertFrom-Json
    $Out = $session.out; $control = $session.control; $app = Get-Process -Id $session.pid
    $Exe = $session.exe
} else {
    if (!$Exe) { $Exe = Join-Path $PSScriptRoot '../dist/desktop-studio-portable/desktop-studio.exe' }
    New-Item -ItemType Directory -Force $Out | Out-Null
    $Out = (Resolve-Path $Out).Path; $control = Join-Path $Out 'control.json'
    $env:VIDEO_MIXER_DATA_DIR = $Out
    $app = Start-Process $Exe -PassThru -ArgumentList "--studio-show=$Out/show.mixer.json", '--local-control-port=0', "--local-control-discovery=$control"
    $SessionFile=Join-Path $Out 'ui-session.json'
    $session=@{out=$Out;control=$control;pid=$app.Id;exe=$Exe};$session|ConvertTo-Json|Set-Content -LiteralPath $SessionFile
}
$checks = [Collections.ArrayList]::new()
$Evidence=Join-Path $Out ('workflows-'+(Get-Date -Format yyyyMMdd-HHmmss-fff))
if (!$FunctionsOnly) {New-Item -ItemType Directory -Path $Evidence | Out-Null}
function Check($condition, $name, $detail = '') {
    [void]$checks.Add([pscustomobject]@{ok=[bool]$condition; name=$name; detail=$detail})
    "$(if ($condition) {'PASS'} else {'FAIL'}) $name $detail"
}
function Roots { [Windows.Automation.AutomationElement]::RootElement.FindAll([Windows.Automation.TreeScope]::Children,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ProcessIdProperty,$app.Id)) }
function Find($name, $type = $null, $index = 0) {
    $hits = @(); foreach ($window in (Roots)) {
        $hits += @($window.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,$name)))
    }
    if ($type) { $hits = @($hits | Where-Object {$_.Current.ControlType -eq $type}) }
    if ($hits.Count -le $index) { throw "Control unavailable: $name" }; return $hits[$index]
}
function Press($name, $index = 0) { (Find $name $null $index).GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke(); Start-Sleep -Milliseconds 180 }
function SetText($name, $value) { (Find $name ([Windows.Automation.ControlType]::Edit)).GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($value) }
function Click($name, $type = $null) {
    # InvokePattern blocks Qt's accessibility provider while a modal callback runs.
    # Use a real pointer click for controls that open dialogs or popup menus.
    $control=Find $name $type; $b=$control.Current.BoundingRectangle
    $owner=0; [void][StudioUI]::GetWindowThreadProcessId([StudioUI]::GetForegroundWindow(),[ref]$owner)
    if ($owner -ne $app.Id) {$window=@(Roots)[0]; [void][StudioUI]::SetForegroundWindow([IntPtr]$window.Current.NativeWindowHandle)}
    [void][StudioUI]::SetCursorPos([int]($b.X+$b.Width/2),[int]($b.Y+$b.Height/2))
    [StudioUI]::mouse_event(2,0,0,0,[UIntPtr]::Zero); [StudioUI]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
    Start-Sleep -Milliseconds 300
}
function Focus($name) { $window=@(Roots)[0]; [void][StudioUI]::SetForegroundWindow([IntPtr]$window.Current.NativeWindowHandle); (Find $name).SetFocus() }
function Keys($text) {
    $owner=0; [void][StudioUI]::GetWindowThreadProcessId([StudioUI]::GetForegroundWindow(),[ref]$owner)
    if ($owner -ne $app.Id) { throw 'Refusing keyboard input outside this Studio instance' }
    [Windows.Forms.SendKeys]::SendWait($text); Start-Sleep -Milliseconds 180
}
function Api($body) { Invoke-RestMethod -Uri ($discovery.base_url.TrimEnd('/')+'/studio/command') -Method Post -Headers @{Authorization="Bearer $($discovery.token)"} -ContentType 'application/json; charset=utf-8' -Body ([Text.Encoding]::UTF8.GetBytes(($body | ConvertTo-Json -Depth 12 -Compress))) }
function Status { Api @{command='chat.status'} }
function State { Invoke-RestMethod -Uri ($discovery.base_url.TrimEnd('/')+'/studio/state') -Headers @{Authorization="Bearer $($discovery.token)"} }
function WaitFor($predicate, $seconds=10) { $end=(Get-Date).AddSeconds($seconds); do { if (& $predicate) { return $true }; Start-Sleep -Milliseconds 200 } while ((Get-Date) -lt $end); return $false }
function Shot($name) {
    Start-Sleep -Milliseconds 300 # Allow the next painted frame after an acknowledged UI change.
    $handle=[StudioUI]::GetForegroundWindow(); $owner=0; [void][StudioUI]::GetWindowThreadProcessId($handle,[ref]$owner)
    if ($owner -ne $app.Id) {throw 'Screenshot foreground belongs to another app'}
    $bounds=[StudioUI+Rect]::new(); [void][StudioUI]::GetWindowRect($handle,[ref]$bounds)
    $bitmap=[Drawing.Bitmap]::new($bounds.right-$bounds.left,$bounds.bottom-$bounds.top); $graphics=[Drawing.Graphics]::FromImage($bitmap); $dc=$graphics.GetHdc()
    try {if (![StudioUI]::PrintWindow($handle,$dc,2)) {throw 'Window capture failed'}} finally {$graphics.ReleaseHdc($dc)}
    try {$bitmap.Save((Join-Path $Evidence "$name.png"),[Drawing.Imaging.ImageFormat]::Png)} finally {$graphics.Dispose();$bitmap.Dispose()}
}
function ProgramShot($name) {
    node (Join-Path $PSScriptRoot 'mcp/studio-cli.mjs') --discovery $control screenshot program --out (Join-Path $Evidence "$name.png") | Out-Null
    if ($LASTEXITCODE) {throw "Program screenshot failed: $name"}
}
function Resize($width, $height) { $window=@(Roots)[0]; [void][StudioUI]::SetWindowPos([IntPtr]$window.Current.NativeWindowHandle,[IntPtr]::Zero,0,0,$width,$height,4); Start-Sleep -Milliseconds 350 }
function SelectFirst { $items=@(Rows); if (!$items.Count) {throw 'No comments to select'}; $items[0].GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select(); Start-Sleep -Milliseconds 180 }
function Rows { @( (Find 'Comments').FindAll([Windows.Automation.TreeScope]::Children,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::ListItem)) ) }
function ChooseSourceType($name) {
    $combo=Find 'Source type' ([Windows.Automation.ControlType]::ComboBox)
    $combo.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern).Expand()
    if (!(WaitFor {try {$null=Find $name ([Windows.Automation.ControlType]::ListItem); return $true} catch {return $false}})) {throw "Source picker did not open: $name"}
    Click $name ([Windows.Automation.ControlType]::ListItem)
    if (!(WaitFor {$combo.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).Current.Value -eq $name})) {throw "Source picker did not select $name"}
}
if ($FunctionsOnly) {$discovery=Get-Content -LiteralPath $control -Raw | ConvertFrom-Json; return}
try {
    if (!(WaitFor {Test-Path $control} 30)) {throw 'No control discovery'}
    $discovery=Get-Content -LiteralPath $control -Raw | ConvertFrom-Json
    if ($discovery.pid -ne $app.Id) {throw 'Discovery PID mismatch'}
    Resize 1280 900
    if ((Status).practice) { Click 'Advanced'; Click 'Stop practising' ([Windows.Automation.ControlType]::MenuItem) }
    Press 'Try it with pretend chat'; Press 'All'; SetText 'Search comments' ''
    Check (WaitFor { (Status).ready -and (Status).count -ge 3 } 30) 'Practice starts and delivers comments'
    SelectFirst
    Check (!(Status).on_screen -and !!(Status).selected) 'Accessible selection remains private'
    Focus 'Guest talkback'; Keys '{ENTER}'
    Check (!(Status).on_screen) 'Enter on an unrelated control cannot broadcast a comment'
    Focus 'Search comments'; Keys '{ENTER}'
    Check (!(Status).on_screen) 'Enter in search cannot broadcast a comment'
    Keys '{TAB}{HOME}{ENTER}'
    Check (WaitFor {(Status).on_screen}) 'Enter on the focused comment list shows the selected comment'
    Press 'Keep up'; Shot '10-comment-on-air'
    Check ((Status).kept) 'Keep up holds the comment'
    Press 'Private break'
    Check (WaitFor {(Status).hidden_by_private_break -and !(Status).on_screen}) 'Private break protects Program'
    Shot '11-private-break'; Press 'Resume show'
    Check (WaitFor {(Status).on_screen}) 'Resume restores the held comment'
    Press 'Hide'; Check (!(Status).on_screen) 'Hide clears Program'
    SelectFirst; Press 'Save'; Press 'Saved' 0
    Check ((Rows).Count -eq 1) 'Saved filter contains the saved comment'
    Shot '12-saved'; Press 'All'
    SetText 'Search comments' 'zz-no-matching-comment-7392'
    Check ((Rows).Count -eq 0) 'Search with no matches is empty'
    Check (WaitFor {try {return !(Find 'No matching comments. Clear Search to see comments again.').Current.IsOffscreen} catch {return $false}}) 'Empty search explains how to recover'
    Shot '13-empty-search'
    SetText 'Search comments' 'great show'
    Check ((Rows).Count -gt 0 -and @((Rows) | Where-Object {$_.Current.Name -notlike '*great show*'}).Count -eq 0) 'Search limits visible comments'
    Focus 'Search comments'; Keys '^a{BACKSPACE}'
    Press 'Thanks'
    Check ((Rows).Count -gt 0 -and @((Rows) | Where-Object {$_.Current.Name -notlike '*5.00*'}).Count -eq 0) 'Thanks filter contains donations'
    Press 'All'; SelectFirst
    foreach ($style in @('Bubble','Big','Bar')) {
        Press $style; Check ((Status).layer.style -eq $style.ToLower()) "Choose $style style in private preview"
    }
    Press 'Back to live'; Check ([string]::IsNullOrEmpty((Status).selected)) 'Back to live clears the private selection'
    foreach ($size in @(@(1024,768),@(1280,720),@(1280,900),@(1920,1080))) {
        Resize $size[0] $size[1]
        Check (!(Find 'Comments').Current.IsOffscreen) "Chat remains open at $($size[0])x$($size[1])"
        Shot "14-layout-$($size[0])x$($size[1])"
    }
    Resize 1280 900
    Press 'Chat'; Resize 1024 768; Resize 1280 900
    Check ((Find 'Chat').Current.IsEnabled) 'Window remains navigable while chat is hidden'
    Press 'Chat'; Check (!(Find 'Comments').Current.IsOffscreen) 'Chat reopens from its toolbar button'
    Press '+ Add'; $before=@((Status).channels).Count
    $inputs=@('javascript:alert(1)','file:///C:/Windows/win.ini','http://twitch.tv/name','https://localhost/name','https://127.0.0.1/name','https://twitch.tv/directory','https://kick.com/categories','https://twitch.tv/name#fragment','https://user:pass@twitch.tv/name','https://twitch.tv/name?token=pretend','https://example.com/name','https://twitch.tv/a%2Fb','https://twitch.tv/has space',('x'*2200),'<script>alert(1)</script>')
    foreach ($value in $inputs) {
        SetText 'Your channel link or name' $value; Press 'Add this channel'
        Check (@((Status).channels).Count -eq $before) 'Invalid channel input does not create a connection' $value.Substring(0,[Math]::Min(70,$value.Length))
    }
    Shot '15-invalid-channel'
    SetText 'Your channel link or name' ''
    Focus 'Your channel link or name'; Keys '{ENTER}'
    Check (!(Status).on_screen -and @((Status).channels).Count -eq $before) 'Empty channel Enter has no output or connection side effects'
    Press '+ Add'
    Check (!(Get-Process -Id $app.Id).HasExited) 'App remains responsive after malformed input'
    Click 'Advanced'; Click 'Comments layer position' ([Windows.Automation.ControlType]::MenuItem)
    (Find 'Left' ([Windows.Automation.ControlType]::Spinner)).GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue('100')
    Press 'OK'
    Check (!(Find 'Keep the Comments layer inside the video.').Current.IsOffscreen) 'Invalid position stays open with a visible explanation'
    Shot '16-layer-validation'
    (Find 'Left' ([Windows.Automation.ControlType]::Spinner)).GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue('0')
    Press 'OK'; Check ((Status).layer.x -eq 0) 'Corrected position applies without reopening the dialog'
    $sceneCount=@((State).scenes).Count
    Click 'Add scene'; SetText 'Scene name' 'Cancelled scene'; Press 'Cancel'
    Check (@((State).scenes).Count -eq $sceneCount) 'Cancel new scene keeps the show unchanged'
    $runTag=Get-Date -Format HHmmss
    $sceneName='UI review <title> '+[char]0x6771+[char]0x4eac+' '+$runTag
    Click 'Add scene'; SetText 'Scene name' $sceneName; Press 'OK'
    Check (@((State).scenes | Where-Object {$_.name -eq $sceneName}).Count -eq 1) 'Add scene accepts Unicode and literal markup in its name'
    $renamed='Review scene '+$runTag
    Click 'Rename'; SetText 'Scene name' $renamed; Press 'OK'
    Check (@((State).scenes | Where-Object {$_.name -eq $renamed}).Count -eq 1) 'Rename updates the selected scene'
    Press 'Duplicate'; Check (@((State).scenes).Count -eq $sceneCount+2) 'Duplicate creates a second scene'
    Press 'Remove scene'; Check (@((State).scenes).Count -eq $sceneCount+1) 'Remove deletes only the selected scene'
    Press 'Undo'; Check (@((State).scenes).Count -eq $sceneCount+2) 'Undo restores the deleted scene'
    Press 'Redo'; Check (@((State).scenes).Count -eq $sceneCount+1) 'Redo reapplies scene deletion'
    $sourceCount=@((State).sources).Count
    Click '+ Add source'
    ChooseSourceType 'Color'
    SetText 'URL / file / text / color' 'this-is-not-a-color'; Press 'OK'
    Check (!(Find 'Enter a valid color, for example #24466f.').Current.IsOffscreen) 'Invalid color leaves an actionable error in the source dialog'
    Shot '17-source-validation'
    ChooseSourceType 'Browser / remote VDO.Ninja feed'
    SetText 'URL / file / text / color' 'javascript:alert(1)'; Press 'OK'
    Check (!(Find 'Enter a complete http:// or https:// source URL.').Current.IsOffscreen) 'Browser source rejects an executable URL with a visible error'
    ChooseSourceType 'Video / audio file'
    SetText 'URL / file / text / color' 'C:\no-such-studio-review-file.mp4'; Press 'OK'
    Check (!(Find 'Choose an existing local media file.').Current.IsOffscreen) 'Missing media stays editable with a visible error'
    Press 'Cancel'; Check (@((State).sources).Count -eq $sourceCount) 'Cancel source validation leaves no partial source'
    Click '+ Add source'
    ChooseSourceType 'Title / slate'
    $titleName='UI review title '+$runTag
    SetText 'Name' $titleName; SetText 'URL / file / text / color' 'Hello from the UI review'; Press 'OK'
    Check (@((State).sources | Where-Object {$_.name -eq $titleName -and $_.on_program}).Count -eq 1) 'Adding a title through the dialog places it on Program'
    Shot '18-title-source'
    ProgramShot '18-title-program'
    Press 'Undo'; Check (@((State).sources | Where-Object {$_.name -eq $titleName}).Count -eq 0) 'Undo removes the newly added source'
    Press 'Redo'; Check (@((State).sources | Where-Object {$_.name -eq $titleName}).Count -eq 1) 'Redo restores the source'
    Press 'Output settings'; Shot '19-output-settings'
    Click 'Studio' ([Windows.Automation.ControlType]::TabItem)
    Check (!(Find '+ Add source').Current.IsOffscreen) 'Output settings has a navigable route back to Studio'
    Check (!(Find 'Search comments').Current.IsOffscreen) 'Chat stays open after visiting Output settings'
    $fixtures=@(
        @{chatname='Markup viewer';chatmessage='UIFUZZ markup <b>bold</b> & quoted "text"';type='twitch'},
        @{chatname=('Viewer '+[char]0x6771+[char]0x4eac);chatmessage=('UIFUZZ Unicode '+[char]0x0645+[char]0x0631+[char]0x062d+[char]0x0628+[char]0x0627+' '+[char]::ConvertFromUtf32(0x1f600));type='youtube'},
        @{chatname=('Long name '*25);chatmessage=('UIFUZZ long '+('long comment words '*90)+' UIFUZZ end marker');type='kick'}
    )
    foreach ($fixture in $fixtures) { Api @{command='chat.inject';message=$fixture} | Out-Null }
    Check (WaitFor {@((Api @{command='chat.messages'}).messages | Where-Object {$_.chatmessage -like 'UIFUZZ*'}).Count -eq 3}) 'Synthetic Unicode, markup and long comments arrive'
    SetText 'Search comments' 'UIFUZZ'; SelectFirst; Shot '20-fuzz-private'
    Check (!(Status).on_screen) 'Fuzz comments stay private until shown'
    foreach ($style in @('Bar','Bubble','Big')) {
        SelectFirst; Press $style; Press 'Show on screen'
        Check (WaitFor {(Status).on_screen -and (Status).layer.style -eq $style.ToLower()}) "$style shows the selected fuzz comment"
        ProgramShot "21-fuzz-$style-program"
        Press 'Hide'
    }
    SetText 'Search comments' 'UIFUZZ long'; SelectFirst; Shot '22-long-comment-private'
    Focus 'Private comment preview'; Keys '{PGDN 30}'; Shot '23-long-comment-bottom'
    $preview=Find 'Private comment preview'
    $previewText=@($preview.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition) | Where-Object {$_.Current.Name -like '*UIFUZZ end marker*'})
    Check ($previewText.Count -gt 0) 'Full private comment is exposed to accessibility tools'
    SetText 'Search comments' ''; Press 'Back to live'
    Press 'Studio mode'
    Check ((State).mode -eq 'studio') 'Studio mode exposes separate Preview and Program'
    $programBefore=(State).program
    $sceneList=Find 'Scenes' ([Windows.Automation.ControlType]::List)
    $breakRow=$sceneList.FindFirst([Windows.Automation.TreeScope]::Children,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,'Break'))
    $breakRow.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Check (WaitFor {(State).preview -ne $programBefore -and (State).program -eq $programBefore}) 'Selecting another scene in Studio mode leaves Program unchanged'
    Shot '24-studio-preview'
    $takeTarget=(State).preview; Press 'Take / Cut'
    Check (WaitFor {(State).program -eq $takeTarget}) 'Take sends Preview to Program'
    ProgramShot '25-take-program'
    $openingRow=$sceneList.FindFirst([Windows.Automation.TreeScope]::Children,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,'Opening'))
    $openingRow.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
    $fadeTarget=(State).preview; Press 'Fade (400 ms)'
    Check (WaitFor {(State).program -eq $fadeTarget -and !(State).transitioning}) 'Fade completes on the selected scene'
    Press 'Studio mode'
    Resize 1280 720; Focus 'Search comments'
    $windowBounds=@(Roots)[0].Current.BoundingRectangle
    $tabStops=for($i=0;$i -lt 70;$i++) {
        Keys '{TAB}'; $f=[Windows.Automation.AutomationElement]::FocusedElement; $b=$f.Current.BoundingRectangle
        [pscustomobject]@{name=$f.Current.Name;id=$f.Current.AutomationId;rect=@($b.X,$b.Y,$b.Width,$b.Height);inWindow=(!$b.IsEmpty -and $b.Left -ge $windowBounds.Left -and $b.Top -ge $windowBounds.Top -and $b.Right -le $windowBounds.Right+1 -and $b.Bottom -le $windowBounds.Bottom+1)}
    }
    $tabStops | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $Evidence 'keyboard-stops.json') -Encoding utf8
    Check (@($tabStops | Where-Object {!$_.inWindow}).Count -eq 0) '70 keyboard tab stops stay within the window at 1280x720'
    Shot '26-keyboard-navigation'; Resize 1280 900
    Check (!(State).output.active -and !(State).recording.active) 'Exploration never started a public stream or recording'
    SelectFirst; Press 'Show on screen'; $shownBeforeRestart=WaitFor {(Status).on_screen}
    Check $shownBeforeRestart 'A comment is on Program before the restart check'
    if ($shownBeforeRestart) {Press 'Keep up'}
    $beforeRestart=State
    node (Join-Path $PSScriptRoot 'mcp/studio-cli.mjs') --discovery $control quit | Out-Null
    if (!$app.WaitForExit(10000)) {throw 'Studio did not close normally'}
    Start-Sleep -Milliseconds 500
    $env:VIDEO_MIXER_DATA_DIR=$Out
    $app=Start-Process $Exe -PassThru -ArgumentList "--studio-show=$Out/show.mixer.json",'--local-control-port=0',"--local-control-discovery=$control"
    if ($SessionFile) { $session.pid=$app.Id; $session | ConvertTo-Json | Set-Content -LiteralPath $SessionFile }
    if (!(WaitFor {try {$candidate=Get-Content -LiteralPath $control -Raw|ConvertFrom-Json;return $candidate.pid -eq $app.Id} catch {return $false}} 30)) {throw 'Restart discovery did not appear'}
    $discovery=Get-Content -LiteralPath $control -Raw|ConvertFrom-Json
    $afterRestart=State
    Check (($beforeRestart.scenes | ConvertTo-Json -Depth 8 -Compress) -eq ($afterRestart.scenes | ConvertTo-Json -Depth 8 -Compress)) 'Restart preserves the edited scene collection'
    Check ((@($beforeRestart.sources.id) -join ',') -eq (@($afterRestart.sources.id) -join ',')) 'Restart preserves the added source identities'
    Check (!$afterRestart.chat.on_screen -and [string]::IsNullOrEmpty($afterRestart.chat.selected)) 'Restart does not rebroadcast or select the previous comment'
    Resize 1280 900; [void][StudioUI]::SetForegroundWindow([IntPtr](@(Roots)[0].Current.NativeWindowHandle)); Shot '27-reopened-show'
    Resize 1280 720
    $sourceList=Find 'Sources in selected scene' ([Windows.Automation.ControlType]::List)
    $sourceRows=@($sourceList.FindAll([Windows.Automation.TreeScope]::Children,[Windows.Automation.Condition]::TrueCondition)|Where-Object{$_.Current.Name -like 'UI review title*'})
    $sourceName=$sourceRows[-1].Current.Name
    $sourceRows[-1].GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Focus '+ Add source'; Keys '+{TAB}'
    Check ([Windows.Automation.AutomationElement]::FocusedElement.Current.Name -eq $sourceName) 'Keyboard focus retains the source selected through accessibility'
    Shot '28-selected-source-keyboard'
    $sceneList=Find 'Scenes' ([Windows.Automation.ControlType]::List)
    $sceneList.FindFirst([Windows.Automation.TreeScope]::Children,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,'Break')).GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Focus 'Add scene'; Keys '+{TAB}'
    Check ([Windows.Automation.AutomationElement]::FocusedElement.Current.Name -eq 'Break') 'Keyboard focus retains the scene selected through accessibility'
    $sceneList.FindFirst([Windows.Automation.TreeScope]::Children,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,'Opening')).GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Resize 1280 900
} catch { Check $false 'Workflow stopped' $_.Exception.Message }
finally {
    $artifact=[pscustomobject]@{exe=$Exe; sha256=(Get-FileHash $Exe -Algorithm SHA256).Hash; at=(Get-Date).ToUniversalTime().ToString('o'); checks=$checks}
    $artifact | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $Evidence 'workflow-results.json') -Encoding utf8
    @{evidence=$Evidence;sha256=$artifact.sha256} | ConvertTo-Json | Set-Content (Join-Path $Out 'latest-ui-workflow.json') -Encoding utf8
    if (!$LeaveOpen) { try {node (Join-Path $PSScriptRoot 'mcp/studio-cli.mjs') --discovery $control quit | Out-Null} catch {} }
    "Evidence: $Evidence"
}
if (@($checks | Where-Object {!$_.ok}).Count) {exit 1}
exit 0
