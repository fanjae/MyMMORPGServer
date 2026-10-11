# 3D 입력·인벤토리·공유 몬스터 — 2026-10-11

3D 연결은 protocol 8로 갱신했다. 기존 2D version 6, Login version 2와 기존 opcode 1~30의 wire layout은 유지한다. 이전 3D version 7 실행 파일은 새 서버에 입장할 수 없으므로 함께 갱신한다.

## 추가 패킷

| opcode | payload | payload 바이트 |
|---|---|---:|
| 31 | InventoryRequest: operation/category/source/target 각 u8, revision u64 | 12 |
| 32 | InventorySnapshot: revision/mesos u64, HP/maxHP/MP/maxMP u16, result/slots u8, 120개 itemId u32 + quantity u16 | 746 |
| 33 | FieldMonsters3D: mapId u32, generation/tick u64, count u16, count개 monsterId u32 + kind u8 + XYZ i32 + yaw u16 | 22 + 19N |

별도 4바이트 게임 헤더를 사용한다. 몬스터 XYZ는 서버 단위 * 1000, yaw는 1/100도다. 현재 맵당 3마리로 몬스터 메시지 총 크기는 83바이트다.

## 권한과 저장

- 인벤토리 요청은 인증된 3D 세션만 처리하며 소유자는 세션의 캐릭터 ID에서 결정한다.
- 슬롯 범위·탭·아이템별 최대 수량·현재 revision을 검증한다. 오래된 요청은 상태를 바꾸지 않고 최신 snapshot을 응답한다.
- 조회와 변경은 연결별 최소 100ms 간격, 진행 중 요청 1개로 제한한다. 초기 조회와 맵 이동에는 같은 캐릭터의 DB 상태를 사용한다.
- 현재 저장소는 MySQL이다. `character_inventories`에 revision·메소·HP/MP, `character_inventory_slots`에 탭·슬롯·아이템·수량, `item_definitions`에 기본 아이템 ID·분류·최대 수량을 저장한다.
- 인증된 계정에 속한 캐릭터 행을 먼저 잠그고 현재 인벤토리를 조회한다. 별도 서버 프로세스가 같은 캐릭터를 갱신해도 행 잠금과 revision으로 중복 적용을 막는다.
- 포션 소비·HP/MP·슬롯·revision을 하나의 InnoDB 트랜잭션으로 저장한다. 슬롯 저장이 실패하면 모두 롤백하며, 커밋 이후에만 클라이언트에 성공 snapshot을 보낸다.
- 캐릭터 조회와 인벤토리 요청은 기존 DB 작업 스레드의 같은 Connection에서 순서대로 실행한다. 대기 작업은 최대 64개, 요청 제한 시간은 5초다. Map tick은 DB 호출을 기다리지 않는다.
- 연결 종료 또는 대기 시간 초과 시 아직 커밋되지 않은 작업을 취소 대상으로 표시한다. 이미 커밋된 요청의 응답이 유실된 경우 재조회로 확정 상태를 확인한다.
- 저장 파일 경로는 제거했다. 기존 `.inventory` 파일을 자동으로 가져오거나 삭제하지 않는다. 새 DB 인벤토리는 첫 조회 때 샘플 아이템으로 한 번만 생성한다.
- 장비 착용·거래·획득은 후속 범위다. DB 초기화·기존 DB 적용·검증은 [INVENTORY_DATABASE.md](INVENTORY_DATABASE.md)를 참고한다.

## 몬스터

MapManager가 지형 로딩 후 각 3D Map에 순찰 몬스터를 등록한다. 서버가 X축 이동·끝점 대기·방향 전환을 결정한다. 지지면·맵 경계와 지형 박스 겹침을 확인하고, 서버 tick에서 실제 경과 시간으로 이동한다. 클라이언트는 받은 목표 위치를 보간한다.

입장 시 현재 상태를 보내고 이후 수신자별 generation을 붙여 같은 필드 상태를 중계한다. 맵을 나간 플레이어에게 보내지 않으며 송신 실패는 해당 세션만 종료 대상으로 둔다. 클라이언트는 이전 Map/generation 또는 이전 tick의 상태를 무시하고, 최신 목록에서 사라진 몬스터를 제거한다.

현재 몬스터는 기본 순찰만 제공하며 전투 AI나 클라이언트가 몬스터 위치를 보고하는 패킷은 없다. 몬스터 메시지와 인벤토리 메시지는 캐릭터 행동 송신 5회 상한에 포함하지 않는다.

## 확인된 결과

- 서버 회귀 검사: 인벤토리 병합·중복 요청 배제·포션 소비·소유자 격리·잘못된 상태 거절, 몬스터 순찰·같은 tick 상태 공유·맵 격리·재입장·송신 실패 격리 통과.
- 실제 MySQL 검사 6개 묶음: 소유자 검증·초기 생성, 조작·재연결 유지, 두 Connection의 동시 소비, SQL 오류 시 전체 롤백, 잘못된 DB 수량 보존, 비동기 제출·대기 작업 취소 통과.
- 두 C# TCP 클라이언트: 슬롯 병합·정렬·소비·오래된 revision 거절, 늦은 입장과 같은 tick의 몬스터 좌표 일치 통과.
- 기존 3D 클라이언트 이동 검사 8개 및 0ms/80±35ms/150±60ms 지연·지터 TCP 검사 통과.
- 실제 LoginServer/GameServer/임시 MySQL과 두 Unity Player: 인증 경로, 상대 점프·착지, 인벤토리 수신, 움직이는 몬스터 3마리와 렌더링 통과.
- DB에 먼저 저장한 캐릭터 1001의 revision 4를 실제 Unity Player가 수신했다. 인벤토리 DB 행 3개를 확인했고 실행 디렉터리에 `inventory-data`가 생성되지 않았다.
- Windows Player의 실제 창 입력: 자동 이동 옵션 없이 씬 지형에서 좌표 변화, jumpId 0→8, I 입력에 따른 창 열기/닫기, 포커스 해제 후 정지 확인. 포션 더블클릭·USE에 따른 수량 감소와 HP 회복도 화면에서 확인했다.

실제 입력 기록은 3D 저장소 `artifacts/input-verification/Logs/input-probe.json`에 보존했다. 입력 횟수 필드는 InputSystem/창 이벤트 관측 횟수이며, 실제 점프 실행은 `jumpId`와 물리 상태로 판단한다. 사람이 Space를 계속 누른 채 착지하는 시나리오는 이 기록만으로 확인했다고 간주하지 않는다.

실제 창 키 이벤트 검증은 `--input-probe`의 별도 기록으로 구분한다. 자동 이동 smoke 결과를 사람의 키보드 입력 확인으로 기록하지 않는다. 실행·조작은 [README_3D.md](../../MyMMORPGClient_3D/README_3D.md)를 참고한다.
