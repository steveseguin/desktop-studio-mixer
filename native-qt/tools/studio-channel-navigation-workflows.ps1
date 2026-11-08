# Connected-channel controls in the packaged application; public reads only.
param([Parameter(Mandatory=$true)][string]$SessionFile,[ValidatePattern('^[a-zA-Z0-9_-]{1,50}$')][string]$KickChannel='clavicular')
$ErrorActionPreference='Stop'
$KickChannel=$KickChannel.ToLowerInvariant()
. "$PSScriptRoot/studio-ui-workflows.ps1" -SessionFile $SessionFile -FunctionsOnly -LeaveOpen
$Evidence=Join-Path $Out ('channels-'+(Get-Date -Format yyyyMMdd-HHmmss));New-Item -ItemType Directory $Evidence|Out-Null
function ChannelControl($platform,$name){
    $channel=@((Status).channels|Where-Object platform -eq $platform)[0]
    Find ($platform+' '+[char]0xb7+' '+$channel.name)
}
function ToggleChannel($platform,$name){(ChannelControl $platform $name).GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle()}
try {
    Resize 1024 768
    if((Status).practice){Click 'Advanced';Click 'Stop practising' ([Windows.Automation.ControlType]::MenuItem)}
    if(@((Status).channels).Count){throw 'Use an isolated profile with no saved channels'}
    SetText 'Your channel link or name' 'https://www.twitch.tv/esl_dota2';Press 'Add this channel'
    if(!(WaitFor {try{$null=ChannelControl 'Twitch' 'esl_dota2';return $true}catch{return $false}})){throw 'No channel control'}
    $chip=ChannelControl 'Twitch' 'esl_dota2';$identity=$chip.GetRuntimeId() -join ',';$before=@((Status).channels)[0].status
    $chip.SetFocus()
    Check (WaitFor {@((Status).channels)[0].status -ne $before} 45) 'Channel status updates after connecting'
    Check (([Windows.Automation.AutomationElement]::FocusedElement.GetRuntimeId() -join ',') -eq $identity) 'Connection status changes preserve keyboard focus'
    Check (((ChannelControl 'Twitch' 'esl_dota2').GetRuntimeId() -join ',') -eq $identity) 'Connection status changes preserve the accessible control'
    foreach($channel in @('https://www.youtube.com/@LofiGirl',('https://kick.com/'+$KickChannel))){Click '+ Add';SetText 'Your channel link or name' $channel;Press 'Add this channel'}
    Check (@((Status).channels).Count -eq 3) 'Three public channels are connected without sign-in'
    Check (WaitFor {(Status).ready -and @((Api @{command='chat.messages'}).messages|Where-Object{!$_.practice}).Count -gt 2} 90) 'Real comments reach the three-channel workspace'
    foreach($size in @(@(1024,768),@(1280,720))){
        Resize $size[0] $size[1]
        $scroll=Find 'Connected channels';$bounds=$scroll.Current.BoundingRectangle
        $bars=@($scroll.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::ScrollBar))|Where-Object{!$_.Current.IsOffscreen -and $_.Current.BoundingRectangle.Width -gt $_.Current.BoundingRectangle.Height})
        Check ($bars.Count -eq 0) "Channels need no sideways scrolling at $($size[0])x$($size[1])"
        $third=ChannelControl 'Kick' $KickChannel;$third.SetFocus();Start-Sleep -Milliseconds 250;$r=$third.Current.BoundingRectangle
        Check (!$third.Current.IsOffscreen -and $r.Left -ge $bounds.Left -and $r.Right -le $bounds.Right -and $r.Top -ge $bounds.Top -and $r.Bottom -le $bounds.Bottom) "Keyboard focus reveals the third channel at $($size[0])x$($size[1])"
        Shot "three-channels-$($size[0])x$($size[1])"
    }
    # Filtering hides existing and incoming rows, but keeps the channel connected.
    foreach($channel in @(@('Twitch','esl_dota2'),@('YouTube','@LofiGirl'),@('Kick',$KickChannel))){ToggleChannel $channel[0] $channel[1]}
    Check (WaitFor {(Rows).Count -eq 0}) 'Turning off every channel hides its comments'
    Check (@((Status).channels).Count -eq 3 -and (Status).ready) 'Filtering preserves the capture connections'
    foreach($channel in @(@('Twitch','esl_dota2'),@('YouTube','@LofiGirl'),@('Kick',$KickChannel))){ToggleChannel $channel[0] $channel[1]}
    Check (WaitFor {(Rows).Count -gt 0}) 'Turning channels back on restores the comment list'
    $remove='Remove channel: Kick '+[char]0xb7+' '+$KickChannel;Focus $remove;Keys ' '
    Check (WaitFor {@((Status).channels).Count -eq 2}) 'A keyboard-accessible remove button disconnects only its channel'
    Check (@((Status).channels|Where-Object platform -eq 'Kick').Count -eq 0) 'The selected channel is the one removed'
    Press 'All';SelectFirst;Press 'Show on screen'
    Check (WaitFor {(Status).on_screen} 15) 'A real comment can be held before disconnecting chat'
    Press 'Keep up';ProgramShot 'before-disconnect'
    foreach($channel in @((Status).channels)){
        $name='Remove channel: '+$channel.platform+' '+[char]0xb7+' '+$channel.name
        Focus $name;Keys ' '
    }
    Check (WaitFor {$s=Status;return @($s.channels).Count -eq 0 -and !$s.ready -and !$s.resources.private_mib} 15) 'Removing the last channel clears readiness and stale memory status'
    Check (WaitFor {@(Get-CimInstance Win32_Process|Where-Object{$_.ParentProcessId -eq $app.Id -and $_.Name -eq 'desktop-studio.exe' -and $_.CommandLine -match '--studio-worker chat'}).Count -eq 0} 15) 'Removing the last channel releases the chat worker'
    ProgramShot 'after-disconnect'
    Check ((Status).on_screen -and (Get-FileHash (Join-Path $Evidence 'before-disconnect.png')).Hash -eq (Get-FileHash (Join-Path $Evidence 'after-disconnect.png')).Hash) 'Disconnecting capture preserves the held comment on Program'
    Press 'Hide';Check (!(Status).on_screen) 'The held comment can still be hidden after disconnecting'
    Check (!(State).output.active -and !(State).recording.active) 'Channel navigation starts no production output'
    Shot 'disconnected'
}catch{Check $false 'Channel navigation completes' $_.Exception.Message}
finally{@{exe=$Exe;sha256=(Get-FileHash $Exe).Hash;checks=$checks}|ConvertTo-Json -Depth 6|Set-Content (Join-Path $Evidence 'results.json');"Evidence: $Evidence"}
if(@($checks|Where-Object{!$_.ok}).Count){exit 1}
