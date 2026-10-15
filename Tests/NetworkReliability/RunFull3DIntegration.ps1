#Requires -Version 7.0
param(
    [string]$ClientDirectory = "$PSScriptRoot/../../../MyMMORPGClient_3D",
    [ValidateRange(1, 65535)][int]$DatabasePort = 3307,
    [ValidateRange(10, 300)][int]$PlayerTimeoutSeconds = 60,
    [switch]$ManualInput,
    [switch]$ManualCombat,
    [switch]$FieldExpansion,
    [switch]$FieldRecovery,
    [ValidateRange(30, 1800)][int]$ManualTimeoutSeconds = 600
)
$ErrorActionPreference = 'Stop'
$serverRoot = (Resolve-Path -LiteralPath "$PSScriptRoot/../..").Path
$clientRoot = (Resolve-Path -LiteralPath $ClientDirectory).Path
$gameExe = Join-Path $serverRoot 'x64/Release/GameServer.exe'
$loginExe = Join-Path $serverRoot 'x64/Release/LoginServer.exe'
$playerExe = Join-Path $clientRoot 'Builds/Movement3D/MyMMORPGClient_3D.exe'
$inventoryExe = Join-Path $serverRoot 'x64/Release/InventoryDatabase.exe'
foreach ($path in @($gameExe, $loginExe, $playerExe, $inventoryExe))
{
    if (-not (Test-Path -LiteralPath $path)) { throw "Build the required executable first: $path" }
}
$listeners = [Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveTcpListeners()
foreach ($port in @($DatabasePort, 7776, 7777, 7778))
{
    if ($listeners | Where-Object { $_.Port -eq $port }) { throw "Port $port is already in use. Existing services were preserved." }
}
docker info --format '{{.ServerVersion}}' | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Start Docker Desktop before running this test.' }
docker image inspect mysql:8.4 --format '{{.Id}}' | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Prepare the mysql:8.4 image before running this test.' }

$runId = [guid]::NewGuid().ToString('N')
$container = 'mymmorpg-full-3d-' + $runId
$output = Join-Path $serverRoot ('artifacts/full-3d-' + $runId)
[IO.Directory]::CreateDirectory($output) | Out-Null
$envFile = Join-Path $output 'mysql-test.env'
$databasePassword = [guid]::NewGuid().ToString('N')
$environmentNames = @('DB_HOST', 'DB_USER', 'DB_PASSWORD', 'DB_NAME', 'SERVER_BIND_IP', 'MMORPG_TEST_PASSWORD')
$previousEnvironment = @{}
foreach ($name in $environmentNames) { $previousEnvironment[$name] = [Environment]::GetEnvironmentVariable($name) }
$processes = [Collections.Generic.List[Diagnostics.Process]]::new()
$containerStarted = $false
$passed = $false

function Start-TestServer([string]$exe, [string[]]$arguments, [string]$label)
{
    $parameters = @{
        FilePath = $exe; WorkingDirectory = $output; WindowStyle = 'Hidden'; PassThru = $true
        RedirectStandardOutput = (Join-Path $output "$label.out")
        RedirectStandardError = (Join-Path $output "$label.err")
    }
    if ($arguments.Count -gt 0) { $parameters.ArgumentList = $arguments }
    $process = Start-Process @parameters
    $processes.Add($process)
    return $process
}
function Start-TestPlayer([string]$account, [uint32]$character)
{
    $directory = Join-Path $output "player-$character"
    [IO.Directory]::CreateDirectory($directory) | Out-Null
    $start = [Diagnostics.ProcessStartInfo]::new($playerExe)
    $start.WorkingDirectory = $directory; $start.UseShellExecute = $false
    $start.CreateNoWindow = -not $ManualInput; $start.WindowStyle = $(if ($ManualInput) { 'Normal' } else { 'Hidden' })
    $start.ArgumentList.Add($(if ($ManualInput) { '--manual-full' } else { '--smoke-full' }))
    if ($ManualInput) { $start.ArgumentList.Add('--input-probe') }
    $start.ArgumentList.Add('-logFile')
    $start.ArgumentList.Add((Join-Path $directory 'player.log'))
    # 테스트 프로세스에만 계정 정보를 넣고 명령줄과 검사 결과에는 비밀번호를 남기지 않는다.
    $start.Environment['MMORPG_TEST_LOGIN'] = $account
    $start.Environment['MMORPG_TEST_PASSWORD'] = 'test1234'
    $start.Environment['MMORPG_TEST_CHARACTER'] = [string]$character
    $process = [Diagnostics.Process]::Start($start)
    $processes.Add($process)
    return $process
}

try
{
    # 기존 Compose DB와 볼륨을 사용하지 않고 메모리 저장소의 임시 DB를 구성한다.
    [IO.File]::WriteAllLines($envFile, @(
        "MYSQL_ROOT_PASSWORD=$databasePassword", 'MYSQL_DATABASE=mymmorpg_full_test',
        'MYSQL_USER=full_test', "MYSQL_PASSWORD=$databasePassword"))
    docker run --detach --rm --name $container --env-file $envFile --publish "127.0.0.1:${DatabasePort}:3306" `
        --tmpfs /var/lib/mysql --mount "type=bind,source=$serverRoot\database\init,target=/docker-entrypoint-initdb.d,readonly" mysql:8.4 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Temporary MySQL startup failed.' }
    $containerStarted = $true
    Remove-Item -LiteralPath $envFile
    $ready = $false; $deadline = [DateTime]::UtcNow.AddSeconds(120)
    while ([DateTime]::UtcNow -lt $deadline)
    {
        $rows = docker exec $container sh -c 'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysql --protocol=TCP -h127.0.0.1 -uroot -Nse "SELECT (SELECT COUNT(*) FROM mymmorpg_full_test.characters) + (SELECT COUNT(*) FROM mymmorpg_full_test.item_definitions)"' 2>$null
        if ($LASTEXITCODE -eq 0 -and "$rows".Trim() -eq '11') { $ready = $true; break }
        Start-Sleep -Milliseconds 1000
    }
    if (-not $ready) { throw 'Temporary MySQL initialization timed out.' }
    Write-Output '[PASS] MySQL initialized with isolated account/character fixtures'
    $env:DB_HOST = "tcp://127.0.0.1:$DatabasePort"; $env:DB_USER = 'full_test'
    $env:DB_PASSWORD = $databasePassword; $env:DB_NAME = 'mymmorpg_full_test'; $env:SERVER_BIND_IP = '127.0.0.1'
    # 같은 임시 DB에서 저장·동시 요청·롤백·취소 검사를 먼저 수행한다.
    & $inventoryExe *> (Join-Path $output 'inventory-database.log')
    if ($LASTEXITCODE -ne 0) { throw "Inventory DB transaction checks failed. Inspect $output/inventory-database.log" }
    Get-Content -LiteralPath (Join-Path $output 'inventory-database.log')
    # 이미 인벤토리·장비가 저장된 DB에 같은 확장 SQL을 재적용해 변경 없이 실행 가능한지 확인한다.
    docker exec $container sh -c 'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysql -uroot mymmorpg_full_test < /docker-entrypoint-initdb.d/003_field_combat.sql && MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysql -uroot mymmorpg_full_test < /docker-entrypoint-initdb.d/004_field_recovery.sql' | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Existing schema migration reapplication failed.' }
    Write-Output '[PASS] Equipment/recovery migrations reapply successfully to the populated temporary database'
    $testMaps=Join-Path $serverRoot 'data/maps.csv'
    if ($ManualCombat -or $FieldExpansion -or $FieldRecovery)
    {
        if ($ManualCombat -and -not $ManualInput) { throw 'ManualCombat requires ManualInput.' }
        # 검증용 복사본에서만 몬스터 앞에 배치하고 충분한 HP를 주어 실제 키 조작 시간을 확보한다.
        $fieldData=Join-Path $output 'field-data'; [IO.Directory]::CreateDirectory($fieldData) | Out-Null
        Get-ChildItem -LiteralPath (Join-Path $serverRoot 'data') -File | Copy-Item -Destination $fieldData
        $testMaps=Join-Path $fieldData 'maps.csv'
        $rows=Import-Csv -LiteralPath $testMaps
        foreach ($row in $rows) { if ($row.mapId -in @('100000002','100000003')) { $row.spawnX='-160'; $row.spawnY='0' } }
        [IO.File]::WriteAllLines($testMaps,@('mapId,spawnX,spawnY,minX,maxX,minY,maxY,moveSpeed,moveBurst')+@($rows | ForEach-Object { "$($_.mapId),$($_.spawnX),$($_.spawnY),$($_.minX),$($_.maxX),$($_.minY),$($_.maxY),$($_.moveSpeed),$($_.moveBurst)" }))
        $worldPath=Join-Path $fieldData 'worlds3d.csv'; $worlds=Import-Csv -LiteralPath $worldPath
        foreach ($row in $worlds) { $row.spawnZ='-130' }
        [IO.File]::WriteAllLines($worldPath,@('mapId,version,minZ,maxZ,spawnZ,radius,height,speed,jumpSpeed,gravity,maxFallSpeed')+@($worlds | ForEach-Object { "$($_.mapId),$($_.version),$($_.minZ),$($_.maxZ),$($_.spawnZ),$($_.radius),$($_.height),$($_.speed),$($_.jumpSpeed),$($_.gravity),$($_.maxFallSpeed)" }))
        if ($ManualCombat)
        {
            docker exec $container sh -c 'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysql -uroot mymmorpg_full_test -e "UPDATE character_inventories SET hp=5000,max_hp=5000 WHERE character_id IN (1002,2001)"' | Out-Null
            if ($LASTEXITCODE -ne 0) { throw 'Manual combat fixture setup failed.' }
        }
    }
    $game = Start-TestServer $gameExe @(('"' + $(if ($FieldExpansion -or $FieldRecovery) { Join-Path $serverRoot 'data/maps.csv' } else { $testMaps }) + '"')) 'game'
    $login = Start-TestServer $loginExe @() 'login'
    $ready = $false; $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while ([DateTime]::UtcNow -lt $deadline)
    {
        if ($game.HasExited -or $login.HasExited) { throw "Server startup failed. Inspect $output" }
        $ready = $true
        foreach ($port in @(7776, 7777, 7778))
        {
            $probe = [Net.Sockets.TcpClient]::new()
            try { $ready = $probe.ConnectAsync('127.0.0.1', $port).Wait(100) -and $probe.Connected -and $ready }
            catch { $ready = $false }
            finally { $probe.Dispose() }
        }
        if ($ready) { break }
        Start-Sleep -Milliseconds 100
    }
    if (-not $ready) { throw 'LoginServer/GameServer startup timed out.' }
    Write-Output '[PASS] Actual LoginServer and GameServer listen on local ports 7776/7777/7778'
    if ($ManualInput)
    {
        # 실제 키 검증은 자동 이동 인자를 쓰지 않고 완료 표시가 있을 때까지 두 창을 유지한다.
        $manualPlayers = @((Start-TestPlayer 'test' 1002), (Start-TestPlayer 'test2' 2001))
        Write-Output "MANUAL_READY: $output"
        $deadline = [DateTime]::UtcNow.AddSeconds($ManualTimeoutSeconds)
        while (-not (Test-Path -LiteralPath (Join-Path $output 'manual-finish')))
        {
            if ([DateTime]::UtcNow -ge $deadline -or $game.HasExited -or $login.HasExited -or @($manualPlayers | Where-Object HasExited).Count -gt 0)
            { throw 'Manual keyboard verification ended before completion.' }
            Start-Sleep -Milliseconds 500
        }
        foreach ($character in @(1002, 2001))
        {
            $evidence = Get-Content -Raw -LiteralPath (Join-Path $output "player-$character/Logs/input-probe.json") | ConvertFrom-Json
            if ($evidence.mode -ne 'manual' -or $evidence.monsters -ne 3 -or $evidence.inventoryRevision -lt 1)
            { throw 'Manual player did not receive authoritative field state.' }
        }
        $events = Get-Content -LiteralPath (Join-Path $output 'player-1002/Logs/input-probe-events.jsonl') | ForEach-Object { $_ | ConvertFrom-Json }
        if (-not @($events | Where-Object { $_.jumpId -gt 0 -and $_.mode -eq 'manual' }).Count)
        { throw 'No actual manual jump evidence; automatic startup is insufficient.' }
        $passed = $true; return
    }
    $players = @((Start-TestPlayer 'test' 1001), (Start-TestPlayer 'test2' 2001))
    $deadline = [DateTime]::UtcNow.AddSeconds($PlayerTimeoutSeconds)
    while (@($players | Where-Object { -not $_.HasExited }).Count -gt 0)
    {
        if ([DateTime]::UtcNow -ge $deadline) { throw "Unity Player integration timed out. Inspect $output" }
        Start-Sleep -Milliseconds 500
    }
    foreach ($index in @(0, 1))
    {
        $players[$index].WaitForExit()
        $character = @(1001, 2001)[$index]
        $resultFile = Join-Path $output "player-$character/Logs/3d-smoke-full-$character.result"
        if ($players[$index].ExitCode -ne 0 -or -not (Test-Path -LiteralPath $resultFile)) { throw "Unity Player $character failed. Inspect $output" }
        $result = [IO.File]::ReadAllText($resultFile)
        if (-not $result.StartsWith('PASS') -or $result -notmatch 'login=True characters=True ticket=True entered=True protocol=12' -or $result -notmatch 'monsters=3 .*monsterMoved=True')
        { throw "Full authentication path was not verified for $character. Inspect $output" }
        Write-Output "[PASS] Unity Player ${character}: DB login, auth ticket, v12 entry, peer jump/land, inventory, shared moving monsters, rendering"
        Write-Output $result
    }
    # 실행 파일의 인벤토리가 DB에 저장되었는지와 파일 저장 경로를 사용하지 않았는지 확인한다.
    $inventoryRows = docker exec $container sh -c 'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysql -uroot -Nse "SELECT COUNT(*) FROM mymmorpg_full_test.character_inventories"'
    if ($LASTEXITCODE -ne 0 -or "$inventoryRows".Trim() -ne '3') { throw 'Inventory header rows were not stored in MySQL.' }
    if (Test-Path -LiteralPath (Join-Path $output 'inventory-data')) { throw 'Production server unexpectedly used file inventory storage.' }
    Write-Output '[PASS] Actual Unity inventory snapshots came from MySQL; no inventory-data files created'
    # 실제 서버에 슬롯 조작을 요청한 뒤 두 서버를 재시작해 같은 DB 상태로 복귀하는지 확인한다.
    $project = Join-Path $clientRoot 'Tests/Movement3D/Movement3D.csproj'
    $env:MMORPG_TEST_PASSWORD = 'test1234'
    dotnet run --project $project -- --field-server mutate 2>&1 | Tee-Object -FilePath (Join-Path $output 'field-mutations.log')
    if ($LASTEXITCODE -ne 0) { throw 'Actual server inventory mutation checks failed.' }
    foreach ($server in @($game, $login)) { Stop-Process -Id $server.Id; $server.WaitForExit(5000) | Out-Null }
    $beforeRestartVitals=docker exec $container sh -c 'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysql -uroot -Nse "SELECT character_id,revision,hp,max_hp,mp,max_mp FROM mymmorpg_full_test.character_inventories ORDER BY character_id"'
    if ($LASTEXITCODE -ne 0) { throw 'Stored vitals baseline could not be read.' }
    $game = Start-TestServer $gameExe @(('"' + (Join-Path $serverRoot 'data/maps.csv') + '"')) 'game-restarted'
    $login = Start-TestServer $loginExe @() 'login-restarted'
    $deadline = [DateTime]::UtcNow.AddSeconds(15); $ready = $false
    while ([DateTime]::UtcNow -lt $deadline)
    {
        if ($game.HasExited -or $login.HasExited) { throw 'Restarted server exited.' }
        $ready = $true
        foreach ($port in @(7776, 7777, 7778))
        {
            $probe = [Net.Sockets.TcpClient]::new()
            try { $ready = $probe.ConnectAsync('127.0.0.1', $port).Wait(100) -and $probe.Connected -and $ready }
            catch { $ready = $false }
            finally { $probe.Dispose() }
        }
        if ($ready) { break }
        Start-Sleep -Milliseconds 100
    }
    if (-not $ready) { throw 'Restarted LoginServer registration timed out.' }
    $afterRestartVitals=docker exec $container sh -c 'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysql -uroot -Nse "SELECT character_id,revision,hp,max_hp,mp,max_mp FROM mymmorpg_full_test.character_inventories ORDER BY character_id"'
    if ($LASTEXITCODE -ne 0 -or ($beforeRestartVitals -join "`n") -ne ($afterRestartVitals -join "`n")) { throw 'Committed HP/MP or revision changed across restart.' }
    Write-Output '[PASS] Exact committed HP/MP/revision matches before and after server restart, including combat damage'
    dotnet run --no-build --project $project -- --field-server verify 2>&1 | Tee-Object -FilePath (Join-Path $output 'field-restart.log')
    if ($LASTEXITCODE -ne 0) { throw 'Server restart persistence or reentry checks failed.' }
    if ($FieldExpansion)
    {
        foreach ($server in @($game,$login)) { Stop-Process -Id $server.Id; $server.WaitForExit(5000) | Out-Null }
        $game=Start-TestServer $gameExe @(('"'+$testMaps+'"')) 'game-combat'
        $login=Start-TestServer $loginExe @() 'login-combat'; Start-Sleep -Seconds 2
        docker exec $container sh -c 'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysql -uroot mymmorpg_full_test -e "UPDATE character_inventories SET hp=1000,max_hp=1000,revision=revision+1 WHERE character_id IN (1002,2001)"' | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'Combat fixture reset failed.' }
        dotnet run --no-build --project $project -- --field-expansion 2>&1 | Tee-Object -FilePath (Join-Path $output 'field-expansion.log')
        if ($LASTEXITCODE -ne 0) { throw 'Actual channel/combat/drop checks failed.' }
        docker exec $container sh -c 'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysql -uroot mymmorpg_full_test -e "UPDATE character_inventories SET hp=1,revision=revision+1 WHERE character_id=1002"' | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'Death fixture reset failed.' }
        Start-Sleep -Milliseconds 300
        dotnet run --no-build --project $project -- --field-expansion death 2>&1 | Tee-Object -FilePath (Join-Path $output 'field-death.log')
        if ($LASTEXITCODE -ne 0) { throw 'Actual death/revive checks failed.' }
    }
    if ($FieldRecovery)
    {
        foreach ($server in @($game,$login)) { Stop-Process -Id $server.Id; $server.WaitForExit(5000) | Out-Null }
        $game = Start-TestServer $gameExe @(('"' + $testMaps + '"')) 'game-recovery'
        $login = Start-TestServer $loginExe @() 'login-recovery'; Start-Sleep -Seconds 2
        docker exec $container sh -c 'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysql -uroot mymmorpg_full_test -e "UPDATE character_inventories SET hp=1000,max_hp=1000,revision=revision+1 WHERE character_id=1002; ALTER TABLE character_inventories ADD CONSTRAINT ck_field_recovery_failure CHECK (character_id<>1002 OR hp=1000)"' | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'Recovery failure fixture setup failed.' }
        $marker = Join-Path $output 'combat-paused'
        $recovery = Start-TestServer 'dotnet' @('run','--no-build','--project',('"' + $project + '"'),'--','--field-recovery',('"' + $marker + '"')) 'field-recovery'
        $limit = [DateTime]::UtcNow.AddSeconds(25)
        while (-not (Test-Path -LiteralPath $marker) -and [DateTime]::UtcNow -lt $limit -and -not $recovery.HasExited) { Start-Sleep -Milliseconds 200 }
        if (-not (Test-Path -LiteralPath $marker)) { throw 'Combat did not pause after forced DB failure.' }
        docker exec $container sh -c 'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysql -uroot mymmorpg_full_test -e "ALTER TABLE character_inventories DROP CHECK ck_field_recovery_failure"' | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'Recovery failure fixture cleanup failed.' }
        if (-not $recovery.WaitForExit(20000) -or $recovery.ExitCode -ne 0) { throw 'Combat recovery failed.' }
        Get-Content -LiteralPath (Join-Path $output 'field-recovery.out')
    }
    $passed = $true
}
finally
{
    # 이번 검사에서 시작한 프로세스와 컨테이너만 종료하고 로그 및 이미지는 보존한다.
    foreach ($process in $processes)
    {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id; $process.WaitForExit(5000) | Out-Null }
        $process.Dispose()
    }
    if ($containerStarted)
    {
        docker logs $container *> (Join-Path $output 'mysql.log')
        docker stop --time 5 $container | Out-Null
        if ($LASTEXITCODE -ne 0) { Write-Warning "Temporary container cleanup failed: $container" }
    }
    if (Test-Path -LiteralPath $envFile) { Remove-Item -LiteralPath $envFile }
    foreach ($name in $environmentNames) { [Environment]::SetEnvironmentVariable($name, $previousEnvironment[$name]) }
    [IO.File]::WriteAllText((Join-Path $output 'integration.result'), $(if ($passed) { $(if ($ManualInput) { 'PASS manual local 3D input session; inspect event evidence' } else { 'PASS full local 3D connection and restart persistence' }) } else { 'FAILED full local 3D connection' }))
    Write-Output "Integration logs: $output"
}
