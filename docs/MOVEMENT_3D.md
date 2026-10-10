# 3D 행동 중계 — 2026-10-10

기존 `MyMMORPGClient`의 version 6 발판 행동 중계는 유지하고, 새 `MyMMORPGClient_3D`의 실제 XYZ 이동을 별도 protocol version 7로 추가했다. 서버는 두 버전을 구분해 서로 다른 차원의 맵에 입장시킨다. Login protocol version은 공통 2다.

## 실행

- 3D Unity 프로젝트의 `Assets/Scenes/SampleScene.unity`를 Play하면 로그인·캐릭터 선택·3D 월드가 자동 구성된다.
- DB 없이 보려면 **Offline preview**를 선택한다. WASD/방향키는 XZ 이동, Space는 점프, R은 오프라인 위치 초기화, Esc 또는 빈 화면 클릭은 입력 포커스 해제다.
- 실제 접속은 기존 LoginServer → 캐릭터 선택 → GameServer 인증을 사용한다. version 7은 맵 `100000002`에서 시작한다. `100000003`과 왕복해 Map 초기화를 확인할 수 있다.
- 서버 솔루션을 Release x64로 빌드하면 새 3D CSV도 실행 파일의 `data` 디렉터리로 복사된다.
- Unity의 **Tools → MyMMORPG → Build 3D Windows Client** 메뉴에서 Windows Player를 만든다. 빌드 위치는 3D 저장소의 `Builds/Movement3D/MyMMORPGClient_3D.exe`다.

상세한 클라이언트 조작은 [README_3D.md](../../MyMMORPGClient_3D/README_3D.md)에 있다. 기존 2D 클라이언트를 version 7로 변경할 필요는 없다. 같은 캐릭터의 중복 접속 차단은 차원과 관계없이 유지된다.

## 좌표와 물리

서버 20단위 = Unity 1m, XZ 평면 이동과 Y축 중력을 사용한다. 캐릭터 위치는 발 기준이다. 기본 반경은 6단위(0.3m), 높이는 36단위(1.8m), 이동 속도는 80단위/s(4m/s)다. 대각선 입력은 정규화한다. 방향은 이동 입력 X/Z와 별도로 yaw를 1/100도 단위로 전달한다.

캐릭터는 캡슐로 표시하지만 충돌 몸체는 축 정렬 박스다. 두 클라이언트가 `Motion3DSimulation`의 같은 20ms 단계와 같은 서버 지형을 사용한다. X 이동 → Z 이동 → Y 이동 순서로 경계와 박스를 검사한다. 고속 이동의 벽·지지면 교차를 검사하며 단차는 점프해서 올라간다. 자동 계단 오르기, 경사, 회전/이동 지형, 캐릭터 간 충돌과 Animator는 이번 범위에 포함하지 않는다.

온라인 지형은 `worlds3d.csv`와 `boxes3d.csv`로 정의한다. 시작 위치와 경계 XY는 기존 `maps.csv`를 사용하고 Z 경계·spawn·몸체·속도는 `worlds3d.csv`에서 읽는다. 시작 지지면, 몸체가 경계 안에 있는지, 박스에 끼어 있지 않은지, 박스 ID·범위를 서버 시작 때 검증한다.

## 이벤트와 서버 역할

클라이언트가 Input·Jump·Land·Checkpoint·Respawn·Fall을 생성한다. 행동 기준 상태는 해당 clientTick의 물리 직후다. 본인은 지상에서 새 점프 눌림을 받아 즉시 계산하며, 공중 눌림은 예약하지 않는다. 착지한 뒤 새 눌림이 있어야 다음 점프를 실행한다.

서버는 인증된 캐릭터와 맵으로 요청을 묶고 generation·배치/행동 순서·XYZ/속도/입력 범위·지상 지지면·jumpId 상태 전이를 검증한다. 같은 jumpId의 Land 또는 올바른 Respawn만 공중 잠금을 해제한다. Input/Checkpoint의 지상 비트로 잠금을 해제하지 않는다. 거절된 점프의 행동 번호도 소비해 착지 후 재사용을 막는다.

서버는 이동·중력·충돌·착지를 시뮬레이션하지 않는다. 착지가 실제 이동 경로에서 가능했는지, 중간에 벽을 통과했는지는 이 검증으로 보장하지 않는다. v7 연결은 3D 행동 opcode만, v6 연결은 기존 2D 행동 opcode만 처리하며 다른 차원의 Map 변경을 거절한다.

## 송신과 복구

- 클라이언트는 최소 200ms 간격에 최대 32개 행동을 묶는다. 이동 관련 게임 메시지는 최대 초당 5회이며 동일 방향 유지/정지 상태에서는 약 1초 체크포인트를 사용한다.
- 서버도 수신자별 최소 200ms 간격에 여러 캐릭터의 행동을 최대 42개·4,058바이트로 묶는다. 같은 맵 전체 중계와 본인 중계 확인을 유지한다.
- 서버의 대기 시간은 실제 steady_clock 경과를 clientTick 단위로 바꿔 반영한다. 발신자→서버→수신자의 실제 편도 지연은 추정하지 않는다.
- 상대는 200ms 버퍼와 같은 물리로 재현한다. 수신 후 2초를 넘는 추가 예측은 중단한다. 무응답 이후 새 행동이 도착하면 이전 예측 시간선을 버리고 새 기준 tick/상태에서 복구한다.
- 표시 보정은 지수 보간 계수 12, 큰 차이는 2m 초과 즉시 보정이다. Respawn과 무응답 복구도 즉시 보정하며 계산 상태와 표시 상태를 분리한다.
- 클라이언트 행동 대기열 128개, 수신자별 서버 행동 대기열 256개를 유지한다. 초과 시 연결의 오류/종료를 처리한다. 서버 수신 빈도 강제 제한과 주변 대상 전송은 별도 정책이다.
- 채팅/포커스 차단은 중립 입력과 대기 점프 제거, Map 변경/재접속은 지형·로컬 행동·원격 객체 정리를 수행한다. 이전 generation의 snapshot/broadcast는 적용하지 않는다.

## 3D wire format

opcode 1~24의 기존 형식은 유지한다. 새 연결 구분은 기존 EnterGameRequest의 protocolVersion=7이다.

| opcode | payload | 크기 |
|---:|---|---:|
| 25 | World3D: Map/generation/version, XYZ 경계, 몸체/이동 설정, spawn, boxCount | 78 |
| 26 | WorldBox3D: Map/generation/id, XYZ min/max | 40 |
| 27 | WorldEnd3D: Map/generation | 12 |
| 28 | MovementState3D: Map/generation, characterId/latestClientTick, 행동 기준 상태 | 108 |
| 29 | MovementActions3D: 기존 30바이트 묶음 헤더 + 84바이트 행동 N개 | 30 + 84N |
| 30 | MovementActionsBroadcast3D: 기존 22바이트 중계 헤더 + 96바이트 사본 N개 | 22 + 96N |

행동은 sequence/clientTick/jumpId 각 u64, 위치 XYZ와 속도 XYZ 각 i64(1/1000 단위), supportId u32, inputX/inputZ i16(-1000~1000이며 벡터 크기 1000 이하), yaw u16, grounded u8, kind u8이다. 메시지 총 크기에는 별도 4바이트 게임 헤더가 추가된다.

## 계측과 테스트

기존 서버 Map 계측에 3D를 포함했다. 3D 클라이언트의 Movement TX/RX는 opcode 29/30의 완료 프레임 횟수·바이트를 집계한다. TCP pending/peak는 게임 연결 전체다. `display gap`은 표시 위치와 원격 예측 목표 사이의 차이이며, 다른 PC와 동일 시점의 좌표 차이나 네트워크 편도 지연은 아니다.

```powershell
# 서버 저장소
.\Tests\NetworkReliability\bin\NetworkReliability.exe
.\Tests\NetworkReliability\RunMovement3D.ps1
.\Tests\NetworkReliability\RunMovementRelay.ps1
.\Tests\PlatformMovement\TestGeometryData.ps1

# 3D 저장소
dotnet run --project .\Tests\Movement3D\Movement3D.csproj
```

loopback fixture는 production Map 코드를 사용하지만 DB와 인증을 생략한다. 외부 인터페이스에 바인딩하지 않으며 실서버 로그인/DB 통합·다른 PC LAN 테스트를 대체하지 않는다. Unity Player의 `--smoke-test`는 오프라인 자동 입력, `--smoke-network --relay-fixture-port=27780`은 같은 PC의 두 Player 자동 연결 검사 전용이다. 자동 입력 검사를 사람의 키 입력 확인으로 기록하지 않는다.
