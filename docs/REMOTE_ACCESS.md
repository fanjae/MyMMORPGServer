# 외부 PC 연결 거부 분석과 실행 설정

2026-10-06 다른 장소의 PC에서 `121.133.186.20:7776`에 연결할 때 발생한 오류를 분석했습니다. 화면의 연결 거부는 TCP 접속 단계의 WSAECONNREFUSED(10061)이며 로그인 ID·비밀번호 검증과 프로토콜 버전 확인보다 앞에서 발생합니다.

## 확인한 원인

조사 시점에 LoginServer 프로세스와 7776 수신 socket이 없었습니다. GameServer의 7777과 서버 간 티켓 포트 7778은 `127.0.0.1`에서만 대기했습니다. 기존 코드도 LoginServer와 게임 클라이언트 포트를 `127.0.0.1`로 고정하므로 서버가 실행돼도 다른 PC에서 접근할 수 없었습니다.

서버 PC의 이더넷에는 공인 IPv4 `121.133.186.20`이 직접 할당돼 있습니다. 현재 구성을 먼저 Windows 수신 주소와 방화벽 기준으로 확인합니다. 공유기 포트 전달은 서버가 공유기/NAT 뒤의 사설 IP에 있을 때 필요한 설정이며 이 PC에 무조건 적용할 단계는 아닙니다.

Windows 방화벽은 현재 Public 프로필에서 활성화되어 있었고 MyMMORPG의 프로그램명 또는 7776·7777에 대한 명시적 허용 규칙은 확인되지 않았습니다. 전체 규칙과 실제 외부 경로의 허용 여부를 이 사실만으로 확정하지는 않습니다.

## 코드 변경

| 항목 | 변경 |
|---|---|
| LoginServer 7776 | `SERVER_BIND_IP`로 클라이언트 수신 주소 선택 |
| GameServer 7777 | 같은 설정으로 클라이언트 수신 주소 선택 |
| GameServer 티켓 7778 | 기존 `127.0.0.1` 유지 |
| 잘못된 IPv4 | 시작 전에 거절하고 이유 출력, wildcard로 잘못 변환되는 경로 차단 |
| 수신 실패 | Winsock 오류 코드와 서버의 대상 주소·포트 출력 |
| 클라이언트 오류 | 대상 서버·주소·포트·10061을 표시하고 시간 초과·주소 오류와 구분 |
| 게임 접속 대상 | 로그인에 사용한 Host를 보존하여 입력란 수정으로 대상이 바뀌는 문제 방지 |
| 반복 스크립트 | 모든 인터페이스의 포트 점유 확인, 테스트 중 루프백 설정과 종료 후 원래 설정 복원 |

설정하지 않은 기본값은 기존 로컬 테스트용 `127.0.0.1`입니다. 외부 테스트에서는 두 서버를 시작하는 PowerShell 각각에 다음을 설정합니다.

```powershell
$env:SERVER_BIND_IP = '0.0.0.0'
```

`0.0.0.0`은 서버가 모든 IPv4 인터페이스에서 수신한다는 의미입니다. 클라이언트 Host에는 접속할 서버의 실제 IP 또는 DNS 이름을 입력합니다. `.env` 파일만 수정하면 실행 중인 서버에 적용되지 않습니다. 서버는 프로세스 환경 변수를 읽고 실행 시 한 번 수신 주소를 정합니다.

## 적용 절차

1. 서버 코드를 Release x64로 갱신합니다. 이번에는 실행 중인 파일을 덮어쓰지 않도록 `Tests/NetworkReliability/bin/RemoteServer`에 별도 빌드했습니다. 해당 디렉터리에 GameServer.exe, LoginServer.exe, MySQL 런타임 DLL과 data를 준비했습니다. 기존 `x64/Release` 서버는 재빌드·재시작 전까지 기존 바인딩을 사용합니다.
2. 운영자가 기존 서버를 종료하고, 기존 DB와 맞는 `DB_HOST`, `DB_USER`, `DB_PASSWORD`, `DB_NAME`을 설정한 두 PowerShell에서 각각 아래 서버를 실행합니다. 같은 계정·캐릭터 DB에 연결해야 합니다.

   ```powershell
   # 첫 번째 PowerShell, 서버 저장소에서 실행
   $env:SERVER_BIND_IP = '0.0.0.0'
   .\Tests\NetworkReliability\bin\RemoteServer\GameServer.exe

   # 두 번째 PowerShell, 같은 DB_* 설정 후 실행
   $env:SERVER_BIND_IP = '0.0.0.0'
   .\Tests\NetworkReliability\bin\RemoteServer\LoginServer.exe
   ```

3. 서버 콘솔의 `GameServer listening on 0.0.0.0:7777`, `LoginServer listening on 0.0.0.0:7776`을 확인합니다. DB 연결이나 포트 점유로 종료됐다면 해당 오류부터 해결합니다. 서버 PC에서 다음으로 실제 수신 상태를 확인할 수 있습니다.

   ```powershell
   [Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveTcpListeners() |
       Where-Object { $_.Port -in @(7776,7777,7778) }
   ```

4. 외부 테스트 PC에서 두 포트를 확인합니다.

   ```powershell
   Test-NetConnection 121.133.186.20 -Port 7776
   Test-NetConnection 121.133.186.20 -Port 7777
   ```

   두 결과의 `TcpTestSucceeded`가 True여야 로그인과 캐릭터 선택 뒤 게임 입장이 가능합니다. 서버 공인 IP가 바뀌면 실제 주소로 교체합니다.

5. 서버 PC의 Windows 방화벽에서 TCP 7776·7777의 인바운드를 허용합니다. 테스트 PC의 공인 IP를 아는 경우 다음처럼 범위를 지정할 수 있습니다. 관리자 PowerShell에서 테스트 PC 주소를 실제 값으로 바꿔 실행합니다. 이번 작업에서는 방화벽 규칙을 변경하지 않았습니다.

   ```powershell
   $testerPublicIp = '테스트_PC의_공인_IPv4'
   New-NetFirewallRule -DisplayName 'MyMMORPG external test' -Direction Inbound `
       -Protocol TCP -LocalPort 7776,7777 -RemoteAddress $testerPublicIp `
       -Profile Public,Private -Action Allow
   ```

   방화벽을 적용해도 연결되지 않으면 외부망의 접근 제한과 서버 앞단 장비를 확인합니다. 서버가 공유기/NAT 뒤에 있는 구성에서는 외부 TCP 7776→서버 7776, 외부 TCP 7777→서버 7777을 전달합니다. 게임 응답은 7777을 안내하므로 두 포트 모두 필요합니다. 티켓 등록 7778과 MySQL 3306은 외부 클라이언트의 접속 포트가 아닙니다.

로그인 실패 결과나 ProtocolMismatch 응답을 받는 경우에는 TCP 연결은 완료된 상태입니다. 이번 화면의 연결 거부와 구분해서 계정 DB 또는 서버·클라이언트 빌드를 확인합니다. 이번 수정은 wire format을 추가로 변경하지 않았습니다.

## 검증과 범위

- 서버 솔루션 별도 Release x64 빌드 성공
- C++ 통신 테스트 PASS 12: 기본 바인딩, 잘못된 주소 거절, wildcard 수신과 실제 이더넷 주소 `121.133.186.20`을 통한 같은 PC의 접속 포함
- C# 통신 테스트 PASS 8: 닫힌 포트에서 실제 10061 발생, 대상 정보 표시와 서버 시작 후 재접속 포함
- 변경된 NetworkManager·LoginScreenController를 Unity 6000.3.7f1의 실제 Core/IMGUI DLL로 컴파일하여 경고 0·오류 0 확인
- 반복 스크립트 구문 검사와 두 저장소 공백 검사 통과

실행 중인 서버·Unity Editor와 기존 ProjectSettings 변경을 유지했습니다. 다른 장소 PC의 실제 재접속과 외부 방화벽 경로는 여기서 최종 확인하지 못했습니다. Windows 플레이어에 새 진단 문구를 포함하려면 실행 중인 Unity Editor에서 Windows 빌드를 갱신해야 합니다. 실제 외부 접속 가능 여부는 위 수신 상태와 외부 PC의 두 포트 검사로 확인합니다.
