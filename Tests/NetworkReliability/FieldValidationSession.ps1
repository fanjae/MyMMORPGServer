param(
    [ValidateSet('Start', 'Stop', 'RestartGame', 'Players', 'ClosePlayers')][string]$Action = 'Start',
    [string]$Label = 'ui',
    [int]$SpawnX = -160,
    [int]$SpawnZ = -130,
    [ValidateRange(1,128)][int]$MonsterCount = 3,
    [int]$GameProxyPort = 0,
    [switch]$StaticMonsters,
    [switch]$AttackWall
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath "$PSScriptRoot/../..").Path
$client = (Resolve-Path -LiteralPath "$root/../MyMMORPGClient_3D").Path
$output = Join-Path $root 'artifacts/field-validation-2026-10-14'
[IO.Directory]::CreateDirectory($output) | Out-Null
$statePath = Join-Path $output 'session.json'
$state = if (Test-Path -LiteralPath $statePath) { Get-Content -Raw -LiteralPath $statePath | ConvertFrom-Json -AsHashtable } else { @{} }
# 기존 테스트 DB의 설정은 자식 프로세스에만 전달하고 검사 기록에는 비밀번호를 남기지 않는다.
$settings = @{}
foreach ($line in Get-Content -LiteralPath (Join-Path $root '.env')) {
    if ($line -match '^([A-Z_]+)=(.*)$') { $settings[$matches[1]] = $matches[2].Trim().Trim('"').Trim("'") }
}
function Stop-Recorded([string]$key) {
    if (!$state.ContainsKey($key)) { return }
    foreach ($id in @($state[$key])) {
        $process = Get-Process -Id $id -ErrorAction SilentlyContinue
        if ($process -and $process.Path -in @((Join-Path $root 'x64/Release/GameServer.exe'), (Join-Path $root 'x64/Release/LoginServer.exe'), (Join-Path $client 'Builds/Movement3D/MyMMORPGClient_3D.exe'))) {
            Stop-Process -Id $id; $process.WaitForExit(5000) | Out-Null
        }
    }
    $state.Remove($key)
}
function Start-Server([string]$name, [string[]]$arguments) {
    $info = [Diagnostics.ProcessStartInfo]::new((Join-Path $root "x64/Release/$name.exe"))
    $info.UseShellExecute = $false; $info.CreateNoWindow = $true; $info.WindowStyle = 'Hidden'; $info.WorkingDirectory = $output
    foreach ($key in @('DB_HOST','DB_USER','DB_PASSWORD','DB_NAME')) { $info.Environment[$key] = $settings[$key] }
    $info.Environment['SERVER_BIND_IP'] = '127.0.0.1'
    foreach ($argument in $arguments) { $info.ArgumentList.Add($argument) }
    $info.RedirectStandardOutput = $info.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($info)
    $stdout = [IO.FileStream]::new((Join-Path $output "$name-$Label.out"), 'Create', 'Write', 'ReadWrite'); $stderr = [IO.FileStream]::new((Join-Path $output "$name-$Label.err"), 'Create', 'Write', 'ReadWrite')
    $copyOut = $process.StandardOutput.BaseStream.CopyToAsync($stdout); $copyErr = $process.StandardError.BaseStream.CopyToAsync($stderr)
    return @{ Process = $process; Out = $stdout; Err = $stderr; CopyOut = $copyOut; CopyErr = $copyErr }
}
if ($Action -in @('Stop','RestartGame','ClosePlayers')) { Stop-Recorded 'players' }
if ($Action -in @('Stop','RestartGame')) { Stop-Recorded 'game' }
if ($Action -eq 'Stop') { Stop-Recorded 'login' }
if ($Action -in @('Start','RestartGame')) {
    $fieldData = Join-Path $output "data-$Label"; [IO.Directory]::CreateDirectory($fieldData) | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $root 'data') -File | Copy-Item -Destination $fieldData
    $maps = Import-Csv -LiteralPath (Join-Path $fieldData 'maps.csv')
    foreach ($row in $maps) { if ($row.mapId -in @('100000002','100000003')) { $row.spawnX = [string]$SpawnX; $row.spawnY = '0' } }
    $maps | Export-Csv -LiteralPath (Join-Path $fieldData 'maps.csv') -NoTypeInformation
    $worlds = Import-Csv -LiteralPath (Join-Path $fieldData 'worlds3d.csv')
    foreach ($row in $worlds) { $row.spawnZ = [string]$SpawnZ }
    $worlds | Export-Csv -LiteralPath (Join-Path $fieldData 'worlds3d.csv') -NoTypeInformation
    # CSV 로더는 따옴표 없는 숫자 레코드를 읽으므로 검사 복사본만 같은 형식으로 저장한다.
    foreach ($file in @('maps.csv','worlds3d.csv')) {
        $path = Join-Path $fieldData $file
        [IO.File]::WriteAllLines($path, @((Get-Content -LiteralPath $path) -replace '"',''))
    }
    if ($AttackWall) {
        Add-Content -LiteralPath (Join-Path $fieldData 'boxes3d.csv') -Value '100000002,6,-180,0,-120,-140,60,-115'
    }
    $serverArgs = @((Join-Path $fieldData 'maps.csv'), "--field-monsters=$MonsterCount")
    if ($StaticMonsters) { $serverArgs += '--static-field' }
    $servers = @((Start-Server 'GameServer' $serverArgs))
    $state.game = $servers[0].Process.Id
    if ($Action -eq 'Start') { $servers += Start-Server 'LoginServer' @(); $state.login = $servers[1].Process.Id }
    [IO.File]::WriteAllText($statePath, ($state | ConvertTo-Json))
    Write-Output "SERVERS_READY $output"
    try { while (@($servers | Where-Object { !$_.Process.HasExited }).Count) { Start-Sleep -Milliseconds 500 } }
    finally {
        foreach ($server in $servers) {
            # 종료 직후 남은 출력도 복사한 뒤 닫아 마지막 검사 결과가 잘리지 않도록 한다.
            [Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]]@($server.CopyOut,$server.CopyErr),5000) | Out-Null
            $server.Out.Dispose(); $server.Err.Dispose()
        }
    }
    exit
}
if ($Action -eq 'Players') {
    $ids = @()
    foreach ($fixture in @(@{ Account='test'; Character=1002 }, @{ Account='test2'; Character=2001 })) {
        $directory = Join-Path $output "$Label-$($fixture.Character)"; [IO.Directory]::CreateDirectory($directory) | Out-Null
        $info = [Diagnostics.ProcessStartInfo]::new((Join-Path $client 'Builds/Movement3D/MyMMORPGClient_3D.exe'))
        $info.UseShellExecute = $false; $info.WorkingDirectory = $directory
        $info.Environment['MMORPG_TEST_LOGIN'] = $fixture.Account; $info.Environment['MMORPG_TEST_CHARACTER'] = [string]$fixture.Character
        $info.Environment['MMORPG_TEST_PASSWORD'] = 'test1234'
        if ($GameProxyPort) { $info.Environment['MMORPG_TEST_GAME_PORT'] = [string]$GameProxyPort }
        foreach ($argument in @('--manual-full','--input-probe','-screen-fullscreen','0','-screen-width','1100','-screen-height','720','-logFile',(Join-Path $directory 'player.log'))) { $info.ArgumentList.Add($argument) }
        $ids += [Diagnostics.Process]::Start($info).Id
    }
    $state.players = $ids
}
[IO.File]::WriteAllText($statePath, ($state | ConvertTo-Json))
