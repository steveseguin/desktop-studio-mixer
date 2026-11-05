# Operates the packaged app's real Chat controls through Windows UI Automation, the route a
# keyboard or screen-reader user takes, and saves screenshots. Pretend chat only, isolated data.
param([string]$Out = (Join-Path $PSScriptRoot "..\build-chat\ui-walkthrough\$(Get-Date -Format yyyyMMdd-HHmmss)"))
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$exe = if ($env:VIDEO_MIXER_CHAT_EXE) { $env:VIDEO_MIXER_CHAT_EXE } else { Join-Path $PSScriptRoot '..\dist\desktop-studio-portable\desktop-studio.exe' }
$client = Join-Path $PSScriptRoot 'mcp\studio-cli.mjs'
$control = Join-Path $Out 'control.json'
$env:VIDEO_MIXER_DATA_DIR = $Out
$app = Start-Process $exe -PassThru -ArgumentList "--studio-show=$Out\show.mixer.json", "--local-control-port=0", "--local-control-discovery=$control"
$results = [System.Collections.ArrayList]::new()
function Note($ok, $name, $detail = '') { [void]$results.Add([pscustomobject]@{ ok = $ok; name = $name; detail = $detail }); "$(if ($ok) { 'PASS' } else { 'FAIL' }) $name $detail" }
function Studio($json) { $json | node $client --discovery $control command --stdin | Out-String | ConvertFrom-Json }
function Shot($name) { Start-Sleep -Milliseconds 600; node $client --discovery $control screenshot window --out (Join-Path $Out $name) | Out-Null }
function Root { [Windows.Automation.AutomationElement]::RootElement.FindAll([Windows.Automation.TreeScope]::Children, (New-Object Windows.Automation.PropertyCondition([Windows.Automation.AutomationElement]::ProcessIdProperty, $app.Id))) }
function Find($name, $type) {
    $conditions = @(New-Object Windows.Automation.PropertyCondition([Windows.Automation.AutomationElement]::NameProperty, $name))
    if ($type) { $conditions += New-Object Windows.Automation.PropertyCondition([Windows.Automation.AutomationElement]::ControlTypeProperty, $type) }
    $condition = if ($conditions.Count -gt 1) { New-Object Windows.Automation.AndCondition($conditions) } else { $conditions[0] }
    foreach ($window in Root) { $hit = $window.FindFirst([Windows.Automation.TreeScope]::Descendants, $condition); if ($hit) { return $hit } }
    return $null
}
function Press($name) {
    $button = Find $name $null
    if (-not $button) { Note $false "Button '$name' is reachable"; return }
    $pattern = $null
    if ($button.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern, [ref]$pattern)) { $pattern.Invoke() }
    elseif ($button.TryGetCurrentPattern([Windows.Automation.TogglePattern]::Pattern, [ref]$pattern)) { $pattern.Toggle() }
    else { Note $false "Control '$name' supports an accessible action"; return }
    Note $true "Pressed '$name'"
}
try {
    for ($i = 0; $i -lt 60 -and -not (Test-Path $control); $i++) { Start-Sleep -Milliseconds 500 }
    Start-Sleep 2
    Note ((Root | Measure-Object).Count -gt 0) 'App window is visible to UI Automation'
    foreach ($window in Root) { try { $transform = $window.GetCurrentPattern([Windows.Automation.TransformPattern]::Pattern); if ($transform.Current.CanResize) { $transform.Resize(1280, 900); break } } catch {} }
    Start-Sleep -Milliseconds 500
    Shot '00-first-run-1280.png'
    $rootWindow = (Root | Select-Object -First 1)
    foreach ($name in @('Try it with pretend chat','Rename','Duplicate')) {
        $button = Find $name ([Windows.Automation.ControlType]::Button)
        if ($button -and $rootWindow) { $rect=$button.Current.BoundingRectangle; $bounds=$rootWindow.Current.BoundingRectangle; Note ($rect.Right -le $bounds.Right -and $rect.Left -ge $bounds.Left) "'$name' fits at 1280 px" }
    }
    Press 'Try it with pretend chat'
    $ready = $false
    for ($i = 0; $i -lt 60; $i++) { $s = Studio '{"command":"chat.status"}'; if ($s.ready -and $s.practice) { $ready = $true; break }; Start-Sleep -Milliseconds 500 }
    Note $ready 'Pretend chat starts from the button'
    $rows = $null
    for ($i = 0; $i -lt 60; $i++) { $rows = (Studio '{"command":"chat.messages"}').messages; if ($rows.Count -ge 3) { break }; Start-Sleep -Milliseconds 500 }
    Note ($rows.Count -ge 1) 'Pretend comments arrive by themselves' "count=$($rows.Count)"
    Shot '01-practice.png'
    # Select with the real list, through its accessible item.
    $target = $rows[-1]
    # Select Comments by its accessible name; the first list is Scenes.
    $list = Find 'Comments' ([Windows.Automation.ControlType]::List)
    $items = if ($list) { $list.FindAll([Windows.Automation.TreeScope]::Children, [Windows.Automation.Condition]::TrueCondition) } else { @() }
    Note ($items.Count -gt 0) 'Comment rows are exposed to screen readers' "items=$($items.Count) first='$(if ($items.Count) { $items[0].Current.Name })'"
    if ($items.Count -gt 0) {
        $items[$items.Count - 1].GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
        Start-Sleep -Milliseconds 500
    }
    $s = Studio '{"command":"chat.status"}'
    Note (-not [string]::IsNullOrEmpty($s.selected)) 'Selecting a row previews it' "selected=$($s.selected)"
    Note (-not $s.on_screen) 'Selecting alone keeps it off screen'
    Shot '02-selected.png'
    # Qt's SingleSelection accessibility provider refuses removal of its last row.
    # Exercise the explicit, always-available action exposed to screen readers.
    Press 'Back to live'
    Start-Sleep -Milliseconds 300
    $s = Studio '{"command":"chat.status"}'
    Note ([string]::IsNullOrEmpty($s.selected) -and $s.pending_comments -eq 0) 'Accessible Back to live releases selection and the frozen list'
    Studio (@{command='chat.select';id=$target.id} | ConvertTo-Json -Compress) | Out-Null
    Press 'Show on screen'
    $on = $false; for ($i = 0; $i -lt 40; $i++) { if ((Studio '{"command":"chat.status"}').on_screen) { $on = $true; break }; Start-Sleep -Milliseconds 250 }
    Note $on 'Show on screen button puts it on output'
    Shot '03-on-screen.png'
    $timerBefore = (Studio '{"command":"chat.status"}').remaining_ms
    Start-Sleep -Seconds 1
    Note ((Studio '{"command":"chat.status"}').remaining_ms -lt $timerBefore) 'Countdown advances'
    Press 'Keep up'
    Start-Sleep 1
    Note ((Studio '{"command":"chat.status"}').kept) 'Keep up button stops the countdown'
    Press 'Private break'
    Start-Sleep 1
    $s = Studio '{"command":"chat.status"}'
    Note ($s.hidden_by_private_break -and -not $s.on_screen) 'Private break hides the comment and says so'
    Shot '04-private-break.png'
    Press 'Resume show'
    Start-Sleep 1
    Press 'Hide'
    Start-Sleep 1
    Note (-not (Studio '{"command":"chat.status"}').on_screen) 'Hide button removes it'
    Shot '05-hidden.png'
    Studio '{"command":"chat.practice.set","enabled":false}' | Out-Null
} catch { Note $false 'Walkthrough stopped' $_.Exception.Message }
finally {
    try { node $client --discovery $control quit | Out-Null } catch {}
    if (-not $app.WaitForExit(6000)) { Stop-Process -Id $app.Id -Force }
    $results | ConvertTo-Json | Set-Content (Join-Path $Out 'results.json') -Encoding utf8
    "Results in $Out"
}

if (@($results | Where-Object { -not $_.ok }).Count -gt 0) { exit 1 }
