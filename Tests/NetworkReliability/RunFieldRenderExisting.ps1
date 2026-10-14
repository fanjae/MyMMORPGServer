param([switch]$OnlineOnly, [ValidatePattern('^[a-z0-9-]+$')][string]$RunLabel='render')
$ErrorActionPreference='Stop'
$root=(Resolve-Path -LiteralPath "$PSScriptRoot/../..").Path
$client=(Resolve-Path -LiteralPath "$root/../MyMMORPGClient_3D").Path
$output=Join-Path $root 'artifacts/field-validation-2026-10-14'
$conditions=if($OnlineOnly) { @(@{Count=128;Detail='all';Online=$true}) } else { @(@{Count=3;Detail='all'},@{Count=32;Detail='all'},@{Count=128;Detail='all'},@{Count=128;Detail='none'},@{Count=128;Detail='names'},@{Count=128;Detail='damage'},@{Count=128;Detail='attack'}) }
foreach($condition in $conditions) {
    $directory=Join-Path $output "$RunLabel-$($condition.Count)-$($condition.Detail)$(if($condition.Online){'-online'})"
    [IO.Directory]::CreateDirectory($directory) | Out-Null
    $info=[Diagnostics.ProcessStartInfo]::new((Join-Path $client 'Builds/Movement3D/MyMMORPGClient_3D.exe'))
    $info.UseShellExecute=$false; $info.WorkingDirectory=$directory
    foreach($argument in @('-screen-fullscreen','0','-screen-width','1100','-screen-height','720','-logFile',(Join-Path $directory 'player.log'),"--field-load-detail=$($condition.Detail)")) { $info.ArgumentList.Add($argument) }
    if($condition.Online) {
        $info.ArgumentList.Add('--manual-full'); $info.ArgumentList.Add('--field-online-load=128')
        $info.Environment['MMORPG_TEST_LOGIN']='test'; $info.Environment['MMORPG_TEST_PASSWORD']='test1234'; $info.Environment['MMORPG_TEST_CHARACTER']='1002'
        $info.Environment.Remove('MMORPG_TEST_GAME_PORT') | Out-Null
    } else { $info.ArgumentList.Add("--field-load=$($condition.Count)") }
    $process=[Diagnostics.Process]::Start($info)
    try {
        if(!$process.WaitForExit(45000)) { throw 'Unity render measurement timed out' }
        if($process.ExitCode -ne 0) { throw "Unity render measurement failed: $directory" }
        $suffix=if($condition.Detail -eq 'all'){''}else{'-'+$condition.Detail}
        if($condition.Online){$suffix+='-online'}
        $result=Join-Path $directory "Logs/field-load-$($condition.Count)$suffix.result"
        if(!(Test-Path -LiteralPath $result)) { throw "Missing render result: $directory" }
        Get-Content -LiteralPath $result
    }
    finally { if(!$process.HasExited){Stop-Process -Id $process.Id}; $process.Dispose() }
}
