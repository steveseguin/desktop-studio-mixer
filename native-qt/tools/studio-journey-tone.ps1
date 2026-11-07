param([Parameter(Mandatory=$true)][string]$Path)
$ErrorActionPreference='Stop'
$player=[System.Media.SoundPlayer]::new($Path);$player.Load();$player.PlayLooping()
try{Start-Sleep -Seconds 45}finally{$player.Stop();$player.Dispose()}
