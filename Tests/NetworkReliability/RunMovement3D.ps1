param(
    [string]$ClientDirectory = "$PSScriptRoot/../../../MyMMORPGClient_3D",
    [ValidateRange(1, 65535)][int]$Port = 27780
)
$ErrorActionPreference = 'Stop'
$fixture = Join-Path $PSScriptRoot 'bin/NetworkReliability.exe'
$clientRoot = (Resolve-Path -LiteralPath $ClientDirectory).Path
$project = Join-Path $clientRoot 'Tests/Movement3D/Movement3D.csproj'
if (-not (Test-Path -LiteralPath $fixture)) { throw 'Build NetworkReliability Release x64 first.' }
dotnet run --project $project
if ($LASTEXITCODE -ne 0) { throw '3D client simulation tests failed.' }
foreach ($condition in @(@{Delay=0; Jitter=0}, @{Delay=80; Jitter=35}, @{Delay=150; Jitter=60}))
{
    $label = "3d-$($condition.Delay)-$($condition.Jitter)"
    $stdout = Join-Path $PSScriptRoot "bin/$label.stdout.log"
    $stderr = Join-Path $PSScriptRoot "bin/$label.stderr.log"
    $process = Start-Process -FilePath $fixture -ArgumentList @('--relay-3d-fixture', $Port) -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    try
    {
        $ready = $false
        for ($attempt = 0; $attempt -lt 30; ++$attempt)
        {
            if ($process.HasExited) { throw "3D fixture startup failed. See $stderr" }
            if ((Test-Path -LiteralPath $stdout) -and (Select-String -LiteralPath $stdout -Pattern '^READY' -Quiet)) { $ready=$true; break }
            Start-Sleep -Milliseconds 100
        }
        if (-not $ready) { throw "3D fixture startup timed out. See $stdout" }
        dotnet run --no-build --project $project -- --tcp $Port $condition.Delay $condition.Jitter
        if ($LASTEXITCODE -ne 0) { throw "3D TCP test failed: $label" }
        if (-not $process.WaitForExit(3000) -or $process.ExitCode -ne 0) { throw "3D fixture cleanup failed: $label" }
    }
    finally
    {
        # 이번 실행에서 시작한 프로세스만 종료하고 사용자 서버에는 접근하지 않는다.
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
        $process.Dispose()
    }
}
Write-Output 'PASS 3D simulation and TCP relay in three delay/jitter conditions'
