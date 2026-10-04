param([string]$ServerExe = "$PSScriptRoot/../../x64/Release/GameServer.exe")

$ErrorActionPreference = 'Stop'
$serverPath = (Resolve-Path -LiteralPath $ServerExe).Path
$fixtureDirectory = Join-Path $env:TEMP ("MyMMORPGMapData-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixtureDirectory | Out-Null
$header = 'mapId,spawnX,spawnY,minX,maxX,minY,maxY,moveSpeed,moveBurst'
$validRow = '100000000,0,0,-400,400,-200,200,80,12'
$cases = [ordered]@{
    DuplicateId = @($header, $validRow, $validRow)
    InvalidSpawn = @($header, '100000000,401,0,-400,400,-200,200,80,12')
    InvalidBounds = @($header, '100000000,0,0,400,-400,-200,200,80,12')
    InvalidSpeed = @($header, '100000000,0,0,-400,400,-200,200,0,12')
    InvalidBurst = @($header, '100000000,0,0,-400,400,-200,200,80,81')
    CoordinateOverflow = @($header, '100000000,0,0,-400,2147483648,-200,200,80,12')
    ExtraColumn = @($header, "$validRow,1")
    MissingColumn = @($header, '100000000,0,0,-400,400,-200,200,80')
    MissingStartMap = @($header, '100000001,0,0,-400,400,-200,200,80,12')
    EmptyMaps = @($header)
}

foreach ($case in $cases.GetEnumerator())
{
    $fixturePath = Join-Path $fixtureDirectory ($case.Key + '.csv')
    $errorPath = Join-Path $fixtureDirectory ($case.Key + '.err')
    $outputPath = Join-Path $fixtureDirectory ($case.Key + '.out')
    [System.IO.File]::WriteAllLines($fixturePath, [string[]]$case.Value, [System.Text.UTF8Encoding]::new($false))
    $process = Start-Process -FilePath $serverPath -ArgumentList @($fixturePath) -WindowStyle Hidden -RedirectStandardError $errorPath -RedirectStandardOutput $outputPath -PassThru

    if (-not $process.WaitForExit(5000))
    {
        Stop-Process -Id $process.Id
        throw "$($case.Key): server did not reject the map data before startup."
    }

    $errorText = [System.IO.File]::ReadAllText($errorPath)
    if ($process.ExitCode -ne 1 -or $errorText -notmatch 'Invalid map data|Start map is missing|Map data is empty')
    {
        throw "$($case.Key): unexpected exit code $($process.ExitCode). $errorText"
    }

    Write-Output "[PASS] Map data: $($case.Key)"
}
