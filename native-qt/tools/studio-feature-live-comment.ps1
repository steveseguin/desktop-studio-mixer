# Real chat through visible controls, then an independent receiver screenshot.
# The caller chooses a retained message and verifies its channel is broadcasting.
param([Parameter(Mandatory=$true)][string]$SessionFile,[Parameter(Mandatory=$true)][string]$ReceiverSessionFile,[Parameter(Mandatory=$true)][string]$MessageId)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/studio-ui-workflows.ps1" -SessionFile $SessionFile -FunctionsOnly -LeaveOpen
$message=@((Api @{command='chat.messages'}).messages|Where-Object{$_.id -eq $MessageId})[0]
if(!$message -or $message.practice){throw 'Choose a retained real comment'}
$Evidence=Join-Path $Out ('featured-'+$message.type+'-'+(Get-Date -Format yyyyMMdd-HHmmss));New-Item -ItemType Directory $Evidence|Out-Null
try{
    Focus 'Chat accounts';SetText 'Search comments' $message.chatmessage
    if(!(WaitFor {(Rows).Count -gt 0})){throw 'The selected real comment is not in the visible feed'}
    foreach($row in Rows){$row.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select();Start-Sleep -Milliseconds 100;if((Status).selected -eq $MessageId){break}}
    Check ((Status).selected -eq $MessageId -and !(Status).on_screen) 'Select the exact real comment privately through the feed'
    Shot 'private-preview';Press 'Show on screen'
    Check (WaitFor {(Status).on_screen -and (Status).on_air.id -eq $MessageId} 15) 'Show the chosen comment on Program'
    Press 'Keep up';ProgramShot 'program';Shot 'on-air';Start-Sleep -Seconds 3
    $receiver=Get-Content $ReceiverSessionFile -Raw|ConvertFrom-Json
    node "$PSScriptRoot/mcp/studio-cli.mjs" --discovery $receiver.control screenshot program --out (Join-Path $Evidence 'received.png')|Out-Null
    Check ($LASTEXITCODE -eq 0) 'Capture the independent receiver while the comment is featured'
    Start-Sleep -Seconds 5;Press 'Hide';Check (!(Status).on_screen) 'Hide the featured comment'
    Check ((State).output.live -and !(Status).practice) 'Live output and real chat remain active'
    $message|ConvertTo-Json -Depth 8|Set-Content (Join-Path $Evidence 'comment.json') -Encoding utf8
}catch{Check $false 'Live comment journey completed' $_.Exception.Message}
finally{SetText 'Search comments' '';Api @{command='chat.select';id=''}|Out-Null;@{exe=$Exe;sha256=(Get-FileHash $Exe).Hash;checks=$checks}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $Evidence 'results.json') -Encoding utf8;Write-Output "Evidence: $Evidence"}
if(@($checks|Where-Object{!$_.ok}).Count){exit 1}
