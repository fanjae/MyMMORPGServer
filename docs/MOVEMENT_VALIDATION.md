# 서버 이동 범위·속도 검증

## 구현 범위

상하좌우 자유 이동을 유지하면서 Map 경계와 이동 속도를 서버에서 검사합니다. 정상 이동과 거절된 이동 모두 요청자에게 확정 좌표를 응답합니다. 같은 Map의 상대 Player에게는 허용된 이동만 전달합니다. 발판, 벽, 캐릭터 크기, 점프·중력·착지는 다음 단계입니다. 현재 경계 검사는 캐릭터의 중심 좌표를 기준으로 합니다.

## 맵 설정

원본 파일은 `data/maps.csv`이며 UTF-8 BOM 없이 저장합니다. 모든 필드는 정수이며 공백, 따옴표, 빈 행과 주석 행은 지원하지 않습니다.

```csv
mapId,spawnX,spawnY,minX,maxX,minY,maxY,moveSpeed,moveBurst
100000000,0,0,-400,400,-200,200,80,12
100000001,100,50,-300,500,-150,250,80,12
```

| 필드 | 의미 |
|---|---|
| mapId | 중복되지 않는 Map ID |
| spawnX, spawnY | 입장 위치, 경계 안에 있어야 함 |
| minX, maxX, minY, maxY | 이동 가능한 중심 좌표 범위, 양 끝 포함 |
| moveSpeed | 초당 이동 가능한 서버 좌표 거리, 1~10000 |
| moveBurst | 누적 이동 허용량 상한, 1~moveSpeed |

좌표는 int32 범위여야 하며 최소 경계는 최대 경계보다 작아야 합니다. 시작 맵 `100000000`은 필수입니다. 잘못된 파일은 DB 연결과 포트 개방 전에 오류를 출력하고 종료합니다. 맵은 시작 시 로딩하며 실행 중 변경은 지원하지 않습니다.

빌드 시 CSV가 `x64/Release/data/maps.csv`로 복사됩니다. 인자 없이 실행하면 실행 파일 옆의 CSV를 읽습니다. 원본을 직접 지정하려면 서버 저장소에서 다음과 같이 실행합니다.

```powershell
.\x64\Release\GameServer.exe .\data\maps.csv
```

MySQL과 `DB_*` 환경 변수 설정은 클라이언트 저장소의 `TEST_CLIENT.md`를 따릅니다.

## 속도 검증

서버의 `steady_clock`으로 경과 시간을 계산합니다. 이동 허용량은 `min(moveBurst, 남은 허용량 + 경과 시간 × moveSpeed)`로 보충하고, 허용된 이동의 실제 직선 거리를 차감합니다. 대각선도 유클리드 거리로 계산합니다. 요청 횟수에 따라 추가 허용량을 부여하지 않습니다.

기본 설정의 최대 누적 허용량은 12단위이며 초당 80단위씩 보충됩니다. 즉 정상적인 50ms 간격 이동은 약 4단위이고, 오래 정지해도 한 번에 12단위를 넘는 이동은 거절됩니다. 이는 현재 테스트 입력에 맞춘 지연 허용 설정이며 실제 게임 물리와 네트워크 환경에 맞춘 추가 조정은 추후 필요합니다.

입장과 성공한 Map 변경 시 허용량을 초기화합니다. 서버 spawn 이동은 일반 `MoveRequest`가 아니므로 속도 제한을 적용하지 않습니다. 범위·속도 거절은 Player 위치를 바꾸지 않고 연결을 유지합니다.

## 프로토콜

기존 opcode는 유지하며 새 opcode `MoveResponse=12`, `MapInfo=13`을 추가했습니다. 모든 숫자는 little-endian, 1바이트 packing이며 아래 길이는 4바이트 헤더를 제외한 payload 길이입니다.

| 패킷 | 필드 순서 | 길이 |
|---|---|---:|
| MoveRequest | uint32 mapId, uint64 sequence, int32 x, int32 y | 20 |
| MoveResponse | uint64 sequence, uint32 mapId, uint8 result, int32 x, int32 y | 21 |
| MapInfo | uint32 mapId, int32 minX/maxX/minY/maxY, uint32 moveSpeed/moveBurst | 28 |

`MoveResult`: Success=0, OutOfBounds=1, SpeedExceeded=2, MapMismatch=3, InvalidSequence=4.

`sequence`는 연결 내에서 1부터 증가시키며 Map 변경 시 초기화하지 않습니다. 서버는 중복되거나 감소한 요청 번호를 거절합니다. `MoveResponse`의 좌표는 성공·실패 모두 서버 현재 좌표입니다. `ChangeMapResponse`도 실패 시 기존 Map ID와 좌표를 전달합니다.

초기 상태 전송 순서는 `EnterGameResponse → MapInfo → PlayerEnterMap → MonsterEnterMap`입니다. Map 변경 성공 시 `ChangeMapResponse → MapInfo → PlayerEnterMap → MonsterEnterMap`입니다. Player나 Monster가 없으면 해당 패킷은 생략합니다.

Unity는 이동 응답을 하나씩 기다리고, 화면 예측과 서버 확정 좌표를 구분합니다. 응답은 현재 Map ID와 대기 중 요청 번호가 일치할 때 적용하며, 거절되면 입력 목표를 서버 좌표로 보정합니다. 정상 입력에 대한 응답은 보간으로 반영합니다. 5초 안에 응답이 없으면 대기를 해제하고 마지막 확정 좌표로 돌아갑니다.

MoveRequest 형식과 초기 수신 순서가 바뀌었으므로 이전 빌드와 호환되지 않습니다. 서버와 C++ TestClient, Unity 클라이언트를 함께 새로 빌드해야 합니다.

## 빌드와 자동 테스트

서버 저장소 PowerShell에서 실행합니다. MSBuild 경로는 설치한 Visual Studio에 맞게 변경할 수 있습니다.

```powershell
$msbuild = 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe'
& $msbuild .\MyMMORPGServer.sln /p:Configuration=Release /p:Platform=x64 /m:1 /nr:false
& $msbuild .\Tests\MovementValidation\MovementValidation.vcxproj /p:Configuration=Release /p:Platform=x64 /m:1 /nr:false
.\Tests\MovementValidation\bin\MovementValidation.exe
.\Tests\MovementValidation\TestMapData.ps1
```

이동 단위 테스트는 PASS 5개, 맵 파일 검사 테스트는 PASS 10개가 기준입니다. 두 서버가 준비된 뒤 Unity 창을 닫고 다음을 실행합니다.

```powershell
.\x64\Release\TestClient.exe
```

C++ TestClient는 PASS 15개와 종료 코드 0이 기준입니다. 클라이언트 저장소에서 다음을 실행합니다.

```powershell
dotnet run --project .\Tests\MapLocalIntegration\MapLocalIntegration.csproj
```

PASS 16개와 종료 코드 0이 기준입니다. `-- --trace`를 덧붙이면 opcode와 payload 길이를 확인할 수 있습니다. 테스트 계정이 이미 접속 중이면 중복 입장으로 실패하므로 두 Unity 창을 먼저 닫습니다.

## Unity 수동 확인

최신 Windows 빌드는 클라이언트 저장소의 `Builds/Windows/MyMMORPGClient.exe`입니다. 두 개를 실행해 `test/test1234`의 Warrior와 `test2/test1234`의 Archer로 입장합니다.

1. 시작 맵에서 양쪽 패널이 X `-400..400`, Y `-200..200`, 속도 80, Burst 12를 표시하는지 확인합니다.
2. Warrior의 `(0,0)`에서 `Move X/Y`를 `(8,4)`로 지정합니다. `Move accepted`와 Local `(8,4)`가 표시되고 Archer 창의 Remote 1001도 `(8,4)`여야 합니다.
3. `(120,45)`로 요청합니다. `SpeedExceeded`가 표시되고 두 창의 Warrior 서버 좌표가 `(8,4)`로 유지되어야 합니다. 예측으로 사각형이 움직였어도 보간 후 서버 위치로 돌아와야 합니다.
4. `(401,4)`로 요청합니다. `OutOfBounds`가 표시되며 연결과 기존 좌표가 유지되어야 합니다. 입력 목표도 초기화되어 추가 요청을 반복하지 않아야 합니다.
5. 빈 게임 영역을 클릭하고 방향키를 각각 1초 누릅니다. 상대 창의 Remote 좌표가 로컬 서버 좌표와 일치하고 다른 Player의 좌표는 유지되어야 합니다. 키를 놓은 뒤 응답과 보간이 끝나면 좌표가 더 이상 변하지 않아야 합니다.
6. Map `100000001`로 변경합니다. `(100,50)`과 X `-300..500`, Y `-150..250`이 표시되어야 합니다. 기존 맵의 상대 Player는 제거되고 새 맵에서 방향키 이동이 바로 가능해야 합니다.
7. 시작 맵으로 돌아와 Player와 Monster가 재생성되는지 확인합니다. `999999999`로 변경 요청이 실패해도 기존 Map ID, 서버 좌표와 객체 수가 유지되어야 합니다.
8. 기존 같은 맵·다른 맵 채팅, 채팅 입력 중 방향키 차단과 Enter 후 이동 재개도 확인합니다.

## 2026-10-04 검증 결과

- 서버 Release x64 및 이동 테스트 프로젝트 빌드 성공
- 이동 단위 테스트 PASS 5, 잘못된 CSV 테스트 PASS 10
- 실제 LoginServer/GameServer를 통한 .NET 통합 테스트 PASS 16, 서버 재시작 전후 두 번 통과
- C++ TestClient PASS 15
- Unity Windows 빌드 성공, 종료 코드 0
- Unity 화면에서 MapInfo, SpeedExceeded/OutOfBounds, 정상 이동 재개와 맵 변경 후 새 경계 확인
- 지속 방향키 입력과 채팅 포커스의 실제 키 홀드 검증은 수동 확인 항목으로 유지

서버 테스트는 기존 DB 볼륨을 수정하지 않고 3307 포트의 임시 MySQL에서 수행했습니다. 테스트를 위해 실행한 프로세스와 임시 컨테이너는 검증 후 종료합니다.
