# 2026-10-15 커밋 준비

실제 커밋·푸시는 수행하지 않았다. 아래는 저장소별 파일 단위 묶음이다. 시작 전에 있던 .gitignore, tools/, Unity 설정·패키지 변경은 포함하지 않는다. 생성된 빌드·로그·검사 DB 자료도 제외한다.

## 1. 서버 기능

feat : 플레이어 거래의 원자적 교환과 이동 RTT 측정 구현

- 같은 채널·맵의 1:1 거래 요청·수락·제안·최종 확인·취소·만료 처리
- 제안 변경 시 양측 확인 초기화와 거래 중 전투·이동·인벤토리 변경 제한
- 양측 아이템·메소·장비 소유권·거래 기록의 DB 트랜잭션 저장
- 같은 거래 번호 재시도의 중복 교환 차단과 재접속·서버 재시작 후 결과 조회
- 커밋 여부가 불명확한 거래의 재시도와 연결 해제 이후 DB 작업 유지
- RTT 측정 패킷 추가와 3D 프로토콜 version 12 적용

포함 파일:

- Protocol/GamePacket.h
- Protocol/TradePacket.h
- GameServer/TradeManager.h
- GameServer/TradeManager.cpp
- GameServer/TradeRules.h
- GameServer/TradeRepository.cpp
- GameServer/InventoryRepository.h
- GameServer/CharacterRepository.h
- GameServer/CharacterRepository.cpp
- GameServer/GameSession.h
- GameServer/GameSession.cpp
- GameServer/GamePacketHandler.cpp
- GameServer/GameServer.cpp
- GameServer/GameServer.vcxproj
- database/init/005_trade.sql

## 2. 3D 클라이언트 기능

feat : 플레이어 거래 UI와 핑·지터 기반 이동 표시 구현

- RTT·도착 지터에 따라 상대 이동 표시 버퍼를 200~400ms로 조절
- 체크포인트 이후 추가 예측 상한과 버퍼의 점진적 복귀 적용
- 아이템·메소 등록, 양측 확인, 취소·결과 조회 UI 구현
- 미확정 거래 번호 저장과 재접속 시 결과 조회
- 거래 중 인벤토리 변경 제한과 RTT·표시 버퍼 표시
- Windows 키 코드·문자 이벤트의 중복 버튼 실행 방지
- 거래·이동·입력 진단 기록과 3D 프로토콜 version 12 적용

포함 파일:

- Assets/Scripts/Game/WindowButtonGate3D.cs 및 .meta
- Assets/Scripts/Game/NetworkTiming3D.cs 및 .meta
- Assets/Scripts/Game/TradeWindow3D.cs 및 .meta
- Assets/Scripts/Protocol/TradeProtocol.cs 및 .meta
- Assets/Scripts/Game/Client3DController.cs
- Assets/Scripts/Game/InventoryWindow.cs
- Assets/Scripts/Game/Player3DView.cs
- Assets/Scripts/Game/RemoteMotion3D.cs
- Assets/Scripts/Game/InputProbe3D.cs
- Assets/Scripts/Network/NetworkManager3D.cs
- Assets/Scripts/Network/PacketOpcode.cs
- Assets/Scripts/Protocol/GameProtocol.cs

공통 제어기·네트워크 파일이 거래·이동 표시·입력 게이트를 함께 참조하므로 이 묶음에서 파일 내부 변경을 임의로 다시 쪼개지 않는다. 새 타입·참조·프로토콜 버전을 같은 묶음에 둔다. 서버 1번과 클라이언트 2번은 함께 배포해야 접속할 수 있다. 각각의 소스 빌드와 실행 호환성은 별개의 조건이다.

## 3. 서버 검사와 검토 문서

test : 거래 DB·필드 부하 검사와 후속 확장 검토 추가

- 원자적 교환·장비 소유권·롤백·커밋 직후 응답 유실·동시 재시도 검사
- 실제 서버 64명·120초·위치 분산·느린 수신자 측정 지원
- 실제 창 입력·자동 검사 결과와 미구현 설계 범위 구분
- 관심 범위 이벤트·장비·드롭·미확정 피격 종료 정책 정리

포함 파일:

- Tests/InventoryDatabase/InventoryDatabase.cpp
- Tests/InventoryDatabase/InventoryDatabase.vcxproj
- Tests/InventoryDatabase/TradeDatabaseTests.cpp
- Tests/NetworkReliability/RunTradeDatabase.ps1
- Tests/NetworkReliability/MovementRelayFixture.cpp
- Tests/NetworkReliability/FieldValidationSession.ps1
- Tests/NetworkReliability/RunFieldLoadExisting.ps1
- Tests/NetworkReliability/RunFull3DIntegration.ps1
- docs/WORK_PLAN_2026-10-15.md
- docs/FIELD_NEXT_DESIGN_2026-10-15.md
- docs/FIELD_TRADE_TIMING_VALIDATION_2026-10-15.md
- docs/FIELD_VALIDATION_2026-10-14.md의 후속 결과 링크
- docs/COMMIT_PLAN_2026-10-15.md

## 4. 3D 클라이언트 검사와 사용 설명

test : 적응형 이동·거래·키 반복 회귀 검사 보완

- 실제 서버 거래·재시작 후 결과 조회·지연 RTT 검사 추가
- 느린 수신자 부하 프록시와 64명 장기 측정 지원
- 고정 규칙·패킷·혼합 키 이벤트 회귀 검사와 실행 설명 갱신

포함 파일:

- Tests/Movement3D/AdaptiveTimingTests.cs
- Tests/Movement3D/TradeTests.cs
- Tests/Movement3D/TradeIntegration.cs
- Tests/Movement3D/TimingIntegration.cs
- Tests/Movement3D/WindowButtonTests.cs
- Tests/Movement3D/SlowPeerProxy.cs
- Tests/Movement3D/FieldLoadIntegration.cs
- Tests/Movement3D/FieldServerIntegration.cs
- Tests/Movement3D/CombatTests.cs
- Tests/Movement3D/Movement3D.csproj
- Tests/Movement3D/Program.cs
- README_3D.md

## 중간 상태의 빌드 기준

서버 기능 묶음은 솔루션 Release x64, 클라이언트 기능 묶음은 Unity Windows 빌드가 성공했다. 두 빌드는 뒤쪽 Tests·docs 변경을 컴파일 입력으로 사용하지 않으므로 기능 파일의 빌드 검증에 해당한다. 검사 묶음은 앞의 기능 묶음 이후에 추가하고 C++ 검사·MySQL·C#·지연 TCP 검사를 통과했다. 공통 파일은 기능 묶음 안에서 통째로 올려 새 타입 누락을 피한다. 실제 중간 커밋을 만들거나 인덱스 일부로 체크아웃한 빌드는 수행하지 않았다.
