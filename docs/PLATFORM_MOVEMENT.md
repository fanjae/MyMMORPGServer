# 발판 데이터 로더와 독립 이동 시뮬레이션

2026-10-05 구현 및 검증 기록입니다. 데이터와 정책의 상세 정의는 [FOOTHOLD_DESIGN.md](FOOTHOLD_DESIGN.md)에 있습니다.

## 구현 범위

- MapGeometry에 맵 정의, 이동 설정, 수평 발판 및 사각형 충돌 데이터를 보관합니다.
- MapGeometryLoader가 세 CSV를 모두 읽고 검증한 뒤 결과를 한 번에 적용합니다. 실패하면 호출자가 가지고 있던 결과는 유지합니다.
- MapManager가 기존 맵 정의와 지형을 연결하고 FindGeometry로 읽기 전용 조회를 제공합니다. 런타임 재로딩은 지원하지 않습니다.
- GameServer가 DB 설정 확인과 포트 개방 전에 지형 파일을 검증합니다.
- MovementSimulation이 20ms 고정 간격으로 좌우 이동, 점프, 중력, 착지, 벽·천장, 경계와 낙하 복귀를 계산합니다.

서버 Map tick과 입력·상태 패킷, Unity 지형 표시 및 예측·보정을 연결했습니다. 시작 맵 100000000은 좌우 방향키·Space 점프를 사용하고 100000001은 기존 자유 이동을 유지합니다. 실제 접속 검증과 조작 방법은 [PLATFORM_NETWORK.md](PLATFORM_NETWORK.md)에 있습니다.

## 데이터 파일 배치

```text
data/
  maps.csv
  map_movement.csv
  footholds.csv
  colliders.csv
```

GameServer 빌드 시 네 파일이 실행 파일 옆의 data 디렉터리로 복사됩니다. 인자 없이 실행하면 실행 파일 기준으로 찾으며 작업 디렉터리에 의존하지 않습니다. maps.csv를 인자로 지정하면 같은 디렉터리의 나머지 세 파일도 필요합니다.

```powershell
.\x64\Release\GameServer.exe .\data\maps.csv
```

CSV는 UTF-8 BOM 없이 저장합니다. 헤더와 열 개수는 정확히 일치해야 하며 공백 행·따옴표·숫자 앞뒤 공백·주석은 지원하지 않습니다. Free와 Platformer는 대소문자를 구분합니다. 발판이나 사각형이 없으면 해당 파일은 헤더만 유지합니다.

모든 Map에 이동 설정이 하나씩 있어야 합니다. Free 모드는 기존 maps.csv의 속도를 사용하며 이동 설정의 horizontalSpeed가 같은 값인지 검사합니다. Free의 spawnFootholdId·점프·중력·낙하 속도는 0이어야 하고 지형 객체를 등록하지 않습니다.

Platformer의 속도·점프·중력·최대 낙하 속도는 1~10000, 반폭·반높이는 1~1000입니다. geometryVersion은 양수입니다. Map마다 발판과 사각형을 각각 최대 4096개까지 허용합니다. 발판 ID와 사각형 ID는 각 Map 안에서 유일하며 서로 다른 종류는 같은 숫자를 사용할 수 있습니다.

입장 위치의 충돌 영역 전체가 경계 안에 있고 벽 내부와 겹치지 않아야 합니다. 지정한 spawnFootholdId의 높이는 spawnY - halfHeight와 같아야 합니다. 공유 끝점의 입장은 가장 작은 발판 ID를 지정합니다. 발판 연결은 양방향 참조와 끝점 일치를 검사합니다. 같은 높이에서 내부 구간이 겹치거나 사각형 내부를 관통하는 발판은 거절합니다.

예시 맵은 발판 5개와 사각형 1개입니다. 발판 1·2는 연결된 바닥, 3·4는 일방향 플랫폼, 5는 사각형 벽의 상단입니다. 서버가 지형을 전송하고 Unity가 발판 선분과 벽을 표시합니다.

## 시뮬레이션 사용 규칙

MovementSimulation은 생성 시 지형을 검증합니다. MapGeometry의 수명은 시뮬레이션보다 길어야 하며 생성 이후 지형을 수정하지 않습니다. Reset으로 입장 상태를 만들고 Step에 PlatformMovementInput을 전달합니다. 각 Step은 실제 경과 시간과 무관하게 20ms 한 번만 갱신합니다.

- horizontal은 -1, 0, 1이며 0으로 바꾸면 수평 이동을 중단합니다.
- jumpHeld는 점프 키의 현재 눌림 상태입니다. 지상에서 새로 눌렀을 때만 점프하고 공중 재점프와 유지 상태의 자동 재점프를 막습니다.
- 중력은 반암시적 Euler 방식으로 적용합니다. 기본값에서 이상적인 최고 높이는 60이며 20ms 계산 결과는 약 57.6입니다.
- 상승 중 일방향 발판을 통과하고 하강 중 교차 시점의 X를 검사해 가장 먼저 만난 발판에 착지합니다.
- 사각형은 캐릭터 반폭·반높이만큼 확장한 뒤 이동 선분을 검사합니다. 접촉 후 남은 변위로 벽을 따라 이동할 수 있습니다.
- 사각형 상단의 지상 상태와 점프에는 상단 발판 정의가 필요합니다.
- 한 Step의 공중 충돌 처리는 최대 8회입니다. 한도에 도달하면 남은 변위를 적용하지 않아 검사하지 않은 경로로 이동하지 않습니다.
- 착지 후 같은 tick의 남은 수평 이동으로 발판을 벗어나면 공중 상태로 바꾸며 다음 tick부터 다시 낙하합니다.
- 좌우·위 경계는 충돌 영역 전체를 제한하고 아래 경계의 낙하는 spawn으로 복귀시킵니다. 복귀 때 점프 키 유지 상태는 보존해 자동 재점프를 막습니다.
- 잘못된 입력, NaN·무한대·벽 내부 등 잘못된 상태는 결과 코드로 거절하고 상태를 변경하지 않습니다.
- Free 지형에는 NotPlatformer를 반환하고 상태를 갱신하지 않습니다.

서버 tick과 요청 번호, 입력 유효 기간, 입장 번호 및 네트워크 전파는 MapManager·Map·Player에서 처리합니다. C++ 시뮬레이션과 C# 예측을 동일 입력 2000 tick으로 비교하는 테스트를 추가했으며 실제 서버 점프 상태 사이의 예측도 검증합니다.

## 빌드 및 테스트 방법

서버 저장소 PowerShell에서 실행합니다. DB나 Unity 없이 지형과 시뮬레이션 테스트를 실행할 수 있습니다.

```powershell
$msbuild = 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe'
$build = Start-Process -FilePath $msbuild -ArgumentList @('MyMMORPGServer.sln', '/p:Configuration=Release', '/p:Platform=x64', '/m:1', '/nr:false', '/v:minimal') -UseNewEnvironment -NoNewWindow -Wait -PassThru
if ($build.ExitCode -ne 0) { throw 'Server build failed.' }

$build = Start-Process -FilePath $msbuild -ArgumentList @('Tests\PlatformMovement\PlatformMovement.vcxproj', '/p:Configuration=Release', '/p:Platform=x64', '/m:1', '/nr:false', '/v:minimal') -UseNewEnvironment -NoNewWindow -Wait -PassThru
if ($build.ExitCode -ne 0) { throw 'Platform movement build failed.' }

.\Tests\PlatformMovement\bin\PlatformMovement.exe
.\Tests\PlatformMovement\TestGeometryData.ps1
```

통과 기준은 PlatformMovement.exe의 PASS 48개·종료 코드 0, TestGeometryData.ps1의 PASS 7개입니다. 전자는 복사된 data 파일로 로더와 물리를 검증하고, 후자는 임시 파일로 실제 GameServer 시작 순서를 검사합니다. 정상 지형의 시작 테스트는 DB 환경 변수가 없다는 진단까지 도달하면 통과합니다. 테스트는 현재 PowerShell의 DB 환경 변수를 저장한 뒤 종료 시 복원합니다.

시뮬레이션 테스트에는 연결 발판 통과, 정지, 점프·중력·착지, 빠른 낙하, 교차 시점 X, 얇은 벽, 천장, 대각선 모서리, 벽을 따라 낙하, 몸체 경계, 낙하 복귀와 잘못된 상태 거절이 포함됩니다. 실패하면 [FAIL] 뒤의 항목명과 원인을 확인합니다.

수정한 CSV 원본을 직접 검사하려면 다음을 실행합니다. 이 테스트의 기준 맵은 제공한 두 Map 정의이므로 Map ID나 maps.csv의 기본 정의를 바꿨다면 테스트의 기준 데이터도 함께 변경해야 합니다.

```powershell
.\Tests\PlatformMovement\bin\PlatformMovement.exe .\data
```

실제 서버 접속 테스트는 [PLATFORM_NETWORK.md](PLATFORM_NETWORK.md)의 RunIntegration.ps1로 실행합니다. 발판 이동은 원본 데이터로 검사하고 기존 자유 이동 회귀는 별도의 임시 Free 데이터로 실행합니다.

## 검증 결과

- 서버 솔루션 및 PlatformMovement 프로젝트 Release x64 빌드 성공, 컴파일 경고·오류 없음
- 지형·이동 시뮬레이션 PASS 48, 서버 시작 지형 검사 PASS 7
- 기존 이동 단위 테스트 PASS 5, 맵 CSV 검사 PASS 10
- 실제 LoginServer/GameServer를 통한 기존 Map-local 자동 통합 테스트 PASS 16
- 기존 C++ TestClient PASS 15
- GameServer를 임시 작업 디렉터리에서 실행해 실행 파일 옆의 네 데이터 파일을 읽는 동작 확인

회귀 테스트는 기존 MySQL 볼륨을 수정하지 않는 localhost 3307의 임시 DB에서 수행했습니다. 이후 이동 입력·상태 패킷과 Unity 런타임 연결까지 구현했으며 해당 검증은 [PLATFORM_NETWORK.md](PLATFORM_NETWORK.md)에 기록합니다.
