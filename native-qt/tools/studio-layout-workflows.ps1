# Run after studio-ui-workflows.ps1 -LeaveOpen; pass that run's ui-session.json.
# Actual packaged UI workflows. Fixtures and output stay in the isolated QA profile.
param([Parameter(Mandatory=$true)][string]$SessionFile)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/studio-ui-workflows.ps1" -SessionFile $SessionFile -LeaveOpen -FunctionsOnly
$Evidence=Join-Path $Out ('layout-'+(Get-Date -Format yyyyMMdd-HHmmss-fff));New-Item -ItemType Directory -Path $Evidence|Out-Null
function Choose($name,$value) {
    $combo=Find $name ([Windows.Automation.ControlType]::ComboBox)
    $combo.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern).Expand()
    if(!(WaitFor {try{$null=Find $value ([Windows.Automation.ControlType]::ListItem);return $true}catch{return $false}})){throw "No choice: $value"}
    Click $value ([Windows.Automation.ControlType]::ListItem)
    if(!(WaitFor {$combo.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).Current.Value -eq $value})){throw 'Choice was not applied'}
}
function HorizontalBars {
    # Qt reports Orientation=None for some scrollbars; inspect actual geometry.
    foreach($window in Roots){$window.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::ScrollBar))|Where-Object{!$_.Current.IsOffscreen -and $_.Current.BoundingRectangle.Width -gt $_.Current.BoundingRectangle.Height}}
}
try {
    foreach($size in @(@(1024,768),@(1280,720),@(1366,768),@(1920,1080))) {
        Resize $size[0] $size[1];$bounds=@(Roots)[0].Current.BoundingRectangle
        $toolbar=@('Invite guest','Wireless camera','Chat','Guest talkback','Output settings','GO LIVE','Record...','Private break','Mute mics','Video settings','Tools')
        $buttons=@($toolbar|ForEach-Object{Find $_})
        Check (@($buttons|Where-Object{$b=$_.Current.BoundingRectangle;$b.Right -gt $bounds.Right -or $b.Left -lt $bounds.Left -or $b.Height -ne 36}).Count -eq 0) "Command bar fits and has uniform heights at $($size[0])x$($size[1])"
        Check (@(HorizontalBars).Count -eq 0) "No sideways workspace or panel scrolling at $($size[0])x$($size[1])"
        Shot "01-layout-$($size[0])x$($size[1])"
    }
    Resize 1024 768
    Click 'Video settings'
    $canvas=Find 'Canvas format' ([Windows.Automation.ControlType]::ComboBox)
    $compositor=Find 'Compositor' ([Windows.Automation.ControlType]::ComboBox)
    for($tab=0;$tab -lt 6 -and [Windows.Automation.AutomationElement]::FocusedElement.Current.Name -ne 'Canvas format';$tab++){Keys '{TAB}'}
    $canvasReached=[Windows.Automation.AutomationElement]::FocusedElement.Current.Name -eq 'Canvas format';Keys '{TAB}'
    Check ($canvasReached -and $canvas.Current.IsKeyboardFocusable -and $compositor.Current.IsKeyboardFocusable -and [Windows.Automation.AutomationElement]::FocusedElement.Current.Name -eq 'Compositor') 'Video controls are exposed to keyboard and accessibility navigation'
    Choose 'Canvas format' '1920x1080 / 30 fps'
    Check (WaitFor {(State).canvas.width -eq 1920 -and (State).canvas.height -eq 1080}) 'Video dialog changes the actual canvas'
    Choose 'Canvas format' '1280x720 / 30 fps';Shot '02-video-settings';Keys '{ESC}'
    Check ((State).canvas.width -eq 1280) 'Canvas choice persists after closing Video settings'
    Click 'Tools';Click 'Local outputs...' ([Windows.Automation.ControlType]::MenuItem)
    foreach($name in @('Spout output','Virtual camera','Guest Program return','Unregister camera')) {Check (!(Find $name).Current.IsOffscreen) "$name is discoverable in Local outputs"}
    Shot '03-local-outputs';Keys '{ESC}'
    Click 'Tools';Click 'Hotkeys...' ([Windows.Automation.ControlType]::MenuItem);Shot '04-hotkeys';Keys '{ESC}'
    Check (!(State).local_outputs.spout -and !(State).local_outputs.virtual_camera -and !(State).output.active) 'Browsing configuration does not start an output'
    $oldName=@((State).scenes|Where-Object{$_.id -eq (State).preview})[0].name
    $longName='A long scene name '+('W'*180)
    Click 'Rename';SetText 'Scene name' $longName;Press 'OK';Shot '05-long-scene'
    Check (@(HorizontalBars).Count -eq 0) 'A long scene name does not force sideways scrolling'
    Click 'Rename';SetText 'Scene name' $oldName;Press 'OK'
    # Select a row explicitly: Home can move the list cursor without selecting
    # a source after another workflow cleared the selection in a reused show.
    $sourceList=Find 'Sources in selected scene' ([Windows.Automation.ControlType]::List)
    Focus '+ Add source';Keys '+{TAB}{HOME}'
    $sourceRow=@($sourceList.FindAll([Windows.Automation.TreeScope]::Children,[Windows.Automation.Condition]::TrueCondition))[0]
    $sourceRow.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
    foreach($edge in @('Left','Top','Right','Bottom')) {
        $crop=@((Roots)[0].FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)|Where-Object{$_.Current.AutomationId.EndsWith('.crop'+$edge)})[0]
        $crop.SetFocus();Start-Sleep -Milliseconds 200
        $field=$crop.Current.BoundingRectangle
        $viewport=@((Roots)[0].FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)|Where-Object{$_.Current.AutomationId.EndsWith('.sourcesPanelScroll.qt_scrollarea_viewport')})[0].Current.BoundingRectangle
        Check ($crop.Current.IsEnabled -and !$crop.Current.IsOffscreen -and $field.Left -ge $viewport.Left -and $field.Right -le $viewport.Right -and $field.Top -ge $viewport.Top -and $field.Bottom -le $viewport.Bottom) ($edge+' crop control remains visible when focused in the narrow inspector')
    }
    Shot '05-crop-controls'
    $ffmpeg=Join-Path (Split-Path $Exe) 'ffmpeg/bin/ffmpeg.exe'
    $tone=Join-Path $Evidence 'tone.wav'
    & $ffmpeg -hide_banner -loglevel error -f lavfi -i 'sine=frequency=440:sample_rate=48000' -t 12 -c:a pcm_s16le $tone
    if($LASTEXITCODE){throw 'Could not create local audio fixture'}
    # Repeated runs reuse the same QA profile to avoid duplicating browser caches.
    # Silence previous fixtures through their actual controls before measuring this one.
    foreach($prior in @((State).sources|Where-Object{$_.has_audio -and !$_.muted})) {
        (Find ($prior.name+' - Mute') ([Windows.Automation.ControlType]::CheckBox)).GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle()
    }
    $toneName='Layout tone '+(Get-Date -Format HHmmss)
    Focus '+ Add source';Start-Sleep -Milliseconds 200
    $action=(Find '+ Add source').Current.BoundingRectangle
    $panel=@((Roots)[0].FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)|Where-Object{$_.Current.AutomationId.EndsWith('.sourcesPanelScroll')})[0].Current.BoundingRectangle
    Check ($action.Top -ge $panel.Top -and $action.Bottom -le $panel.Bottom) 'Add source becomes visible when focused after scrolling the inspector'
    Click '+ Add source';ChooseSourceType 'Video / audio file';SetText 'Name' $toneName;SetText 'URL / file / text / color' $tone;Press 'OK'
    Check (WaitFor {@((State).sources|Where-Object{$_.name -eq $toneName -and $_.has_audio -and $_.peak -gt 0}).Count -eq 1} 20) 'Audio file added through the dialog reaches the real mixer'
    Check (@(HorizontalBars).Count -eq 0) 'Populated audio controls fit at 1024 pixels'
    $mute=Find ($toneName+' - Mute');$mute.SetFocus();$mute.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle()
    # The meter intentionally decays; its displayed silence floor is -60 dBFS.
    Check (WaitFor {@((State).sources|Where-Object{$_.name -eq $toneName -and $_.muted -and $_.peak -le .001}).Count -eq 1}) 'Track mute brings the meter below its silence floor'
    $mute.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle()
    Check (WaitFor {@((State).sources|Where-Object{$_.name -eq $toneName -and !$_.muted -and $_.peak -gt 0}).Count -eq 1}) 'Unmute restores audio in the mixer'
    Shot '06-audio-controls'
    # Recording is intentionally short and fails before starting if disk headroom is inadequate.
    if((Get-PSDrive C).Free -lt 600MB){throw 'At least 600 MiB free is required for this recording workflow'}
    Resize 1280 720
    $recordButtonBounds=(Find 'Record...').Current.BoundingRectangle
    $privacyButtonBounds=(Find 'Private break').Current.BoundingRectangle
    Click 'Record...'
    if(!(WaitFor {try{$null=Find 'File name:' ([Windows.Automation.ControlType]::Edit);return $true}catch{try{$null=Find 'Yes' ([Windows.Automation.ControlType]::Button);return $true}catch{return $false}}})){throw 'No recording confirmation or save dialog appeared'}
    # Practice acknowledgement is retained within a run, so repeated recordings
    # may go directly to the file picker.
    $confirm=try{Find 'Yes' ([Windows.Automation.ControlType]::Button)}catch{$null}
    if($confirm){Click 'Yes' ([Windows.Automation.ControlType]::Button)}
    if(!(WaitFor {try{$null=Find 'File name:' ([Windows.Automation.ControlType]::Edit);return $true}catch{return $false}})){throw 'Recording save dialog did not open'}
    $clip=Join-Path $Evidence 'toolbar-recording.mkv';SetText 'File name:' $clip;Click 'Save' ([Windows.Automation.ControlType]::Button)
    Check (WaitFor {(State).recording.active}) 'Record toolbar starts recording through the save dialog'
    $stopBounds=(Find 'Stop recording').Current.BoundingRectangle;$privacyNow=(Find 'Private break').Current.BoundingRectangle
    Check ($stopBounds -eq $recordButtonBounds -and $privacyNow -eq $privacyButtonBounds) 'Starting recording preserves the Record and privacy button positions'
    $recordClock=[Diagnostics.Stopwatch]::StartNew()
    Shot '07-recording';Start-Sleep -Seconds 2
    $mute.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle();$mutedAt=$recordClock.Elapsed.TotalSeconds;Start-Sleep -Seconds 3
    $mute.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle();$audibleAt=$recordClock.Elapsed.TotalSeconds;Start-Sleep -Seconds 3
    $breakAt=$recordClock.Elapsed.TotalSeconds
    Press 'Private break';Shot '08-recording-private-break';Start-Sleep -Seconds 3
    Press 'Resume show';Start-Sleep -Seconds 3;Press 'Stop recording'
    Check (WaitFor {!(State).recording.busy}) 'Stop recording finalizes the file'
    & $ffmpeg -v error -i $clip -f null - 2> (Join-Path $Evidence 'decode-errors.txt')
    Check ($LASTEXITCODE -eq 0 -and (Get-Item -LiteralPath $clip).Length -gt 0) 'Recorded video and audio decode completely'
    & $ffmpeg -v error -i $clip -map 0:v:0 -vf fps=1 -f framemd5 (Join-Path $Evidence 'recorded-frames.txt')
    $hashes=@(Get-Content (Join-Path $Evidence 'recorded-frames.txt')|Where-Object{$_ -match ',\s*([a-f0-9]{32})$'}|ForEach-Object{$Matches[1]})
    Check ($hashes.Count -ge 7 -and @($hashes|Select-Object -Unique).Count -ge 2) 'Recording contains the normal picture and the private-break picture'
    & $ffmpeg -v error -ss ($breakAt+1).ToString('F2',[Globalization.CultureInfo]::InvariantCulture) -i $clip -frames:v 1 (Join-Path $Evidence '09-recorded-private-break.png')
    if($LASTEXITCODE){throw 'Recorded frame extraction failed'}
    foreach($segment in @(@{name='muted';time=$mutedAt},@{name='audible';time=$audibleAt})) {
        $start=($segment.time+.7).ToString('F2',[Globalization.CultureInfo]::InvariantCulture)
        $volumeLog=Join-Path $Evidence ($segment.name+'-volume.txt')
        $process=Start-Process $ffmpeg -WindowStyle Hidden -Wait -PassThru -ArgumentList @('-hide_banner','-nostats','-ss',$start,'-i',('"'+$clip+'"'),'-t','1.5','-vn','-af','volumedetect','-f','null','-') -RedirectStandardError $volumeLog -RedirectStandardOutput (Join-Path $Evidence ($segment.name+'-stdout.txt'))
        $volume=Get-Content $volumeLog -Raw
        if($process.ExitCode -ne 0 -or $volume -notmatch 'max_volume: (-?inf|[-\d.]+) dB'){throw 'Recorded audio measurement failed'}
        $peakDb=if($Matches[1] -eq '-inf'){[double]::NegativeInfinity}else{[double]::Parse($Matches[1],[Globalization.CultureInfo]::InvariantCulture)}
        if($segment.name -eq 'muted'){Check ($peakDb -lt -65) 'Recorded output is silent while the track is muted' "$peakDb dB"}
        else {Check ($peakDb -gt -40) 'Recorded output contains audio after unmuting' "$peakDb dB"}
    }
    Check (!(State).output.active -and !(State).private_break) 'The workflow ends without a public stream or private break active'
} catch {Check $false 'Layout workflow stopped' $_.Exception.Message}
finally {
    # A failed UI operation must not leave an owned recording running.
    try{if((State).recording.active){Api @{command='record.stop'}|Out-Null}}catch{}
    @{exe=$Exe;sha256=(Get-FileHash $Exe).Hash;checks=$checks}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $Evidence 'layout-results.json') -Encoding utf8
    @{evidence=$Evidence}|ConvertTo-Json|Set-Content (Join-Path $Out 'latest-layout-workflow.json') -Encoding utf8
    "Evidence: $Evidence"
}
if(@($checks|Where-Object{!$_.ok}).Count){exit 1}
exit 0
