# MyMMORPG Server

> C++로 개발하는 MMORPG 서버 프로젝트입니다. 로그인·계정·캐릭터 서버와 게임 서버를 분리하고, 서버가 필드 상태와 플레이어 행동을 검증하며 MySQL에 캐릭터 데이터를 저장합니다.

## 프로젝트 개요

| 항목 | 내용 |
|---|---|
| 프로젝트명 | MyMMORPG Server |
| 개발 인원 | 1명 |
| 장르 | 3D MMORPG 서버 |
| 개발 환경 | Windows, Visual Studio 2022, C++17 |
| 네트워크 | Winsock2, IOCP, TCP |
| 데이터베이스 | MySQL 8.4, MySQL Connector/C++ |
| 클라이언트 | [MyMMORPGClient_3D](https://github.com/fanjae/MyMMORPGClient_3D) |

## 서버 구성

| 프로젝트 | 역할 | 기본 포트 |
|---|---|---:|
| `LoginServer` | 계정 로그인·가입, 캐릭터 조회·선택, 게임 입장 인증 | TCP 7776 |
| `GameServer` | 게임 세션, 맵·이동·필드 상태·채팅·전투·아이템·거래 | TCP 7777, 서버 간 티켓 TCP 7778 |
| `ServerCore` | Winsock, IOCP, 리스너·세션, 수신 버퍼, DB 연결 기반 | — |
| `Protocol` | 클라이언트·서버 간 패킷 구조와 공통 데이터 | — |
| `TestClient`, `Tests` | 서버 동작 및 네트워크·기능 검증 도구 | — |

## 구현 기능

| 기능 | 설명 |
|---|---|
| 계정·캐릭터 | 계정 가입과 로그인, 캐릭터 목록·선택, 게임 접속용 인증 티켓 |
| 서버 네트워크 | IOCP 기반 TCP 세션과 비동기 입출력 |
| 필드 | 맵 지형 로딩, 플레이어 이동 검증, 입장·퇴장과 상태 동기화 |
| 커뮤니케이션 | 필드 채팅, 귓속말, 연결된 플레이어 상태 전달 |
| 전투·생명력 | 서버 판정 기본 공격, 몬스터 상태, HP·MP, 사망·부활 |
| 아이템 | 캐릭터별 인벤토리, 장비·소비 아이템, 필드 드롭 획득 |
| 거래 | 플레이어 간 제안·확인, DB 트랜잭션 기반 아이템·메소 교환 |
| 영속성 | MySQL 계정·캐릭터·인벤토리·전투·거래 데이터 저장 |

## 사용 기술

| 기술 | 용도 |
|---|---|
| C++17 / Visual Studio 2022 (v143) | 서버 애플리케이션 빌드 |
| Winsock2 / IOCP | Windows TCP 연결과 비동기 네트워크 처리 |
| MySQL 8.4 | 계정, 캐릭터, 인벤토리, 전투·거래 관련 데이터 |
| MySQL Connector/C++ | C++ 서버의 MySQL 연결 |
| CSV | 맵과 3D 이동 지형 데이터 |
| Docker Compose | 로컬 MySQL 실행 편의 |

## 로컬 실행

### 준비 사항

- Windows와 Visual Studio 2022의 **Desktop development with C++** 구성 요소
- MySQL Connector/C++ 26.7 (현재 Visual Studio 프로젝트는 기본 설치 경로 `C:\Program Files\MySQL\MySQL Connector C++ 26.7`을 참조합니다.)
- Docker Desktop 또는 별도로 실행한 MySQL 8.4

### 데이터베이스 준비

저장소 루트에서 예시 환경 파일을 복사하고 로컬 비밀번호를 설정합니다.

```powershell
Copy-Item .env.example .env
```

`.env`의 예시 비밀번호를 로컬 개발용 값으로 바꾼 뒤 MySQL을 시작합니다.

```powershell
docker compose up -d mysql
```

빈 데이터 볼륨으로 시작하면 `database/init/001_init.sql`부터 `006_registration.sql`까지 초기화 SQL이 순서대로 적용됩니다. 기존 데이터베이스에는 초기화 스크립트가 자동으로 다시 실행되지 않으므로 필요한 스키마 변경 파일을 별도로 적용하세요.

### 빌드 및 시작

1. `MyMMORPGServer.sln`을 Visual Studio 2022에서 엽니다.
2. `Release`와 `x64`를 선택해 솔루션을 빌드합니다.
3. 서버 프로세스에 DB 환경 변수를 설정합니다. `.env` 파일은 Docker Compose가 사용하며, C++ 서버는 프로세스 환경 변수 `DB_HOST`, `DB_USER`, `DB_PASSWORD`, `DB_NAME`을 직접 읽습니다.
4. 빌드된 `LoginServer.exe`와 `GameServer.exe`를 각각 실행합니다. 두 프로세스는 같은 데이터베이스를 사용해야 합니다.

예를 들어 저장소 루트에서 각 서버를 실행할 PowerShell에 다음 값을 설정합니다.

```powershell
$env:DB_HOST = 'tcp://127.0.0.1:3306'
$env:DB_USER = 'game'
$env:DB_PASSWORD = '로컬에서 설정한 비밀번호'
$env:DB_NAME = 'mymmorpg'
$env:SERVER_BIND_IP = '127.0.0.1'
```

클라이언트는 로그인 포트 `7776`에 접속한 뒤 게임 포트 `7777`로 연결합니다. `SERVER_BIND_IP` 기본값은 로컬 전용인 `127.0.0.1`입니다. 다른 PC에서 시험할 때에는 수신 주소·방화벽·외부 주소를 함께 설정해야 합니다. 자세한 절차는 [원격 연결 안내](docs/REMOTE_ACCESS.md)를 참고하세요.

## 프로토콜 호환성

현재 로그인 프로토콜은 2, 3D 게임 프로토콜은 12입니다. 클라이언트와 서버를 같은 프로토콜 버전으로 빌드해야 합니다. 구버전 실행 파일은 접속이 거절될 수 있습니다.

## 개발 인원 및 역할

| 개발 인원 | 담당 |
|---:|---|
| 1명 | 서버 구조·프로토콜, 네트워크, 계정·게임 로직, DB 스키마, 클라이언트 연동 및 검증 |

## 참고 문서

- [3D 클라이언트 실행 및 조작 안내](../MyMMORPGClient_3D/README_3D.md)
- [MMORPG 개발 로드맵](docs/MMORPG_ROADMAP.md)
- [필드 기능 및 데이터 저장 안내](docs/FIELD_FEATURES.md)
- [3D 이동 검증 안내](docs/MOVEMENT_3D.md)
- [인벤토리 데이터베이스 안내](docs/INVENTORY_DATABASE.md)
