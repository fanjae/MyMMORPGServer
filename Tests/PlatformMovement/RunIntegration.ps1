param([string]$ClientDirectory = "$PSScriptRoot/../../../MyMMORPGClient")

$ErrorActionPreference = 'Stop'
$serverRoot = (Resolve-Path -LiteralPath "$PSScriptRoot/../..").Path
$clientRoot = (Resolve-Path -LiteralPath $ClientDirectory).Path
$gameExe = Join-Path $serverRoot 'x64/Release/GameServer.exe'
$loginExe = Join-Path $serverRoot 'x64/Release/LoginServer.exe'
$physicsExe = Join-Path $PSScriptRoot 'bin/PlatformMovement.exe'
$project = Join-Path $clientRoot 'Tests/MapLocalIntegration/MapLocalIntegration.csproj'
foreach ($name in @('DB_HOST', 'DB_USER', 'DB_PASSWORD', 'DB_NAME'))
{
    if ([string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable($name)))
    {
        throw "Set $name before running this test."
    }
}

foreach ($path in @($gameExe, $loginExe, $physicsExe, $project))
{
    if (-not (Test-Path -LiteralPath $path))
    {
        throw "Build or locate the required file: $path"
    }
}

foreach ($port in @(7776, 7777, 7778))
{
    $probe = [Net.Sockets.TcpClient]::new()
    try
    {
        if ($probe.ConnectAsync('127.0.0.1', $port).Wait(300) -and $probe.Connected)
        {
            throw "Port $port is already in use. Close the test clients and servers first."
        }
    }
    catch [AggregateException] { }
    finally { $probe.Dispose() }
}

$fixture = Join-Path $env:TEMP ('MyMMORPGPlatformIntegration-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
$processes = [Collections.Generic.List[Diagnostics.Process]]::new()
$passed = $false

function Start-TestServers([string]$MapPath, [string]$Label)
{
    $game = Start-Process -FilePath $gameExe -ArgumentList @(('"' + $MapPath + '"')) -WorkingDirectory $fixture -WindowStyle Hidden -RedirectStandardOutput (Join-Path $fixture "$Label-game.out") -RedirectStandardError (Join-Path $fixture "$Label-game.err") -PassThru
    $processes.Add($game)
    $login = Start-Process -FilePath $loginExe -WorkingDirectory $fixture -WindowStyle Hidden -RedirectStandardOutput (Join-Path $fixture "$Label-login.out") -RedirectStandardError (Join-Path $fixture "$Label-login.err") -PassThru
    $processes.Add($login)
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while ([DateTime]::UtcNow -lt $deadline)
    {
        if ($game.HasExited -or $login.HasExited)
        {
            throw "Server startup failed. Inspect logs in $fixture"
        }

        $ready = $true
        foreach ($port in @(7776, 7777, 7778))
        {
            $probe = [Net.Sockets.TcpClient]::new()
            try { $ready = $probe.ConnectAsync('127.0.0.1', $port).Wait(100) -and $probe.Connected -and $ready }
            catch { $ready = $false }
            finally { $probe.Dispose() }
        }

        if ($ready) { return }
        Start-Sleep -Milliseconds 100
    }

    throw "Server startup timed out. Inspect logs in $fixture"
}

function Stop-TestServers
{
    foreach ($process in $processes)
    {
        if (-not $process.HasExited)
        {
            Stop-Process -Id $process.Id
            $process.WaitForExit()
        }
    }

    $processes.Clear()
}

try
{
    dotnet build $project --nologo
    if ($LASTEXITCODE -ne 0) { throw 'Integration test build failed.' }
    $trace = Join-Path $fixture 'physics.csv'
    & $physicsExe (Join-Path $serverRoot 'data') $trace
    if ($LASTEXITCODE -ne 0) { throw 'Physics tests failed.' }

    Start-TestServers (Join-Path $serverRoot 'data/maps.csv') 'platform'
    dotnet run --project $project --no-build -- --platform "--physics-trace=$trace"
    if ($LASTEXITCODE -ne 0) { throw 'Platform integration failed.' }
    Stop-TestServers

    # 원본 맵 데이터는 유지하고 자유 이동 회귀 테스트에만 별도 설정을 사용한다.
    $free = Join-Path $fixture 'free'
    New-Item -ItemType Directory -Path $free | Out-Null
    Copy-Item -LiteralPath (Join-Path $serverRoot 'data/maps.csv') -Destination $free
    $utf8 = [Text.UTF8Encoding]::new($false)
    [IO.File]::WriteAllLines((Join-Path $free 'map_movement.csv'), @('mapId,geometryVersion,movementMode,spawnFootholdId,halfWidth,halfHeight,horizontalSpeed,jumpSpeed,gravity,maxFallSpeed', '100000000,1,Free,0,6,6,80,0,0,0', '100000001,1,Free,0,6,6,80,0,0,0'), $utf8)
    [IO.File]::WriteAllText((Join-Path $free 'footholds.csv'), "mapId,footholdId,x1,y1,x2,y2,prevId,nextId`n", $utf8)
    [IO.File]::WriteAllText((Join-Path $free 'colliders.csv'), "mapId,colliderId,minX,minY,maxX,maxY`n", $utf8)
    Start-TestServers (Join-Path $free 'maps.csv') 'free'
    & (Join-Path $serverRoot 'x64/Release/TestClient.exe')
    if ($LASTEXITCODE -ne 0) { throw 'C++ Free regression failed.' }
    dotnet run --project $project --no-build
    if ($LASTEXITCODE -ne 0) { throw 'C# Free regression failed.' }
    $passed = $true
}
finally
{
    Stop-TestServers
    # 이번 실행에서 만든 임시 디렉터리만 정상 종료 후 정리한다.
    $resolved = [IO.Path]::GetFullPath($fixture)
    $parent = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\')
    if ($passed -and [IO.Path]::GetDirectoryName($resolved).Equals($parent, [StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetFileName($resolved).StartsWith('MyMMORPGPlatformIntegration-'))
    {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
    elseif (-not $passed)
    {
        Write-Output "Test logs preserved: $fixture"
    }
}
