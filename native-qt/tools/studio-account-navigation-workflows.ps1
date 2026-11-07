# Non-destructive packaged account navigation. Preserves all saved sessions.
param([Parameter(Mandatory=$true)][string]$SessionFile)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/studio-ui-workflows.ps1" -SessionFile $SessionFile -FunctionsOnly -LeaveOpen
$studioApp=$app
$Evidence=Join-Path $Out ('account-navigation-'+(Get-Date -Format yyyyMMdd-HHmmss));New-Item -ItemType Directory $Evidence|Out-Null
function OwnerPid {$owner=0;[void][StudioUI]::GetWindowThreadProcessId([StudioUI]::GetForegroundWindow(),[ref]$owner);return $owner}
try{
    if((State).output.active -or (State).recording.active){throw 'Preserving active production; account walkthrough requires idle output'}
    if((Status).account.open){throw 'An account window is already open'}
    for($cycle=1;$cycle -le 2;$cycle++){
        $app=$studioApp;Focus 'Chat accounts';Click 'Chat accounts';Click 'Sign in to Twitch...' ([Windows.Automation.ControlType]::MenuItem)
        Check (WaitFor {(Status).account.open -and (Status).account.platform -eq 'Twitch'} 15) "Cycle $cycle opens Twitch"
        $worker=Get-CimInstance Win32_Process|Where-Object{$_.ParentProcessId -eq $studioApp.Id -and $_.Name -eq 'desktop-studio.exe' -and $_.CommandLine -match '--studio-worker chat'}|Select-Object -First 1
        $app=Get-Process -Id $worker.ProcessId
        Check (WaitFor {(OwnerPid) -eq $app.Id} 5) "Cycle $cycle focuses its account window"
        if($cycle -eq 2){
            # Keep the native button provider; enumerating the whole web document
            # during replacement navigation can encounter stale browser nodes.
            if(!(WaitFor {try{$script:reloadButton=(Find 'Reload').GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern);return $true}catch{return $false}})){throw 'Reload button did not become available'}
            for($reload=0;$reload -lt 4;$reload++){$script:reloadButton.Invoke();Start-Sleep -Milliseconds 180}
        }
        Start-Sleep -Seconds 6
        $warning='The sign-in page could not load. Use Reload, or Return to chat and try Accounts again.'
        $warningVisible=try{!(Find $warning).Current.IsOffscreen}catch{$false}
        Check (!$warningVisible) "Cycle $cycle does not show a false failure after navigation or reload"
        Shot "cycle-$cycle-loaded"
        if($cycle -eq 1){
            $window=@(Roots|Where-Object{$_.Current.Name -eq 'Sign in to Twitch - Desktop Studio'})[0]
            [void][StudioUI]::SetWindowPos([IntPtr]$window.Current.NativeWindowHandle,[IntPtr]::Zero,100,100,650,600,4);Start-Sleep -Seconds 1
            $bounds=$window.Current.BoundingRectangle
            foreach($name in @('Back','Reload','Return to chat')){$button=Find $name;Check (!$button.Current.IsOffscreen -and $button.Current.BoundingRectangle.Right -le $bounds.Right) "$name remains reachable in a small sign-in window"}
            Shot 'small-signin'
            Press 'Reload';Start-Sleep -Seconds 4;Shot 'after-reload'
        }
        Press 'Return to chat';$app=$studioApp
        Check (WaitFor {!(Status).account.open -and (Status).ready} 15) "Cycle $cycle returns to ready chat"
        Check (WaitFor {(OwnerPid) -eq $studioApp.Id} 3) "Cycle $cycle returns keyboard focus to Studio"
        Focus 'Chat accounts';Shot "cycle-$cycle-returned"
    }
    Check (!(State).output.active -and !(State).recording.active) 'Account navigation leaves production idle'
}catch{Check $false 'Account navigation completed' $_.Exception.Message}
finally{@{exe=$Exe;sha256=(Get-FileHash $Exe).Hash;checks=$checks}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $Evidence 'results.json') -Encoding utf8;"Evidence: $Evidence"}
if(@($checks|Where-Object{!$_.ok}).Count){exit 1}
