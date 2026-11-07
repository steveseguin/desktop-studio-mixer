# Visible packaged workflows in a disposable show; never clears account storage.
param([Parameter(Mandatory=$true)][string]$SessionFile,[switch]$SkipComments)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/studio-ui-workflows.ps1" -SessionFile $SessionFile -FunctionsOnly -LeaveOpen
$Evidence=Join-Path $Out ('usability-'+(Get-Date -Format yyyyMMdd-HHmmss));New-Item -ItemType Directory $Evidence|Out-Null
try{
    if((State).output.active -or (State).recording.active){throw 'An output is active'}
    Resize 1280 900;Focus 'Chat accounts'
    if(!(Status).practice){if(@((Status).channels).Count){Click 'Advanced';Click 'Try it with pretend chat' ([Windows.Automation.ControlType]::MenuItem)}else{Press 'Try it with pretend chat'}}
    if(!(WaitFor {(Status).ready} 30)){throw 'Practice did not start'}
    Focus '+ Add source';Start-Sleep -Milliseconds 200;Click '+ Add source';ChooseSourceType 'Title / slate';SetText 'Name' 'Usability reference';SetText 'URL / file / text / color' 'Program stays private until Show';Press 'OK'
    $tag='UX'+(Get-Date -Format HHmmss)
    $commentCases=@(
        @{name='unbroken';body=(('W'*500)+' END_TOKEN')},
        @{name='url';body=('https://example.invalid/'+('abcdef0123456789'*60)+'/END_URL')},
        @{name='multiline';body=("First line`n`nSecond line`n"+('word '*100)+'END_LINES')}
    )
    if($SkipComments){$commentCases=@()}
    foreach($case in $commentCases){
        Press 'All';SetText 'Search comments' '';Api @{command='chat.select';id=''}|Out-Null
        $name=$tag+'-'+$case.name;Api @{command='chat.inject';message=@{chatname=$name;chatmessage=$case.body;type='twitch'}}|Out-Null
        Check (WaitFor {@((Api @{command='chat.messages'}).messages|Where-Object{$_.chatname -eq $name}).Count -eq 1}) ($case.name+' arrives intact')
        SetText 'Search comments' $name;SelectFirst;Focus 'Private comment preview';Keys '{PGDN 12}'
        $preview=Find 'Private comment preview'
        $bars=@($preview.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::ScrollBar))|Where-Object{!$_.Current.IsOffscreen -and $_.Current.BoundingRectangle.Height -gt $_.Current.BoundingRectangle.Width})
        Check ($bars.Count -gt 0) ($case.name+' wraps into a scrollable private preview')
        if($bars.Count){$range=$bars[0].GetCurrentPattern([Windows.Automation.RangeValuePattern]::Pattern).Current;Check ($range.Maximum -gt $range.Minimum -and $range.Value -eq $range.Maximum) ($case.name+' keyboard scrolling reaches the end')}
        Focus 'Private comment preview'
        Shot ($case.name+'-preview-end')
        Check (!(Status).on_screen) ($case.name+' inspection stays private')
        Press 'Bar';Press 'Show on screen';Check (WaitFor {(Status).on_screen} 15) ($case.name+' can be shown on Program');ProgramShot ($case.name+'-program');Press 'Hide'
        SetText 'Search comments' 'nothing-should-match-this-search';Check ((Rows).Count -eq 0 -and !(Status).on_screen) 'Empty search keeps output clear'
    }
    SetText 'Search comments' '';Api @{command='chat.select';id=''}|Out-Null
    # File-picker workflows use deliberately damaged copies in this QA folder.
    $show=Get-Content (Join-Path $Out 'show.mixer.json') -Raw
    $cases=@(
        @{name='broken-json';text='{ definitely not json'},
        @{name='wrong-root';text='[1,2,3]'},
        @{name='future-version';text=($show -replace '"version"\s*:\s*\d+','"version":999')},
        @{name='oversize';text=(' '*2100000)}
    )
    foreach($case in $cases){
        $path=Join-Path $Evidence ($case.name+'.mixer.json');[IO.File]::WriteAllText($path,$case.text)
        $before=State;$ids=@($before.scenes.id)+@($before.sources.id)
        Click 'Open show...';SetText 'File name:' $path;Click 'Open' ([Windows.Automation.ControlType]::Button)
        Check (WaitFor {@(foreach($root in Roots){$root.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)|Where-Object{!$_.Current.IsOffscreen -and $_.Current.Name -like '*Your current show has been kept.*'}}).Count -gt 0}) ($case.name+' has a persistent visible import error')
        Shot ($case.name+'-error');Press 'OK'
        $after=State;Check ((($ids -join ',') -eq ((@($after.scenes.id)+@($after.sources.id)) -join ','))) ($case.name+' preserves the active show')
    }
    # A connected channel plus selected tray stresses the minimum supported size.
    if(!@((Status).channels).Count){Click '+ Add';SetText 'Your channel link or name' 'https://www.twitch.tv/lirik';Press 'Add this channel'}
    Check (WaitFor {@((Status).channels).Count -eq 1}) 'A real channel can be added alongside practice'
    Press 'All';SetText 'Search comments' '';SelectFirst
    foreach($size in @(@(1024,768),@(1280,720))){
        Resize $size[0] $size[1]
        $window=@(Roots)[0];$b=$window.Current.BoundingRectangle
        foreach($name in @('Chat accounts','Search comments','Advanced','Show on screen')){Focus $name;Start-Sleep -Milliseconds 200;$c=Find $name;$r=$c.Current.BoundingRectangle;$clip=(Find 'Chat panel').Current.BoundingRectangle;Check (!$c.Current.IsOffscreen -and $r.Top -ge $clip.Top -and $r.Bottom -le $clip.Bottom -and $r.Right -le $clip.Right) "$name is reachable with a connected channel and preview at $($size[0])x$($size[1])"}
        Shot "connected-$($size[0])x$($size[1])"
        Press 'Show on screen';Check (WaitFor {(Status).on_screen} 15) "Show works at $($size[0])x$($size[1])";Press 'Keep up'
        Focus 'Search comments';Keys '{TAB}';Check ([Windows.Automation.AutomationElement]::FocusedElement.Current.Name -eq 'Comments') 'Tab reaches the comment list';Keys '{DOWN}';Focus 'Show on screen';Start-Sleep -Milliseconds 200;$r=(Find 'Show on screen').Current.BoundingRectangle;$clip=(Find 'Chat panel').Current.BoundingRectangle;Check ($r.Top -ge $clip.Top -and $r.Bottom -le $clip.Bottom) 'The next comment remains reachable while another is on air';Shot "next-comment-$($size[0])x$($size[1])"
        Focus 'Hide';Press 'Hide';Check (!(Status).on_screen) 'Hide remains usable while inspecting the next comment'
    }
    Resize 1280 900
    Check (!(State).output.active -and !(State).recording.active) 'Exploration started no public output or recording'
}catch{Check $false 'Usability walkthrough completed' $_.Exception.Message}
finally{try{Api @{command='chat.hide'}|Out-Null;Api @{command='chat.select';id=''}|Out-Null;SetText 'Search comments' ''}catch{};@{exe=$Exe;sha256=(Get-FileHash $Exe).Hash;checks=$checks}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $Evidence 'results.json') -Encoding utf8;"Evidence: $Evidence"}
if(@($checks|Where-Object{!$_.ok}).Count){exit 1}
