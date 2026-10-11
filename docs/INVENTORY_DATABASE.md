# 인벤토리 MySQL 저장

2026-10-11 파일 저장 구현을 MySQL 저장으로 교체했다. 클라이언트 UI와 protocol 8 패킷 형식은 동일하므로 이번 DB 변경만을 위해 Unity를 다시 빌드할 필요는 없다.

## 테이블

| 테이블 | 역할 | 기본 키 |
|---|---|---|
| item_definitions | 샘플 아이템 ID·분류·최대 수량 | id |
| character_inventories | 캐릭터별 인벤토리 revision·메소·HP/MP | character_id |
| character_inventory_slots | 비어 있지 않은 슬롯의 아이템·수량 | character_id, category, slot |

슬롯은 5개 분류 각각 24개다. 빈 슬롯은 행을 저장하지 않는다. 캐릭터·인벤토리·아이템 분류를 외래 키로 연결하며 슬롯 범위와 양수 수량을 CHECK로 제한한다. 서버에서도 지원 아이템과 최대 수량을 검증한다. 아이템 이름·아이콘·지원 기능은 현재 클라이언트 카탈로그와 서버 규칙에 정의되어 있으므로 아이템 종류 추가 시 함께 갱신한다.

## 기존 DB에 적용

스키마는 [002_inventory.sql](../database/init/002_inventory.sql)에 있다. 새 Docker 볼륨은 `001_init.sql` 다음에 자동으로 적용한다. 이미 데이터가 들어 있는 기존 볼륨은 새 초기화 SQL을 자동 실행하지 않는다.

기존 Compose의 MySQL 서비스가 사용하는 DB에 다음 SQL만 한 번 적용한다. accounts/characters 초기화나 볼륨 삭제는 하지 않는다.

```powershell
Get-Content -Raw .\database\init\002_inventory.sql |
    docker compose exec -T mysql sh -c 'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysql -uroot "$MYSQL_DATABASE"'
```

이 SQL은 새 테이블 생성과 샘플 아이템 정의 추가만 수행한다. 인벤토리는 인증된 캐릭터의 첫 조회 때 샘플 데이터로 생성한다. 이전 파일 저장 데이터는 자동으로 가져오지 않으며 파일도 삭제하지 않는다. 보존할 기존 캐릭터 파일이 있다면 별도 이관이 필요하다. 현재 검증은 기존 Compose 데이터가 아닌 임시 DB에서 수행했다.

## 처리와 일관성

GameSession은 인증된 계정·캐릭터 ID와 요청을 기존 CharacterRepository 작업 큐에 전달한다. InventoryRepository는 DB 작업 스레드에서 계정 소유권을 확인하면서 캐릭터 행을 잠근 뒤 인벤토리와 슬롯을 읽는다. 현재 revision과 요청 revision이 다르면 최신 상태를 반환하고 변경하지 않는다.

현재 규칙 계산은 DB와 분리된 InventoryService에서 수행한다. 유효한 변경만 슬롯·메소·HP/MP·revision과 함께 하나의 트랜잭션으로 저장한다. 갱신되는 슬롯은 현재 캐릭터의 슬롯 목록을 교체하는 기본 구현이다. SQL 실패·대기 작업 취소·제한 시간 초과는 커밋 전에 롤백한다. 커밋된 결과만 IOCP 실행 스레드에서 응답한다. 응답 유실은 다음 조회와 revision 검사로 처리한다.

같은 MySQL Connection을 캐릭터 조회와 인벤토리가 공유하므로 동시에 접근하지 않는다. 진행 중 인벤토리 요청은 연결당 1개, 최소 요청 간격은 100ms, DB 대기열은 64개로 제한한다. DB 조회나 행 잠금 대기 중에도 Map tick은 진행한다. 큐가 가득 차면 RateLimited, DB 실패나 제한 시간 초과는 StorageError로 응답한다.

DB에 잘못된 아이템·수량이 발견되면 오류를 반환하고 기존 상태를 샘플 데이터로 덮어쓰지 않는다. DB 권한이 부족하거나 스키마가 없으면 파일 저장으로 돌아가지 않는다.

## 검증

서버 솔루션, `Tests/InventoryDatabase/InventoryDatabase.vcxproj`, `Tests/NetworkReliability/NetworkReliability.vcxproj`를 Release x64로 빌드한다. InventoryDatabase.exe는 기존 MySQL Connector DLL이 있는 `x64/Release`에 생성된다.

```powershell
.\Tests\NetworkReliability\RunFieldFeatures.ps1
.\Tests\NetworkReliability\RunFull3DIntegration.ps1
```

첫 스크립트는 DB 없는 테스트 저장소로 인벤토리 규칙과 몬스터 공유를 확인한다. 두 번째는 기존 데이터·볼륨과 분리한 임시 MySQL에서 다음 6개 검사 묶음과 실제 LoginServer/GameServer·Unity 두 개의 연결을 확인한다.

1. 계정 소유권 검증과 인벤토리 최초 생성
2. 병합·소비·오래된 요청 거절·캐릭터 격리·Connection 재생성 후 유지
3. 두 Connection이 같은 revision으로 소비할 때 한 번만 커밋
4. 슬롯 저장 CHECK 오류 시 슬롯·revision·MP 모두 롤백
5. 최대 수량을 초과한 DB 상태를 덮어쓰지 않고 거절
6. DB 행 잠금 중 제출 함수가 대기하지 않으며 취소된 대기 작업은 커밋하지 않음

DB 오류 로그 두 줄은 의도적으로 실패를 발생시키는 검사에서 나온다. 검사는 PASS 결과와 종료 코드 0으로 판단한다. 실제 Unity Player는 DB에 저장한 revision을 수신하며 서버 실행 디렉터리에 인벤토리 파일이 생기지 않는 것도 확인한다.
