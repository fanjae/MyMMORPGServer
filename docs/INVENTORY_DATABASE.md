# 인벤토리 MySQL 저장

2026-10-11 파일 저장 구현을 MySQL 저장으로 교체했다. 클라이언트 UI와 protocol 8 패킷 형식은 동일하므로 이번 DB 변경만을 위해 Unity를 다시 빌드할 필요는 없다.

현재 protocol 11에는 장비 인스턴스·착용 정보와 요청 식별자가 포함된다. 기존 002 적용 DB에도 [003_field_combat.sql](../database/init/003_field_combat.sql)을 추가 적용하고 서버·3D 클라이언트를 함께 빌드한다. 003은 기존 장비 슬롯의 인스턴스를 생성하며 장비 착용·드롭 획득 기록 테이블을 추가한다. [004_field_recovery.sql](../database/init/004_field_recovery.sql)은 피격 처리 번호를 저장하는 테이블을 추가한다. 테스트는 임시 DB에서 수행했으며 기존 사용자 DB에는 자동 적용하지 않았다.

## 테이블

| 테이블 | 역할 | 기본 키 |
|---|---|---|
| item_definitions | 샘플 아이템 ID·분류·최대 수량 | id |
| character_inventories | 캐릭터별 인벤토리 revision·메소·HP/MP | character_id |
| character_inventory_slots | 비어 있지 않은 슬롯의 아이템·수량·장비 인스턴스 | character_id, category, slot |
| character_item_instances | 캐릭터 소유 장비의 고유 번호 | instance_id |
| character_equipment | 무기·모자 착용 위치 | character_id, slot |
| field_drop_claims | 한 번만 확정하는 드롭 지급 기록 | drop_id |
| field_combat_mutations | 응답 유실 후 피격 재시도에서 중복 적용을 막는 기록 | mutation_id |

슬롯은 5개 분류 각각 24개다. 빈 슬롯은 행을 저장하지 않는다. 캐릭터·인벤토리·아이템 분류를 외래 키로 연결하며 슬롯 범위와 양수 수량을 CHECK로 제한한다. 서버에서도 지원 아이템과 최대 수량을 검증한다. 아이템 이름·아이콘·지원 기능은 현재 클라이언트 카탈로그와 서버 규칙에 정의되어 있으므로 아이템 종류 추가 시 함께 갱신한다.

## 기존 DB에 적용

스키마는 [002_inventory.sql](../database/init/002_inventory.sql)에 있다. 새 Docker 볼륨은 `001_init.sql` 다음에 자동으로 적용한다. 이미 데이터가 들어 있는 기존 볼륨은 새 초기화 SQL을 자동 실행하지 않는다.

기존 Compose의 MySQL 서비스가 사용하는 DB에 다음 SQL만 한 번 적용한다. accounts/characters 초기화나 볼륨 삭제는 하지 않는다.

```powershell
foreach ($migrationPath in @('.\database\init\002_inventory.sql', '.\database\init\003_field_combat.sql', '.\database\init\004_field_recovery.sql'))
{
    Get-Content -Raw -LiteralPath $migrationPath |
        docker compose exec -T mysql sh -c 'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysql -uroot "$MYSQL_DATABASE"'
    if ($LASTEXITCODE -ne 0) { throw "DB migration failed: $migrationPath" }
}
```

게임 서버를 종료하고 기존 DB 백업을 보관한 뒤 적용한다. 002는 인벤토리 테이블과 샘플 아이템 정의, 003은 기존 장비의 인스턴스 번호 이관과 착용·획득 기록, 004는 피격 재시도 기록을 추가한다. 인벤토리 데이터가 있는 임시 DB에서 003/004 재적용도 확인한다. 인벤토리는 인증된 캐릭터의 첫 조회 때 샘플 데이터로 생성한다. 이전 파일 저장 데이터는 자동으로 가져오지 않으며 파일도 삭제하지 않는다. 보존할 기존 캐릭터 파일이 있다면 별도 이관이 필요하다. 현재 검증은 기존 Compose 데이터가 아닌 임시 DB에서 수행했다.

## 처리와 일관성

GameSession은 인증된 계정·캐릭터 ID와 요청을 기존 CharacterRepository 작업 큐에 전달한다. InventoryRepository는 DB 작업 스레드에서 계정 소유권을 확인하면서 캐릭터 행을 잠근 뒤 인벤토리와 슬롯을 읽는다. 현재 revision과 요청 revision이 다르면 최신 상태를 반환하고 변경하지 않는다.

현재 규칙 계산은 DB와 분리된 InventoryService에서 수행한다. 유효한 변경만 슬롯·메소·HP/MP·revision과 함께 하나의 트랜잭션으로 저장한다. 2026-10-12부터 원본과 비교한 변경 슬롯만 INSERT/UPDATE/DELETE한다. 물약 사용은 슬롯 한 행, 병합은 두 행만 변경하며 이미 정렬된 상태는 revision과 DB 쓰기를 유지한다. SQL 실패·대기 작업 취소·제한 시간 초과는 커밋 전에 롤백한다. 커밋된 결과만 IOCP 실행 스레드에서 응답한다. 응답 유실은 다음 조회와 revision 검사로 처리한다.

같은 MySQL Connection을 캐릭터 조회와 인벤토리가 공유하므로 동시에 접근하지 않는다. 진행 중 인벤토리 요청은 연결당 1개, 최소 요청 간격은 100ms, DB 대기열은 64개로 제한한다. DB 조회나 행 잠금 대기 중에도 Map tick은 진행한다. 큐가 가득 차면 RateLimited, DB 실패나 제한 시간 초과는 StorageError로 응답한다.

DB에 잘못된 아이템·수량이 발견되면 오류를 반환하고 기존 상태를 샘플 데이터로 덮어쓰지 않는다. DB 권한이 부족하거나 스키마가 없으면 파일 저장으로 돌아가지 않는다. 연결 오류가 발생하면 해당 변경 요청을 재실행하지 않고, 같은 DB 작업 스레드의 다음 인벤토리 조회 또는 캐릭터 조회에서 연결을 복구한다. 재연결 실패 표시는 다음 시도까지 유지한다.

## 검증

서버 솔루션, `Tests/InventoryDatabase/InventoryDatabase.vcxproj`, `Tests/NetworkReliability/NetworkReliability.vcxproj`를 Release x64로 빌드한다. InventoryDatabase.exe는 기존 MySQL Connector DLL이 있는 `x64/Release`에 생성된다.

```powershell
.\Tests\NetworkReliability\RunFieldFeatures.ps1
.\Tests\NetworkReliability\RunFull3DIntegration.ps1
```

첫 스크립트는 DB 없는 테스트 저장소로 인벤토리 규칙과 몬스터 공유를 확인한다. 두 번째는 기존 데이터·볼륨과 분리한 임시 MySQL에서 다음 9개 검사 묶음과 실제 LoginServer/GameServer·Unity 두 개의 연결을 확인한다.

1. 계정 소유권 검증과 인벤토리 최초 생성
2. 병합·소비·오래된 요청 거절·캐릭터 격리·Connection 재생성 후 유지
3. 두 Connection이 같은 revision으로 소비할 때 한 번만 커밋
4. 슬롯 저장 CHECK 오류 시 슬롯·revision·MP 모두 롤백
5. 최대 수량을 초과한 DB 상태를 덮어쓰지 않고 거절
6. DB 행 잠금 중 제출 함수가 대기하지 않으며 취소된 대기 작업은 커밋하지 않음

추가 검사로 DB 연결 강제 종료 뒤 조회 복구, 변경 슬롯 쓰기 횟수, 공유 작업 큐의 캐릭터 조회 복구를 확인한다. SQL 제약 오류·잘못된 상태·연결 종료 로그는 의도적인 실패 검사에서 나온다. 검사는 PASS 결과와 종료 코드 0으로 판단한다. 실제 Unity Player는 DB에 저장한 revision을 수신하며 서버 실행 디렉터리에 인벤토리 파일이 생기지 않는 것도 확인한다.

## 10월 13일 전투·획득 복구

피격 처리 번호와 HP 변경은 함께 커밋한다. 같은 피격 번호의 재시도는 저장 상태만 반환한다. DB 장애 중 신규 전투를 중단하고 성공 후 재개한다. 획득 결과 조회는 인증된 캐릭터의 소유 여부와 현재 인벤토리를 읽으며 지급을 다시 실행하지 않는다. 두 기록 테이블은 30일 보관하며 작업 시 최대 5분마다 각 500행까지 오래된 기록을 정리한다.

적용 후 character_item_instances, character_equipment, field_drop_claims, field_combat_mutations 및 character_inventory_slots.instance_id를 확인하고 서버와 3D 클라이언트를 protocol 11로 함께 빌드한다. 상세 정책·제한 사항은 [FIELD_EXPANSION_2026-10-13.md](FIELD_EXPANSION_2026-10-13.md)를 참고한다.
