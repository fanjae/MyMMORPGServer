param([Parameter(Mandatory)][string]$Sql)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath "$PSScriptRoot/../..").Path
Push-Location $root
try {
    # SQL은 표준 입력으로 전달해 따옴표나 식을 셸 명령으로 해석하지 않도록 한다.
    $Sql | docker compose exec -T mysql sh -c 'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysql -uroot "$MYSQL_DATABASE"'
    if ($LASTEXITCODE -ne 0) { throw 'Existing test database command failed.' }
}
finally { Pop-Location }
