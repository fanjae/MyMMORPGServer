param([switch]$UseExistingTestDatabase, [switch]$ComparisonOnly, [switch]$SpreadOnly, [ValidatePattern('^[a-z0-9-]+$')][string]$LabelPrefix='load')
$ErrorActionPreference='Stop'
if (!$UseExistingTestDatabase) { throw 'Explicitly select the disposable existing test database.' }
$root=(Resolve-Path -LiteralPath "$PSScriptRoot/../..").Path
$client=(Resolve-Path -LiteralPath "$root/../MyMMORPGClient_3D").Path
$output=Join-Path $root 'artifacts/field-validation-2026-10-14'
$session=Join-Path $PSScriptRoot 'FieldValidationSession.ps1'
$database=Join-Path $PSScriptRoot 'FieldValidationDatabase.ps1'
$seed=[Collections.Generic.List[string]]::new()
for($i=0;$i -lt 32;++$i) {
    $account=100+$i; $character=3001+$i; $number=$i+1
    $seed.Add("INSERT INTO accounts(id,login_id,password_hash) SELECT $account,'load$number',password_hash FROM accounts WHERE id=1 ON DUPLICATE KEY UPDATE id=$account;")
    $seed.Add("INSERT INTO characters(id,account_id,name,level) VALUES($character,$account,'Load$number',1) ON DUPLICATE KEY UPDATE id=characters.id;")
    $seed.Add("INSERT INTO character_inventories(character_id,hp,max_hp) VALUES($character,60000,60000) ON DUPLICATE KEY UPDATE hp=60000,max_hp=60000,revision=revision+1;")
}
& $database -Sql ($seed -join [Environment]::NewLine)
$conditions=@()
foreach($monsters in @(3,32,128)) { foreach($players in @(2,8,32)) { $conditions+=@{Monsters=$monsters;Players=$players;Layout='same'} } }
$conditions+=@{Monsters=128;Players=32;Layout='spread'}
if($ComparisonOnly) { $conditions=@($conditions | Where-Object Players -eq 32) }
if($SpreadOnly) { $conditions=@($conditions | Where-Object Layout -eq 'spread') }
$env:MMORPG_TEST_PASSWORD='test1234'
Remove-Item Env:\MMORPG_TEST_GAME_PORT -ErrorAction SilentlyContinue
foreach($condition in $conditions) {
    $label="$LabelPrefix-$($condition.Monsters)-$($condition.Players)-$($condition.Layout)"
    # 이번 검사 세션의 서버만 재시작하고 기존 DB·테스트 결과는 그대로 사용한다.
    $starter=Start-Process -FilePath 'pwsh' -ArgumentList @('-File',$session,'-Action','RestartGame','-Label',$label,'-MonsterCount',$condition.Monsters) -WindowStyle Hidden -PassThru
    try {
        Start-Sleep -Seconds 2
        if($starter.HasExited) { throw "Load server failed: $label" }
        dotnet (Join-Path $client 'Tests/Movement3D/bin/Debug/net9.0/Movement3D.dll') --field-load $condition.Players $condition.Layout > (Join-Path $output "$label.result") 2>&1
        if($LASTEXITCODE -ne 0) { Get-Content (Join-Path $output "$label.result"); throw "Actual server load failed: $label" }
        Get-Content (Join-Path $output "$label.result")
    }
    finally {
        & $session -Action ClosePlayers
        # 다음 조건은 기록된 GameServer만 정리한다. LoginServer는 같은 DB 연결을 유지한다.
        $state=Get-Content -Raw (Join-Path $output 'session.json') | ConvertFrom-Json
        $game=Get-Process -Id $state.game -ErrorAction SilentlyContinue
        if($game -and $game.Path -eq (Join-Path $root 'x64/Release/GameServer.exe')) { Stop-Process -Id $game.Id }
        $starter.WaitForExit(5000) | Out-Null
    }
}
