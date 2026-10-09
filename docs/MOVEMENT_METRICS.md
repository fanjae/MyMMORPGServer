# 행동 중계량과 송신 대기량 계측

2026-10-09 구현. Game protocol version 6과 Login version 2를 유지한다. 이동·충돌 권한, 점프 잠금, 200ms 송신 간격, 1초 체크포인트는 기존 정책을 사용한다.

## 서버 로그

GameServer 시작 로그의 `GameServer protocol version=6`으로 빌드에 포함된 버전을 확인한다. DB 연결 등 시작 절차가 성공한 뒤 출력되므로 로그가 없다는 이유만으로 버전을 판단하지 않는다.

기존 `Server metrics`와 맵별 `Movement metrics`를 약 5초마다 출력한다. `intervalMs`는 직전 집계부터 실제 지난 시간이다. 첫 구간은 서버 준비 시간도 포함할 수 있다. 패킷/초는 `packets * 1000 / intervalMs`, 바이트/초도 같은 방식으로 계산한다.

| 항목 | 집계 기준 |
|---|---|
| receivedPackets / receivedBytes | Map의 행동 검증까지 도달한 묶음 수와 게임 헤더 포함 바이트. 인증·프레임 크기 오류로 핸들러에서 종료된 요청은 포함하지 않음 |
| rejectedPackets | 맵·generation·배치 순서·레코드 범위 등으로 묶음 전체를 거절한 횟수 |
| acceptedActions / rejectedActions | 정상 묶음 내부의 허용/거절 행동 수. 공중 재점프처럼 행동만 거절한 경우를 별도 집계 |
| relayPackets / relayBytes / relayActions | 수신자 Session.Send가 수락한 중계 메시지·바이트·행동 사본 수. TCP 쓰기 완료나 수신 확인은 아님 |
| sendFailures | 중계 메시지 송신을 수락하지 못한 수신자 횟수 |
| queueOverflows | 수신자의 256개 행동 한도로 추가하지 못한 행동 사본 수 |
| pendingActions | 집계 시점에 모든 수신자의 행동 대기열에 남은 사본 수의 합 |
| maxPendingPerRecipient | 집계 시점의 수신자별 대기열 중 가장 큰 개수 |
| peakPendingPerRecipient | 해당 구간에 관찰한 수신자별 행동 대기열의 최대 개수. 구간 시작에 남은 대기열도 포함 |
| oldestPendingMs | 현재 남은 행동 중 가장 오래 기다린 행동의 서버 내부 대기 시간 |
| maxRelayWaitMs | 이번 구간에 송신을 시도한 행동의 최대 서버 내부 대기 시간. 실패한 송신 시도도 포함 |
| Server metrics의 queuedBytes | 집계 시점에 전체 Session의 TCP 송신 대기 바이트 합. 이동 외 패킷과 서버 간 연결도 포함하며 순간 최대값은 아님 |

행동 사본은 본인에게 돌아가는 중계도 포함한다. 예를 들어 32명에게 하나의 행동을 보내면 `acceptedActions=1`, `relayActions=32`가 된다. 집계 시 카운터만 초기화하며 대기 중인 행동을 제거하지 않는다. 맵 퇴장 시 기존 정리 규칙대로 해당 수신자와 발신자의 행동 사본을 제거한다.

시간은 서버의 steady_clock으로 계산한다. `maxRelayWaitMs`는 발신자부터 서버까지의 네트워크 지연, 서버 TCP 대기, 수신자까지의 네트워크 지연과 Unity 표시 버퍼를 포함하지 않는다.

## 클라이언트 화면과 연결별 집계

게임 테스트 패널에 다음을 표시한다. 작은 창에서도 Map 변경 버튼에 접근할 수 있도록 패널을 스크롤할 수 있다.

- `Game protocol: 6`: 현재 클라이언트 코드의 게임 프로토콜 버전.
- `Movement TX`: opcode 23의 쓰기 완료 메시지 수와 바이트 수.
- `Movement RX`: opcode 24의 완전한 메시지 수신 횟수와 바이트 수. 본인 행동의 중계도 포함한다.
- `Game TX/RX`: 로그인 연결과 구분한 게임 연결 전체 송수신 바이트 수.
- `TCP pending / Peak`: 게임 연결 전체의 현재 송신 대기량과 연결 수명 동안의 최대값. 쓰기 진행 중인 메시지도 완료 전까지 대기량에 포함한다.
- `Actions pending`: 아직 행동 묶음으로 꺼내지 않은 클라이언트 행동 수. TCP 대기량과는 별개다.

`TcpSession.GetTraffic()`은 연결 전체, `GetTraffic(opcode)`는 해당 opcode의 송수신 집계를 반환한다. opcode를 지정해도 대기량과 최대 대기량은 연결 전체 기준이다. 수신 루프와 송신 요청이 함께 집계하므로 같은 잠금으로 카운터와 대기량을 읽고 변경한다.

송신 성공은 NetworkStream 쓰기가 완료된 시점이다. 상대 수신·화면 반영까지 보장하지 않는다. 수신은 헤더와 payload가 모두 도착한 뒤 집계하며 역직렬화·Unity 메인 스레드 처리 전에 센다. 분할 수신이나 TCP 세그먼트 수를 메시지 수로 세지 않는다. 송신 실패 시 일부 바이트가 운영체제로 넘어갔을 수 있지만 완료되지 않은 메시지는 성공 집계에 넣지 않는다.

바이트 수는 4바이트 게임 헤더를 포함한다. TCP/IP 헤더·ACK·재전송은 포함하지 않는다. Map 변경은 같은 연결의 누적값을 유지한다. 연결 종료 시 조회는 0을 반환하고 새 연결은 집계와 대기량 최대값을 새로 시작한다. 이전 연결의 늦은 쓰기 완료가 새 집계를 변경하지 않는다. 종료 전 수치를 비교하려면 미리 기록한다.

## 로컬 반복 검증

서버 솔루션과 `Tests/NetworkReliability/NetworkReliability.vcxproj`를 Release x64로 빌드한 뒤 실행한다.

```powershell
.\Tests\NetworkReliability\bin\NetworkReliability.exe
.\Tests\NetworkReliability\RunMovementRelay.ps1
dotnet run --project ..\MyMMORPGClient\Tests\MapLocalIntegration\MapLocalIntegration.csproj -- --network
```

스크립트는 loopback 전용 C++ fixture와 두 C# 클라이언트를 사용한다. 0ms, 80±35ms, 150±60ms의 각 편도 지연/지터 조건에서 계측값과 실제 송신·파싱한 프레임의 수·바이트가 일치하는지 확인한다. `[METRICS]` 줄의 값은 시나리오 전체 누적값이며 초당 수치가 아니다. fixture는 DB와 로그인 인증을 생략하므로 실서버 통합 검증과 구분한다.

## 수동 확인 시 기록할 값

1. 새로 빌드한 서버의 시작 버전 로그와 Unity 패널의 버전 6을 확인한다. 계측 패널이 없는 이전 Unity 실행 파일은 이번 코드로 다시 빌드해야 한다.
2. 측정 시작/종료 시각, 참여 인원, Map ID, 양쪽 TX/RX 누적값을 적고 구간 증가량을 계산한다.
3. 서버의 같은 시간대 `intervalMs`, 중계 바이트, `pendingActions`, `peakPendingPerRecipient`, `oldestPendingMs`, `queuedBytes`를 함께 기록한다.
4. 점프·착지·방향 전환·정지·포커스 차단과 Map 변경/재입장을 나누어 확인한다. 위치 오차와 키 입력부터 상대 화면 표시까지의 지연은 화면 관찰·녹화가 필요하며 이 패킷 집계에서 자동 산출하지 않는다.

## 후속 정책 검토

- 1초 체크포인트와 원격 예측의 2초 무응답 한도를 유지한다. 체크포인트를 2초로 늘리면 전송 대기·네트워크 지터·프레임 지연만으로 무응답 한도에 닿을 수 있다. 주기 변경 전에 무응답 한도의 여유와 새 보고 수신 시 예측 재개·기준 상태 복구를 함께 검증한다.
- 현재 상대 표시의 보정 계수는 초당 12이며 4 Unity 단위보다 큰 오차 또는 Respawn은 즉시 보정한다. 서버 20단위가 Unity 1단위이므로 현재 큰 오차 기준은 서버 80단위 초과다. 실제 화면에서의 최적 기준은 이번 계측만으로 결정하지 않는다.
- 같은 맵 전체 중계는 참여 인원만큼 행동 사본을 늘린다. 주변 대상 전송은 입장/이탈 및 최신 기준 상태 전달을 포함해 별도 설계한다. 단순 거리 필터만 넣으면 주변에 새로 나타난 캐릭터의 기준 상태가 누락될 수 있다.
- 입력 변경 합치기는 중간 방향 유지가 이동 경로에 영향을 주는지 먼저 확인한다. Jump·Land·Fall·Respawn과 jumpId·행동 순서를 손실시키지 않는다.
- 클라이언트는 표현·물리 재현을 담당하고 서버는 인증·소속·순서·행동 범위를 확인하는 현재 분업을 유지한다. 전투·피격·보상 등 게임 판정에는 별도의 서버 검증이 필요하다.

서로 다른 PC의 LAN/원격망 테스트와 최소 3D 샘플은 이번 작업에 포함하지 않았다.
