#Requires -Version 7.0
$ErrorActionPreference='Stop'
$root=(Resolve-Path -LiteralPath "$PSScriptRoot/../..").Path
$settings=@{}
foreach($line in Get-Content -LiteralPath (Join-Path $root '.env')) { if($line -match '^([A-Z_]+)=(.*)$') { $settings[$matches[1]]=$matches[2].Trim().Trim('"').Trim("'") } }
$schema='mymmorpg_full_test_trade_'+[Guid]::NewGuid().ToString('N')
$granted=$false
$dbUser=$settings['DB_USER']
if($dbUser -notmatch '^[A-Za-z0-9_]+$') { throw 'Unexpected test DB user name.' }
function Invoke-TestSql([string]$sql) {
    $sql | docker exec -i mymmorpg-mysql sh -c 'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysql -uroot'
    if($LASTEXITCODE -ne 0) { throw 'Test DB setup failed.' }
}
try {
    Invoke-TestSql "CREATE DATABASE $schema CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci; GRANT ALL ON $schema.* TO '$dbUser'@'%';"
    $granted=$true
    $sql="USE $schema;`n"+((Get-ChildItem -LiteralPath (Join-Path $root 'database/init') -Filter '*.sql' | Sort-Object Name | ForEach-Object { Get-Content -Raw -LiteralPath $_.FullName }) -join "`n")
    Invoke-TestSql $sql
    # 비밀번호는 검사 프로세스의 환경 변수로만 전달한다.
    foreach($key in @('DB_HOST','DB_USER','DB_PASSWORD')) { [Environment]::SetEnvironmentVariable($key,$settings[$key],'Process') }
    $env:DB_NAME=$schema
    $env:PATH='C:/Program Files/MySQL/MySQL Connector C++ 26.7/lib64/vs14;'+$env:PATH
    & (Join-Path $root 'x64/Release/InventoryDatabase.exe')
    if($LASTEXITCODE -ne 0) { throw 'Trade database regression failed.' }
}
finally {
    # 이번 실행에서 만든 고유 검사 스키마만 정리한다.
    if($schema -match '^mymmorpg_full_test_trade_[a-f0-9]{32}$') {
        try { if($granted) { Invoke-TestSql "REVOKE ALL ON $schema.* FROM '$dbUser'@'%';" } }
        finally { Invoke-TestSql "DROP DATABASE IF EXISTS $schema;" }
    }
}
