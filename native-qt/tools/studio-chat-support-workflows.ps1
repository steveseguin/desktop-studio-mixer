# Packaged UI workflows with synthetic support events routed through the real
# practice capture, SSN engine, native panel and Program renderer. No purchases.
param([Parameter(Mandatory=$true)][string]$SessionFile)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/studio-ui-workflows.ps1" -SessionFile $SessionFile -LeaveOpen -FunctionsOnly
$Evidence=Join-Path $Out ('support-'+(Get-Date -Format yyyyMMdd-HHmmss-fff));New-Item -ItemType Directory -Path $Evidence|Out-Null
$token='SupportQA'+(Get-Date -Format HHmmss)
function Messages { (Api @{command='chat.messages'}).messages }
function Fixture($name) { @(Messages|Where-Object{$_.chatname -eq $name}) }
try {
    Resize 1280 900
    if(!(Status).practice){
        if(@((Status).channels).Count){Click 'Advanced';Click 'Try it with pretend chat' ([Windows.Automation.ControlType]::MenuItem)}
        else{Press 'Try it with pretend chat'}
    }
    if(!(WaitFor {(Status).ready} 30)){throw 'Practice engine did not become ready'}
    Press 'All';SetText 'Search comments' ''
    $fixtures=@(
        @{kind='donation';type='youtube';chatmessage='Thank you for the show';hasDonation='$25.00'},
        @{kind='kickgift';type='kick';chatmessage='A gift for the stream';hasDonation='100 KICKs';event='gift'},
        @{kind='emptygift';type='kick';chatmessage='';hasDonation='1 KICK';event='gift';expectedBody='Sent a gift'},
        @{kind='membership';type='youtube';chatmessage='';membership='Gifted 5 memberships';event='subscription_gift';expectedBody='Sent a gift'},
        @{kind='unpricedgift';type='tiktok';chatmessage='Sent a Rose';event='gift'},
        @{kind='literal';type='youtube';chatmessage='Literal <b>gift</b> & thanks';hasDonation='EUR 12.50';membership='6 months'},
        @{kind='literalmetadata';type='youtube';chatmessage='Gift metadata stays literal';hasDonation='USD &lt;10';expectedDonation='USD <10';membership='Member &lt;b&gt;Gold&lt;/b&gt; &amp; fans';expectedMembership='Member <b>Gold</b> & fans'},
        @{kind='longlabel';type='youtube';chatmessage='The full membership detail remains readable';event='membermilestone';membership=('Membership '+('long '*22)+'END_LABEL')},
        @{kind='subscriberchat';type='twitch';chatmessage='An ordinary comment from a subscriber';membership='SUBSCRIBER';thanks=$false},
        @{kind='imagepost';type='twitch';chatmessage='';contentimg='https://example.invalid/image.gif';expectedBody='Shared an image';thanks=$false},
        @{kind='subscriberimage';type='twitch';chatmessage='';contentimg='https://example.invalid/image.gif';membership='SUBSCRIBER';expectedBody='Shared an image';thanks=$false},
        @{kind='imagecaption';type='kick';chatmessage='Keep the original caption';contentimg='https://example.invalid/image.gif';thanks=$false}
    )
    foreach($fixture in $fixtures){
        Press 'All';SetText 'Search comments' '';Api @{command='chat.select';id=''}|Out-Null
        $name=$token+'-'+$fixture.kind;$message=@{chatname=$name;chatmessage=$fixture.chatmessage;type=$fixture.type}
        foreach($key in @('hasDonation','membership','event','contentimg')){if($fixture.ContainsKey($key)){$message[$key]=$fixture[$key]}}
        Api @{command='chat.inject';message=$message}|Out-Null
        if(!(WaitFor {@(Fixture $name).Count -eq 1} 12)){Check $false ($fixture.kind+' reaches the panel');continue}
        $captured=@(Fixture $name)[0];$expectedBody=if($fixture.expectedBody){$fixture.expectedBody}else{$fixture.chatmessage}
        $expectedDonation=if($fixture.expectedDonation){$fixture.expectedDonation}else{$fixture.hasDonation}
        $expectedMembership=if($fixture.expectedMembership){$fixture.expectedMembership}else{$fixture.membership}
        Check ($captured.chatmessage -ceq $expectedBody -and $captured.type -eq $fixture.type) ($fixture.kind+' keeps readable text and platform')
        Check ((!$fixture.event -or $captured.event -eq $fixture.event) -and (!$expectedDonation -or $captured.hasDonation -ceq $expectedDonation) -and (!$expectedMembership -or $captured.membership -ceq $expectedMembership)) ($fixture.kind+' keeps support details')
        Press 'Thanks';SetText 'Search comments' $name
        $inThanks=if($fixture.ContainsKey('thanks')){$fixture.thanks}else{$true}
        Check ((Rows).Count -eq $(if($inThanks){1}else{0})) ($fixture.kind+' is correctly classified in Thanks')
        if((Rows).Count -ne 1){Press 'All'}
        SelectFirst
        Check (!(Status).on_screen) ($fixture.kind+' selection stays private')
        $preview=@((Find 'Private comment preview').FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)|ForEach-Object{$_.Current.Name}) -join "`n"
        Check ((!$expectedDonation -or $preview.Contains($expectedDonation)) -and (!$expectedMembership -or $preview.Contains($expectedMembership))) ($fixture.kind+' private preview exposes the full support details')
        Focus 'Search comments';Shot ($fixture.kind+'-private')
        foreach($style in @('Bar','Bubble','Big')){
            Press $style;Press 'Show on screen'
            $shown=WaitFor {(Status).on_screen} 12
            Check ($shown -and (Status).on_air.chatmessage -ceq $expectedBody) ($fixture.kind+' shows in '+$style)
            if($shown){ProgramShot ($fixture.kind+'-'+$style.ToLower())}
            Press 'Hide';SetText 'Search comments' $name;SelectFirst
        }
        Api @{command='chat.select';id=''}|Out-Null
    }
    Press 'All';SetText 'Search comments' ''
    $streakName=$token+'-streak';$streakId=$token+'-rose'
    foreach($count in @(1,3,5,3,5)) {
        Api @{command='chat.inject';message=@{chatname=$streakName;chatmessage=('Sent Rose x'+$count);type='tiktok';event='gift';hasDonation=($count.ToString()+' coins');meta=@{tiktokGiftStreakId=$streakId;tiktokGiftCount=$count;giftName='Rose'}}}|Out-Null
        Start-Sleep -Milliseconds 500
    }
    $streak=@(Fixture $streakName)
    Check ($streak.Count -eq 1 -and $streak[0].chatmessage -eq 'Sent Rose x5' -and $streak[0].hasDonation -eq '5 coins') 'Gift streak updates one row and ignores older counts'
    if($streak.Count -eq 1){
        $stableId=$streak[0].id;SetText 'Search comments' $streakName;SelectFirst;Press 'Save'
        Api @{command='chat.inject';message=@{chatname=$streakName;chatmessage='Sent Rose x8';type='tiktok';event='gift';hasDonation='8 coins';meta=@{tiktokGiftStreakId=$streakId;tiktokGiftCount=8}}}|Out-Null
        Check (WaitFor { $m=@(Fixture $streakName);return $m.Count -eq 1 -and $m[0].id -eq $stableId -and $m[0].saved -and $m[0].chatmessage -eq 'Sent Rose x8' }) 'Gift update preserves the saved selection while paused'
        Check (!(Status).on_screen) 'An incoming gift update cannot put itself on output'
        Shot 'streak-final';Press 'Bar';Press 'Show on screen';Check (WaitFor {(Status).on_screen -and (Status).on_air.hasDonation -eq '8 coins'}) 'The updated gift count reaches output';ProgramShot 'streak-program';Press 'Hide'
    }
    Check (!(State).output.active -and !(State).recording.active) 'No public stream or recording was started'
} catch {Check $false 'Support workflow completed' $_.Exception.Message}
finally {
    try{Api @{command='chat.hide'}|Out-Null;Api @{command='chat.select';id=''}|Out-Null;SetText 'Search comments' '';Press 'All'}catch{}
    @{exe=$Exe;sha256=(Get-FileHash $Exe).Hash;checks=$checks}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $Evidence 'support-results.json') -Encoding utf8
    "Evidence: $Evidence"
}
if(@($checks|Where-Object{!$_.ok}).Count){exit 1}
