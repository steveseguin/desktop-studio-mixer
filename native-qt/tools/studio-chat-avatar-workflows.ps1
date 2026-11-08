# Packaged UI journey: real public photos, then local-only malformed-photo fixtures.
param([Parameter(Mandatory=$true)][string]$SessionFile)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/studio-ui-workflows.ps1" -SessionFile $SessionFile -FunctionsOnly -LeaveOpen
if($Out -notmatch '[\\/]build-integration[\\/]ui-exploration[\\/]'){throw 'Avatar fixtures require an isolated QA profile'}
$Evidence=Join-Path $Out ('avatar-workflow-'+(Get-Date -Format yyyyMMdd-HHmmss));New-Item -ItemType Directory $Evidence|Out-Null
$proof=@()
function ResumeFeed {
    SetText 'Search comments' ''
    try {if(!(Find 'Back to live').Current.IsOffscreen){Press 'Back to live'}} catch {}
}
try {
    if((State).recording.busy -or (State).output.active){throw 'Run this avatar journey with production idle'}
    Resize 1280 900;Focus 'Search comments';Press 'All'
    foreach($type in @('youtube','twitch','kick')) {
        ResumeFeed
        $found=WaitFor {@((Api @{command='chat.messages'}).messages|Where-Object {$_.type -eq $type -and $_.chatimg -and !$_.practice}).Count -gt 0} 75
        Check $found "$type supplies real public avatar URLs"
        if(!$found){continue}
        # Freeze the review list before searching: a busy channel can otherwise
        # evict an older comment while the screenshot delay runs.
        SelectFirst
        $sample=@((Api @{command='chat.messages'}).messages|Where-Object {$_.type -eq $type -and $_.chatimg -and !$_.practice})[-1]
        SetText 'Search comments' $sample.chatname;Start-Sleep -Seconds 4;Focus 'Search comments';Shot "photo-$type"
        SelectFirst;Check (!(Status).on_screen -and !!(Status).selected) "$type avatar-row selection remains private"
        Press 'Show on screen';Check (WaitFor {(Status).on_screen} 15) "$type comment can still be featured"
        ProgramShot "featured-$type";Press 'Hide'
        $proof+=@{platform=$type;author=$sample.chatname;avatar=$sample.chatimg;stats=(Status).avatars}
    }
    ResumeFeed;Click 'Advanced';Click 'Try it with pretend chat' ([Windows.Automation.ControlType]::MenuItem)
    Check (WaitFor {(Status).practice -and (Status).ready} 15) 'Local practice mode starts for avatar fallback fixtures'
    SetText 'Search comments' 'AVATAR-FALLBACK';Start-Sleep -Seconds 2
    [void](WaitFor {(Status).avatars.in_flight -eq 0} 12)
    $before=(Status).avatars.requests
    $urls=@('','file:///C:/Windows/win.ini','http://yt3.ggpht.com/a','https://127.0.0.1/a','https://yt3.ggpht.com.invalid/a','data:image/png;base64,a',('https://yt3.ggpht.com/'+('x'*2050)))
    for($i=0;$i -lt $urls.Count;$i++) {
        $reply=Api @{command='chat.inject';message=@{chatname="Fallback $i";chatmessage="AVATAR-FALLBACK $i comment remains readable";chatimg=$urls[$i];type='youtube'}}
        if($reply.error){throw $reply.error}
    }
    Check (WaitFor {@((Api @{command='chat.messages'}).messages|Where-Object {$_.chatmessage -like 'AVATAR-FALLBACK*'}).Count -eq $urls.Count} 15) 'Missing and malformed avatar fields preserve every comment'
    Start-Sleep -Seconds 2;Focus 'Search comments';Shot 'fallback-initials'
    Check ((Status).avatars.requests -eq $before) 'Malformed avatar fields do not start native image requests'
    $bad=@((Api @{command='chat.messages'}).messages|Where-Object {$_.chatmessage -like 'AVATAR-FALLBACK*' -and $_.chatimg})
    Check ($bad.Count -eq 0) 'The native feed drops disallowed avatar URLs'
    SelectFirst;Check (!(Status).on_screen) 'Fallback initials retain private selection behavior'
    ResumeFeed;Click 'Advanced';Click 'Stop practising' ([Windows.Automation.ControlType]::MenuItem)
    Check (!(Status).practice -and @((Api @{command='chat.messages'}).messages|Where-Object {$_.chatmessage -like 'AVATAR-FALLBACK*'}).Count -eq 0) 'Leaving practice removes the fallback fixtures'
    Check ((Status).avatars.cache_kib -le 4096 -and (Status).avatars.in_flight -le 4) 'Native avatar memory and concurrent loads remain bounded'
    Focus 'Search comments';Shot 'returned-to-real-chat'
} catch {Check $false 'Avatar UI journey completes' $_.Exception.Message}
finally {
    @{exe=$Exe;sha256=(Get-FileHash $Exe).Hash;checks=$checks;proof=$proof;avatars=(Status).avatars}|ConvertTo-Json -Depth 8|Set-Content "$Evidence/results.json" -Encoding utf8
    "Evidence: $Evidence"
}
if(@($checks|Where-Object{!$_.ok}).Count){exit 1}
