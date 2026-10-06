# 우선 안정성 보강 검증: 2026-10-06

서버 저장소 `MyMMORPGServer`와 클라이언트 저장소 `MyMMORPGClient`를 각각 수정했습니다. 범위·프로토콜·재실행 절차는 [NETWORK_RELIABILITY.md](NETWORK_RELIABILITY.md)에 있습니다. 기존 한국어 주석처럼 처리 이유와 객체 수명·검증 규칙을 짧게 설명하는 스타일을 사용했습니다.

## 반영 범위

- 입장·이동·채팅·퇴장 전송 실패를 수신자 연결에 격리하고 Map 순회 이후 종료 예약을 처리
- 실패한 Accept 재등록, 종료 뒤 IOCP completion 무시, 최대 TCP 패킷의 분할 수신 처리
- 서버 간 티켓 등록 3초, DB 연결/읽기/쓰기 3초, 게임 입장 DB 작업 5초 제한
- GameServer 캐릭터 DB 조회를 작업 스레드로 분리하고 종료된 연결의 대기 작업과 완료 결과 무시
- 서버·클라이언트 송신 대기량, Unity 수신 대기량과 프레임별 처리량 제한
- 이전 송신·수신·종료 콜백이 새 연결에 적용되지 않도록 Connection별 상태와 이벤트 순서 보강
- UTF-8 이름 16자·65바이트 공통 처리, Login version 2와 Game version 3을 서버·C++·C#에 동시 반영
- 반복 스크립트 단계 제한 시간, 테스트 stdout/stderr와 실패 원인 파일 보존, 서버 로그 즉시 기록
- 서버 tick 처리 시간·지연·대기 송신량·catch-up 한도 초과 계측

## 빌드와 자동 검증

| 검증 | 결과 |
|---|---|
| 서버 솔루션 Release x64 | 성공 |
| C++ NetworkReliability | PASS 10 |
| C# 통신·대기열·재접속 | PASS 7 |
| C++ 발판 지형·물리 | PASS 48 |
| 발판 실서버와 2,000 tick C++/C# 비교 | PASS 14 |
| 서버 재시작 뒤 Free C++ 회귀 | PASS 15 |
| 서버 재시작 뒤 Free C# 회귀 | PASS 16 |
| 32명 접속·이동·정지·DB 지연·재접속 | 추가 PASS 5 |
| 최종 전체 반복 실행 | PASS 115, 종료 코드 0 |
| .NET 테스트 프로젝트 | 경고 0, 오류 0 |
| Unity 6000.3.7f1 Windows 최종 빌드 | `Build Finished, Result: Success`, 종료 코드 0 |
| 두 저장소 `git diff --check`와 PowerShell 구문 검사 | 통과 |

초기 서버 빌드에서는 환경 변수 Path/PATH 중복과 이전 컴파일러의 PCH가 발견되어 실행 환경 정리와 Rebuild로 해결했습니다. C++ UTF-8 컴파일 설정을 추가해 한글 주석 인코딩 경고를 제거했습니다. Unity의 샌드박스 IPC 실패와 batchmode headless 라이선스 오류 후 기존 절차대로 일반 Editor 모드에서 빌드했습니다. 최종 결과는 새 `Builds/Windows/MyMMORPGClient_Data/Managed/Assembly-CSharp.dll`과 성공 로그로 확인했습니다.

## 부하와 DB 지연

기존 MySQL `mymmorpg-mysql`과 분리한 3307 포트의 `--rm` 임시 컨테이너를 사용했습니다. 원본 초기 SQL과 `002_load.sql`로 32개 테스트 계정을 추가했고 한글 16자 이름이 로그인 목록과 게임 입장에서 일치함을 확인했습니다.

32명이 같은 발판 맵에서 이동하고 정지한 뒤 서로의 입장 수와 상태를 확인했습니다. 캐릭터 테이블을 2초 동안 잠근 상태에서 입장 요청을 보낸 연결을 종료해도 기존 플레이어의 tick은 진행됐습니다. 잠금 해제 뒤 새 인증으로 같은 캐릭터가 정상 입장하여 이전 조회 결과에 의한 유령 Player가 없음을 확인했습니다.

최종 실행의 구간별 계측 최대값은 tick 처리 시간 4,372μs, tick 예정 시각 대비 지연 80ms였으며 catch-up 한도 초과는 모든 기록 구간에서 0이었습니다. 32명 구간의 송신 대기량 기록은 59,644바이트였습니다. 이는 기록 시점의 모든 연결 대기량 합계이며 순간 최대값은 아닙니다. 최종 Unity 빌드와 병행한 로컬 환경의 측정이므로 서비스 수용 인원이나 화면 체감 품질의 기준으로 사용하지 않습니다.

## 종료와 로그 보존

- 성공 실행에서 발판 → 서버 종료 → 임시 Free 설정으로 서버 재시작 → 회귀 → 서버 종료를 확인
- 성공 시 출력된 임시 테스트 디렉터리가 실제 삭제됐음을 확인
- `-FailAfterPlatform` 의도적 실패에서 서버와 테스트 출력, 물리 trace와 `failure.txt` 보존 확인
- `-StepTimeoutSeconds 1` 시간 초과에서 서버·테스트 프로세스 종료와 실패 원인 보존 확인
- 실패 보존 로그의 서버 stdout이 비어 있지 않아 즉시 기록이 동작함을 확인
- 테스트 후 GameServer·LoginServer 프로세스가 남지 않았고 이번 임시 DB와 임시 초기화 디렉터리를 제거
- 기존 MySQL 컨테이너와 원본 CSV·초기 SQL 유지

검증 출력은 로컬 빌드 디렉터리의 [전체 실행 로그](../Tests/NetworkReliability/bin/integration-final.log), [의도적 실패 로그](../Tests/NetworkReliability/bin/integration-failure-final.log), [시간 초과 로그](../Tests/NetworkReliability/bin/integration-timeout-final.log)에 있습니다. 이 빌드 디렉터리는 Git에 포함하지 않습니다. 최종 Unity 로그는 클라이언트 `Logs/priority-build-final.log`에 있습니다.

최종 실패 보존 디렉터리는 다음 두 곳입니다. 요청한 실패 로그 보존을 위해 삭제하지 않았습니다.

```text
C:\Users\hjjan\AppData\Local\Temp\MyMMORPGPlatformIntegration-f1e96583e718409e8916da635beeff62
C:\Users\hjjan\AppData\Local\Temp\MyMMORPGPlatformIntegration-7e24a43c22cf46ff81bd8554aeedeb02
```

## 다음 확인 범위

지연 환경의 실제 Unity 입력·화면 보간, 미확인 입력 저장/재실행 보정, 채팅 빈도와 문자열 검증, 포탈 기반 맵 이동과 경사·사다리는 이번 범위에 포함하지 않았습니다. LoginServer의 DB 조회·비밀번호 검증은 제한 시간이 있는 기존 실행 스레드에 남아 있습니다. 로그인 UI 응답 제한과 프레임 처리량 변경은 빌드와 통신 계층 자동 검증까지 확인했으며 실제 UI의 수동 조작은 별도 확인합니다.

작업 시작 전에 있던 서버 `docker-compose.yml`, 클라이언트 `.gitignore`와 `Packages/packages-lock.json` 변경은 유지했습니다. 커밋은 생성하지 않았습니다.
