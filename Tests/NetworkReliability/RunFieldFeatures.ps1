param([ValidateRange(1, 65535)][int]$Port = 27781)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath "$PSScriptRoot/../..").Path
$fixture = Join-Path $PSScriptRoot 'bin/NetworkReliability.exe'
$project = Join-Path $root '../MyMMORPGClient_3D/Tests/Movement3D/Movement3D.csproj'
if (-not (Test-Path -LiteralPath $fixture)) { throw 'Build NetworkReliability Release x64 first.' }
& $fixture
if ($LASTEXITCODE -ne 0) { throw 'Server field/reliability tests failed.' }
# 규칙 검사 다음에 loopback 서버와 두 클라이언트로 실제 TCP 전달을 확인한다.
foreach ($condition in @(@{Delay=0; Jitter=0}, @{Delay=80; Jitter=35}, @{Delay=150; Jitter=60}))
{
    $process = Start-Process -FilePath $fixture -ArgumentList @('--field-3d-fixture', $Port) -WorkingDirectory $root -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $PSScriptRoot 'bin/field-fixture.log') -RedirectStandardError (Join-Path $PSScriptRoot 'bin/field-fixture.err')
    try
    {
        $ready = $false
        for ($attempt = 0; $attempt -lt 30; ++$attempt)
        {
            if ($process.HasExited) { throw 'Field fixture startup failed.' }
            if (Select-String -LiteralPath (Join-Path $PSScriptRoot 'bin/field-fixture.log') -Pattern '^READY' -Quiet) { $ready = $true; break }
            Start-Sleep -Milliseconds 100
        }
        if (-not $ready) { throw 'Field fixture startup timed out.' }
        dotnet run --project $project -- --field-tcp $Port $condition.Delay $condition.Jitter
        if ($LASTEXITCODE -ne 0) { throw 'Client field TCP checks failed.' }
        if (-not $process.WaitForExit(3000) -or $process.ExitCode -ne 0) { throw 'Field fixture did not exit cleanly.' }
    }
    finally
    {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
        $process.Dispose()
    }
}
Write-Output 'PASS field inventory rules, private mutations, shared monster patrol and basic combat in three delay/jitter conditions (DB-free fixture)'
