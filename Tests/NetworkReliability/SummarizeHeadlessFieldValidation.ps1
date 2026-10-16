#Requires -Version 7.0
param([Parameter(Mandatory)][string]$RunDirectory)
$ErrorActionPreference='Stop'
$run=(Resolve-Path -LiteralPath $RunDirectory).Path
function Read-SharedLines([string]$name) {
    $path=Join-Path $run $name
    if(!(Test-Path -LiteralPath $path)) { return }
    $stream=[IO.FileStream]::new($path,'Open','Read','ReadWrite'); $reader=[IO.StreamReader]::new($stream)
    try { while(!$reader.EndOfStream) { $reader.ReadLine() } } finally { $reader.Dispose() }
}
function Metric([string]$line) {
    $result=@{}
    foreach($match in [regex]::Matches($line,'([A-Za-z][A-Za-z0-9]*)=(-?\d+(?:\.\d+)?)')) { $result[$match.Groups[1].Value]=[double]::Parse($match.Groups[2].Value,[Globalization.CultureInfo]::InvariantCulture) }
    return $result
}
function Stats($values) {
    $sorted=@($values | Sort-Object)
    if(!$sorted.Count) { return $null }
    return @{ samples=$sorted.Count; min=$sorted[0]; p50=$sorted[[Math]::Max(0,[Math]::Ceiling($sorted.Count*0.5)-1)]; p95=$sorted[[Math]::Max(0,[Math]::Ceiling($sorted.Count*0.95)-1)]; max=$sorted[-1] }
}
function Sum-Field($rows,[string]$key) { return ($rows | ForEach-Object { $_[$key] } | Measure-Object -Sum).Sum }
function Max-Field($rows,[string]$key) { return ($rows | ForEach-Object { $_[$key] } | Measure-Object -Maximum).Maximum }
$manifest=Get-Content -Raw -LiteralPath (Join-Path $run 'manifest.json') | ConvertFrom-Json
$server=[Collections.Generic.List[object]]::new(); $movement=[Collections.Generic.List[object]]::new(); $field=[Collections.Generic.List[object]]::new()
$population=$null
foreach($line in Read-SharedLines 'game.out') {
    # 실행 중 아직 쓰고 있는 줄은 제외하고 끝까지 기록된 진단만 집계한다.
    if($line -like 'Field population:*') { $population=Metric $line }
    elseif($line -match '^Server metrics:.*catchUpLimits=\d+$') { $server.Add((Metric $line)) }
    elseif($line -match '^Movement metrics:.*map=100000002 channel=1 .*maxRelayWaitMs=\d+$') { $movement.Add((Metric $line)) }
    elseif($line -match '^Field metrics: map=100000002 channel=1 .*dropBytes=\d+$') { $field.Add((Metric $line)) }
}
$progress=[Collections.Generic.List[object]]::new(); $load=$null; $measurementStart=$null; $measured=$null; $slowRx=$null
foreach($line in Read-SharedLines 'client.out') {
    if($line -like 'PROGRESS *') { $progress.Add((Metric $line)) }
    elseif($line -like 'LOAD *') { $load=Metric $line }
    elseif($line -like 'MEASUREMENT_START *') { $measurementStart=$line }
    elseif($line -like 'MEASURED *') { $measured=$line }
    elseif($line -like 'SLOW_RX *') { $slowRx=Metric $line }
}
$resources=[Collections.Generic.List[object]]::new()
foreach($line in Read-SharedLines 'resources.jsonl') { try { $resources.Add(($line | ConvertFrom-Json)) } catch { } }
$resultPath=Join-Path $run 'validation.result'; $result=if(Test-Path -LiteralPath $resultPath) { (Get-Content -Raw -LiteralPath $resultPath).Trim() } else { 'RUNNING' }
$layout=$manifest.layout
if(!$layout -and $measurementStart -match 'layout=(\w+)') { $layout=$Matches[1] }
$summary=@{
    mode=$manifest.mode; result=$result; requestedPlayers=$manifest.players; requestedSeconds=$manifest.seconds; layout=$layout; slowPeer=[bool]$manifest.slowPeer; fieldPopulation=$population; measurementStart=$measurementStart; clientLoad=$load; clientMeasured=$measured; slowReceiver=$slowRx
    progressSamples=$progress.Count; lastProgress=if($progress.Count) { $progress[-1] } else { $null }
    serverScope='Complete reporting windows of the entire process, potentially including preparation and diagnostic peers; excludes the unfinished last window'
    serverFiveSecondWindowMaxTickUs=(Stats @($server | ForEach-Object { $_['maxTickUs'] }))
    maxTickLagMs=(Max-Field $server 'maxTickLagMs'); catchUpLimits=(Sum-Field $server 'catchUpLimits')
    peakSampledQueuedBytes=(Max-Field $server 'peakSampledQueuedBytes'); maxSessions=(Max-Field $server 'sessions')
    channelOneMovement=@{ bytes=(Sum-Field $movement 'relayBytes'); actions=(Sum-Field $movement 'relayActions'); sendFailures=(Sum-Field $movement 'sendFailures'); queueOverflows=(Sum-Field $movement 'queueOverflows'); maxPendingPerRecipient=(Max-Field $movement 'peakPendingPerRecipient'); maxRelayWaitMs=(Max-Field $movement 'maxRelayWaitMs') }
    channelOneField=@{ monsterBytes=(Sum-Field $field 'bytes'); monsterBuilds=(Sum-Field $field 'monsterBuilds'); attacks=(Sum-Field $field 'attacks'); attackBytes=(Sum-Field $field 'attackBytes'); maxAttackUs=(Max-Field $field 'maxAttackUs'); vitalsBytes=(Sum-Field $field 'vitalsBytes'); dropBytes=(Sum-Field $field 'dropBytes'); failures=(Sum-Field $field 'failures') }
    invalidManagedEstimateSamples=@($progress | Where-Object { $_['managedBytes'] -lt 0 }).Count
    resourceScope='External one-minute process samples; timestamps delimit the measured subrange'
    gamePrivateBytes=(Stats @($resources | ForEach-Object { $_.gamePrivateBytes })); clientPrivateBytes=(Stats @($resources | ForEach-Object { $_.clientPrivateBytes }))
    firstResource=if($resources.Count) { $resources[0] } else { $null }; lastResource=if($resources.Count) { $resources[-1] } else { $null }
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $run 'summary.json') -Encoding utf8
$progress | ForEach-Object { [pscustomobject]$_ } | Export-Csv -LiteralPath (Join-Path $run 'client-progress.csv') -NoTypeInformation -Encoding utf8
$resources | Export-Csv -LiteralPath (Join-Path $run 'process-resources.csv') -NoTypeInformation -Encoding utf8
$window=0; $elapsed=0
$server | ForEach-Object {
    ++$window; $elapsed+=$_['intervalMs']/1000
    [pscustomobject][ordered]@{ window=$window; elapsedSeconds=$elapsed; sessions=$_['sessions']; players=$_['players']; maxTickUs=$_['maxTickUs']; maxTickLagMs=$_['maxTickLagMs']; queuedBytes=$_['queuedBytes']; peakSampledQueuedBytes=$_['peakSampledQueuedBytes']; catchUpLimits=$_['catchUpLimits'] }
} | Export-Csv -LiteralPath (Join-Path $run 'server-windows.csv') -NoTypeInformation -Encoding utf8
Write-Output "SUMMARY result=$result progressSamples=$($progress.Count) serverWindows=$($server.Count) output=$(Join-Path $run 'summary.json')"
