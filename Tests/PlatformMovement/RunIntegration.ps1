param(
    [string]$ClientDirectory = "$PSScriptRoot/../../../MyMMORPGClient",
    [ValidateRange(1, 600)][int]$StepTimeoutSeconds = 120,
    [switch]$FailAfterPlatform,
    [string]$LoadTestContainer = '',
    [string]$MeasurementOutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
$serverRoot = (Resolve-Path -LiteralPath "$PSScriptRoot/../..").Path
$clientRoot = (Resolve-Path -LiteralPath $ClientDirectory).Path
$gameExe = Join-Path $serverRoot 'x64/Release/GameServer.exe'
$loginExe = Join-Path $serverRoot 'x64/Release/LoginServer.exe'
$physicsExe = Join-Path $PSScriptRoot 'bin/PlatformMovement.exe'
$networkExe = Join-Path $serverRoot 'Tests/NetworkReliability/bin/NetworkReliability.exe'
$testClientExe = Join-Path $serverRoot 'x64/Release/TestClient.exe'
$project = Join-Path $clientRoot 'Tests/MapLocalIntegration/MapLocalIntegration.csproj'
foreach ($name in @('DB_HOST', 'DB_USER', 'DB_PASSWORD', 'DB_NAME'))
{
    if ([string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable($name)))
    {
        throw "Set $name before running this test."
    }
}

foreach ($path in @($gameExe, $loginExe, $physicsExe, $networkExe, $testClientExe, $project))
{
    if (-not (Test-Path -LiteralPath $path))
    {
        throw "Build or locate the required file: $path"
    }
}

$listeners = [Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveTcpListeners()
foreach ($port in @(7776, 7777, 7778))
{
    if ($listeners | Where-Object { $_.Port -eq $port })
    {
        throw "Port $port is already in use. Close the test clients and servers first."
    }
}

$fixture = Join-Path $env:TEMP ('MyMMORPGPlatformIntegration-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
$processes = [Collections.Generic.List[Diagnostics.Process]]::new()
$reportedGameLogs = [Collections.Generic.HashSet[string]]::new()
$passed = $false
$previousBindIp = [Environment]::GetEnvironmentVariable('SERVER_BIND_IP')
Write-Output "Test fixture created: $fixture"

function Invoke-TestStep([string]$FilePath, [string[]]$Arguments, [string]$Label)
{
    $stdout = Join-Path $fixture "$Label.out"
    $stderr = Join-Path $fixture "$Label.err"
    $parameters = @{
        FilePath = $FilePath
        WorkingDirectory = $fixture
        WindowStyle = 'Hidden'
        RedirectStandardOutput = $stdout
        RedirectStandardError = $stderr
        PassThru = $true
    }
    if ($Arguments.Count -gt 0)
    {
        $parameters.ArgumentList = @($Arguments | ForEach-Object { '"' + $_.Replace('"', '\"') + '"' })
    }
    $step = Start-Process @parameters
    $processes.Add($step)
    $deadline = [DateTime]::UtcNow.AddSeconds($StepTimeoutSeconds)
    while (-not $step.WaitForExit(1000))
    {
        if ([DateTime]::UtcNow -ge $deadline)
        {
            throw "$Label timed out after $StepTimeoutSeconds seconds. Inspect logs in $fixture"
        }
    }
    $step.WaitForExit()
    Get-Content -LiteralPath $stdout
    Get-Content -LiteralPath $stderr | Write-Output
    if ($step.ExitCode -ne 0)
    {
        throw "$Label failed with exit code $($step.ExitCode). Inspect logs in $fixture"
    }
}

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
    $hadProcesses = $processes.Count -gt 0
    foreach ($process in $processes)
    {
        if (-not $process.HasExited)
        {
            Stop-Process -Id $process.Id
            if (-not $process.WaitForExit(5000))
            {
                throw "Process cleanup timed out: $($process.Id)"
            }
        }
    }

    $processes.Clear()
    if ($hadProcesses)
    {
        Get-ChildItem -LiteralPath $fixture -Filter '*-game.out' | ForEach-Object {
            if ($reportedGameLogs.Add($_.FullName))
            {
                Get-Content -LiteralPath $_.FullName | Where-Object { $_.StartsWith('Server metrics:') }
            }
        }
    }
}

try
{
    # 외부 접속 설정이 있어도 반복 테스트 서버는 로컬 수신으로 실행한다.
    $env:SERVER_BIND_IP = '127.0.0.1'
    Invoke-TestStep 'dotnet' @('build', $project, '--nologo') 'client-build'
    Invoke-TestStep $networkExe @() 'server-network'
    Invoke-TestStep 'dotnet' @('run', '--project', $project, '--no-build', '--', '--network') 'client-network'
    Invoke-TestStep 'dotnet' @('run', '--project', $project, '--no-build', '--', '--chat-unit') 'client-chat-unit'
    Invoke-TestStep 'dotnet' @('run', '--project', $project, '--no-build', '--', '--reconciliation') 'client-reconciliation'
    Invoke-TestStep 'dotnet' @('run', '--project', $project, '--no-build', '--', '--actions') 'client-actions'
    Invoke-TestStep 'dotnet' @('run', '--project', $project, '--no-build', '--', '--proxy-smoke') 'client-proxy-smoke'
    $trace = Join-Path $fixture 'physics.csv'
    Invoke-TestStep $physicsExe @((Join-Path $serverRoot 'data'), $trace) 'physics'

    Start-TestServers (Join-Path $serverRoot 'data/maps.csv') 'platform'
    Invoke-TestStep 'dotnet' @('run', '--project', $project, '--no-build', '--', '--chat') 'chat-test'
    Invoke-TestStep 'dotnet' @('run', '--project', $project, '--no-build', '--', '--platform', "--physics-trace=$trace") 'platform-test'
    Invoke-TestStep 'dotnet' @('run', '--project', $project, '--no-build', '--', '--latency', "--latency-trace=$(Join-Path $fixture 'latency.csv')") 'latency-test'
    if (-not [string]::IsNullOrWhiteSpace($MeasurementOutputDirectory))
    {
        # 측정 결과는 명시한 출력 위치에 보존하고 임시 테스트 디렉터리는 기존처럼 정리한다.
        $measurementRoot = [IO.Path]::GetFullPath($MeasurementOutputDirectory)
        [IO.Directory]::CreateDirectory($measurementRoot) | Out-Null
        Copy-Item -LiteralPath (Join-Path $fixture 'latency.csv') -Destination (Join-Path $measurementRoot 'latency.csv')
        Copy-Item -LiteralPath (Join-Path $fixture 'latency-test.out') -Destination (Join-Path $measurementRoot 'latency.out')
    }
    if (-not [string]::IsNullOrWhiteSpace($LoadTestContainer))
    {
        Invoke-TestStep 'dotnet' @('run', '--project', $project, '--no-build', '--', '--load', "--db-delay-container=$LoadTestContainer") 'load-test'
    }
    Stop-TestServers
    if ($FailAfterPlatform) { throw 'Injected failure after platform tests.' }

    # 원본 맵 데이터는 유지하고 자유 이동 회귀 테스트에만 별도 설정을 사용한다.
    $free = Join-Path $fixture 'free'
    New-Item -ItemType Directory -Path $free | Out-Null
    # Free 회귀는 기존 2D 맵만 사용해 3D 전용 지형 파일이 필요하지 않도록 한다.
    $freeMapRows = [IO.File]::ReadAllLines((Join-Path $serverRoot 'data/maps.csv')) | Where-Object {
        $_.StartsWith('mapId,') -or $_.StartsWith('100000000,') -or $_.StartsWith('100000001,')
    }
    [IO.File]::WriteAllLines((Join-Path $free 'maps.csv'), [string[]]$freeMapRows, $utf8)
    $utf8 = [Text.UTF8Encoding]::new($false)
    [IO.File]::WriteAllLines((Join-Path $free 'map_movement.csv'), @('mapId,geometryVersion,movementMode,spawnFootholdId,halfWidth,halfHeight,horizontalSpeed,jumpSpeed,gravity,maxFallSpeed', '100000000,1,Free,0,6,6,80,0,0,0', '100000001,1,Free,0,6,6,80,0,0,0'), $utf8)
    [IO.File]::WriteAllText((Join-Path $free 'footholds.csv'), "mapId,footholdId,x1,y1,x2,y2,prevId,nextId`n", $utf8)
    [IO.File]::WriteAllText((Join-Path $free 'colliders.csv'), "mapId,colliderId,minX,minY,maxX,maxY`n", $utf8)
    Start-TestServers (Join-Path $free 'maps.csv') 'free'
    Invoke-TestStep $testClientExe @() 'free-cpp'
    Invoke-TestStep 'dotnet' @('run', '--project', $project, '--no-build') 'free-csharp'
    $passed = $true
}
catch
{
    [IO.File]::WriteAllText((Join-Path $fixture 'failure.txt'), $_.Exception.Message + "`n" + $_.ScriptStackTrace, [Text.UTF8Encoding]::new($false))
    throw
}
finally
{
    [Environment]::SetEnvironmentVariable('SERVER_BIND_IP', $previousBindIp)
    $cleanupError = $null
    try { Stop-TestServers }
    catch
    {
        $passed = $false
        $cleanupError = $_
        Write-Warning "Server cleanup failed: $_"
    }
    # 이번 실행에서 만든 임시 디렉터리만 정상 종료 후 정리한다.
    $resolved = [IO.Path]::GetFullPath($fixture)
    $parent = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\')
    if ($passed -and [IO.Path]::GetDirectoryName($resolved).Equals($parent, [StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetFileName($resolved).StartsWith('MyMMORPGPlatformIntegration-'))
    {
        Remove-Item -LiteralPath $resolved -Recurse -Force
        Write-Output "Test fixture cleaned: $resolved"
    }
    elseif (-not $passed)
    {
        Write-Output "Test logs preserved: $fixture"
    }
    if ($null -ne $cleanupError) { throw $cleanupError }
}
