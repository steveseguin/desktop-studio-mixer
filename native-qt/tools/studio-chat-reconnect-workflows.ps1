# Operates a packaged app connected to real public chat. Use disposable accounts.
param([Parameter(Mandatory=$true)][string]$SessionFile)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/studio-ui-workflows.ps1" -SessionFile $SessionFile -FunctionsOnly -LeaveOpen
if($Out -notmatch '[\\/]build-integration[\\/]ui-exploration[\\/]'){throw 'Sign-out requires an isolated QA profile'}
$Evidence=Join-Path $Out ('chat-reconnect-'+(Get-Date -Format yyyyMMdd-HHmmss));New-Item -ItemType Directory $Evidence|Out-Null
function ChannelMessages($id){@((Api @{command='chat.messages'}).messages|Where-Object channel -eq $id)}
function Accounts {Focus 'Chat accounts';Keys '{ESC}';Click 'Chat accounts'}
try{
 if((State).recording.active -or (State).output.active){throw 'Run this account workflow with production idle'}
 if((Status).account.open){throw 'Close the account window first'}
 $back=try{Find 'Back to live'}catch{$null};if($back -and !$back.Current.IsOffscreen){Press 'Back to live'}
 Press 'All';SetText 'Search comments' ''
 $youtube=@((Status).channels|Where-Object platform -eq 'YouTube')|Select-Object -First 1
 if(!$youtube){throw 'Connect a live YouTube channel before this workflow'}
 $link=if($youtube.resolve){$youtube.resolve}else{$youtube.url}
 $otherIds=@((Status).channels|Where-Object id -ne $youtube.id|ForEach-Object id)
 Check (WaitFor {(ChannelMessages $youtube.id).Count -gt 0} 90) 'YouTube has real comments before reconnecting'
 if(!($checks[-1].ok)){throw 'No positive YouTube capture baseline'}
 for($cycle=1;$cycle -le 3;$cycle++){
  # The handle becomes the public channel title after resolution.
  $youtube=@((Status).channels|Where-Object id -eq $youtube.id)|Select-Object -First 1
  Focus ('Remove channel: YouTube '+[char]0xb7+' '+$youtube.name);Keys ' '
  Check (@((Status).channels|Where-Object id -eq $youtube.id).Count -eq 0) "Cycle $cycle removes the old capture"
  Focus '+ Add';Click '+ Add'
  if(!(WaitFor {try{return !(Find 'Your channel link or name').Current.IsOffscreen}catch{return $false}} 10)){throw 'The channel entry did not open'}
  SetText 'Your channel link or name' $link;Press 'Add this channel'
  $youtube=@((Status).channels|Where-Object platform -eq 'YouTube')|Select-Object -First 1
  if(!$youtube){throw 'Replacement YouTube channel did not appear'}
  Check (WaitFor {(ChannelMessages $youtube.id).Count -gt 0} 90) "Cycle $cycle receives real comments from the replacement capture"
  Check (@(Compare-Object $otherIds @((Status).channels|Where-Object id -ne $youtube.id|ForEach-Object id)).Count -eq 0) "Cycle $cycle preserves the other channels"
  Focus 'Search comments';Shot "reconnected-$cycle"
 }
 $channels=@((Status).channels);$before=@($channels.id)
 Accounts;Click 'Sign out of all chat accounts...' ([Windows.Automation.ControlType]::MenuItem);Click 'Cancel' ([Windows.Automation.ControlType]::Button)
 Check (!(Status).account.clearing -and @(Compare-Object $before @((Status).channels.id)).Count -eq 0) 'Cancelling sign-out preserves every connected channel'
 $initial=@((Api @{command='chat.messages'}).messages.id)
 Accounts;Click 'Sign out of all chat accounts...' ([Windows.Automation.ControlType]::MenuItem);Click 'Yes' ([Windows.Automation.ControlType]::Button)
 Check (WaitFor {(Status).account.notice -eq 'Signed out of chat accounts on this device.' -and !(Status).account.clearing} 30) 'Sign-out completes'
 Check (@(Compare-Object $before @((Status).channels.id)).Count -eq 0) 'Sign-out preserves all channel identities'
 foreach($channel in $channels|Where-Object{$_.platform -in @('YouTube','Twitch','Kick')}){
  Check (WaitFor {@((ChannelMessages $channel.id)|Where-Object{$_.id -notin $initial}).Count -gt 0} 90) ($channel.platform+' receives new anonymous comments after sign-out')
 }
 Check (!(State).recording.active -and !(State).output.active) 'Reconnect and sign-out leave production idle'
 Focus 'Search comments';Shot 'after-signout'
}catch{Check $false 'Reconnect workflow completes' $_.Exception.Message}
@{exe=$Exe;sha256=(Get-FileHash $Exe).Hash;checks=$checks;meaning='Real capture reconnection and sign-out liveness; independent source comparisons qualify delivery completeness separately.'}|ConvertTo-Json -Depth 8|Set-Content "$Evidence/results.json"
"Evidence: $Evidence"
if(@($checks|Where-Object{!$_.ok}).Count){exit 1}
