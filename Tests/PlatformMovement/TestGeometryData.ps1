param([string]$ServerExe = "$PSScriptRoot/../../x64/Release/GameServer.exe", [string]$DataDirectory = "$PSScriptRoot/../../data")

$ErrorActionPreference = 'Stop'
$serverPath = (Resolve-Path -LiteralPath $ServerExe).Path
$dataPath = (Resolve-Path -LiteralPath $DataDirectory).Path
$tempRoot = [System.IO.Path]::GetFullPath($env:TEMP)
$fixtureRoot = Join-Path $tempRoot ("MyMMORPGGeometryStartup-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixtureRoot | Out-Null
$encoding = [System.Text.UTF8Encoding]::new($false)
$success = $false
$savedEnvironment = @{}

# DB 설정 없이 실행해 지형 오류가 DB 연결 전에 검출되는지 확인한다.
foreach ($name in @('DB_HOST', 'DB_USER', 'DB_PASSWORD', 'DB_NAME'))
{
    $savedEnvironment[$name] = Get-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue
}

$cases = @(
    @{ Name = 'ValidGeometry'; File = ''; Before = ''; After = '' },
    @{ Name = 'DuplicateMap'; File = 'map_movement.csv'; Before = '100000000,1,Platformer,1,6,6,80,240,480,320'; After = "100000000,1,Platformer,1,6,6,80,240,480,320`n100000000,1,Platformer,1,6,6,80,240,480,320" },
    @{ Name = 'InvalidGravity'; File = 'map_movement.csv'; Before = '80,240,480,320'; After = '80,240,0,320' },
    @{ Name = 'SpawnHeight'; File = 'map_movement.csv'; Before = 'Platformer,1,6,6'; After = 'Platformer,1,6,7' },
    @{ Name = 'MissingNeighbor'; File = 'footholds.csv'; Before = '0,-6,0,2'; After = '0,-6,0,9999' },
    @{ Name = 'SpawnInWall'; File = 'colliders.csv'; Before = '220,-6,244,80'; After = '-5,-5,5,5' },
    @{ Name = 'MissingFile'; File = 'colliders.csv'; Before = ''; After = '' }
)

try
{
    foreach ($case in $cases)
    {
        $caseDirectory = Join-Path $fixtureRoot $case.Name
        New-Item -ItemType Directory -Path $caseDirectory | Out-Null
        foreach ($name in @('maps.csv', 'map_movement.csv', 'footholds.csv', 'colliders.csv'))
        {
            Copy-Item -LiteralPath (Join-Path $dataPath $name) -Destination $caseDirectory
        }

        if ($case.Name -eq 'MissingFile')
        {
            Remove-Item -LiteralPath (Join-Path $caseDirectory $case.File)
        }
        elseif ($case.File)
        {
            $filePath = Join-Path $caseDirectory $case.File
            $source = [System.IO.File]::ReadAllText($filePath)
            if (-not $source.Contains($case.Before))
            {
                throw "$($case.Name): fixture replacement did not match."
            }

            [System.IO.File]::WriteAllText($filePath, $source.Replace($case.Before, $case.After), $encoding)
        }

        $mapPath = Join-Path $caseDirectory 'maps.csv'
        $outputPath = Join-Path $caseDirectory 'server.out'
        $errorPath = Join-Path $caseDirectory 'server.err'
        $process = Start-Process -FilePath $serverPath -ArgumentList @(('"{0}"' -f $mapPath)) -WindowStyle Hidden -RedirectStandardOutput $outputPath -RedirectStandardError $errorPath -PassThru
        if (-not $process.WaitForExit(5000))
        {
            Stop-Process -Id $process.Id
            throw "$($case.Name): server did not finish startup validation."
        }

        $process.WaitForExit()

        $errorText = [System.IO.File]::ReadAllText($errorPath)
        if ($process.ExitCode -ne 1)
        {
            throw "$($case.Name): unexpected exit code $($process.ExitCode)."
        }

        if ($case.Name -eq 'ValidGeometry')
        {
            if ($errorText -notmatch 'Environment variable is missing: DB_HOST' -or $errorText -match 'Invalid geometry|Geometry data could not')
            {
                throw "Valid geometry did not reach DB configuration validation. $errorText"
            }
        }
        elseif ($errorText -notmatch 'Invalid geometry|Geometry data could not' -or $errorText -match 'Environment variable is missing')
        {
            throw "$($case.Name): geometry error was not detected before DB configuration. $errorText"
        }

        Write-Output "[PASS] Geometry startup: $($case.Name)"
    }

    $success = $true
}
finally
{
    foreach ($entry in $savedEnvironment.GetEnumerator())
    {
        if ($entry.Value)
        {
            Set-Item -LiteralPath "Env:$($entry.Key)" -Value $entry.Value.Value
        }
        else
        {
            Remove-Item -LiteralPath "Env:$($entry.Key)" -ErrorAction SilentlyContinue
        }
    }

    if ($success)
    {
        $resolvedFixture = (Resolve-Path -LiteralPath $fixtureRoot).Path
        if ([System.IO.Path]::GetDirectoryName($resolvedFixture) -ne $tempRoot.TrimEnd('\') -or [System.IO.Path]::GetFileName($resolvedFixture) -notlike 'MyMMORPGGeometryStartup-*')
        {
            throw 'Fixture cleanup path verification failed.'
        }

        Remove-Item -LiteralPath $resolvedFixture -Recurse -Force
    }
    else
    {
        Write-Output "Failed fixtures preserved: $fixtureRoot"
    }
}
