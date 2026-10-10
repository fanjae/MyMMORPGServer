#Requires -Version 7.0
param(
    [string]$ClientDirectory = "$PSScriptRoot/../../../MyMMORPGClient_3D",
    [ValidateRange(1, 65535)][int]$DatabasePort = 3307,
    [ValidateRange(10, 300)][int]$PlayerTimeoutSeconds = 60
)
$ErrorActionPreference = 'Stop'
$serverRoot = (Resolve-Path -LiteralPath "$PSScriptRoot/../..").Path
$clientRoot = (Resolve-Path -LiteralPath $ClientDirectory).Path
$gameExe = Join-Path $serverRoot 'x64/Release/GameServer.exe'
$loginExe = Join-Path $serverRoot 'x64/Release/LoginServer.exe'
$playerExe = Join-Path $clientRoot 'Builds/Movement3D/MyMMORPGClient_3D.exe'
foreach ($path in @($gameExe, $loginExe, $playerExe))
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
$environmentNames = @('DB_HOST', 'DB_USER', 'DB_PASSWORD', 'DB_NAME', 'SERVER_BIND_IP')
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
    $start.CreateNoWindow = $true; $start.WindowStyle = 'Hidden'
    $start.ArgumentList.Add('--smoke-full'); $start.ArgumentList.Add('-logFile')
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
        $rows = docker exec $container sh -c 'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysql -uroot -Nse "SELECT COUNT(*) FROM mymmorpg_full_test.characters"' 2>$null
        if ($LASTEXITCODE -eq 0 -and "$rows".Trim() -eq '3') { $ready = $true; break }
        Start-Sleep -Milliseconds 1000
    }
    if (-not $ready) { throw 'Temporary MySQL initialization timed out.' }
    Write-Output '[PASS] MySQL initialized with isolated account/character fixtures'
    $env:DB_HOST = "tcp://127.0.0.1:$DatabasePort"; $env:DB_USER = 'full_test'
    $env:DB_PASSWORD = $databasePassword; $env:DB_NAME = 'mymmorpg_full_test'; $env:SERVER_BIND_IP = '127.0.0.1'
    $game = Start-TestServer $gameExe @(('"' + (Join-Path $serverRoot 'data/maps.csv') + '"')) 'game'
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
        if (-not $result.StartsWith('PASS') -or $result -notmatch 'login=True characters=True ticket=True entered=True protocol=7')
        { throw "Full authentication path was not verified for $character. Inspect $output" }
        Write-Output "[PASS] Unity Player ${character}: DB login, character list, auth ticket, v7 game entry, peer jump/land, rendering"
        Write-Output $result
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
    [IO.File]::WriteAllText((Join-Path $output 'integration.result'), $(if ($passed) { 'PASS full local 3D connection' } else { 'FAILED full local 3D connection' }))
    Write-Output "Integration logs: $output"
}
