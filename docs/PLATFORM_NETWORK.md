# 발판 이동의 서버·Unity 연결

2026-10-05 구현 기록입니다. 지형 CSV와 물리 규칙은 [PLATFORM_MOVEMENT.md](PLATFORM_MOVEMENT.md)를 참고합니다.

## 구현과 제한

- 100000000은 Platformer, 100000001은 Free 맵입니다. Platformer에서 좌우 방향키와 Space를 사용하고 절대 좌표 요청은 서버가 WrongMovementMode로 거절합니다. Free는 기존 상하좌우 이동·경계·속도 검증을 유지합니다.
- MapManager는 IOCP를 다음 tick까지 대기시키고 20ms 간격으로 갱신합니다. 입력 처리 전에 경과 tick을 이전 입력으로 진행합니다. 지연 시 최대 5 tick만 따라잡고 초과를 로그에 기록합니다. 인증 티켓 정리는 별도 1초 주기입니다.
- 클라이언트는 입력 변경 시와 50ms마다 입력을 전송합니다. 키 해제·창 비활성·채팅 포커스 시 중립 입력으로 전환합니다. 서버는 마지막 유효 입력 후 250ms가 지나면 수평 입력을 제거하고 중력은 계속 적용합니다.
- Player의 입장 번호는 맵 변경마다 증가합니다. Map ID·입장 번호·단조 증가 요청 번호·방향·점프 비트를 검사하며 잘못된 입력은 위치 응답으로 거절하고 연결을 유지합니다. tick 사이의 짧은 점프 눌림·해제는 한 번의 점프 입력으로 보존합니다.
- 서버 상태는 같은 맵의 본인과 상대에게 3 tick(60ms)마다 전송합니다. 점프·착지·낙하 복귀·입력 만료는 즉시 전송합니다. 응답의 sequence는 마지막 시뮬레이션에 적용한 입력 번호입니다.
- Unity PlatformSimulation은 서버와 같은 20ms 계산 규칙으로 로컬 위치를 예측합니다. 서버 상태를 수신하면 그 상태에서 다시 예측하며 화면 오차는 PlayerView에서 보간합니다. 상대는 서버 snapshot 사이를 짧게 보간합니다. 큰 오차·낙하 복귀는 즉시 위치를 맞춥니다.
- 아직 미확인 입력을 저장해 다시 실행하는 완전한 reconciliation은 없습니다. 지연이 커지면 로컬 위치 보정이 눈에 띌 수 있습니다. 경사 발판·사다리·포탈·드롭 점프는 이번 범위에 포함하지 않습니다.
- 서버 지형을 녹색 발판 선분과 회색 벽으로 표시하고 서버의 캐릭터 반폭·반높이를 표시 크기에 적용합니다. Character ID별 색상과 로컬 카메라 추적은 유지합니다.

## wire format

모든 패킷은 little-endian, 1바이트 packing이며 header는 전체 size uint16 + opcode uint16입니다. Game protocol version은 2입니다. EnterGameRequest는 authKey uint64 뒤에 protocolVersion uint32를 추가합니다. 이전 8바이트 요청이나 다른 버전은 ProtocolMismatch(6) 응답을 받으며 인증 티켓을 소비하지 않습니다. 서버와 클라이언트를 함께 갱신해야 합니다.

| opcode | 패킷 | payload byte | 필드 순서 |
|---|---|---:|---|
| 14 | MovementInput | 22 | mapId u32, generation u64, sequence u64, horizontal i8, jumpHeld u8 |
| 15 | MovementState | 70 | mapId u32, generation u64, characterId u32, serverTick u64, sequence u64, x/y/vx/vy i64 각 4개, footholdId u32, grounded u8, reason u8 |
| 16 | MapGeometry | 57 | mapId u32, generation u64, version u32, mode u8, halfWidth/halfHeight/horizontalSpeed/jumpSpeed/gravity/maxFallSpeed u32 각 6개, spawnX/spawnY i32, spawnFootholdId u32, footholdCount/colliderCount u16 |
| 17 | Foothold | 40 | mapId u32, generation u64, id u32, x1/y1/x2/y2 i32, prevId/nextId u32 |
| 18 | Collider | 32 | mapId u32, generation u64, id u32, minX/minY/maxX/maxY i32 |
| 19 | GeometryEnd | 12 | mapId u32, generation u64 |

MovementState의 좌표·속도는 서버 단위의 1/1000을 int64에 저장합니다. 서버는 llround를 사용하고 C#은 정수 좌표 표시 시 AwayFromZero를 사용합니다. generation은 상태를 받는 Player의 현재 입장 번호입니다. 서버 tick이 이전이면 적용하지 않습니다. reason은 Normal=0, Respawned=1, InputRejected=2, InputExpired=3입니다.

입장/맵 변경 성공 응답 → MapInfo → MapGeometry → 개별 Foothold/Collider → GeometryEnd → 로컬 MovementState(Platformer) → 기존 Player·Monster 순서입니다. PlayerEnterMap 뒤에는 해당 상대의 MovementState도 전송합니다. 지형은 각 종류 최대 4096개이며 개별 패킷으로 전송하므로 ServerCore와 TcpSession의 4096바이트 제한 안에 들어갑니다. 완료 시 개수·중복·입장 번호를 검사하고 Unity 메인 스레드에서만 객체를 생성합니다. 지형이 완료되기 전에는 입력을 전송하지 않습니다.

## 자동 테스트 실행

먼저 서버 솔루션과 Tests/PlatformMovement 프로젝트를 Release x64로 빌드합니다. 빌드 명령은 [PLATFORM_MOVEMENT.md](PLATFORM_MOVEMENT.md)에 있습니다. test/test1234, test2/test1234와 캐릭터 1001·2001이 들어 있는 테스트 DB가 필요합니다. 기존 DB를 건드리지 않는 임시 MySQL 준비 방법은 클라이언트 TEST_CLIENT.md를 참고합니다.

Unity 창과 기존 테스트 서버를 닫고 서버 저장소 PowerShell에서 테스트 DB의 환경 변수를 설정합니다. 아래 PASSWORD 값은 실제 개발용 MYSQL_PASSWORD로 직접 바꿉니다.

```powershell
$env:DB_HOST = 'tcp://127.0.0.1:3307'
$env:DB_USER = 'mymmorpg'
$env:DB_PASSWORD = '개발용 MYSQL_PASSWORD'
$env:DB_NAME = 'mymmorpg'
.\Tests\PlatformMovement\RunIntegration.ps1
```

DB_USER·DB_NAME도 .env의 MYSQL_USER·MYSQL_DATABASE와 다르면 해당 값으로 바꿉니다. 스크립트는 .NET 테스트를 빌드하고 원본 지형에서 발판 테스트를 실행한 다음 서버를 재시작해 임시 Free 설정에서 기존 회귀를 실행합니다. 원본 CSV는 수정하지 않고 자신이 실행한 서버만 종료합니다. 포트가 사용 중이면 시작 전에 중단합니다. 실패 로그는 출력된 임시 디렉터리에 보존합니다.

통과 기준은 다음과 같습니다.

- C++ 지형·물리 PASS 48
- 발판 프로토콜·실서버 PASS 14(2,000 tick C++/C# 비교 포함)
- 임시 Free 설정의 기존 C++ TestClient PASS 15
- 임시 Free 설정의 기존 C# Map-local PASS 16
- 스크립트가 예외 없이 종료

서버를 직접 준비한 경우 클라이언트 저장소에서 `dotnet run --project .\Tests\MapLocalIntegration\MapLocalIntegration.csproj -- --platform`을 실행할 수 있습니다. trace 파일 없이 실행하면 2,000 tick 비교가 제외되어 PASS 13입니다. 기존 기본 실행은 두 맵이 Free인 임시 설정에만 사용하며 기본 발판 맵에서는 설정 안내와 함께 실패합니다.

## Unity 두 창 수동 테스트

최신 실행 파일은 MyMMORPGClient/Builds/Windows/MyMMORPGClient.exe입니다. Desktop의 이전 실행 파일 대신 이 파일을 두 번 실행합니다. 창모드는 960×540입니다. 동일 DB 설정으로 최신 GameServer와 LoginServer를 실행합니다.

1. A는 test/test1234 → Warrior(1001), B는 test2/test1234 → Archer(2001)로 입장합니다. 양쪽 Map 100000000, Remote players 1, Monsters 1, 동일 ID의 동일 색상, 녹색 발판을 확인합니다. 같은 spawn에서는 두 사각형이 겹칠 수 있습니다.
2. A의 빈 게임 영역을 클릭하고 오른쪽을 1초 누른 뒤 놓습니다. A만 이동하고 B 화면의 Remote 1001이 같은 좌표로 갱신되어야 합니다. B의 Local은 유지되어야 합니다. 2초 뒤 A Local과 B Remote 1001이 같고 멈춰 있어야 합니다.
3. A에서 Space를 짧게 눌렀다가 놓습니다. 본인과 상대 화면에서 점프·중력·착지가 보여야 합니다. Space를 계속 눌러도 착지 후 자동 재점프하면 안 됩니다. 왼쪽 이동과 점프를 함께 하면 낮은 플랫폼(서버 X -140..-20, 발 높이 34)에 올라갈 수 있습니다.
4. 오른쪽 이동을 계속하면 벽 앞 중심 X=214에서 멈춰야 합니다. 위/아래 방향키는 발판 맵에서 자유 이동을 만들지 않습니다. 절대 좌표 Send는 비활성입니다.
5. A의 Map Chat에 포커스를 두고 방향키·Space를 눌러도 이동하면 안 됩니다. 메시지를 입력하고 Enter로 전송한 뒤 게임 입력을 다시 확인합니다. B를 비활성화해도 A의 이동·점프 상태를 계속 받아야 합니다.
6. A를 Map 100000001로 이동합니다. A의 기존 발판·벽·remote·monster가 제거되고 B에서 1001이 제거되어야 합니다. A는 상하좌우 자유 이동과 좌표 Send를 사용할 수 있습니다. A 채팅은 B에게 전달되면 안 됩니다.
7. A를 100000000으로 되돌립니다. 발판·벽, 상대와 monster가 중복 없이 생성되고 좌표 (0,0)에서 다시 점프할 수 있어야 합니다. 999999999 요청은 MapNotFound이고 기존 지형·객체·입장 상태를 유지해야 합니다.
8. A를 종료하면 B의 Remote players가 0으로 바뀌어야 합니다. 새 로그인으로 재접속할 수 있어야 합니다.

빌드 중 batchmode의 headless 라이선스 오류 198이 발생하면 이번 검증처럼 -batchmode와 -nographics를 제외한 일반 Editor 모드로 -projectPath, -buildWindows64Player, -quit를 사용합니다. 일반 Editor 빌드도 실패하면 Unity Hub의 해당 Editor 라이선스와 Editor Console을 확인합니다.

## 검증 기록

서버 Release x64와 Unity 6000.3.7f1 Windows 빌드를 완료했습니다. 임시 MySQL 3307에서 발판 PASS 14, 기존 Free C++ PASS 15·C# PASS 16을 확인했습니다. 독립 물리 PASS 48, 이동 예산 PASS 5, 맵 CSV PASS 10, 지형 시작 검사 PASS 7도 통과했습니다. Unity 화면의 지속 키 입력과 보간의 체감 품질은 위 수동 절차로 별도 확인합니다.

다음 작업은 지연 환경에서 입력 재실행을 포함한 위치 보정 검증, 채팅 빈도 제한 및 연결 종료·재접속 보강입니다. 맵 콘텐츠는 현재 수평 테스트 발판에 한정되므로 경사·사다리 등은 별도 데이터와 이동 규칙을 정한 뒤 추가합니다.
