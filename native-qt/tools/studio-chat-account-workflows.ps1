# Uses actual platform pages in the packaged app. Never fills credentials.
# Only run with an isolated QA profile: the sign-out workflow clears its sessions.
param([Parameter(Mandatory=$true)][string]$SessionFile,[switch]$StorageOnly)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/studio-ui-workflows.ps1" -SessionFile $SessionFile -LeaveOpen -FunctionsOnly
$studioApp=$app
if($Out -notmatch '[\\/]build-integration[\\/]ui-exploration[\\/]'){throw 'Account walkthrough requires the isolated QA profile'}
$Evidence=Join-Path $Out ('accounts-workflow-'+(Get-Date -Format yyyyMMdd-HHmmss));New-Item -ItemType Directory $Evidence|Out-Null
$observations=[Collections.ArrayList]::new()
function AccountWorker {
    $worker=Get-CimInstance Win32_Process|Where-Object{$_.ParentProcessId -eq $studioApp.Id -and $_.Name -eq 'desktop-studio.exe' -and $_.CommandLine -match '--studio-worker chat'}|Select-Object -First 1
    if(!$worker){throw 'Owned chat worker not found'}
    Get-Process -Id $worker.ProcessId
}
function HasButton($name) {try{return !(Find $name ([Windows.Automation.ControlType]::Button)).Current.IsOffscreen}catch{return $false}}
try {
    Resize 1280 900
    if(!(WaitFor {(Status).ready -and (Status).page_count -ge (@((Status).channels).Count+3)} 30)){throw 'Connected capture pages did not become ready'}
    Focus 'Chat accounts';Keys '{ESC}'
    Click 'Chat accounts';Click 'Sign out of all chat accounts...' ([Windows.Automation.ControlType]::MenuItem);Click 'Yes' ([Windows.Automation.ControlType]::Button)
    if(!(WaitFor {(Status).account.notice -eq 'Signed out of chat accounts on this device.' -and !(Status).account.clearing} 25)){throw 'Could not establish a clean QA browser baseline'}
    $platforms=@(
        @{name='Twitch';link='https://www.twitch.tv/gaules';button='Log In'},
        @{name='Kick';link='https://kick.com/coringa';button='Log In'},
        @{name='TikTok';link='https://www.tiktok.com/@tiktok/live'},
        @{name='Facebook';link='https://www.facebook.com/nasa/live'},
        @{name='Instagram';link='https://www.instagram.com/nasa/live'},
        @{name='X';link='https://x.com/nasa'},
        @{name='Rumble';link='https://rumble.com/c/RTNews/live'}
    )
    if($StorageOnly){$platforms=@($platforms|Where-Object{$_.name -eq 'Rumble'})}
    foreach($platform in $platforms){
        $app=$studioApp;$added=$false
        $channel=@((Status).channels|Where-Object{$_.platform -eq $platform.name})|Select-Object -First 1
        if(!$channel){Click '+ Add';SetText 'Your channel link or name' $platform.link;Press 'Add this channel';$added=$true}
        if(!(WaitFor {(Status).ready} 30)){throw 'Chat is unavailable'}
        if($platform.name -eq 'Rumble' -and !$StorageOnly){
            # Establish the preference baseline immediately before this check;
            # the connected live page also uses the platform's shared profile.
            Click 'Chat accounts';Click 'Sign out of all chat accounts...' ([Windows.Automation.ControlType]::MenuItem);Click 'Yes' ([Windows.Automation.ControlType]::Button)
            Start-Sleep -Milliseconds 500
            if(!(WaitFor {(Status).account.notice -eq 'Signed out of chat accounts on this device.' -and !(Status).account.clearing} 25)){throw 'Could not reset Rumble preference baseline'}
        }
        Click 'Chat accounts';Click ('Sign in to '+$platform.name+'...') ([Windows.Automation.ControlType]::MenuItem)
        Check (WaitFor {(Status).account.open -and (Status).account.platform -eq $platform.name} 15) ($platform.name+' opens its account window')
        $app=AccountWorker
        Start-Sleep -Milliseconds 500
        $owner=0;[void][StudioUI]::GetWindowThreadProcessId([StudioUI]::GetForegroundWindow(),[ref]$owner)
        Check ($owner -eq $app.Id) ($platform.name+' login receives foreground focus')
        if($owner -ne $app.Id){$window=@(Roots)[0];[void][StudioUI]::SetForegroundWindow([IntPtr]$window.Current.NativeWindowHandle)}
        $window=@(Roots)[0];$bounds=$window.Current.BoundingRectangle
        $desktop=[Windows.Forms.Screen]::FromHandle([IntPtr]$window.Current.NativeWindowHandle).WorkingArea
        Check ($bounds.Left -ge $desktop.Left -and $bounds.Top -ge $desktop.Top -and $bounds.Right -le $desktop.Right -and $bounds.Bottom -le $desktop.Bottom) ($platform.name+' login fits on the available desktop')
        Start-Sleep -Seconds 5
        $buttons=@(foreach($root in Roots){$root.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::Button))|ForEach-Object{$_.Current.Name}})
        # Keep only button labels, never input values, cookies, page URLs or tokens.
        [void]$observations.Add(@{platform=$platform.name;buttons=$buttons;meaning='Login page loaded for visual review; no account authentication attempted'})
        Shot ($platform.name.ToLower()+'-login')
        if($platform.name -eq 'Kick'){
            if(HasButton 'Cookie Settings, Opens the preference center dialog'){Click 'Cookie Settings, Opens the preference center dialog';Click 'Confirm My Choices';Start-Sleep -Seconds 1}
            Click 'Log In' ([Windows.Automation.ControlType]::Button)
            Check (WaitFor {HasButton 'Forgot password?'} 10) 'Kick exposes its actual login form after cookie choices'
            Shot 'kick-login-form'
        }
        if($platform.name -eq 'Instagram' -and (HasButton 'Log in')){Click 'Log in' ([Windows.Automation.ControlType]::Button);Start-Sleep -Seconds 3;Shot 'instagram-login-form'}
        if($platform.name -eq 'Rumble'){
            if(!(WaitFor {HasButton 'Essential Cookies Only'} 10)){throw 'Expected fresh Rumble cookie preference prompt'}
            Click 'Essential Cookies Only';Check (WaitFor {!(HasButton 'Essential Cookies Only')}) 'Rumble saves the essential-only cookie preference';Shot 'rumble-cookie-choice'
        }
        if($platform.name -eq 'Twitch'){
            Check ($buttons -contains 'Log In') 'Twitch exposes the actual login form'
            Click 'Continue with Google';Start-Sleep -Seconds 2
            $message='This sign-in provider is not supported in Studio. Choose Reload and use the platform''s email, username or QR code option.'
            Check (WaitFor {try{$null=Find $message;return $true}catch{return $false}}) 'Unsupported social sign-in explains how to recover'
            Shot 'twitch-provider-explanation';Press 'Reload';Start-Sleep -Seconds 3
            Check (!(Find 'Log In').Current.IsOffscreen) 'Reload returns to the Twitch login form'
        }
        Press 'Return to chat';$app=$studioApp
        Check (WaitFor {!(Status).account.open -and (Status).ready}) ($platform.name+' returns to functioning chat without claiming authentication')
        if($added -and $platform.name -ne 'Rumble'){$id=@((Status).channels|Where-Object{$_.platform -eq $platform.name})[0].id;Api @{command='chat.channel.remove';id=$id}|Out-Null}
    }
    $app=$studioApp
    $channel=@((Status).channels|Where-Object{$_.platform -eq 'YouTube'})|Select-Object -First 1
    if($channel){
        Click 'Chat accounts';Click 'Sign in to YouTube...' ([Windows.Automation.ControlType]::MenuItem)
        $explanation='YouTube public chat works without sign-in. Google account sign-in is not supported in Studio yet.'
        Check (!(Find $explanation).Current.IsOffscreen) 'YouTube account limitation is explicit'
        Shot 'youtube-signin-limit';Press 'OK'
    }
    # This proves profile persistence with a visible non-credential preference.
    # It does not claim that any account has authenticated successfully.
    node (Join-Path $PSScriptRoot 'mcp/studio-cli.mjs') --discovery $control quit|Out-Null
    Wait-Process -Id $studioApp.Id -Timeout 15 -ErrorAction SilentlyContinue;Start-Sleep -Seconds 2
    $env:VIDEO_MIXER_DATA_DIR=$Out
    $studioApp=Start-Process $Exe -PassThru -ArgumentList "--studio-show=$Out/show.mixer.json",'--local-control-port=0',"--local-control-discovery=$control"
    $app=$studioApp
    $session.pid=$studioApp.Id;$session|ConvertTo-Json|Set-Content -LiteralPath $SessionFile
    if(!(WaitFor {try{$discovery=Get-Content $control -Raw|ConvertFrom-Json;return $discovery.pid -eq $studioApp.Id}catch{return $false}} 30)){throw 'Restart discovery did not appear'}
    $discovery=Get-Content $control -Raw|ConvertFrom-Json
    if(!(WaitFor {(Status).ready} 30)){throw 'Restarted chat was not ready'}
    Click 'Chat accounts';Click 'Sign in to Rumble...' ([Windows.Automation.ControlType]::MenuItem)
    if(!(WaitFor {(Status).account.open} 15)){throw 'Rumble account window did not reopen'}
    $app=AccountWorker
    if(!(WaitFor {HasButton 'Forgot Password?'} 15)){throw 'Rumble login form did not load after restart'}
    Start-Sleep -Seconds 3
    Check (!(HasButton 'Essential Cookies Only')) 'Saved browser preference survives a full Studio restart'
    Shot 'rumble-persisted';Press 'Return to chat';$app=$studioApp
    if(!(WaitFor {!(Status).account.open})){throw 'Rumble window did not close'}
    $before=@((Status).channels|ForEach-Object id)
    Click 'Chat accounts';Click 'Sign out of all chat accounts...' ([Windows.Automation.ControlType]::MenuItem);Click 'Cancel' ([Windows.Automation.ControlType]::Button)
    Check (!(Status).account.clearing -and @((Status).channels).Count -eq $before.Count) 'Cancel sign-out preserves connected channels'
    Click 'Chat accounts';Click 'Sign out of all chat accounts...' ([Windows.Automation.ControlType]::MenuItem);Click 'Yes' ([Windows.Automation.ControlType]::Button)
    Check (WaitFor {(Status).account.notice -eq 'Signed out of chat accounts on this device.' -and !(Status).account.clearing} 20) 'Sign-out completes through the profile clearing operation'
    Check (@(Compare-Object $before @((Status).channels|ForEach-Object id)).Count -eq 0) 'Signing out preserves channel identities'
    Check (WaitFor {(Status).ready} 20) 'Capture engine remains ready after sign-out'
    Click 'Chat accounts';Click 'Sign in to Rumble...' ([Windows.Automation.ControlType]::MenuItem)
    if(!(WaitFor {(Status).account.open} 15)){throw 'Rumble account window did not reopen after sign-out'}
    $app=AccountWorker
    Check (WaitFor {HasButton 'Essential Cookies Only'} 15) 'Sign-out actually removes the stored browser preference'
    Shot 'rumble-cleared';Press 'Return to chat';$app=$studioApp
    if(!(WaitFor {!(Status).account.open})){throw 'Rumble window did not close'}
    Check (!(State).output.active -and !(State).recording.active) 'Account workflow starts no public output or recording'
    Shot 'after-signout'
}catch{Check $false 'Account workflow completed' $_.Exception.Message}
finally{
    $app=$studioApp
    @{exe=$Exe;sha256=(Get-FileHash $Exe).Hash;checks=$checks;pageObservations=$observations}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $Evidence 'account-results.json') -Encoding utf8
    "Evidence: $Evidence"
}
if(@($checks|Where-Object{!$_.ok}).Count){exit 1}
