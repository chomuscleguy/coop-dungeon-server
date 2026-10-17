# coop-dungeon-server

**2~4인 협동 로그라이트 던전 러너**의 서버를, C++/Boost.Asio로 처음부터 하나씩 직접
만들어가는 프로젝트. 탕탕특공대(Survivor.io)의 오토 슈팅 전투에 던전앤파이터식 방 진행을
얹은 형태다 — 조작은 이동만 하고 사격은 자동, 방마다 몬스터 웨이브를 격파하며 나아가서
마지막 보스를 잡으면 던전 클리어. 드랍된 아이템은 파티끼리 경매로 나눈다.

판 안에서 얻는 레벨과 스킬은 그 던전에서만 유효하고, 드랍된 장비는 계정에 남는다.
매번 빌드를 새로 짜는 재미와 캐릭터가 쌓이는 재미를 둘 다 가져가는 구조.

서버 권위형(server-authoritative)으로 간다 — 방 상태, 보스 체력, 경매 결과 같은
공유 상태는 항상 서버가 판정하고, 클라이언트(Unity/C#)는 요청만 보낸다.

**서버는 둘로 나뉜다.**

| | `server/` (Step 1~12) | `battle_server/` (Step 13~) |
|---|---|---|
| 프로토콜 | TCP + JSON | UDP + 바이너리 |
| 역할 | 로비 — 로그인·방·채팅·매칭·보상·경매·친구 | 실시간 전투 — 30Hz 틱, 스테이지 진행, 몬스터 |
| 흐름 | 파티를 꾸려 배틀 서버로 보냄 | 던전 결과를 로비로 돌려줌 |
| 보장 | 도착·순서 (TCP가 해줌) | **없음 — 직접 만든다** |

**왜 TCP로 전투를 안 하나:** TCP는 패킷이 유실되면 재전송될 때까지 **그 뒤 데이터를 전부
붙잡아둔다**(head-of-line blocking). 초당 30번 위치를 보내는데 0.03초 전 위치를 기다리느라
지금 위치를 못 받는 건 말이 안 된다. **낡은 위치는 버리고 최신 것만 쓰면 되기 때문**이다.
TCP는 "모든 바이트가 중요하다"를 전제하는데, 실시간 게임은 그 전제가 틀린 영역이다.

> **참고:** 이 로비 서버의 보스 전투(Step 8~9)는 **메시지 흐름을 익히기 위한 placeholder**다.
> `BossAttack`을 보내면 서버가 고정 데미지를 깎는 턴제에 가까운 구조로, 실제 액션 전투는
> UDP 배틀 서버가 맡는다. 다만 그 위에 얹은 매칭·보상·파티 경매는 전투가 어떻게 생겼든
> 그대로 쓰이므로, 로비 기능을 익히는 발판으로는 제 역할을 한다.

**던전 규칙** (배틀 서버 설계의 전제)

- 방 클리어 조건은 **웨이브 N개 격파**. 몬스터가 플레이어를 추적해 오므로 맵 구석에 남은
  한 마리를 찾아다닐 일이 없다.
- 쓰러지면 **다운** 상태가 되고 파티원이 살릴 수 있다. **전원이 다운되면 던전 실패**.
- 다음 방으로는 **전원이 문에 도달하면 즉시**, **과반수가 도달하면 카운트다운 뒤** 넘어간다.
- 던전 길이(방 개수)는 클라이언트 설계 때 정한다.

## 진행 상황

**로비 서버 (TCP)** — `server/`

- [x] Step 1: TCP 접속을 받아서 로그만 찍는 서버
- [x] Step 2: 받은 데이터를 그대로 돌려주는 echo 서버
- [x] Step 3: 여러 명이 동시에 접속 가능하게 (async_accept + io_context)
- [x] Step 4: 길이-prefix + JSON 프로토콜 얹기
- [x] Step 5: 로그인 (유저네임)
- [x] Step 6: 방 생성/참여/퇴장
- [x] Step 7: 채팅 (방 단위 브로드캐스트)
- [x] Step 8: 매칭 큐 + 보스 스폰
- [x] Step 9: 보스 공격 + 클리어 리워드
- [x] Step 10: 파티 경매 (보스 드랍 분배, 타이머 마감)
- [x] Step 11: 친구 (요청/수락/거절/삭제/목록, 온라인 여부)
- [x] Step 12: 라즈베리파이 배포 (도커로 상시 구동)

**배틀 서버 (UDP)** — `battle_server/`

- [x] Step 13: 연결 계층 (UDP 소켓, 바이너리 헤더, 세션 맵, 하트비트)
- [x] Step 14: 신뢰성 계층 (ack_bits, 유실 탐지, 선택적 재전송, RTT/RTO)
- [x] Step 15: 30Hz 고정 틱 + 입력 + 위치 동기화 (서버 권위)
- [x] Step 16: 몬스터 (World 분리, 스폰, 최근접 추적, 자동 사격, Event 본문)
- [x] Step 17: 웨이브 격파 + 스테이지 진행 (방 상태, 문 도달, 난이도)
- [x] Step 18: 다운/부활/전멸 + 로비로 결과 전달
- [x] Step 19: 측정·부하 테스트 (바이트 계측, C++ 봇, 유실 시뮬)

**서버 사이 연결**

- [x] Step 20: 로비 ↔ 배틀 연동 (지속 연결, 토큰 입장, 결과 귀속)
- [x] Step 21: 던전 하나당 World 하나 (한 서버가 여러 판 동시 진행)
- [x] Step 22: 끝이 있는 던전 (보스방, clear 결과)

**클라이언트 (Unity)** — `client/`

- [x] Step 23: 보는 클라이언트 (UDP 소켓, ack, 스냅샷 그리기, 입력 30Hz)

## 프로젝트 구조

```
server/                  로비 서버 (TCP)
  CMakeLists.txt         vcpkg(Windows)와 apt(리눅스) 양쪽에서 빌드되게
  CMakePresets.json      vcpkg 툴체인 경로 지정
  vcpkg.json             boost-asio, nlohmann-json
  src/
    main.cpp             포트 결정, 시그널, io.run()
    Room.h               Item · Boss · RoomPhase · LootAuction · Room
    Friendship.h         두 사람 사이의 관계 하나
    PlayerRegistry.h     계정 · 재화 · 인벤토리 · 친구 관계
    BattleToken.h        배틀 서버와 공유하는 시크릿 · 주소 · 1회용 토큰
    Session.h / .cpp     연결 하나. Server 를 쓰는 메서드는 .cpp 에
    Server.h             레지스트리, 방 관리, 브로드캐스트, 틱

battle_server/           배틀 서버 (UDP) — 별도 CMake 프로젝트, 별도 실행 파일
  CMakeLists.txt
  CMakePresets.json
  vcpkg.json             boost-asio만 (JSON 안 씀)
  src/
    main.cpp             포트 결정, 시그널, io.run()
    Protocol.h/.cpp      바이너리 헤더 ↔ 구조체 변환
    Connection.h         연결 하나의 상태 (소켓을 갖지 않는다)
    World.h/.cpp         게임 상태 — 플레이어·몬스터·전투. 네트워크를 모른다
                         던전 한 판에 하나씩 (BattleServer 의 Battle 이 소유)
    BattleServer.h/.cpp  소켓, 수신 루프, 세션 맵, 틱, 브로드캐스트
    LobbyLink.h/.cpp     로비(TCP)와의 지속 연결. 끊기면 다시 붙는다
    bot.h / bot_main.cpp 부하 테스트용 봇 (battle_bot 실행 파일, Protocol.cpp 공유)

client/                  유니티 클라이언트 (Unity 6, URP 2D)
  Assets/Scripts/
    BattleClient.cs      UDP 소켓, 핸드셰이크, ack 생성, 하트비트, 입력 30Hz
    Net/
      Protocol.cs        Protocol.h 를 C# 으로 옮긴 것. 순수 바이트 변환
      WorldView.cs       스냅샷을 네모로 그린다. ID 로 짝지어 추적

tools/
  test-client.ps1        PowerShell 임시 테스트 클라이언트 (로비용)
Dockerfile               로비 서버용. 멀티 스테이지 (빌드 단계 / 실행 단계)
Dockerfile.battle        배틀 서버용. JSON 의존성이 없다
docker-compose.yml       두 서버를 같이 띄운다. 서비스 이름이 곧 호스트 이름
.dockerignore            build/ 를 빌드 컨텍스트에서 제외 (없으면 수 GB를 보낸다)
```

**로비는 한 파일로 버티다 Step 21 뒤에 나눴다.** 배틀 서버는 처음부터 나눴고, 그 차이가
기록으로 남았다.

로비는 "한 파일로 버티다 한계를 겪는" 과정 자체가 목적이었다. Step 6 데브로그에 *"헤더/소스
분리가 필요해지는 이유를 한 파일 안에서 미리 겪은 셈"*이라고 적어뒀는데, 1,640줄까지 가서
실제로 겪었다 — `Session` 이 `Server` 보다 먼저 정의돼 있어 `server_.xxx()` 를 쓰는 메서드를
클래스 안에 둘 수 없었고, 그걸 잊어서 두 번 되돌렸다. "어느 클래스 안"을 줄 번호 없이 말할
수 없다는 것도 마찰이었다.

나누고 나니 제약이 구조로 풀렸다. `Session.h` 는 `class Server;` 전방 선언만 갖고,
`Server.h` 가 `Session.h` 를 포함하고, `Session.cpp` 가 둘 다 포함해 정의를 쓴다.
**나눌 경계는 이미 코드에 드러나 있었다** — 그대로 파일이 됐다.

배틀 서버는 그 교훈을 적용해 처음부터 나눴고, **나눴기 때문에 생긴 차이**를 바로 만났다 —
Boost 1.74 우회(`<utility>`)를 `main.cpp` 가 아니라 **`BattleServer.h` 에** 넣어야 했다.
각 `.cpp` 는 독립적으로 컴파일되므로, 헤더가 `<boost/asio.hpp>` 를 끌어오면 우회도 헤더에
있어야 한다.

**클라이언트는 Step 23 에서 붙였다.** 프로토콜을 C# 으로 옮긴 `Protocol.cs` 는
`UnityEngine` 을 전혀 쓰지 않는다 — 순수 바이트 변환이라 서버 쪽 `Protocol.cpp` 와
나란히 놓고 비교할 수 있다. 다만 **거울이 둘이 되면 언젠가 어긋난다.** 지금 규칙은
"`Protocol.h` 를 고치면 `Protocol.cs` 도 같이 고친다"이고, 이게 비용으로 느껴지면
그때 생성기를 만든다.

## 문서

README 는 개요와 빌드/배포만 담는다. 자세한 것은 따로 나눠 뒀다.

| | |
|---|---|
| [프로토콜](docs/protocol.md) | 로비(TCP) · 배틀(UDP) · 서버 사이 연결의 패킷 형식과 규칙 |
| [테스트](docs/testing.md) | 테스트 클라이언트, C++ 봇, 유실 시뮬레이션, 측정 결과 |
| [Devlog](docs/devlog/README.md) | 스텝마다 무엇을 왜 그렇게 했는지 |

## 빌드

**Windows (개발용)** — Visual Studio + vcpkg:

1. Visual Studio에서 `server` 폴더를 "폴더 열기"로 열기
2. 구성(Configuration) 드롭다운에서 `default` 선택 (vcpkg가 의존성 자동 설치)
3. `Ctrl+Shift+B`로 빌드, `Ctrl+F5`로 실행

`Listening on port 7777...`이 뜨면 정상.

**리눅스** — apt로 의존성을 받는다. vcpkg로 boost를 소스 빌드하면 ARM에서 몇 시간이 걸린다.

```bash
sudo apt install g++ cmake ninja-build libboost-dev nlohmann-json3-dev
cd server
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/game_server
```

포트는 **명령행 인자 > `GAME_SERVER_PORT` 환경변수 > 기본값 7777** 순으로 정해진다.
잘못된 값이면 시작하지 않고 종료한다 — 요청한 포트와 다른 데서 듣는 게 더 나쁘기 때문.
시작 로그에 출처가 찍히므로(`Port 7777 (from GAME_SERVER_PORT)`) 전달이 됐는지 바로 알 수 있다.

**배틀 서버**는 별도 프로젝트다. 같은 방식으로 빌드하되 JSON이 필요 없다.

```bash
# Windows: Visual Studio 에서 battle_server 폴더를 "폴더 열기"
# 리눅스:
sudo apt install g++ cmake ninja-build libboost-dev
cd battle_server
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/battle_server
```

포트는 `BATTLE_SERVER_PORT`, 기본값 **7778**(UDP). TCP 7777과 프로토콜이 달라 사실 번호가
겹쳐도 충돌하지 않지만, 헷갈리지 않게 나눴다.

## 배포 (라즈베리파이)

도커로 돌린다. 의존성이 이미지에 고정되므로 파이 OS를 다시 깔아도 똑같이 동작한다.

```bash
git clone https://github.com/chomuscleguy/coop-dungeon-server.git
cd coop-dungeon-server
docker compose up -d --build
```

**파이에서 직접 빌드한다.** 도커 이미지는 CPU 아키텍처를 타는데(파이는 arm64, 개발 PC는 x86_64),
이 프로젝트는 `main.cpp` 하나라 파이에서 컴파일해도 몇 분이면 끝난다. 크로스 빌드
(`docker buildx --platform linux/arm64`)보다 훨씬 간단하다.

| 명령 | 하는 일 |
|---|---|
| `docker compose up -d --build` | 빌드 + 백그라운드 실행 |
| `docker compose logs -f` | 로그 실시간 보기 |
| `docker compose stop` | 정상 종료 (SIGTERM) |
| `docker compose down` | 종료 + 컨테이너 제거 |
| `docker compose ps` | 상태 확인 |

`restart: unless-stopped`라 **크래시하면 자동으로 다시 뜨고, 파이를 재부팅해도 살아난다.**
`docker compose stop`으로 직접 내린 것만 재부팅 후에도 꺼진 채로 남는다.

**포트를 바꾸려면 `docker-compose.yml`의 두 곳을 같이** 고쳐야 한다.

```yaml
ports:
  - "8888:8888"          # 호스트:컨테이너
environment:
  - GAME_SERVER_PORT=8888
```

환경변수만 바꾸면 컨테이너는 8888에서 듣는데 도커는 7777을 연결하고 있어서 접속이 안 된다.

**이미지 구성** — 빌드 단계와 실행 단계를 나눈다(멀티 스테이지). Boost.Asio와 nlohmann-json이
둘 다 헤더 전용이라 **런타임에는 `libstdc++`만 있으면 된다.** 컴파일러와 헤더를 뺀 최종
이미지는 114MB다. root가 아닌 전용 사용자로 돌린다.
