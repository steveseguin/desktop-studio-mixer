# Deliberately faults only workers belonging to an isolated, recording QA app.
param([Parameter(Mandatory=$true)][string]$SessionFile,[Parameter(Mandatory=$true)][string]$ReceiverSessionFile)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/studio-ui-workflows.ps1" -SessionFile $SessionFile -FunctionsOnly -LeaveOpen
if($Out -notmatch '[\\/]build[^\\/]*[\\/]'){throw 'Worker faults require an isolated build-directory profile'}
if(!(State).recording.active -or !(State).output.live){throw 'Start the isolated recording and stream before this workflow'}
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ChatFaultThreads {
 [DllImport("kernel32.dll")] public static extern IntPtr OpenThread(uint access,bool inherit,uint id);
 [DllImport("kernel32.dll")] public static extern uint SuspendThread(IntPtr thread);
 [DllImport("kernel32.dll")] public static extern uint ResumeThread(IntPtr thread);
 [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr handle);
}
'@
$Evidence=Join-Path $Out ('worker-recovery-'+(Get-Date -Format yyyyMMdd-HHmmss));New-Item -ItemType Directory $Evidence|Out-Null
$events=[Collections.ArrayList]::new();$suspended=[Collections.ArrayList]::new()
function OwnedChatWorker {
    $worker=Get-CimInstance Win32_Process|Where-Object{$_.ParentProcessId -eq $app.Id -and $_.Name -eq 'desktop-studio.exe' -and $_.CommandLine -match '--studio-worker chat'}|Select-Object -First 1
    if(!$worker -or $worker.ExecutablePath -ne $Exe){throw 'Owned chat worker could not be verified'}
    return $worker
}
function ReceiverShot($name){$receiver=Get-Content $ReceiverSessionFile -Raw|ConvertFrom-Json;node "$PSScriptRoot/mcp/studio-cli.mjs" --discovery $receiver.control screenshot program --out (Join-Path $Evidence "$name.png")|Out-Null;if($LASTEXITCODE){throw 'Receiver screenshot failed'}}
function VerifyRecovery($label,$oldBrowser,$oldIds,$clock){
    $recovered=WaitFor {(Status).ready -and (Status).browser_pid -ne $oldBrowser} 60
    Check $recovered "$label restarts chat automatically" ($clock.Elapsed.TotalSeconds.ToString('F2')+' seconds')
    Check ((State).recording.active -and (State).output.live) "$label preserves recording and live output"
    Check ((Status).on_screen -and (Status).on_air.id -eq $shownId) "$label preserves the held comment on Program"
    Check (WaitFor {@((Api @{command='chat.messages'}).messages|Where-Object{$_.id -notin $oldIds}).Count -gt 0} 60) "$label resumes incoming comments"
    ProgramShot ($label+'-program');ReceiverShot ($label+'-received');Shot ($label+'-workspace')
}
try{
    Focus 'Chat accounts';SetText 'Search comments' ''
    # Selection pauses the visible feed. Resume it before choosing a recent
    # API-observed comment, which may still be queued behind that pause.
    $back=try{Find 'Back to live'}catch{$null};if($back -and !$back.Current.IsOffscreen){Press 'Back to live'}
    $message=@((Api @{command='chat.messages'}).messages|Where-Object{!$_.practice -and $_.chatmessage.Length -gt 3 -and $_.chatmessage.Length -lt 100})[-1]
    if(!$message){throw 'No suitable real comment yet'}
    SetText 'Search comments' $message.chatmessage
    foreach($row in Rows){$row.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select();if((Status).selected -eq $message.id){break}}
    Check ((Status).selected -eq $message.id -and !(Status).on_screen) 'Select a real comment privately before worker failures'
    Press 'Show on screen';Check (WaitFor {(Status).on_screen -and (Status).on_air.id -eq $message.id} 15) 'Feature the real comment before worker failures';Press 'Keep up';$shownId=$message.id
    ProgramShot 'before-faults';ReceiverShot 'before-faults-received'
    $worker=OwnedChatWorker;$before=Status;$ids=@((Api @{command='chat.messages'}).messages.id);$clock=[Diagnostics.Stopwatch]::StartNew()
    [void]$events.Add(@{kind='chat-worker-termination';utc=[DateTimeOffset]::UtcNow.ToString('o');pid=$worker.ProcessId})
    Stop-Process -Id $worker.ProcessId -Force
    VerifyRecovery 'terminated-worker' $before.browser_pid $ids $clock
    $worker=OwnedChatWorker;$before=Status;$ids=@((Api @{command='chat.messages'}).messages.id);$clock=[Diagnostics.Stopwatch]::StartNew()
    [void]$events.Add(@{kind='chat-worker-suspension';utc=[DateTimeOffset]::UtcNow.ToString('o');pid=$worker.ProcessId})
    foreach($thread in (Get-Process -Id $worker.ProcessId).Threads){$h=[ChatFaultThreads]::OpenThread(2,$false,$thread.Id);if($h -ne [IntPtr]::Zero){if([ChatFaultThreads]::SuspendThread($h) -ne [uint32]::MaxValue){[void]$suspended.Add($h)}else{[void][ChatFaultThreads]::CloseHandle($h)}}}
    if(!$suspended.Count){throw 'No owned worker thread was suspended'}
    VerifyRecovery 'unresponsive-worker' $before.browser_pid $ids $clock
    foreach($h in $suspended){[void][ChatFaultThreads]::ResumeThread($h);[void][ChatFaultThreads]::CloseHandle($h)};$suspended.Clear()
    $browser=(Status).browser_pid;$network=Get-CimInstance Win32_Process|Where-Object{$_.ParentProcessId -eq $browser -and $_.Name -eq 'msedgewebview2.exe' -and $_.CommandLine -match '--utility-sub-type=network.mojom.NetworkService'}|Select-Object -First 1
    if(!$network){throw 'Owned network service was not found'}
    $ids=@((Api @{command='chat.messages'}).messages.id);[void]$events.Add(@{kind='chat-network-service-termination';utc=[DateTimeOffset]::UtcNow.ToString('o');pid=$network.ProcessId})
    Stop-Process -Id $network.ProcessId -Force
    Check (WaitFor {$currentBrowser=(Status).browser_pid;@(Get-CimInstance Win32_Process|Where-Object{$_.ParentProcessId -eq $currentBrowser -and $_.Name -eq 'msedgewebview2.exe' -and $_.CommandLine -match '--utility-sub-type=network.mojom.NetworkService' -and $_.ProcessId -ne $network.ProcessId}).Count -gt 0} 30) 'The owned browser replaces its failed network service'
    Check (WaitFor {@((Api @{command='chat.messages'}).messages|Where-Object{$_.id -notin $ids}).Count -gt 0} 90) 'Comments resume after the network service fails'
    Check ((State).recording.active -and (State).output.live -and (Status).on_screen) 'Network service failure preserves recording, live output and the held comment'
    ProgramShot 'network-recovered-program';ReceiverShot 'network-recovered-received';Press 'Hide';Check (!(Status).on_screen) 'Hide still clears the held comment after all failures'
}catch{Check $false 'Worker recovery workflow completes' $_.Exception.Message}
finally{
    foreach($h in $suspended){[void][ChatFaultThreads]::ResumeThread($h);[void][ChatFaultThreads]::CloseHandle($h)}
    SetText 'Search comments' '';if((Status).on_screen){Press 'Hide'}
    @{exe=$Exe;sha256=(Get-FileHash $Exe).Hash;checks=$checks;events=$events}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $Evidence 'results.json') -Encoding utf8
    "Evidence: $Evidence"
}
if(@($checks|Where-Object{!$_.ok}).Count){exit 1}
