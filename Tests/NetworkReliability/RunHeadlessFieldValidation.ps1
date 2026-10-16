#Requires -Version 7.0
param(
    [ValidateSet('Database','Registration','TradeLoss','TradeScenarios','TradeDisconnect','TradeDamage','TradeRecovery','MovementTransitions','Visibility','JumpBoundary','Soak')][string]$Mode='TradeLoss',
    [ValidateRange(12,10800)][int]$Seconds=7200,
    [ValidateSet(64,96,128)][int]$Players=96,
    [ValidateSet('same','spread')][string]$Layout='same',
    [ValidateRange(1024,65535)][int]$DatabasePort=3307,
    [switch]$SlowPeer
)
$ErrorActionPreference='Stop'
$root=(Resolve-Path -LiteralPath "$PSScriptRoot/../..").Path
$client=(Resolve-Path -LiteralPath "$root/../MyMMORPGClient_3D").Path
$runId=[Guid]::NewGuid().ToString('N')
$runDate=[DateTime]::Now.ToString('yyyy-MM-dd')
$output=Join-Path $root "artifacts/headless-$runDate/$Mode-$runId"
[IO.Directory]::CreateDirectory($output) | Out-Null
$container="mymmorpg-headless-$runId"
$processes=[Collections.Generic.List[object]]::new()
$containerStarted=$false; $passed=$false
$environmentNames=@('DB_HOST','DB_USER','DB_PASSWORD','DB_NAME','SERVER_BIND_IP','MMORPG_TEST_PASSWORD','MMORPG_TEST_GAME_PORT','MMORPG_LOAD_SLOW_PEER')
$previous=@{}; foreach($name in $environmentNames) { $previous[$name]=[Environment]::GetEnvironmentVariable($name) }
function Start-Owned([string]$file,[string[]]$arguments,[string]$label) {
    $info=[Diagnostics.ProcessStartInfo]::new($file); $info.UseShellExecute=$false; $info.CreateNoWindow=$true; $info.WindowStyle='Hidden'; $info.WorkingDirectory=$output
    foreach($argument in $arguments) { $info.ArgumentList.Add($argument) }
    $info.RedirectStandardOutput=$info.RedirectStandardError=$true
    $process=[Diagnostics.Process]::Start($info)
    $stdout=[IO.FileStream]::new((Join-Path $output "$label.out"),'Create','Write','ReadWrite',1)
    $stderr=[IO.FileStream]::new((Join-Path $output "$label.err"),'Create','Write','ReadWrite',1)
    $entry=@{ Process=$process; Out=$stdout; Err=$stderr; CopyOut=$process.StandardOutput.BaseStream.CopyToAsync($stdout); CopyErr=$process.StandardError.BaseStream.CopyToAsync($stderr) }
    $entry.Label=$label; $entry.StartedUtc=$process.StartTime.ToUniversalTime().ToString('O'); $entry.Executable=$info.FileName
    $processes.Add($entry)
    $identities=@($processes | ForEach-Object { @{ label=$_.Label; pid=$_.Process.Id; startedUtc=$_.StartedUtc; executable=$_.Executable } })
    [IO.File]::WriteAllText((Join-Path $output 'processes.json'),($identities | ConvertTo-Json -Depth 3))
    return $entry
}
function Invoke-FixtureSql([string]$sql) {
    # 실행마다 만든 컨테이너 안의 테스트 DB만 변경하고 기존 MySQL과 권한은 건드리지 않는다.
    $sql | docker exec -i $container sh -c 'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysql -uroot mymmorpg_full_test'
    if($LASTEXITCODE -ne 0) { throw 'Isolated fixture SQL failed.' }
}
try {
    if($SlowPeer -and $Mode -ne 'Soak') { throw 'SlowPeer is only supported by the Soak profile.' }
    $listeners=[Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveTcpListeners()
    $ports=if($Mode -eq 'Database') { @($DatabasePort) } else { @($DatabasePort,7776,7777,7778) }
    foreach($port in $ports) { if($listeners.Port -contains $port) { throw "Port $port is already in use; existing processes were preserved." } }
    foreach($path in @('x64/Release/GameServer.exe','x64/Release/LoginServer.exe')) { if(!(Test-Path -LiteralPath (Join-Path $root $path))) { throw "Build missing: $path" } }
    $testBin=Join-Path $client 'Tests/Movement3D/bin/Debug/net9.0'
    if(!(Test-Path -LiteralPath (Join-Path $testBin 'Movement3D.dll'))) { throw 'Build the C# test project first.' }
    $runtime=Join-Path $output 'runtime'; [IO.Directory]::CreateDirectory($runtime) | Out-Null
    # 장시간 실행 중 소스를 다시 빌드해도 실행 중인 바이너리는 바뀌지 않도록 복사한다.
    foreach($file in @('GameServer.exe','LoginServer.exe')) { Copy-Item -LiteralPath (Join-Path $root "x64/Release/$file") -Destination $runtime }
    if($Mode -eq 'Database') { Copy-Item -LiteralPath (Join-Path $root 'x64/Release/InventoryDatabase.exe') -Destination $runtime }
    Get-ChildItem -LiteralPath (Join-Path $root 'x64/Release') -Filter '*.dll' -File | Copy-Item -Destination $runtime
    $testRuntime=Join-Path $runtime 'tests'; [IO.Directory]::CreateDirectory($testRuntime) | Out-Null
    Get-ChildItem -LiteralPath $testBin -File | Copy-Item -Destination $testRuntime
    $manifest=@{ mode=$Mode; seconds=if($Mode -eq 'Soak') { $Seconds } else { $null }; players=if($Mode -eq 'Soak') { $Players } elseif($Mode -eq 'Database') { 0 } else { 2 }; layout=$Layout; slowPeer=[bool]$SlowPeer; startedUtc=[DateTime]::UtcNow.ToString('O'); container=$container; databasePort=$DatabasePort; gameHash=(Get-FileHash -LiteralPath (Join-Path $runtime 'GameServer.exe')).Hash; clientHash=(Get-FileHash -LiteralPath (Join-Path $testRuntime 'Movement3D.dll')).Hash }
    [IO.File]::WriteAllText((Join-Path $output 'manifest.json'),($manifest | ConvertTo-Json))
    $data=Join-Path $output 'data'; [IO.Directory]::CreateDirectory($data) | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $root 'data') -File | Copy-Item -Destination $data
    $maps=Import-Csv -LiteralPath (Join-Path $data 'maps.csv')
    foreach($row in $maps) { if($row.mapId -in @('100000002','100000003')) { $row.spawnX='-160'; $row.spawnY='0' } }
    $maps | Export-Csv -LiteralPath (Join-Path $data 'maps.csv') -NoTypeInformation
    $worlds=Import-Csv -LiteralPath (Join-Path $data 'worlds3d.csv'); foreach($row in $worlds) { $row.spawnZ='-130' }
    $worlds | Export-Csv -LiteralPath (Join-Path $data 'worlds3d.csv') -NoTypeInformation
    foreach($file in @('maps.csv','worlds3d.csv')) { $path=Join-Path $data $file; [IO.File]::WriteAllLines($path,@((Get-Content -LiteralPath $path) -replace '"','')) }
    docker info --format '{{.ServerVersion}}' | Out-Null; if($LASTEXITCODE -ne 0) { throw 'Start the Docker engine first.' }
    docker image inspect mysql:8.4 --format '{{.Id}}' | Out-Null; if($LASTEXITCODE -ne 0) { throw 'Prepare mysql:8.4 first.' }
    $password=[Guid]::NewGuid().ToString('N'); $envFile=Join-Path $output 'mysql-test.env'
    [IO.File]::WriteAllLines($envFile,@("MYSQL_ROOT_PASSWORD=$password",'MYSQL_DATABASE=mymmorpg_full_test','MYSQL_USER=headless_test',"MYSQL_PASSWORD=$password"))
    docker run --detach --rm --name $container --env-file $envFile --publish "127.0.0.1:${DatabasePort}:3306" --tmpfs /var/lib/mysql --mount "type=bind,source=$root\database\init,target=/docker-entrypoint-initdb.d,readonly" mysql:8.4 | Out-Null
    if($LASTEXITCODE -ne 0) { throw 'Temporary MySQL startup failed.' }; $containerStarted=$true
    Remove-Item -LiteralPath $envFile
    $ready=$false; $deadline=[DateTime]::UtcNow.AddSeconds(120)
    while([DateTime]::UtcNow -lt $deadline) {
        $rows=docker exec $container sh -c 'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysql --protocol=TCP -h127.0.0.1 -uroot -Nse "SELECT COUNT(*) FROM mymmorpg_full_test.character_trade_records"' 2>$null
        if($LASTEXITCODE -eq 0 -and "$rows".Trim() -eq '0') { $ready=$true; break }; Start-Sleep -Seconds 1
    }
    if(!$ready) { throw 'Temporary MySQL initialization timed out.' }
    $env:DB_HOST="tcp://127.0.0.1:$DatabasePort"; $env:DB_USER='headless_test'; $env:DB_PASSWORD=$password; $env:DB_NAME='mymmorpg_full_test'; $env:SERVER_BIND_IP='127.0.0.1'; $env:MMORPG_TEST_PASSWORD='test1234'
    if($Mode -eq 'Registration') {
        # 기존 데이터가 있는 상태와 같은 구조 변경을 한 번 더 적용해 재실행도 확인한다.
        Invoke-FixtureSql (Get-Content -Raw -LiteralPath (Join-Path $root 'database/init/006_registration.sql'))
    }
    Remove-Item Env:\MMORPG_TEST_GAME_PORT -ErrorAction SilentlyContinue; Remove-Item Env:\MMORPG_LOAD_SLOW_PEER -ErrorAction SilentlyContinue
    if($SlowPeer) { $env:MMORPG_LOAD_SLOW_PEER='1' }
    if($Mode -eq 'Database') {
        $databaseTest=Start-Owned (Join-Path $runtime 'InventoryDatabase.exe') @() 'client'
        if(!$databaseTest.Process.WaitForExit(120000)) { throw 'Inventory DB validation timed out.' }
        if($databaseTest.Process.ExitCode -ne 0) { throw 'Inventory DB validation failed.' }
        $passed=$true; return
    }
    $monsterCount=if($Mode -in @('Soak','MovementTransitions','Visibility','JumpBoundary')) { 128 } else { 3 }
    if($Mode -eq 'TradeRecovery') {
        Invoke-FixtureSql 'INSERT INTO character_inventories(character_id,hp,max_hp) VALUES (1002,1000,1000); ALTER TABLE character_inventories ADD CONSTRAINT ck_trade_recovery_failure CHECK (character_id<>1002 OR hp=1000);'
    }
    if($Mode -in @('MovementTransitions','Visibility','JumpBoundary')) {
        Invoke-FixtureSql 'INSERT INTO character_inventories(character_id,hp,max_hp) VALUES (1002,60000,60000),(2001,60000,60000);'
    }
    if($Mode -eq 'Soak') {
        $seed=[Collections.Generic.List[string]]::new()
        for($i=0;$i -lt $Players;++$i) { $account=100+$i; $character=3001+$i; $number=$i+1
            $seed.Add("INSERT INTO accounts(id,login_id,password_hash) SELECT $account,'load$number',password_hash FROM accounts WHERE id=1;")
            $seed.Add("INSERT INTO characters(id,account_id,name,level) VALUES($character,$account,'Load$number',1);")
            $seed.Add("INSERT INTO character_inventories(character_id,hp,max_hp) VALUES($character,60000,60000);")
        }; Invoke-FixtureSql ($seed -join [Environment]::NewLine)
    }
    $serverArgs=@((Join-Path $data 'maps.csv'),"--field-monsters=$monsterCount")
    if($Mode -in @('TradeLoss','TradeScenarios','TradeDisconnect','JumpBoundary')) { $serverArgs+='--static-field' }
    $game=Start-Owned (Join-Path $runtime 'GameServer.exe') $serverArgs 'game'
    $login=Start-Owned (Join-Path $runtime 'LoginServer.exe') @() 'login'
    $ready=$false; $deadline=[DateTime]::UtcNow.AddSeconds(20)
    while([DateTime]::UtcNow -lt $deadline) {
        if($game.Process.HasExited -or $login.Process.HasExited) { throw 'Validation server exited during startup.' }
        $ready=$true
        foreach($port in @(7776,7777,7778)) { $probe=[Net.Sockets.TcpClient]::new(); try { $ready=$probe.ConnectAsync('127.0.0.1',$port).Wait(200) -and $probe.Connected -and $ready } catch { $ready=$false } finally { $probe.Dispose() } }
        if($ready) { break }; Start-Sleep -Milliseconds 200
    }
    if(!$ready) { throw 'Validation server startup timed out.' }
    Write-Output "HEADLESS_READY mode=$Mode output=$output"
    $testArgs=switch($Mode) {
        'Registration' { @('--registration-server',$output) }
        'TradeLoss' { @('--trade-loss-server') }
        'TradeScenarios' { @('--trade-scenarios-server') }
        'TradeDisconnect' { @('--trade-disconnect-server',$output) }
        'TradeDamage' { @('--trade-damage-server') }
        'TradeRecovery' { @('--trade-recovery-server',$output) }
        'MovementTransitions' { @('--movement-transitions-server',$output) }
        'Visibility' { @('--field-visibility-server') }
        'JumpBoundary' { @('--jump-boundary-server') }
        'Soak' { @('--field-load',[string]$Players,$Layout,[string]$Seconds) }
    }
    $test=Start-Owned 'dotnet' (@((Join-Path $testRuntime 'Movement3D.dll'))+$testArgs) 'client'
    if($Mode -eq 'TradeDisconnect') {
        $deadline=[DateTime]::UtcNow.AddSeconds(35)
        while(!(Test-Path -LiteralPath (Join-Path $output 'trade-ready')) -and !$test.Process.HasExited -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 100 }
        if(!(Test-Path -LiteralPath (Join-Path $output 'trade-ready'))) { throw 'Trade preparation marker missing.' }
        # 검사 연결에서만 행을 잠그고 양측 연결 해제 후 정상 커밋할 수 있도록 잠금을 해제한다.
        $lock=Start-Owned 'docker' @('exec',$container,'sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysql -uroot --unbuffered -N mymmorpg_full_test -e "BEGIN; SELECT id FROM characters WHERE id=1002 FOR UPDATE; SELECT 999; DO SLEEP(8); COMMIT;"') 'db-lock'
        $deadline=[DateTime]::UtcNow.AddSeconds(10); $locked=$false
        while([DateTime]::UtcNow -lt $deadline -and !$lock.Process.HasExited) {
            $lock.Out.Flush()
            if((Get-Content -LiteralPath (Join-Path $output 'db-lock.out') -ErrorAction SilentlyContinue) -contains '999') { $locked=$true; break }
            Start-Sleep -Milliseconds 100
        }
        if(!$locked) { throw 'DB row lock marker missing.' }
        [IO.File]::WriteAllText((Join-Path $output 'trade-lock-ready'),'locked')
    }
    $timeoutSeconds=if($Mode -eq 'Soak') { $Seconds+180 } elseif($Mode -eq 'TradeScenarios') { 240 } elseif($Mode -in @('Registration','MovementTransitions','Visibility')) { 120 } else { 60 }
    $deadline=[DateTime]::UtcNow.AddSeconds($timeoutSeconds)
    $recoveryReleased=$false
    $registrationFailure=$false; $registrationReleased=$false
    $nextResources=[DateTime]::UtcNow
    while(!$test.Process.HasExited) {
        if([DateTime]::UtcNow -ge $deadline) { throw 'Headless validation timed out.' }
        if($Mode -eq 'Registration') {
            if(!$registrationFailure -and (Test-Path -LiteralPath (Join-Path $output 'registration-fixture-ready'))) {
                Invoke-FixtureSql "ALTER TABLE characters ADD CONSTRAINT ck_registration_test_failure CHECK (name<>'Player');"
                [IO.File]::WriteAllText((Join-Path $output 'registration-failure-ready'),'ready'); $registrationFailure=$true
            }
            if($registrationFailure -and !$registrationReleased -and (Test-Path -LiteralPath (Join-Path $output 'registration-failure-checked'))) {
                $orphans=Invoke-FixtureSql "SELECT COUNT(*) FROM accounts WHERE login_id='rollback_user';"
                if("$orphans".Trim() -notmatch '^COUNT\(\*\)\s+0$') { throw 'Character failure left an account behind.' }
                Invoke-FixtureSql 'ALTER TABLE characters DROP CHECK ck_registration_test_failure;'
                [IO.File]::WriteAllText((Join-Path $output 'registration-retry-ready'),'ready'); $registrationReleased=$true
            }
        }
        if($Mode -eq 'TradeRecovery' -and !$recoveryReleased -and (Test-Path -LiteralPath (Join-Path $output 'recovery-held'))) {
            Invoke-FixtureSql 'ALTER TABLE character_inventories DROP CHECK ck_trade_recovery_failure;'
            $recoveryReleased=$true
        }
        if($Mode -eq 'Soak' -and [DateTime]::UtcNow -ge $nextResources) {
            # GC 추정값과 별도로 소유한 두 프로세스의 실제 메모리·CPU 표본을 남긴다.
            try {
                $game.Process.Refresh(); $test.Process.Refresh()
                $row=@{ utc=[DateTime]::UtcNow.ToString('O'); gamePid=$game.Process.Id; clientPid=$test.Process.Id; gamePrivateBytes=$game.Process.PrivateMemorySize64; gameWorkingSetBytes=$game.Process.WorkingSet64; gameCpuSeconds=$game.Process.TotalProcessorTime.TotalSeconds; clientPrivateBytes=$test.Process.PrivateMemorySize64; clientWorkingSetBytes=$test.Process.WorkingSet64; clientCpuSeconds=$test.Process.TotalProcessorTime.TotalSeconds }
                Add-Content -LiteralPath (Join-Path $output 'resources.jsonl') -Value ($row | ConvertTo-Json -Compress)
            }
            catch { if(!$test.Process.HasExited) { throw } }
            $nextResources=[DateTime]::UtcNow.AddSeconds(60)
        }
        Start-Sleep -Seconds 1
    }
    if($test.Process.ExitCode -ne 0) { throw "Headless client failed with code $($test.Process.ExitCode)." }
    if($Mode -eq 'Registration') {
        $accounts=@(Get-Content -Raw -LiteralPath (Join-Path $output 'registration-accounts.json') | ConvertFrom-Json)
        $expected=$accounts.Count
        $query=@'
SELECT CONCAT(
(SELECT COUNT(*) FROM accounts WHERE id>2),'|',
(SELECT COUNT(*) FROM characters WHERE account_id>2),'|',
(SELECT COUNT(DISTINCT account_id) FROM characters WHERE account_id>2 AND level=1 AND name=CONCAT('Player',id)),'|',
(SELECT COUNT(DISTINCT SUBSTRING_INDEX(SUBSTRING_INDEX(password_hash,'$',3),'$',-1)) FROM accounts WHERE id>2),'|',
(SELECT COUNT(*) FROM accounts WHERE id>2 AND password_hash NOT LIKE 'pbkdf2-sha256$600000$%')) AS registration_summary;
'@
        $actual=Invoke-FixtureSql $query
        if("$actual".Trim() -notmatch ("registration_summary\s+"+[regex]::Escape("$expected|$expected|$expected|$expected|0")+'$')) { throw 'Registration SQL counts, atomicity or password salts differ.' }
        Write-Output "[PASS] Registration DB: $expected accounts, one Lv1 character each, distinct salts and no plain-text passwords"
    }
    $passed=$true
}
finally {
    foreach($entry in $processes) {
        if(!$entry.Process.HasExited) { Stop-Process -Id $entry.Process.Id; $entry.Process.WaitForExit(5000) | Out-Null }
        [Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]]@($entry.CopyOut,$entry.CopyErr),5000) | Out-Null
        $entry.Out.Dispose(); $entry.Err.Dispose(); $entry.Process.Dispose()
    }
    if($containerStarted) { docker logs $container *> (Join-Path $output 'mysql.log'); docker stop $container | Out-Null }
    if($envFile -and (Test-Path -LiteralPath $envFile)) { Remove-Item -LiteralPath $envFile }
    foreach($name in $environmentNames) { [Environment]::SetEnvironmentVariable($name,$previous[$name]) }
    [IO.File]::WriteAllText((Join-Path $output 'validation.result'),$(if($passed) { 'PASS' } else { 'FAIL' }))
    Write-Output "HEADLESS_RESULT passed=$passed output=$output"
    if(Test-Path -LiteralPath (Join-Path $output 'client.out')) { Get-Content -LiteralPath (Join-Path $output 'client.out') -Tail 8 }
    if(Test-Path -LiteralPath (Join-Path $output 'client.err')) { Get-Content -LiteralPath (Join-Path $output 'client.err') -Tail 5 }
}
