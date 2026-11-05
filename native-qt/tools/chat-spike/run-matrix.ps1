# Runs several window modes at the same time against the same channels, then
# prints one summary row per configuration. Run output stays under runs/ (git-ignored).
param(
    [Parameter(Mandatory)][string]$Name,
    [Parameter(Mandatory)][string]$Channel,
    [Parameter(Mandatory)][string]$YouTube,
    [double]$Minutes = 4,
    [string]$SsnRoot = 'C:/Users/Steve/code/social_stream',
    [string[]]$Configs = @('visible|none', 'hidden|none', 'hidden|flags', 'offscreen|none', 'offscreen|flags', 'minimized|flags')
)
$here = $PSScriptRoot
$flags = '--disable-background-timer-throttling --disable-backgrounding-occluded-windows --disable-renderer-backgrounding'
$base = Join-Path $here "runs\$Name"
New-Item -ItemType Directory -Force $base | Out-Null
$truth = Join-Path $base 'truth'
$irc = Start-Process node -ArgumentList "`"$here\twitch-truth.mjs`" --channel $Channel --out `"$truth`" --duration-min $($Minutes + 0.7)" -PassThru -WindowStyle Hidden
Start-Sleep 5
$procs = foreach ($config in $Configs) {
    $mode, $flagSet, $share = $config -split '\|'
    $label = $config -replace '\|', '-'
    $out = Join-Path $base $label
    $spikeArgs = "--ssn-root $SsnRoot --source twitch:$Channel --source youtube:$YouTube --mode $mode --out `"$out`" --duration-min $Minutes"
    if ($flagSet -eq 'flags') { $spikeArgs += " --browser-args=`"$flags`"" }
    if ($share -eq 'shared') { $spikeArgs += ' --shared-browser' }
    Start-Process "$here\build\chat-spike.exe" -ArgumentList $spikeArgs -PassThru
}
$timeoutSeconds = [int]($Minutes * 60 + 90)
$procs | ForEach-Object { Wait-Process -Id $_.Id -Timeout $timeoutSeconds -ErrorAction SilentlyContinue }
Wait-Process -Id $irc.Id -Timeout 90 -ErrorAction SilentlyContinue
$results = foreach ($config in $Configs) {
    $label = $config -replace '\|', '-'
    $out = Join-Path $base $label
    Copy-Item "$truth\truth-*" $out -Force
    $report = node "$here\analyze.mjs" $out | Out-String | ConvertFrom-Json
    $tw = $report.sources."twitch-$Channel"; $twm = $report.metrics."twitch-$Channel"
    $yt = $report.sources."youtube-$YouTube"; $ytm = $report.metrics."youtube-$YouTube"
    [pscustomobject]@{
        config = $label
        twRate = $tw.comparison.captureRate; twMatched = $tw.comparison.matched; twRefs = $tw.comparison.referenceMessages
        twLatP50 = $tw.comparison.latencyMs.p50; twLatP95 = $tw.comparison.latencyMs.p95; twGap = $tw.longestSilentGapSeconds
        ytMsgs = $yt.totalCapturedChatMessages; ytGap = $yt.longestSilentGapSeconds
        rafTw = [math]::Round($twm.rafPerSecond.mean, 1); timerTw = [math]::Round($twm.timerPerSecond.mean, 1)
        rafYt = [math]::Round($ytm.rafPerSecond.mean, 1); timerYt = [math]::Round($ytm.timerPerSecond.mean, 1)
        cpuTw = [math]::Round($twm.cpuPercent.mean, 2); cpuYt = [math]::Round($ytm.cpuPercent.mean, 2)
        memTw = [math]::Round($twm.privateWorkingSetMiB.mean); memYt = [math]::Round($ytm.privateWorkingSetMiB.mean)
    }
}
$results | Export-Csv (Join-Path $base 'summary.csv') -NoTypeInformation
$results | Format-Table -AutoSize | Out-String -Width 220
