param(
    [string]$ClientDirectory = "$PSScriptRoot/../../../MyMMORPGClient",
    [ValidateRange(1, 65535)][int]$Port = 27779
)

$ErrorActionPreference = 'Stop'
$relayExecutable = Join-Path $PSScriptRoot 'bin/NetworkReliability.exe'
$clientRoot = (Resolve-Path -LiteralPath $ClientDirectory).Path
$clientProject = Join-Path $clientRoot 'Tests/MapLocalIntegration/MapLocalIntegration.csproj'
if (-not (Test-Path -LiteralPath $relayExecutable)) { throw 'Build NetworkReliability Release x64 first.' }

& $relayExecutable --movement-actions
if ($LASTEXITCODE -ne 0) { throw 'Server movement action tests failed.' }
dotnet run --project $clientProject -- --actions
if ($LASTEXITCODE -ne 0) { throw 'Client movement action tests failed.' }

foreach ($condition in @(@{ Delay = 0; Jitter = 0 }, @{ Delay = 80; Jitter = 35 }, @{ Delay = 150; Jitter = 60 }))
{
    $label = "relay-$($condition.Delay)-$($condition.Jitter)"
    $stdout = Join-Path $PSScriptRoot "bin/$label.stdout.log"
    $stderr = Join-Path $PSScriptRoot "bin/$label.stderr.log"
    $relayProcess = Start-Process -FilePath $relayExecutable -ArgumentList @('--relay-fixture', $Port) -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    try
    {
        $ready = $false
        for ($attempt = 0; $attempt -lt 30; ++$attempt)
        {
            if ($relayProcess.HasExited) { throw "Relay startup failed. See $stderr" }
            if ((Test-Path -LiteralPath $stdout) -and (Select-String -LiteralPath $stdout -Pattern '^READY' -Quiet)) { $ready = $true; break }
            Start-Sleep -Milliseconds 100
        }
        if (-not $ready) { throw "Relay startup timed out. See $stdout" }
        dotnet run --no-build --project $clientProject -- "--relay-fixture=$Port" "--relay-delay=$($condition.Delay)" "--relay-jitter=$($condition.Jitter)"
        if ($LASTEXITCODE -ne 0) { throw "TCP relay test failed for $label" }
        if (-not $relayProcess.WaitForExit(3000) -or $relayProcess.ExitCode -ne 0) { throw "Relay cleanup failed. See $stderr" }
    }
    finally
    {
        # 이번 실행에서 시작한 테스트 프로세스만 종료한다.
        if (-not $relayProcess.HasExited) { Stop-Process -Id $relayProcess.Id }
        $relayProcess.Dispose()
    }
}
Write-Output 'PASS client/server action tests and three TCP delay/jitter conditions'
