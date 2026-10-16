# 계정 가입과 기본 캐릭터 생성

2026-10-16. 서버와 3D 클라이언트에 ID·비밀번호 가입을 추가했다. 2D 클라이언트에는 가입 UI를 추가하지 않았다.

## 사용 방법

1. 3D 클라이언트 로그인 화면에서 서버 주소·포트, ID·비밀번호를 입력한다.
2. `CREATE ACCOUNT`를 누른다. 성공하면 같은 정보로 자동 로그인한다.
3. 목록에 나타난 레벨 1의 `Player<캐릭터 ID>`를 선택해 입장한다. 기본 인벤토리는 기존의 최초 입장 초기화 절차로 생성한다.

ID는 영문·숫자·밑줄 3~31자이며 저장 시 영문을 소문자로 통일한다. 비밀번호는 Unicode 문자 8개 이상, UTF-8 63바이트 이하이고 제어 문자를 허용하지 않는다. 비밀번호의 대소문자와 앞뒤 공백은 그대로 사용한다. 입력 필드는 마스킹하고 가입 실패·로그인 전환 시 보관한 비밀번호와 입력 필드를 정리한다.

중복 ID는 기존 비밀번호를 변경하지 않고 거절한다. 응답 유실·시간 초과는 DB 저장 실패를 뜻하지 않으므로 먼저 `LOGIN`으로 확인하도록 안내한다. 가입만으로 서버 인증을 생략하지 않으며 자동 로그인도 기존 비밀번호 검증을 통과해야 한다.

## 서버 처리

- 계정 INSERT, 캐릭터 INSERT, 기본 이름 확정을 하나의 트랜잭션으로 처리한다. 캐릭터 생성 실패 시 계정도 롤백한다. 동시 중복 가입은 DB의 기존 ID 유일 제약으로 최종 차단한다.
- ID는 DB의 AUTO_INCREMENT로 발급한다. 캐릭터 이름은 발급된 ID를 붙여 `Player<ID>`로 만든다. 현재 가입 단계의 닉네임·직업 선택은 없다.
- 비밀번호는 계정마다 무작위 16바이트 salt를 생성하고 PBKDF2-HMAC-SHA256 600,000회, 32바이트 결과로 저장한다. 기존 로그인 검증 형식과 동일하며 평문 비밀번호를 DB·검사 결과·명령줄에 기록하지 않는다.
- 해시 계산과 가입 DB 작업은 전용 작업 스레드·전용 연결로 처리한다. IOCP는 완료 여부를 확인하며 가입 해시 계산을 직접 수행하지 않는다. 기존 로그인 검증은 기존 경로를 유지한다.
- 대기열은 8개이며 처리 중인 작업은 별도 한 개다. 같은 연결의 가입 시도 간격은 2초다. 작업 기한 8초, 세션 응답 기한 10초, DB 행 잠금 대기는 3초다. 연결 종료·시간 초과는 아직 커밋하지 않은 작업의 취소 신호로 사용하며 이미 커밋한 계정을 삭제하지 않는다. 세션 응답 기한은 진행 중 DB 호출을 강제 중단하는 기능은 아니다.
- Login protocol 2에 요청 7·응답 8을 추가했다. 기존 opcode 1~6과 로그인 버전은 유지한다. 가입 요청은 100바이트이며 고정 문자열의 종료·패딩·문자 범위를 검사한다. 게임 protocol 12는 바꾸지 않았다.

현재 로그인 TCP 전송에는 TLS가 없다. 로컬 테스트 환경에서 검증했으며 외부 서비스 배포 시에는 자격 증명 전송 보호를 별도로 적용해야 한다.

## DB 적용

신규 DB는 `database/init/006_registration.sql`을 포함해 초기화한다. 기존 DB는 서버를 중지하고 스키마를 백업한 뒤 같은 SQL을 한 연결에서 실행한다. 컬럼의 기존 unsigned 타입·값과 외래 키를 유지하면서 ID 자동 발급만 활성화한다. 세션의 외래 키 검사 설정은 기존 값으로 복원한다. MySQL DDL은 일반 데이터 트랜잭션처럼 롤백되지 않으므로 적용 전 백업과 적용 후 확인이 필요하다.

기존 로컬 테스트 DB에도 적용했다. 적용 전후 계정 2개·캐릭터 3개·관련 외래 키 7개가 같고, 두 ID 컬럼의 자동 발급과 외래 키 검사 복원을 확인했다. 원래 DB에 검사용 계정을 추가하지 않았다.

- 스키마 백업: `artifacts/registration-original-schema-2026-10-16.sql`
- 적용 전 상태: `artifacts/registration-original-baseline-2026-10-16.json`
- 적용 결과: `artifacts/registration-original-migration-2026-10-16.json`

## 검증 결과

| 구분 | 결과와 범위 | 기록 |
|---|---|---|
| 서버 빌드 | Release x64 성공 | `artifacts/registration-server-build-2026-10-16.log` |
| 클라이언트 규칙 | 24개 검사 묶음 통과. 가입 ID·Unicode·비밀번호·패킷 경계 포함 | `artifacts/registration-pure-regression-2026-10-16.log` |
| 실제 서버 가입 | 17계정에 기본 캐릭터 한 개씩, 서로 다른 salt, 잘못된 입력·기존 로그인 호환 검사 통과 | `artifacts/registration-integration-2026-10-16.log` |
| 동시 요청 | 같은 ID 8개 요청 중 성공 1개·중복 7개. 서로 다른 ID 16개 동시 요청에서 수용 9개·Busy 7개, 이후 재시도 가능 | 위 서버 가입 기록 |
| 장애 | 커밋 후 가입 성공 응답 유실 시 로그인으로 복구. 캐릭터 생성 실패 시 계정 롤백 후 재가입 성공 | 위 서버 가입 기록 |
| Unity UI | 실제 입력 필드·버튼 콜백, 비밀번호 보존·마스킹·정리, 처리 중/비활성 입력 차단, 중복 UI 생성 방지 통과 | `artifacts/registration-ui-final-2026-10-16.result` |
| Unity 빌드 | Windows Player 빌드 성공 | `artifacts/registration-unity-build-2026-10-16.log` |
| 두 실행 파일 가입 | 실제 LoginServer·GameServer·임시 MySQL에서 가입→자동 로그인→기본 캐릭터 입장·인벤토리·상대 점프/착지·렌더링 통과 | `artifacts/full-3d-805f131dfad54a849a3f5765697c9c7b/integration.result` |
| 기존 경로 회귀 | 기존 계정 두 실행 파일 접속, DB 23개 검사 묶음, 인벤토리 조작·격리·맵 이동·서버 재시작 후 저장 복구 통과 | `artifacts/registration-legacy-regression-2026-10-16.log` |

Unity UI 검사는 자동 콜백 검사이고 실행 파일 검사는 자동 가입 모드다. 실제 사람의 마우스·키보드 조작 검사로 기록하지 않는다. Computer Use는 사용하지 않았다. 검사용 서버·Player·임시 DB는 검사 후 정리했다.

## 변경 파일

서버 가입 구현:

- `Protocol/LoginPacket.h`, `Protocol/RegistrationCredentials.h`
- `LoginServer/RegistrationService.h`, `LoginServer/RegistrationService.cpp`
- `LoginServer/AccountRepository.h`, `LoginServer/AccountRepository.cpp`
- `LoginServer/PasswordVerifier.h`, `LoginServer/PasswordVerifier.cpp`
- `LoginServer/LoginSession.h`, `LoginServer/LoginSession.cpp`
- `LoginServer/LoginPacketHandler.h`, `LoginServer/LoginPacketHandler.cpp`
- `LoginServer/LoginServer.cpp`, `LoginServer/LoginServer.vcxproj`, `LoginServer/LoginServer.vcxproj.filters`
- `database/init/006_registration.sql`

3D 클라이언트 구현:

- `Assets/Scripts/Protocol/LoginProtocol.cs`, `Assets/Scripts/Network/PacketOpcode.cs`
- `Assets/Scripts/Network/NetworkManager3D.cs`
- `Assets/Scripts/Game/Client3DController.cs`, `Assets/Scripts/Game/FieldUI.cs`
- `Assets/Scenes/SampleScene.unity`, `README_3D.md`

검사:

- 서버 `Tests/NetworkReliability/RunHeadlessFieldValidation.ps1`의 Registration 모드, `RunFull3DIntegration.ps1`의 `-Registration` 옵션
- 3D `Tests/Movement3D/RegistrationTests.cs`, `RegistrationIntegration.cs`, `Movement3D.csproj`, `Program.cs`
- 3D `Assets/Editor/RegistrationUiValidation.cs`와 `.meta`

기존 이동·거래 검사와 공통 검사 파일을 공유하므로 파일 전체 커밋 시 선행 변경도 확인한다. 이번 작업에서 커밋·푸시는 실행하지 않았다.
