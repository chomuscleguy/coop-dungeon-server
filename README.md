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
- [ ] Step 18: 다운/부활/전멸 + 로비로 결과 전달
- [ ] Step 19: 측정·부하 테스트 (바이트 로그, 유실 시뮬, C++ 봇)

## 프로젝트 구조

```
server/                  로비 서버 (TCP)
  CMakeLists.txt         vcpkg(Windows)와 apt(리눅스) 양쪽에서 빌드되게
  CMakePresets.json      vcpkg 툴체인 경로 지정
  vcpkg.json             boost-asio, nlohmann-json
  src/main.cpp           서버 전체 (Session + Server + PlayerRegistry + main)

battle_server/           배틀 서버 (UDP) — 별도 CMake 프로젝트, 별도 실행 파일
  CMakeLists.txt
  CMakePresets.json
  vcpkg.json             boost-asio만 (JSON 안 씀)
  src/
    main.cpp             포트 결정, 시그널, io.run()
    Protocol.h/.cpp      바이너리 헤더 ↔ 구조체 변환
    Connection.h         연결 하나의 상태 (소켓을 갖지 않는다)
    World.h/.cpp         게임 상태 — 플레이어·몬스터·전투. 네트워크를 모른다
    BattleServer.h/.cpp  소켓, 수신 루프, 세션 맵, 틱, 브로드캐스트

tools/
  test-client.ps1        PowerShell 임시 테스트 클라이언트 (로비용)
Dockerfile               로비 서버용. 멀티 스테이지 (빌드 단계 / 실행 단계)
docker-compose.yml       포트·환경변수·재시작 정책
.dockerignore            build/ 를 빌드 컨텍스트에서 제외 (없으면 수 GB를 보낸다)
```

**로비는 한 파일, 배틀은 처음부터 나눴다.** 로비는 "한 파일로 버티다 한계를 겪는" 과정 자체가
기록이라 그대로 뒀다(Step 6 데브로그 참고). 1,475줄이 됐고, 나눌 경계는 이미 코드에 드러나 있다 —
`Session` / `Server` / `PlayerRegistry` / `Room`·`Boss`·`LootAuction` / `Friendship`.

배틀 서버는 그 교훈을 적용해 처음부터 나눴다. 그리고 **나눴기 때문에 생긴 차이**를 바로 만났다 —
Boost 1.74 우회(`<utility>`)를 `main.cpp`가 아니라 **`BattleServer.h`에** 넣어야 했다.
각 `.cpp`는 독립적으로 컴파일되므로, 헤더가 `<boost/asio.hpp>`를 끌어오면 우회도 헤더에 있어야 한다.

서버 전체가 `main.cpp` 한 파일이다. Step 6 데브로그에 *"헤더/소스 분리가 필요해지는 이유를
한 파일 안에서 미리 겪은 셈"*이라고 적어뒀는데, 12스텝을 지나며 1,400줄이 넘었다.
지금 나누면 좋을 경계는 이미 코드에 드러나 있다 — `Session` / `Server` / `PlayerRegistry` /
`Room`·`Boss`·`LootAuction` / `Friendship`.

클라이언트(Unity)는 아직 없음. 서버가 어느 정도 완성된 뒤 붙일 예정.

## 프로토콜 — 로비 (TCP)

모든 메시지는 `[4바이트 빅엔디안 길이][UTF-8 JSON 본문]` 형태로 감싸서 주고받는다.
본문 구조는 `{"type": "...", "data": {...}}`.

**Client → Server**

| type | data | 설명 |
|---|---|---|
| `Login` | `{"username": "alice"}` | 로그인. 이것만 로그인 전에 허용됨 |
| `Ping` | `{}` | 연결 확인 |
| `RoomCreate` | `{"name": "Boss Room"}` | 방 생성 후 자동 입장 |
| `RoomJoin` | `{"roomId": 1}` | 방 참여 |
| `RoomLeave` | `{}` | 현재 방에서 나가기 |
| `RoomList` | `{}` | 방 목록 조회 |
| `ChatSend` | `{"text": "hello"}` | 현재 방에 채팅. 방에 있어야 하고 500자까지 |
| `MatchEnqueue` | `{}` | 매칭 대기열 등록. 2명 모이면 방이 자동 생성됨 |
| `MatchCancel` | `{}` | 매칭 대기 취소 |
| `BossAttack` | `{}` | 보스 공격. **데미지는 서버가 정한다** |
| `LootBid` | `{"amount": 300}` | 파티 경매 입찰. 즉시 차감되고, 밀려나면 환불 |
| `InventoryList` | `{}` | 내 재화와 아이템 조회 |
| `FriendRequest` | `{"username": "bob"}` | 친구 신청 |
| `FriendRespond` | `{"username": "alice", "accept": true}` | 수락 / 거절 |
| `FriendList` | `{}` | 친구 + 받은 신청 + 보낸 신청 조회 |
| `FriendRemove` | `{"username": "bob"}` | 친구 삭제. 수락 전이면 신청 취소 |

**Server → Client**

| type | data | 설명 |
|---|---|---|
| `LoginOk` | `{"playerId": 1, "username": "alice"}` | 로그인 성공 |
| `Pong` | `{}` | Ping 응답 |
| `Error` | `{"message": "login required"}` | 요청 거부. 사유를 메시지로 전달 |
| `RoomState` | `{"id": 1, "name": "Boss Room", "members": [1, 2], "state": "waiting"}` | 방 현재 상태. **요청자뿐 아니라 방 전원에게 전송** |
| `RoomListResult` | `[{"id": 1, ...}, ...]` | 방 목록 (배열) |
| `RoomLeaveOk` | `{}` | 퇴장 완료 (나간 본인에게만) |
| `ChatBroadcast` | `{"fromId": 1, "fromName": "alice", "text": "hello"}` | 같은 방 전원에게 채팅 전달 |
| `MatchQueued` | `{"waiting": 1, "needed": 2}` | 대기열 등록됨. 아직 인원이 안 참 |
| `MatchCancelOk` | `{}` | 대기 취소 완료 |
| `MatchFound` | `RoomState`와 같은 모양 | 매칭 성사. 방이 생겼고 보스도 이미 떠 있음 |
| `BossState` | `{"hp": 450, "maxHp": 500, "damage": 50, "attackerId": 1, "attackerName": "alice"}` | 공격 결과. 방 전원에게 |
| `RewardGrant` | `{"currency": 500, "reason": "boss_clear", "balance": 1500}` | 재화 지급. 받는 본인에게만 |
| `LootAuctionStarted` | `{"item": {...}, "highestBid": 0, "highestBidder": 0, "secondsLeft": 30}` | 파티 경매 시작 |
| `LootBidUpdate` | `LootAuctionStarted`와 같은 모양 | 최고가 갱신. 방 전원에게 |
| `LootAuctionClosed` | `{"item": {...}, "result": "sold", "winnerId": 2, "winningBid": 300}` | 마감. `result`는 `sold` / `expired` |
| `InventoryResult` | `{"items": [{"id": 1, "name": "Slime Core"}], "currency": 200}` | 재화 + 아이템. 요청한 본인에게만 |
| `FriendRequestSent` | `{"toId": 2, "toName": "bob"}` | 신청 접수됨. 보낸 본인에게 |
| `FriendRequestReceived` | `{"fromId": 1, "fromName": "alice"}` | 신청 도착. 받는 쪽이 **접속 중일 때만** |
| `FriendAdded` | `{"playerId": 2, "username": "bob", "online": true}` | 친구 성립. **양쪽 모두** |
| `FriendRemoved` | `{"playerId": 2, "username": "bob"}` | 친구 끊김. **양쪽 모두** |
| `FriendRespondOk` | `{"username": "carol", "accepted": false}` | 거절 완료. 거절한 본인에게만 |
| `FriendRemoveOk` | `{"username": "alice", "cancelled": true}` | 신청 취소 완료. 취소한 본인에게만 |

**친구 목록**은 관계를 세 갈래로 나눠 돌려준다.

```json
{"friends":  [{"playerId":2,"username":"bob","online":true}],
 "incoming": [{"playerId":3,"username":"carol","online":false}],
 "outgoing": []}
```

`incoming`은 내가 수락하면 되는 신청, `outgoing`은 상대의 응답을 기다리는 신청이다.
셋 다 없으면 빈 배열이 온다.

거절당했다는 알림은 **보내지 않는다.** 신청자 입장에선 `outgoing`에서 사라질 뿐이다.
신청 취소도 마찬가지로 상대에게 알리지 않는다 — 아직 못 봤을 수도 있으므로.

`RewardGrant`의 `reason`은 셋이다 — `boss_clear`(클리어 보상) / `outbid`(입찰이 밀려서 환불) /
`loot_share`(낙찰금 분배). 지급 경로마다 메시지를 새로 만들지 않고 한 타입을 재사용한다.

**파티 경매**는 보스를 잡으면 자동으로 열린다. 드랍된 아이템을 파티원끼리 30초간 입찰하고,
낙찰자가 아이템을 갖는 대신 **낸 돈은 나머지 파티원이 균등 분배**한다. 아무도 입찰하지 않으면
아이템은 소멸한다.

입찰금은 **거는 즉시 차감**된다(에스크로). 더 높은 값에 밀려나면 그때 환불되고, 마감 시엔
낙찰자에게서 추가로 빼지 않는다. 걸어놓고 그 사이에 돈을 다 써버리는 걸 막기 위함이다.

경매가 열려 있는 동안 방 정보에 `loot` 필드가 실린다:

```json
{"id":1,"state":"cleared","members":[1,2],
 "boss":{"name":"Slime King","maxHp":500,"hp":0},
 "loot":{"item":{"id":1,"name":"Slime Core"},
         "highestBid":300,"highestBidder":2,"secondsLeft":22}}
```

`secondsLeft`는 서버가 계산해서 넣는다. 마감 시각을 그대로 보내면 클라이언트가 자기 시계로
계산해야 하는데, 서버와 시계가 다르면 어긋나기 때문이다.

`LoginOk`에는 `currency`(현재 재화)가 함께 온다. 계정은 연결이 끊겨도 남으므로,
같은 유저네임으로 다시 로그인하면 **같은 `playerId`와 그동안 모은 재화**를 그대로 받는다.

방 정보에는 `state`가 붙는다 — `waiting`(대기) / `boss_fight`(전투 중) / `cleared`(클리어).
`waiting`이 아닐 때만 `boss` 필드가 함께 실린다. 클리어 후 결과 화면을 띄울 수 있도록
`cleared`에도 남긴다.

```json
{"id":1,"name":"Matchmade Room 1","members":[1,2],"state":"boss_fight",
 "boss":{"name":"Slime King","maxHp":500,"hp":500}}
```

보스가 없는 방에 빈 보스를 실어 보내면 클라이언트가 "이름이 비었으면 없는 것"이라는
암묵적 규칙을 따로 알아야 한다. `state`로 먼저 구분하고 있을 때만 싣는다.

공격은 `BossState`로 회신하고, 마지막 일격이면 뒤이어 `RoomState`(`cleared`)와
각자의 `RewardGrant`가 따라온다.

`RoomState`는 요청에 대한 응답이 아니라 **서버가 먼저 미는 메시지**다. 방 생성/참여/퇴장,
그리고 누군가 연결이 끊겼을 때 방에 남은 전원이 받는다. 클라이언트는 자기가 요청하지 않은
메시지도 언제든 도착할 수 있다고 가정해야 한다.

## 프로토콜 — 배틀 (UDP)

**길이-prefix가 없다.** TCP와 반대로 UDP는 메시지 경계를 보장하므로, 보낸 한 덩어리가
그대로 한 덩어리로 도착한다. 대신 **아예 안 올 수 있다.**

모든 패킷은 11바이트 헤더로 시작한다. 네트워크 바이트 순서(빅엔디안)로 직접 찍는다.

```
 0         2         4         6                 10       11
 +---------+---------+---------+-----------------+--------+
 | proto   | seq     | ack     | ack_bits        | type   |
 |  (2)    |  (2)    |  (2)    |      (4)        |  (1)   |
 +---------+---------+---------+-----------------+--------+
```

| 필드 | 뜻 |
|---|---|
| `proto` | 프로토콜 ID(`0x0B47`). 다르면 파싱도 안 하고 버린다 |
| `seq` | 이 패킷의 번호. 65535를 넘으면 0으로 돈다 |
| `ack` | **내가 마지막으로 받은** 상대 패킷 번호 |
| `ack_bits` | 그 이전 32개를 받았는지 비트로. 패킷 하나가 **33개의 수신 여부**를 나른다 |
| `type` | 패킷 종류 |

| type | 값 | 방향 | 설명 |
|---|---|---|---|
| `Handshake` | 1 | 클라 → 서버 | 입장 요청. **세션 없이 받는 유일한 패킷** |
| `HandshakeAck` | 2 | 서버 → 클라 | 세션 생성 완료 |
| `Heartbeat` | 3 | 양방향 | 살아있음. 서버는 받은 걸 되돌려준다 |
| `Input` | 4 | 클라 → 서버 | 이동 방향 2바이트. **위치가 아니다** |
| `Snapshot` | 5 | 서버 → 클라 | 모든 엔티티의 위치. 30Hz, 재전송 없음 |
| `Event` | 6 | 서버 → 클라 | 한 번뿐인 사건. **유일하게 재전송되는 타입** |
| `RoomState` | 7 | 서버 → 클라 | 방 진행 상황. 바뀔 때 + 1초마다 |

**UDP라서 생기는 규칙 넷**

- **아무나 보낼 수 있다.** 프로토콜 ID가 다르거나 11바이트가 안 되면 버린다. 로그도 안 남긴다 —
  포트 스캐너 하나에 로그가 폭발한다.
- **모르는 주소에는 응답하지 않는다.** 출발지 주소를 위조한 패킷에 답하면 **증폭 공격**의
  발판이 된다. 핸드셰이크 응답만 예외인데, 요청 11바이트 : 응답 11바이트라 증폭이 안 된다.
- **핸드셰이크는 멱등하다.** 응답이 유실되면 클라가 다시 보내는데, 그때 새 세션을 만들면
  같은 클라이언트에 세션이 둘 생긴다. 이미 있으면 **응답만 다시** 보낸다.
- **연결 끊김을 감지할 수 없다.** TCP는 소켓이 닫히면 읽기가 에러로 깨어났지만 UDP는
  그냥 안 온다. 1초 하트비트 / 5초 타임아웃으로 **조용한 시간을 재는 것**이 유일한 방법이다.
  타임아웃을 하트비트의 5배로 잡은 건 하트비트도 유실되기 때문 — 1:1이면 패킷 하나에 끊긴다.

**패킷 크기 상한은 1400바이트.** 이더넷 MTU 1500 − IP 20 − UDP 8 = 1472에서 여유를 둔 값이다.
넘으면 IP 단편화가 일어나는데, **조각 하나만 유실돼도 전체가 버려져서** 유실률이 조각 수만큼
곱해진다.

**신뢰성은 타입마다 다르다** (Step 14)

TCP는 전부 재전송하고 순서까지 맞춘다. 게임에서는 그게 손해다. 0.5초 전 위치를 기어코 다시
보내고, **그게 도착할 때까지 뒤 패킷을 전부 막는다.** 최신 위치가 이미 도착해 있는데도.

| 메시지 | 재전송 | 이유 |
|---|---|---|
| `Snapshot` (위치·체력) | ❌ | 30Hz로 계속 오니 **다음 것이 덮는다** |
| `Event` (입장·보스 등장·드랍) | ✅ | **한 번뿐인 사건.** 놓치면 영영 모른다 |
| `Heartbeat` | ❌ | 1초 뒤에 또 온다 |

수신 측은 `ack`(마지막으로 받은 번호)와 `ack_bits`(그 이전 32개)로 **패킷 하나에 33개의
수신 여부**를 실어 보낸다. 그래서 ack 자체를 재전송할 필요가 없다 — 유실돼도 **다음 패킷이
같은 정보를 또 나른다.**

송신 측은 그걸 읽어 자기가 보낸 패킷을 셋으로 나눈다.

| 판정 | 조건 |
|---|---|
| 도착 | `seq == ack` 이거나 `ack_bits`의 해당 비트가 1 |
| **판단 보류** | `seq`가 `ack`보다 최신 — 상대가 아직 못 받았을 뿐일 수 있다 |
| 유실 | `ack`보다 33칸 이상 뒤인데 비트가 0 — 창 밖이라 영영 알 수 없다 |

"판단 보류"를 유실로 치면 **멀쩡한 걸 다시 보낸다.** 세 번째 상태가 꼭 있어야 한다.

유실 판정 경로는 둘이다.

| 경로 | 언제 | 성격 |
|---|---|---|
| `ack_bits` 창 밖 | 상대 `ack`보다 **33칸 이상** 뒤 | 확정 판정. 느리다 (30Hz에서 1.1초) |
| RTO 초과 | 보낸 지 **RTT × 2** 경과 | 조기 재전송. 보통 이쪽이 먼저 걸린다 |

RTT는 `ack`가 돌아온 시각에서 보낸 시각을 빼 재고, 새 표본을 10%만 반영해 평활화한다.
단순 평균은 **옛날 값이 영원히 남아서** 회선이 바뀌어도 한참을 못 따라간다.

**재전송은 새 번호로 나간다.** `seq`는 메시지가 아니라 **패킷의 신분증**이다. 그래서 재전송
사본도 일반 패킷과 똑같이 추적되고, 그것이 또 유실되면 또 걸린다. 특별 취급하는 코드가 없다.

> TCP는 같은 번호를 재사용해서 ack가 원본의 것인지 사본의 것인지 모른다. RTT 측정에서
> 재전송을 제외하는 **Karn's algorithm**이 거기서 필요해진다. 우리는 그 문제 자체가 없다.

**재전송은 5회에서 포기한다.** 상한이 없으면 재전송 사본이 또 RTO를 타고 거기에 매 틱 새
이벤트가 더해져서, 상대가 받지 못하는 동안 **송신량이 틱마다 늘어난다.** UDP에는 혼잡 제어가
없으니 직접 멈춰야 한다. RTT 50ms면 5회가 약 1.5초인데, **타임아웃(5초)보다는 짧아야 한다** —
신뢰성 계층이 타임아웃보다 오래 버티면 이미 없는 상대에게 보내는 꼴이다.

**본문이 붙는 두 패킷** (Step 15)

```
Input (클라 -> 서버, 13바이트)
 +------------------+--------+--------+
 |   헤더 (11)      | move_x | move_y |
 |                  |  int8  |  int8  |
 +------------------+--------+--------+
   -127 ~ 127 을 -1.0 ~ 1.0 으로 읽는다

Snapshot (서버 -> 클라, 11 + 1 + 8N)
 +------------------+-------+-----------------------------+
 |   헤더 (11)      | count |  엔티티 N개                 |
 |                  | uint8 |  id(4) x(2) y(2)  ...       |
 +------------------+-------+-----------------------------+
   좌표는 int16 에 100배로 담는다 (-5000 ~ 5000, 정밀도 0.01)
```

| 좌표 방식 | 엔티티당 | 1400바이트에 |
|---|---|---|
| `float` | 12바이트 | 115개 |
| **`int16` (×100)** | **8바이트** | **173개** |

4인 파티면 둘 다 넉넉하다. 몬스터가 들어오는 Step 16에서 차이가 난다.

**클라이언트는 위치를 보내지 않는다.** `Input`에 담기는 건 방향뿐이고, 속도·충돌·경계는
서버가 계산한다. 위치를 받으면 벽 통과와 순간이동이 공짜가 된다. 로비에서 보스 체력을 서버가
계산하기로 한 것(Step 9)과 같은 판단이다.

**틱은 두 개의 주기로 나뉜다.**

| 주기 | 하는 일 |
|---|---|
| 30Hz (33.3ms) | 이동 적용, 스냅샷 브로드캐스트, 재전송 검사 |
| 1Hz | 연결 타임아웃, 통계 로그 |

타이머는 하나다. 틱 카운터로 나눈다. `expires_after`(지금부터 N 뒤)가 아니라
**`expires_at`(이 시각에)**를 쓰는 것이 핵심 — 전자는 처리 시간이 매번 더해져서
30Hz 에서 1분에 수 초씩 밀린다.

이동은 실제 경과 시간이 아니라 **고정 dt(1/30초)**로 계산한다. 결정적이라 재현이 되고,
클라이언트가 같은 수식으로 예측할 수 있고, 틱 간격을 흔들어 더 움직이는 치트가 막힌다.

**스냅샷은 자를지언정 1400바이트를 넘기지 않는다.** 넘으면 IP 단편화가 일어나고 조각 하나만
유실돼도 전체가 버려진다. 173번째 엔티티가 한 프레임 안 보이는 쪽이 낫다. 무엇을 자를
것인가(관심 영역)는 몬스터가 늘어나는 Step 16의 문제다.

**엔티티와 이벤트** (Step 16)

```
Snapshot payload (11 + 1 + 9N)
 +-------+------------------------------------+
 | count |  엔티티 N개                        |
 | uint8 |  id(4) type(1) x(2) y(2)  ...       |
 +-------+------------------------------------+
   type: 0 = 플레이어, 1 = 몬스터

Event payload (11 + 5)
 +------------------+-------+-----------+
 |   헤더 (11)      | kind  | entity_id |
 |                  | uint8 |   uint32  |
 +------------------+-------+-----------+
   kind: 1 = MonsterDied, 2 = MonsterSpawned
```

타입을 ID 범위(몬스터는 `0x80000000` 이상 등)로 구분할 수도 있었다. 1바이트를 아끼지만
**읽는 사람이 규칙을 알아야 하고 범위를 넘기면 조용히 깨진다.** 타입 필드는 명시적이고,
1400바이트에 154개라 여전히 넉넉하다.

**엔티티 ID는 `World` 가 발급한다.** 연결 ID(`BattleServer` 발급)와는 별개다. 몬스터는 연결이
없으므로 연결 ID 공간을 쓸 수 없는데, 스냅샷에서는 플레이어와 나란히 놓여야 한다. ID가 겹치면
클라이언트가 **몬스터 위치를 플레이어 캐릭터에 그린다.**

| 패킷 | 신뢰 | 본문을 기억하나 |
|---|---|---|
| `Snapshot` | ❌ | ❌ — 다시 안 보낼 것을 기억할 이유가 없다 |
| `Event` | ✅ | ✅ — 재전송하려면 본문도 들고 있어야 한다 |

신뢰 패킷만 본문을 `SentPacket` 에 복사해 둔다. 30Hz 스냅샷(4인 기준 44바이트)을 전부
복사하면 초당 5KB 를 **재전송하지도 않을 것을 위해** 들고 있게 된다. 14c 의 선택적 신뢰성이
메모리까지 절약해 준다.

**이벤트를 스냅샷보다 먼저 보낸다.** 둘 다 같은 송신 버퍼를 쓰므로 순서가 바뀌면 이벤트가
스냅샷 본문을 덮어쓴다. 의미상으로도 "몬스터 7번이 죽었다"가 "7번이 없는 스냅샷"보다 먼저
도착해야 클라이언트가 사망 연출을 할 수 있다. UDP 라 **보장은 아니고 경향**이다.

**전투 규칙**

| | 값 | 왜 |
|---|---|---|
| 플레이어 속도 | 5.0 | 뚫고 지나갈 수 있어야 한다 |
| 몬스터 속도 | 2.0 | 도망은 되지만 포위는 당한다 |
| 사거리 | 8.0 | 조준도 발사도 입력이 아니다 — 들어오면 쏜다 |
| 쿨다운 | 0.5초 | 몬스터(30hp)를 3발에 |

발사체는 없다. **즉시 명중**이다. 총알을 만들면 그것도 엔티티가 되고 속도·수명·명중 판정이
붙는다. 몬스터가 죽는 걸 먼저 봐야 웨이브(Step 17)를 만들 수 있어서 미뤘다.

**던전 진행** (Step 17)

```
RoomState payload (11 + 8)
 +-------+-------+-------+----------+---------+---------+--------------+
 | room  | wave  | phase | monsters | at_door | players | countdown_ms |
 | uint8 | int8  | uint8 |  uint8   |  uint8  |  uint8  |    uint16    |
 +-------+-------+-------+----------+---------+---------+--------------+
   wave = -1 : 아직 시작 전 (uint8 에 0xFF 로 담아 int8 로 읽는다)
   phase: 0 = Fighting, 1 = Cleared, 2 = Transitioning
```

| 규칙 | |
|---|---|
| 방 클리어 | 웨이브 3개 격파 |
| 웨이브 밀도 | `12 + room×4 + wave×4` (방 0 은 12/16/20) |
| 몬스터 체력 | `30 + room×10` |
| 방 전환 | **전원 문 도달 시 즉시**, **과반수면 5초 뒤** |
| 과반수 이탈 | 카운트다운 **취소** |

**아무도 없으면 월드가 흐르지 않는다.** `update_phase` 가 `players_.empty()` 에서 먼저
돌아선다. 이게 없으면 서버가 켜지자마자 빈 방에서 웨이브가 돌고, 파티가 다 나간 방도 계속
CPU 를 먹는다.

**`wave_index` 는 -1 에서 시작한다.** 그래서 "다음 웨이브로 간다"는 규칙 하나가 **시작까지
처리**한다. 0 에서 시작하면 "첫 웨이브는 누가 스폰하나"라는 특수 케이스가 생긴다.
(14a 의 `has_received_any` 와는 반대 상황 — 거기선 0 이 유효한 값이라 sentinel 을 못 썼고,
여기선 음수 웨이브가 존재하지 않아 쓸 수 있다.)

**과반수는 나눗셈 없이 센다.**

```cpp
bool majority = (at_door * 2 > total);   // at_door > total / 2 가 아니다
```

정수 나눗셈은 버림을 한다. `total / 2` 로 써도 결과는 맞지만 **왜 맞는지가 버림에 의존**한다.
양변에 2를 곱하면 조건이 그대로 읽힌다.

**몬스터는 맵 중심이 아니라 파티 주변(반지름 25)에 나온다.** 중심 기준이면 파티가 구석에
있을 때 반대편 몬스터가 80유닛을 걸어오고, **도망이 전략이 아니라 시간 끌기**가 된다.

이 하나로 템포가 바뀌었다.

| | 전 | 후 |
|---|---|---|
| 접근 대기 | 16초 | **6.8초** |
| 웨이브당 마릿수 | 4/5/6 | **12/16/20** |
| 3인 방 클리어 | 108초 | **56초** |

몬스터를 4배로 늘렸는데 시간은 절반이 됐다. **병목이 전투가 아니라 기다림이었다.**

**난이도는 수가 아니라 체력으로 올린다.** 수는 1400바이트 스냅샷(154개)에 묶여 있지만
체력은 스냅샷에 안 실린다. 다만 클라이언트가 체력바를 그리려면 그때는 실려야 하므로,
"지금은 공짜"지 "영원히 공짜"는 아니다.

**`RoomState` 는 바뀔 때 즉시 + 1초마다 보낸다.** 바뀔 때만 보내면 그 한 번을 잃었을 때
복구가 안 되고(비신뢰), 매 틱 보내면 대부분이 같은 내용이다. 둘을 합치면 최악의 경우에도
1초 안에 맞춰진다. 실측으로 **75초에 77개** — 30Hz 로 보냈을 때(2250개)의 3.4%다.

`countdown_ms` 는 **비교에서 뺀다.** 매 틱 33씩 줄어서 비교에 넣으면 항상 "바뀜"이 된다.
클라이언트는 한 번 받으면 자기 시계로 세고, 1초마다 보정받으면 충분하다. Step 10 의
"남은 시간을 깎지 말고 목표 시각을 박아두라"를 네트워크로 옮긴 것이다.

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

## 테스트

Unity 클라이언트가 아직 없어서, PowerShell 스크립트로 임시 클라이언트를 만들어 쓴다.

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
. .\tools\test-client.ps1
```

`Set-ExecutionPolicy`는 현재 창에만 적용되고 창을 닫으면 원복된다.
dot sourcing(`. ` 접두사)으로 불러와야 함수가 현재 세션에 남는다.

| 함수 | 용도 |
|---|---|
| `New-Client` | 연결 하나를 열고 스트림을 돌려줌. 여러 번 불러서 한 창에서 여러 명을 흉내낼 수 있음 |
| `Send-Framed` | 길이-prefix를 붙여 전송 |
| `Read-Framed` | 한 개를 읽음. 올 때까지 블로킹 |
| `Read-Available` | 지금 도착해 있는 것만 전부 읽음. 브로드캐스트 확인용 |
| `Close-Clients` | 열어둔 연결 전부 닫기 |

```powershell
$alice = New-Client
$bob   = New-Client

Send-Framed $alice '{"type":"Login","data":{"username":"alice"}}'
Send-Framed $bob   '{"type":"Login","data":{"username":"bob"}}'
Read-Available $alice
Read-Available $bob

Send-Framed $alice '{"type":"RoomCreate","data":{"name":"Boss Room"}}'
Send-Framed $bob   '{"type":"RoomJoin","data":{"roomId":1}}'
Read-Available $alice     # bob이 들어온 걸 alice도 통보받는다

Send-Framed $alice '{"type":"ChatSend","data":{"text":"bob 왔냐"}}'
Read-Available $bob
```

`Read-Framed`는 메시지가 올 때까지 멈춰 있어서 브로드캐스트 확인에 쓰기 어렵다.
서버가 언제 몇 개를 밀어줄지 모르기 때문에, 도착해 있는 것만 꺼내는 `Read-Available`을 쓴다.

**알려진 한계 — 프레임 동기화가 깨질 수 있다.** `Read-Available`은 `DataAvailable`로 "읽을 게
있나"만 보고 `Read-Framed`에 들어간다. 그런데 TCP는 메시지 경계를 보장하지 않으므로(Step 4),
큰 응답이 여러 세그먼트로 쪼개져 오면 프레임 한가운데서 읽기가 멈출 수 있다. 그 상태로 다음
읽기를 하면 본문 중간의 4바이트를 헤더로 착각해 **이후 전부 어긋난다** — 출력에서 정확히
4바이트씩 사라진 JSON 조각이 나오는 게 그 증상이다.

방어책으로 `New-Client`가 `ReadTimeout`을 걸고, `Read-Framed`가 길이 상한(1MB)을 검사하고,
`Read-Available`이 예외를 만나면 그 연결을 닫는다. **멈추거나 조용히 틀린 결과를 내는 대신
시끄럽게 터지게** 하는 것까지가 목적이다. 서버가 Step 4에서 길이 상한을 둔 것과 같은 이유.

제대로 고치려면 수신 버퍼를 따로 두고 완전한 프레임이 모였을 때만 꺼내야 한다 — 서버가
`async_read`로 하는 일이다. 임시 도구에 그건 과하다고 판단해서, 대신 **응답이 커지는 시나리오
(방이 여러 개인 상태에서 `RoomList` 반복 조회 등)는 피해서 테스트를 짠다.** Unity 클라이언트는
비동기 수신 + 버퍼 구조로 만들 것이므로 이 문제가 없다.

스크립트 파일은 **UTF-8 with BOM**으로 저장해야 한다. Windows PowerShell 5.1은 BOM이 없으면
`.ps1`을 시스템 ANSI 코드페이지(한국어 환경이면 949)로 읽어서, 한글이 들어간 경로나 문자열이 깨진다.

---

# Devlog

<details>
<summary><b>Step 1 — 접속만 받아서 로그 찍기</b></summary>


**Decision:** `boost::asio::ip::tcp::acceptor`로 7777 포트를 열고,
`accept()`(동기/블로킹)로 접속을 하나씩 받아서 로그 찍고 바로 닫는 방식.

**Why:** 비동기(async_accept)부터 시작하면 io_context, 콜백, 소켓 생명주기 같은
개념이 한꺼번에 몰려서 파악하기 어려움. 제일 단순한 "acceptor가 뭔지, 소켓이 뭔지"부터
확인하고 싶어서 블로킹 버전으로 시작.

**Alternatives considered:** 처음부터 async_accept + io_context.run()으로 시작하는 것.
비동기가 결국 필요하긴 하지만(Step 3에서 도입 예정), 첫 걸음부터 개념을 겹쳐 쌓지 않기로 함.

**막혔던 부분:**
- Visual Studio로 폴더를 그냥 열면 vcpkg를 자동으로 못 찾음 →
  `CMakePresets.json`에 `CMAKE_TOOLCHAIN_FILE`을 vcpkg 경로로 직접 명시해서 해결.
- `CMake can not determine linker language` 에러 → `src/main.cpp` 파일을 오타로 찾지 못하는 문제 발생

**검증:** 새 PowerShell 창에서 `Test-NetConnection -ComputerName 127.0.0.1 -Port 7777` 실행 →
`TcpTestSucceeded : True` 확인, 서버 콘솔에 `Client connected: 127.0.0.1:...` 로그 찍힘.

</details>

<details>
<summary><b>Step 2 — echo 서버</b></summary>


**Decision:** 접속을 받은 뒤 바로 끊지 않고, `socket.read_some()`으로 데이터를 읽어서
`boost::asio::write()`로 그대로 돌려보내는 걸 클라이언트가 끊을 때까지(`EOF`) 반복.

**Why:** 소켓으로 실제 데이터를 읽고 쓰는 가장 단순한 형태를 먼저 익히고 싶어서.
아직 비동기는 안 쓰고, Step 1과 마찬가지로 한 번에 한 클라이언트만 처리하는
블로킹 방식 그대로 유지 (비동기는 Step 3에서 도입 예정).

**배운 것:** 서버가 `while(true)`라 스스로 안 끝나기 때문에, 코드 고치고 재빌드한 뒤
꼭 이전에 띄워놓은 서버 프로세스부터 꺼야 함. 안 그러면 새 프로세스가 같은 포트를
못 열어서 `Address already in use` 에러가 남.

**검증:** PowerShell에서 `System.Net.Sockets.TcpClient`로 직접 소켓을 열어서
`"hello server"` 전송 → 그대로 `"hello server"` 돌아옴 확인, 서버 로그에
`Client connected` + 수신 바이트 수 출력 확인.

</details>

<details>
<summary><b>Step 3 — 비동기로 전환 (여러 명 동시 접속)</b></summary>


**Decision:** `acceptor.accept()`(블로킹)로 한 명씩 순서대로 처리하던 구조를,
`async_accept` + `Session` 클래스(`enable_shared_from_this` 상속) 기반으로 전환.
각 연결마다 독립된 `Session` 객체가 자기 소켓/버퍼를 들고, `do_read()` ↔ `do_write()`가
서로를 콜백에서 다시 호출하며 순환하는 구조.

**Why:** 기존 구조로 직접 재현해봄 — 클라이언트 A가 접속만 해놓고 아무것도 안 보내면,
서버가 `acceptor.accept()` → 읽기 대기 루프에 갇혀서 클라이언트 B는 접속은 되어도
서버가 다시 `accept()`를 호출할 때까지 응답을 아예 못 받음(무한 대기). 여러 명을
동시에 다루려면 "한 명 처리 끝날 때까지 다음 사람을 못 받는" 구조 자체를 깨야 했음.

**Alternatives considered:** 연결마다 OS 스레드를 하나씩 새로 띄우는 방식(thread-per-connection).
구현은 더 직관적이지만, 접속자가 늘어날수록 스레드 개수가 그대로 늘어나서 메모리/컨텍스트
스위칭 비용이 커짐. `io_context` 기반 비동기는 스레드 하나로 수천 개 연결도 다룰 수 있어서
장기적으로 더 확장성 있는 선택.

**검증:** PowerShell 두 창에서 각각 `TcpClient`로 동시에 접속 → 서버 로그에 두 접속이
거의 동시에(`Client connected`) 찍힘 → 각자 다른 메시지를 보내고, 서로 막힘 없이
자기 메시지를 정확히 돌려받음 확인.

</details>

<details>
<summary><b>Step 4 — 길이-prefix + JSON 프로토콜</b></summary>


**Decision:** 모든 메시지를 `[4바이트 빅엔디안 길이][UTF-8 JSON 본문]` 형태로 감쌈.
JSON 본문은 `{"type": "...", "data": {...}}` 구조. 읽기는 `async_read_some` 대신
`boost::asio::async_read`(요청한 만큼 다 채울 때까지 반복)로 교체, 쓰기는 `std::deque`
기반 송신 큐를 통해 순서대로 전송.

**Why:** TCP는 바이트 스트림이라 "메시지 경계"를 전혀 보장하지 않음. 직접 확인해봄 —
클라이언트가 3002바이트를 한 번에 보냈는데 서버는 1024/1024/954 세 번에 나눠서 받았음.
JSON을 이런 식으로 받으면 파싱이 깨지므로, 메시지 경계를 우리가 직접 표시해야 했음.

**Alternatives considered:**
- **구분자(delimiter) 방식** (예: 줄바꿈으로 메시지 끝 표시): 구현은 더 간단하지만,
  본문에 그 구분자가 들어가면 깨짐 → 이스케이프 처리가 필요해짐. JSON 문자열 안에
  줄바꿈이 들어갈 수 있어서 부적합.
- **Protobuf**: 더 작고 빠르지만 codegen 단계가 추가됨. 스키마가 계속 바뀌는 초기
  단계에선 눈으로 읽히는 JSON이 디버깅에 유리하다고 판단. 프레이밍(길이-prefix)과
  본문 인코딩을 분리해뒀으니 나중에 본문만 Protobuf로 교체 가능.

**송신 큐를 둔 이유:** 같은 소켓에서 `async_write`가 동시에 두 개 진행되면 바이트가
뒤섞여 스트림이 깨짐. 클라이언트가 메시지 두 개를 한 번에 보내면 서버가 응답 두 개를
연달아 보내려 하므로 실제로 발생 가능한 상황이라, 큐로 직렬화함.

**방어 코드:** 길이 필드를 그대로 믿고 `resize()`하면 악의적 클라이언트가 4GB를
요청해서 메모리를 터뜨릴 수 있음. 1MB 상한 검사 추가.

**검증:**
- framed 메시지 두 개를 **하나의 TCP write로** 붙여서 전송 → 서버가 정확히 두 개로 분리해 처리 확인
- `{"type":"Ping"}` 전송 → `{"data":{},"type":"Pong"}` 수신 (양방향 왕복 성공)

</details>

<details>
<summary><b>Step 5 — 로그인</b></summary>


**Decision:** `Session`이 처음으로 게임 상태(`player_id_`, `username_`, `logged_in_`)를
갖게 됨. `handle_message`를 타입별 라우팅 구조로 바꾸고, **Login만 로그인 전에 허용,
나머지 메시지는 전부 차단**하는 게이팅 추가. 거부 사유는 `{"type":"Error"}` 메시지로 회신.

**Why:** 지금까지 서버는 "누가 보낸 메시지인지" 전혀 몰랐음. 채팅/방/경매 전부 "누가"가
전제되는 기능이라, 그 앞에 신원 확인 단계가 필요했음. Session이 연결마다 독립된 객체라
"이 연결은 누구인가"를 담기에 자연스러운 자리였음.

**Alternatives considered:**
- **플레이어 ID를 로그인 시점에 발급**: 더 자연스럽지만, ID 발급기를 Session 밖
  어딘가(중앙 레지스트리)에 둬야 해서 Session이 바깥을 참조해야 함. 아직 그럴 필요가
  없어서, 일단 접속 시점에 순번을 붙여 생성자로 넘기는 방식으로 단순하게 감.
  → 계정 시스템이나 채팅 브로드캐스트가 들어오는 시점에 중앙 레지스트리로 바뀔 예정.

**테스트 환경을 만듦:** 서버는 만들고 있는데 정작 클라이언트가 없어서, 동작을 확인할
수단 자체가 없었음. PowerShell로 프레이밍(길이-prefix)을 처리하는 임시 클라이언트를 작성.
처음엔 매번 창에 붙여넣다가 반복이 심해져서 `tools/test-client.ps1`로 분리하고,
dot sourcing(`. .\tools\test-client.ps1`)으로 불러 쓰는 방식으로 정리함.

**현재 한계 (의도적):**
- 비밀번호 없음 — 유저네임만 대면 누구든 그 이름으로 로그인됨. 외부 공개 전 반드시 보완 필요.
- 영속성 없음 — 서버 재시작하면 전부 사라짐. `player_id_`도 "계정 번호"가 아니라
  사실상 "접속 순번"에 가까움.

**검증:**
1. 로그인 전 `Ping` → `{"message":"login required"}` 에러 회신 확인
2. `Login {"username":"alice"}` → `{"playerId":1,"username":"alice"}` 회신 확인
3. 로그인 후 `Ping` → `Pong` 정상 회신 확인

</details>

<details>
<summary><b>Step 6 — 방 생성/참여/퇴장</b></summary>

**Decision:** `Server` 클래스를 도입해 공유 상태(방 목록)와 접속 수락을 맡기고,
`Session`은 `Server&`를 참조해 방 요청을 위임. 방은 `Room{id, name, members}` 구조체로,
`Server`가 `unordered_map<uint32_t, Room>`으로 보관.

**Why:** 지금까지 모든 상태는 Session 안에 있었고 그게 맞았음 — 소켓/유저네임/로그인 여부는
전부 "그 연결만의 것"이니까. 그런데 방 목록은 **모두가 공유하는 상태**라 Session 안에 둘 수 없음.
alice의 Session에 방 목록을 넣으면 bob이 그걸 볼 방법이 없음. 그래서 모든 Session 바깥에
중앙 관리자가 필요해졌음.

**순환 참조 문제:** Session은 Server를 알아야 하고(방 요청), Server도 Session을 알아야 함(생성).
C++은 위에서 아래로 읽으므로 둘 다 먼저 쓸 수 없음.
→ `class Server;` 전방 선언으로 Session이 `Server&` 멤버를 갖게 하고, 실제로 Server를 호출하는
함수들은 **클래스 안에선 선언만, 정의는 Server 뒤로** 미뤄서 해결. 참조 멤버는 대상의 내용을
몰라도 선언 가능하다는 점을 이용한 것. 헤더/소스 분리가 필요해지는 이유를 한 파일 안에서
미리 겪은 셈.

**리팩터링 먼저 (6a):** 방 기능을 붙이기 전에 accept 루프를 `Server::do_accept()`로 먼저 옮김.
부수 효과로 `std::function<void()> do_accept` 자기참조 꼼수가 사라짐 — 멤버 함수는 자기 이름을
그대로 호출할 수 있어서. 캡처도 `[&]` → `[this]`로 줄고, `main()`은 5줄이 됨. 이 단계에서 기능은
하나도 바꾸지 않고 Step 5 테스트를 그대로 재실행해 동작이 동일함을 확인한 뒤 6b로 넘어감.

**설계 판단:**
- `room_id_ = 0`을 "어느 방에도 없음"으로 사용. 방 번호를 1부터 발급해 0을 sentinel로 씀.
- 한 번에 한 방만 — 이미 방에 있으면 생성/참여 거부.
- 빈 방은 자동 삭제. 안 그러면 아무도 없는 방이 목록에 계속 쌓임.
- 멤버는 `player_id`만 저장. username까지 보여주려면 Session 목록이 필요한데, 그건
  Step 7(채팅 브로드캐스트)에서 진짜로 필요해질 때 추가 예정.

**소멸자 대신 명시적 정리:** 연결이 끊기면 방에서 빼야 하는데, `~Session()`에서
`server_.leave_room()`을 호출하면 위험. 프로그램 종료 시 Server가 먼저 파괴되고 Session이
나중에 파괴되면 이미 죽은 Server를 참조하게 됨. 그래서 읽기 에러 핸들러에서 `on_disconnect()`를
명시적으로 호출하는 방식으로 함.

**검증:**
1. alice가 `RoomCreate` → `{"id":1,"members":[1],"name":"Boss Room"}` 수신
2. **bob이 `RoomList` → alice가 만든 방이 보임** (공유 상태 동작 확인)
3. bob이 `RoomJoin` → `{"id":1,"members":[1,2]}` — 멤버 2명으로 증가
4. alice 연결 종료 → bob이 `RoomList` → `members:[2]`로 자동 정리됨 확인

</details>

<details>
<summary><b>Step 7 — 채팅 (방 단위 브로드캐스트)</b></summary>

**Decision:** `Server`에 `unordered_map<uint64_t, shared_ptr<Session>> sessions_`(세션 레지스트리)를 두고,
로그인 시점에 등록 / 연결 종료 시점에 제거. `broadcast_to_room()`이 방 멤버의 `player_id`를 돌면서
이 표로 실제 연결을 찾아 전송. 프로토콜에 `ChatSend` → `ChatBroadcast` 추가.

**Why:** 방은 멤버를 `player_id`로만 들고 있다(Step 6의 결정). 그래서 "2번 플레이어에게 보내라"를
실행할 방법이 없었음 — 번호는 알지만 그 번호가 어느 연결인지 모름. 번호와 연결을 잇는 표가
Session 바깥에 필요해졌음. Step 6에서 방 목록 때문에 `Server`를 만든 것과 정확히 같은 이유.

**Alternatives considered:**
- **`Room`이 `Session` 포인터를 직접 보관**: 브로드캐스트만 보면 제일 짧음. 그런데 방 목록 JSON을
  만들 때마다 Session을 끌고 다녀야 하고, 무엇보다 **방 밖으로 보내는 메시지에는 쓸 수 없음**.
  경매 낙찰 알림(Step 10), 친구 요청 도착(Step 11)은 같은 방 사람이 아니다. 레지스트리는 방과
  무관하게 재사용되므로 이쪽을 택함.
- **`weak_ptr`로 보관**: 제거를 깜빡해도 메모리가 새지 않는 안전장치가 됨. 대신 보낼 때마다
  `.lock()`으로 살아있는지 확인해야 하고, 죽은 항목이 표에 계속 남음. Step 6에서 이미 "연결이
  끊긴 시점에 명시적으로 정리한다"를 규칙으로 정했으므로 같은 규칙을 적용.

**소유권 주의:** `sessions_`가 `shared_ptr`로 들고 있으니, `on_disconnect()`에서 빼지 않으면
소켓이 닫혀도 Session 객체가 영원히 안 죽는다. 반대로 `erase`하는 순간 참조 카운트가 0이 되어
자기 자신이 파괴될 수도 있는데, `on_disconnect()`는 async 핸들러 람다 안에서 호출되고 그 람다가
`self`(shared_ptr)를 붙잡고 있어서 함수가 끝날 때까지는 안전하다.

**등록 시점을 로그인으로 잡은 이유:** 접속 시점에 등록하면 이름도 없는 연결이 표에 들어간다.
아직 누구인지 모르는 상대에게 보낼 메시지는 없음. 부수 효과로 `handle_login`이 `Server`를
호출하게 되면서, Step 6에서 만든 "클래스 안엔 선언만, 정의는 Server 뒤" 그룹에 합류했다.

**같이 드러난 버그:** 지금까지 `RoomCreate`/`RoomJoin`의 `RoomState` 응답은 **요청한 본인에게만**
갔음. bob이 들어와도 alice는 아무 통보를 못 받아서, 직접 `RoomList`를 다시 조회해야 멤버가
늘어난 걸 알 수 있었음. 브로드캐스트 수단이 생기자마자 고칠 수 있게 되어, 생성/참여/퇴장은 물론
**연결이 끊긴 경우까지** 방에 남은 전원에게 `RoomState`를 밀어주도록 바꿨다.
나간 본인은 이미 멤버 목록에서 빠져서 브로드캐스트 대상이 아니므로 `RoomLeaveOk`를 따로 회신한다.

**방어 코드:** 빈 문자열 거부, 500자 상한. Step 4에서 프레임 길이에 1MB 상한을 둔 것과 같은 이유로,
클라이언트가 보낸 값을 그대로 믿지 않는다. 채팅은 프레임과 달리 **여러 명에게 복제**되므로
긴 문자열 하나가 인원수만큼 증폭된다.

**테스트 클라이언트를 고쳤다:** 브로드캐스트는 요청 없이 서버가 먼저 보내는 메시지라, 기존
`Read-Framed`(올 때까지 블로킹)로는 확인이 불편했음 → 도착해 있는 것만 꺼내는 `Read-Available` 추가.
그리고 보내는 쪽과 받는 쪽이 동시에 살아있어야 해서, 창을 두 개 띄우는 대신 한 창에서 여러 연결을
만들 수 있게 `New-Client`를 추가했다. 스크립트를 불러오는 순간 연결이 하나 고정으로 생기던 것도 없앴다.

**막혔던 부분:** 빌드가 안 됐는데 원인이 코드가 아니었음. `build/vcpkg_installed` 안의 파일들이
크기·날짜는 그대로인데 **내용이 전부 NUL 바이트**로 날아가 있었음(`boost/asio.hpp` 8,386바이트가 전부 `\0`).
CMake는 `Parse error. Expected a command name, got bad character`를, 컴파일러는 include는 성공하는데
`'boost': 클래스 또는 네임스페이스 이름이 아닙니다`를 냄 — 빈 파일을 읽었으니 당연한 결과.
비정상 종료 때 쓰기 버퍼가 디스크에 안 내려가면 생기는 손상. `build/`를 지우고 재구성해서 해결.
→ 지울 때도 한 번 더 막힘: vcpkg 소스 트리에 260자를 넘는 경로가 있어서 `Remove-Item`이 실패함.
빈 폴더를 `robocopy /MIR`로 덮어씌우는 방식으로 우회.

**현재 한계 (의도적):**
- **로비 채팅 없음** — 방에 있어야만 채팅 가능. 방 밖에서 보내면 `not in a room` 에러.
- **귓속말 없음** — 특정 상대에게 보내는 건 Step 11(친구)에서.
- **`RoomState`의 members는 여전히 player_id만** — 이제 `sessions_`를 뒤지면 username을 채울 수 있지만,
  그러면 "방 정보"를 만드는 데 "연결 정보"가 섞인다. 계정 개념이 생기는 시점에 하는 게 맞다고 판단해 미룸.
- **브로드캐스트할 때마다 JSON을 인원수만큼 직렬화** — `send()`가 각자 `dump()`를 한다.
  인원이 적어서 지금은 문제 없음. 한 번 직렬화해서 프레임을 공유하는 건 필요해지면.

**검증:** alice/bob/carol 세 연결을 한 창에서 띄워서 확인.

1. 로그인 3명 → `playerId` 1, 2, 3 발급
2. alice가 `RoomCreate` → alice만 `RoomState` 수신, bob은 조용함 (방 밖에는 안 감)
3. **bob이 `RoomJoin` → bob과 alice 둘 다** `{"id":1,"members":[1,2],"name":"Boss Room"}` 수신
   — 이전 단계에서는 alice가 아무것도 못 받던 부분
4. alice가 `ChatSend` → 양쪽 모두
   `{"fromId":1,"fromName":"alice","text":"bob 왔냐"}` 수신 (보낸 본인도 포함)
5. bob이 회신 → 양쪽 모두 `{"fromId":2,"fromName":"bob","text":"ㅇㅇ 방금"}` 수신
6. 방에 없는 carol이 `ChatSend` → `{"message":"not in a room"}`, 이때 alice는 아무것도 안 받음
7. 빈 문자열 → `{"message":"text required"}` / 501자 → `{"message":"text too long"}`
8. bob이 `RoomLeave` → bob은 `RoomLeaveOk`, alice는 `members:[1]`로 줄어든 `RoomState` 수신
9. bob이 재입장 후 **소켓을 그냥 닫음** → alice가 `members:[1]` `RoomState` 자동 수신
   (요청 없이 서버가 먼저 밀어준 것)
10. carol이 `RoomList` → `[{"id":1,"members":[1],"name":"Boss Room"}]`

</details>

<details>
<summary><b>Step 8 — 매칭 큐 + 보스 스폰</b></summary>

**Decision:** `Server`에 FIFO 대기열을 두고 2명이 모이면 방을 자동 생성해 전원을 넣고 보스를 스폰한 뒤
`MatchFound`를 보낸다. 그 전에 **방 소속을 `Session`에서 `Server`로 옮기는 리팩터링(8a)을 먼저** 했다.

**Why (8a가 먼저여야 했던 이유):** 지금까지 "내가 어느 방에 있나"는 각 `Session`이 `room_id_`로 들고
있었고, 그걸 바꾸는 건 언제나 자기 자신이었다. 방을 만들거나 참여하는 건 본인이 요청한 일이니까.

매칭은 다르다. **서버가 남을 방에 집어넣는다.** alice와 bob이 매칭되면 Server가 두 사람의 소속을
동시에 바꿔야 하는데, `Server`는 `alice_session->room_id_`를 건드릴 수 없다. private이고, 애초에
"그 연결만의 것"이라는 전제로 거기 둔 값이다.

**중복된 상태였다는 걸 이때 알았다:** 사실 "누가 어느 방에 있나"는 이미 `rooms_[].members`에 들어
있었다. `Session::room_id_`는 그 사실의 **사본**이었고, 지금까지 어긋나지 않은 건 바꾸는 주체가
항상 하나(자기 자신)였기 때문이다. 한 손으로 장부 두 개를 동시에 쓰니 맞을 수밖에 없었다.
매칭은 그 전제를 깬다 — Server가 `members`를 바꿔도 Session의 `room_id_`는 옛날 값으로 남는다.

그래서 사본을 없애고 `Server`에 역방향 색인 하나로 합쳤다.

```cpp
std::unordered_map<uint64_t, uint32_t> player_to_room_;   // 0 = 어느 방에도 없음
```

`members`를 매번 뒤지지 않고 O(1)로 찾기 위한 색인이라 여전히 중복이긴 하다. 다만 성격이 다르다 —
이건 **빨리 찾기 위한 색인**이지 별개의 장부가 아니고, 넣고 빼는 코드가 `create_room` /
`join_room` / `leave_current_room` 세 함수에만 있어서 어긋날 여지를 한 클래스로 좁혔다.

**Alternatives considered:**
- **`Session::set_room_id()` 공개 setter**: 제일 적게 고치는 방법. 그런데 사본이 남는 건 그대로라,
  "Server가 members를 바꿨는데 Session에 통보를 깜빡하는" 버그가 계속 가능하다. 문제를 미루는 쪽.
- **`std::queue`로 대기열**: 이름은 맞지만 못 쓴다. 대기 중 취소·연결 종료 때 **중간에서** 빼야 하고,
  "이미 대기 중인지" 검사하려면 순회가 필요한데 `std::queue`는 둘 다 안 된다. `vector` + erase-remove.

**리팩터링 검증:** 8a를 끝내고 기능은 하나도 안 바꾼 채 **Step 7 테스트를 그대로 재실행**해서
출력이 문자 단위로 동일한 걸 확인한 뒤 8b로 넘어갔다. Step 6에서 쓴 방식 그대로.
매칭과 같이 했다면 테스트가 깨졌을 때 "리팩터링을 잘못한 건가, 매칭 로직이 틀린 건가"를 구분할 수 없었다.

**부수 효과:** Step 7에서 `handle_room_leave`에 썼던 "방 번호를 지역 변수에 미리 복사해두는" 꼼수가
사라졌다. `leave_current_room()`이 **나간 방 번호를 반환**하게 만들었더니 9줄이 5줄이 됐다.

```cpp
uint32_t left_room = server_.leave_current_room(player_id_);
if (left_room == 0) { send_error("not in a room"); return; }
```

"방에 있었나?" 확인과 "어느 방이었나?" 조회가 한 번에 나온다. 값이 한 군데로 모이니 따라온 결과.

**매칭 성사는 Server만 할 수 있다:** `enqueue_for_match()`가 방 생성·소속 변경·보스 스폰·통지를
한꺼번에 한다. 이 안의 한 줄이 Step 8의 전부다.

```cpp
for (uint64_t pid : party) {
    player_to_room_[pid] = id;      // 남의 소속을 바꾸는 코드
}
```

반환값을 `bool`로 둬서 **누가 응답을 보낼지**를 가른다. 성사되면 `MatchFound`가 이미 전원에게
나갔으니 Session은 더 보낼 게 없고, 안 됐으면 Session이 `MatchQueued`를 보낸다.

**설계 판단:**
- **대기 인원 2명** — 테스트에 필요한 최소값. `kPartySize` 상수 하나로 빼두고 `public`에 둬서,
  Session이 `MatchQueued`에 `"needed": 2`를 실을 때 같은 값을 쓴다. 4인으로 바꿔도 클라이언트는 그대로.
- **매칭은 이벤트 기반, 타이머 없음** — N번째 사람이 등록하는 그 순간 성사된다. 서버에 아직
  "시간"이라는 개념이 없는데 매칭도 보스 스폰도 그게 필요 없었다. 타이머가 진짜 필요해지는 건 Step 10.
- **보스 스폰은 매칭방에만** — 직접 만든 방(`RoomCreate`)은 친구끼리 모이는 대기실이고, 매칭방은
  처음부터 전투가 목적이라서. 대기실에서 "보스 도전"을 누르는 흐름이 생기면 그때 경로가 하나 더 붙는다.
- **`BossTemplate`(설계도)과 `Boss`(실물)를 나눔** — 템플릿엔 현재 체력이 없다. `spawn_boss()`가
  최대 체력을 현재 체력에 복사해 실물을 찍어낸다. 보스가 여러 종류가 되면 템플릿만 데이터 파일로 뺀다.
- **`state`를 불리언이 아니라 문자열로** — Step 9에서 `cleared`가 생기면 값이 셋이 된다. 불리언이면
  필드를 더 만들어야 하지만 문자열이면 값만 하나 늘리면 된다.

**대기열에서도 빼야 한다 (직접 재현함):** 8b-3까지 만들고 테스트했더니 이렇게 나왔다.

```
carol 대기 등록  →  {"needed":2,"waiting":1}
carol 연결 종료
dave 등록        →  {"id":2,"members":[3,4],...}   ← MatchFound!
```

dave는 대기 중이어야 하는데 매칭이 터졌다. `members`의 3번이 carol인데 이미 없는 사람이다.
dave는 혼자 있는 방에서 유령과 파티를 맺었고, `player_to_room_[3] = 2`까지 기록돼서
carol이 재접속하면 유령 방에 소속된 채로 시작한다.

`on_disconnect()`에 `cancel_match()` 한 줄을 넣어 막았다. `cancel_match`가 erase-remove라
큐에 없는 번호를 지워도 아무 일이 안 일어나서, 조건 검사 없이 그냥 부를 수 있다.

**눈에 띈 신호:** 이제 `on_disconnect()`가 정리하는 게 셋이다 — 방(Step 6), 세션 레지스트리(Step 7),
매칭 대기열(Step 8). **플레이어 번호를 어딘가 저장할 때마다 여기 한 줄이 늘어난다.**
Step 10 경매(입찰자), Step 11 친구까지 가면 더 길어지고 언젠가 빼먹는다. 아직 세 줄이라 그냥 두지만,
여섯 줄쯤 되면 "연결 끊겼을 때 정리할 것들"을 한 군데로 모으는 구조가 필요해질 것.

**현재 한계 (의도적):**
- **보스는 떠 있기만 한다** — 공격도 체력 감소도 없음. Step 9.
- **큐가 하나뿐** — 난이도·레벨대 구분 없음.
- **대기 타임아웃 없음** — 한 명이 등록하고 아무도 안 오면 영원히 기다린다. 타이머가 생기는 이후에.
- **매칭방도 그냥 방** — 나가면 일반 방처럼 빈 방이 삭제된다. 전투 중 이탈 처리는 없음.

**검증:** alice/bob/carol/dave/eve 다섯 연결로 확인.

1. alice `MatchEnqueue` → `{"needed":2,"waiting":1}` — 아직 혼자
2. alice가 또 등록 → `{"message":"already in queue"}`
3. alice `MatchCancel` → `MatchCancelOk` / 큐에 없는 carol이 취소 → `{"message":"not in queue"}`
4. **alice + bob 등록 → 양쪽 다** `MatchFound` 수신
   `{"boss":{"hp":500,"maxHp":500,"name":"Slime King"},"id":1,"members":[1,2],"name":"Matchmade Room 1","state":"boss_fight"}`
5. 이미 방에 있는 alice가 또 등록 → `{"message":"already in a room"}`
6. 매칭된 방에서 `ChatSend` → bob이 정상 수신 (Step 7 기능이 매칭방에서도 그대로 동작)
7. carol이 `RoomList` → 매칭방이 `state:"boss_fight"` + 보스와 함께 보임
8. **carol이 대기 중 연결 종료 → dave가 등록하니 `waiting:1`** (유령과 매칭되지 않음)
   → 이어서 eve가 등록하니 dave와 eve가 `members:[4,5]`로 매칭됨
9. alice 연결 종료 → bob이 `members:[2]` `RoomState` 수신, 보스 정보는 그대로 유지

</details>

<details>
<summary><b>Step 9 — 보스 공격 + 클리어 보상</b></summary>

**Decision:** `PlayerRegistry`를 도입해 계정 정보(번호·유저네임·재화)를 `Session` 바깥으로 빼고(9a),
`BossAttack`으로 보스 HP를 깎고(9b), 클리어 시 보상을 균등 분배하고(9c),
전투 중 끊긴 사람의 자리를 비워뒀다가 재접속하면 복귀시킨다(9d).

**Why (9a가 먼저여야 했던 이유):** 보스를 잡으면 재화를 줘야 하는데 **줄 곳이 없었다.**
플레이어 정보가 전부 `Session` 안에 있어서 연결이 끊기면 같이 사라졌다. 재화는 그러면 안 된다.
"연결보다 오래 사는 정보"를 담을 자리가 필요해졌고, 그게 Step 5에서 *"계정 시스템이 들어오는 시점에
중앙 레지스트리로 바뀔 예정"*이라고 미뤄둔 바로 그것이었다.

**Alternatives considered:**
- **클라이언트가 데미지를 보내는 방식**: 완성본 참고 구현은 `{"damage": 50}`을 받아 그대로 믿는다.
  그건 이 README 두 번째 문단의 *"서버 권위형... 클라이언트는 요청만 보낸다"* 선언과 정면으로
  모순된다. `BossAttack`의 `data`를 비우고 `kAttackDamage` 상수를 서버가 적용하는 쪽으로 갔다.
- **중복 로그인 시 두 번째 접속 거부**: 구현이 짧다. 그런데 랙으로 끊겼다가 바로 재접속하면
  서버가 아직 옛 연결을 살아있다고 믿는 동안 로그인이 막힌다. **기존 연결을 밀어내는 쪽**을 택했다.
- **`bool cleared`를 하나 더 추가**: `in_battle=true, cleared=true` 같은 **말이 안 되는 조합**이
  표현 가능해진다. 방 상태는 셋 중 정확히 하나이므로 `enum class RoomPhase`로 타입이 강제하게 했다.
  (Step 8에서 "값이 셋이 되면 그때 바꾼다"고 적어둔 지점)

**`player_id`가 접속 순번에서 계정 번호로:** 발급 시점이 `accept` → `login`으로 옮겨갔다.
`PlayerRegistry`가 `username → id` 맵을 들고 있어서, 같은 이름으로 다시 오면 같은 번호를 준다.
부수 효과로 `Session` 생성자에서 `player_id` 인자가 빠지고 `Server::next_player_id_`가 사라졌다.
접속만 하고 로그인 안 한 연결은 이제 번호를 먹지 않는다.

**`kick()`이 소켓만 닫는 이유:** `sessions_.erase()`로 지우면 소켓이 열린 채 남아서, 쫓겨난
클라이언트는 자기가 나간 줄 모르고 방에서도 안 빠진다. 소켓을 닫으면 그 연결의 `async_read`가
에러로 깨어나 `on_disconnect()`가 돌고, Step 6~8에서 쌓아온 정리 코드가 전부 그대로 재사용된다.

**쫓겨난 세션이 새 세션을 지우는 문제 (직접 겪음):** `kick`을 만들자마자 생긴 구멍이다.

```
1. 새 Session B가 로그인 → kick(1) → 옛 Session A의 소켓 닫힘
2. sessions_[1] = B                              ← B로 덮어씀
   ...(비동기로 시간이 지난 뒤)...
3. A의 read가 에러로 깨어남 → A::on_disconnect()
4. A가 unregister_session(1) → sessions_.erase(1)
   ❌ 지워진 건 B다. LoginOk는 받았는데 레지스트리에 없는 유령이 된다.
```

지금까지는 한 `player_id`에 Session이 하나뿐이라 안 터졌다. 계정 번호가 재사용되면서
**같은 키에 두 Session이 겹치는 시간**이 처음 생긴 것. `unregister_session`이 `this`를 받아
**"맵에 있는 게 나일 때만 지우는"** compare-and-delete로 바꿔 막았다.

```cpp
void unregister_session(uint64_t player_id, const Session* who) {
    auto it = sessions_.find(player_id);
    if (it == sessions_.end()) return;
    if (it->second.get() != who) return;   // 이미 새 세션이 자리를 차지함
    sessions_.erase(it);
}
```

`shared_ptr`이 아니라 생포인터로 비교하는 건, `on_disconnect`가 소멸 직전에도 불릴 수 있어서
그 시점의 `shared_from_this()`는 안전하지 않기 때문이다. 주소 비교만 하면 되지 소유권은 필요 없다.

**설계 판단:**
- **`BossState`를 `RoomState`와 따로 둠** — 공격은 초당 여러 번 오는데 그때마다 멤버 목록 전체를
  실어 보내는 건 낭비다. 그리고 "누가 얼마나 때렸는지"는 `RoomState`에 넣을 자리가 없다.
  클리어될 때만 `RoomState`(`cleared`)를 추가로 보낸다.
- **`has_boss()`가 `phase != Waiting`** — 클리어된 방에도 보스 정보가 남아야 결과 화면
  ("슬라임 킹 처치, 0/500")을 띄울 수 있다. `phase == BossFight`로 했으면 클리어하는 순간 사라진다.
- **`Room::attack_boss()`가 `bool` 반환** — `hp == 0`인지 밖에서 검사하면 여러 명이 동시에
  마지막 일격을 날렸을 때 보상이 두 번 나간다. **상태 전이가 일어난 그 한 번만** `true`를 준다.
- **에러를 둘로 나눔** — `no boss in this room`(대기실에서 공격)과 `boss already cleared`
  (막타가 한 박자 빨랐음)는 클라이언트가 다르게 반응해야 한다. 후자는 정상적으로 자주 일어나므로
  에러 팝업이 아니라 결과 화면으로 넘어가면 된다.
- **재화 지급과 알림의 순서** — `grant_currency()`를 먼저 하고 세션을 찾는다. 뒤집으면
  **끊긴 사람이 재화를 못 받는다.** 막타 직전에 나갔어도 파티원이었으면 몫이 있어야 한다.
- **`balance`를 같이 보냄** — 클라이언트가 직접 더하게 두면 한 번 어긋났을 때 영영 틀린다.
  서버 권위형이면 정답을 서버가 준다.

**전투 중 재접속 (9d):** `on_disconnect`에서 방 상태가 `BossFight`이면 **방에서 빼지 않는다.**
`player_to_room_` 색인이 남아 있으면 재접속 시 `room_of()`가 그대로 방 번호를 주고, `members`에도
있으니 보상도 받는다. 로그인 직후 그 사람에게만 `RoomState`를 보내 진행 상황을 알려준다.

되돌릴 수 없는 것(진행 중 전투)만 보호하고, 대기실이나 클리어된 방은 예전대로 정리한다.

**정리할 거리 (기록용):**
- `logged_in_`과 `player_id_ != 0`이 이제 같은 뜻이다. Step 8에서 없앤 사본 패턴과 닮았지만,
  두 값이 같은 함수 안에서 나란히 설정돼 어긋날 수가 없어서 이번엔 두었다.
- `on_disconnect`의 `cancel_match` / `leave_current_room`에도 compare-and-delete가 없다.
  쫓겨난 세션이 새 세션의 방·대기열을 건드릴 수 있는 **아주 좁은 창**이 이론적으로 남아 있다.
- `Session`이 `server_.find_room()`으로 `Room*`을 직접 읽는다. Step 10에서 `Server`의 public
  표면이 더 넓어질 텐데, 그때 조회 함수들을 정리하는 게 나을 듯.

**현재 한계 (의도적):**
- **안 돌아오면 자리가 영영 비어있다** — 보스방에 유령이 낀 채로 남고, 클리어되거나 나머지가
  다 나가기 전까지 방이 안 사라진다. `{"members":[1,2]}`인데 실제로는 1명만 접속 중일 수 있다.
  "끊긴 지 N초 지나면 정리"가 필요한데 서버에 아직 시간 개념이 없다. Step 10의 `steady_timer` 이후.
- **보스가 반격하지 않는다** — 플레이어 HP도 없다. 일방적으로 때리기만 한다.
- **공격 쿨다운 없음** — 메시지를 빨리 보내는 만큼 빨리 깎인다.
- **보상은 재화만** — 아이템/인벤토리는 Step 10(경매)에서.
- **비밀번호 여전히 없음** — 이름만 대면 그 계정이 된다. 이제 **재화가 걸려 있어서** 위험이 커졌다.

**검증:** 네 묶음으로 나눠 확인.

*9a — 계정*
1. 로그인 순서로 번호 발급: alice=1, bob=2
2. **접속만 하고 로그인 안 한 연결이 있어도** carol=3 (번호를 안 먹음)
3. **bob이 끊었다 재접속 → 다시 2번** (계정 번호로 동작)
4. 같은 이름으로 재로그인 → 옛 연결 소켓 닫힘, 새 연결이 `RoomState`·`ChatBroadcast` 정상 수신
   (compare-and-delete가 없었다면 여기서 유령이 됨)

*9b — 공격*
5. 방 밖 공격 → `not in a room` / 대기실 공격 → `no boss in this room`
6. alice 공격 → 양쪽 다 `{"attackerName":"alice","damage":50,"hp":450,"maxHp":500}`
7. 막타 → `BossState`(hp 0) 직후 `RoomState`(`state:"cleared"`) 순서로 수신
8. 클리어된 보스 재공격 → `boss already cleared`
9. `RoomList`에서 클리어된 방도 `boss` 필드 유지 (결과 화면용)

*9c — 보상*
10. 2인 파티 클리어 → 각자 `{"currency":500,"balance":500,"reason":"boss_clear"}`
11. **재접속 → `LoginOk`에 `currency:500`** (연결이 죽어도 재화가 남음)
12. bob이 두 번째 보스도 클리어 → `balance:1000` (계정별 누적)

*9d — 재접속*
13. 전투 중 alice 끊김 → **bob에게 알림 없음**, `RoomList`에 `members:[1,2]` HP 350 그대로
14. **alice 재접속 → `LoginOk` 직후 `RoomState`(hp 350)** 수신
15. 이어서 공격 → 350 → 300, bob에게도 전달
16. 끝까지 잡음 → **끊겼던 alice도 `balance:500`** 수령
17. 대기실에서 dave 끊김 → 예전대로 방에서 빠지고 erin이 `members:[4]` 수신

</details>

<details>
<summary><b>Step 10 — 파티 경매 (보스 드랍 분배)</b></summary>

**Decision:** `steady_timer` 1초 틱을 도입해(10a) 서버에 시간 개념을 들이고, 아이템과 인벤토리를
만들고(10b), 보스 클리어 시 드랍 아이템으로 파티 경매를 열고(10c), 입찰을 받고(10d),
틱이 마감을 처리한다(10e). 낙찰자가 아이템을 갖는 대신 **낸 돈은 나머지 파티원이 균등 분배**한다.

**Why:** 지금까지 서버의 **모든 코드가 "메시지가 오면 무엇을 한다"**였다. 경매는 마감 시각이 있고,
아무도 아무것도 안 보내도 시간이 되면 스스로 낙찰돼야 한다. "시간이 지났다"를 알아차리는
수단이 처음으로 필요해졌다.

**Alternatives considered:**
- **경매마다 `steady_timer`를 하나씩**: ±0초로 정확하다. 그런데 경매가 100개면 타이머 100개고,
  각각 생명주기를 관리해야 한다. **방이 사라질 때 타이머도 취소**해야 하는데 놓치면 죽은 방을
  참조한다. 틱 하나가 전부를 훑으면 타이머가 하나뿐이라 관리가 단순하고, 방이 `rooms_`에서
  빠지면 자동으로 순회 대상에서 제외된다. 1초 오차는 30초 경매에서 문제가 안 된다.
- **마감 때 돈을 깎기**: 1000원을 걸어놓고 그 사이에 다 써버리면 낙찰됐는데 돈이 없다.
  **입찰하는 순간 잡아두고(에스크로) 밀려나면 환불**하는 쪽으로 갔다. 덕분에 마감 로직도
  단순해졌다 — 낙찰자에게서 더 뺄 게 없고 나눠주기만 하면 된다.
- **남은 시간을 틱마다 `-1`씩 깎기**: 틱이 한 번 밀리면 그만큼 어긋난다. **마감 시각을 박아두고
  `now >= ends_at`만 보면** 틱 간격이 흔들려도 마감 시각은 정확하다.

**`steady_clock`이어야 하는 이유:** `system_clock`은 벽시계라 NTP 동기화나 사용자가 시계를
바꾸면 점프한다. 1분 뒤로 밀리면 경매가 1분 더 돌거나 즉시 마감된다. **"얼마나 지났나"를 재는
데는 항상 `steady_clock`**(단조 증가)이다. 대신 날짜로 변환할 수 없어서, 클라이언트에는
마감 시각 대신 서버가 계산한 `secondsLeft`를 보낸다.

**`make_item`이 인벤토리에 안 넣는 이유:** 보스 드랍은 주인 없이 태어난다. 경매가 끝나야 임자가
정해진다. 개체 번호만 발급해서 경매에 걸고, 낙찰되면 `give_item`으로 넣는다.
**유찰 시 "소멸"이 구현상 제일 쉬운 게 이 구조 덕**이다 — 어디에도 안 넣으면 그냥 사라진다.

**`spend_currency`가 `bool`인 이유:** `uint64_t`는 부호 없는 정수라 `500 - 1000`이 음수가 아니라
`18446744073709551116`이 된다. 돈이 없어서 못 사려던 사람이 천문학적 부자가 되는 것.
검사와 차감을 한 함수에 묶어서 밖에서 순서를 틀릴 여지를 없앴다.

**설계 판단:**
- **`RewardGrant`를 세 용도로 재사용** — `boss_clear` / `outbid` / `loot_share`.
  Step 9에서 `reason` 필드를 넣어둔 덕에 환불·분배에 새 메시지 타입을 안 만들었다.
- **`LootAuctionStarted`와 `RoomState`를 둘 다 보냄** — `RoomState`에 `loot`가 이미 실리지만,
  전자는 **사건 알림**(경매 UI를 띄우라는 신호)이고 후자는 **상태 스냅샷**(나중에 조회해도 현재
  상황 파악)이다. Step 9에서 `BossState`와 `RoomState`를 나눈 것과 같은 판단.
- **상태를 다 바꾸고 맨 마지막에 알림** — 클리어 처리에서 보상 분배 → 경매 열기 → 브로드캐스트
  순서다. 중간에 알리면 `loot.active`가 아직 `false`라 `loot` 필드가 빠진 `RoomState`가 나간다.
- **보상 분배를 경매보다 먼저** — 파티원에게 입찰할 돈이 먼저 생겨야 한다.
- **`result`를 문자열로** — `sold` / `expired`. 나중에 `cancelled` 같은 게 생겨도 값만 늘리면 된다.
- **닫기 전에 결과를 복사** — `LootAuction result = room.loot;` 후 닫는다. 정산 중에 방 상태가
  바뀔 여지를 없앤다. Step 8의 `party` 복사와 같은 습관.
- **본인이 자기 입찰을 올리는 건 허용** — 이전 자기 입찰금을 환불받고 새 금액을 건다.
  실제 경매에서도 가능한 동작이고 코드가 자연스럽게 처리한다.

**Step 9의 숙제를 갚았다 (10g):** *"전투 중 끊긴 자리를 비워두는데, 안 돌아오면 영영 비어있다.
시간이 생기면 붙인다"*고 적어둔 것. 이제 틱이 있으니 가능해졌다.

`pending_reconnect_`에 **"이때까지 안 오면 정리" 시각**을 박아두고(경매의 `ends_at`과 같은 방식),
틱이 지난 것을 찾아 방에서 뺀다. 재접속하면 `clear_seat_hold`로 그 예약을 취소한다.
자리를 정리해도 계정은 안 건드리므로 재화·아이템은 그대로다.

**데브로그에 예고한 함정이 실제로 왔다:** Step 10 본문에 *"`on_tick`이 `rooms_`를 순회하는데,
순회 중 컨테이너를 바꾸면 이터레이터가 무효화된다. 그때는 삭제 대상을 먼저 모아두고 순회 후에
지우는 패턴으로 바꿔야 한다"*고 적어뒀었다. 10g가 정확히 그 경우다 — `pending_reconnect_`를
순회하면서 거기서 지워야 하고, `leave_current_room`은 빈 방이 되면 `rooms_`에서 방까지 지운다.
예고한 대로 **먼저 모으고 나중에 처리**하는 두 단계로 썼다.

**stdout이 버퍼링된다는 걸 이때 알았다:** 틱이 도는지 확인하려고 `std::cout << "tick\n"`을 넣고
로그 파일을 봤는데 **0바이트**였다. `std::cout`은 출력 대상이 콘솔이 아니면 전체 버퍼링되고,
`\n`은 줄바꿈 문자일 뿐 flush가 아니다. 서버는 스스로 안 끝나니 강제 종료했고, 그러면 버퍼가
통째로 버려진다. `std::endl`로 바꾸니 보였다.

**Step 12에서 이게 문제가 된다.** systemd로 올리면 stdout이 journald로 파이프되는데, 지금 코드
그대로면 `journalctl`에 최근 로그가 안 보이고 크래시하면 직전 로그가 통째로 사라진다 —
원인 추적이 제일 필요한 순간에. `main()` 첫 줄에 `setvbuf(stdout, nullptr, _IOLBF, 0)`으로
줄 단위 버퍼링을 강제하는 게 로그 서버엔 보통 적절하다. Step 12에서 정리할 것.

**정리할 거리 (기록용):**
- `on_tick`이 `rooms_`를 순회하면서 `close_auction`을 부른다. 지금은 `close_auction`이 `rooms_`를
  안 건드려서 안전하지만, "경매 끝나면 방 삭제" 같은 게 들어오면 **순회 중 컨테이너 변경**으로
  이터레이터가 무효화된다. 그때는 삭제 대상을 먼저 모아두고 순회 후에 지우는 패턴으로 바꿔야 한다.
- 입찰에서 `spend_currency` 성공 후 `place_bid`가 실패하면 돈만 사라진다. 앞에서 이미 검사하니
  실제로 일어나지 않지만 순서상 가능한 구멍이라 되돌리기를 넣어뒀다. **상태가 메모리에만 있고
  단일 스레드라 지금은 이걸로 충분하지만, DB가 붙으면 진짜 트랜잭션이 되어야 할 부분.**

**현재 한계 (의도적):**
- **유저간 경매장은 만들지 않기로 함** — 자기 물건을 올리고 아무나 입찰하는 거래소는 범위 밖.
  경매는 보스 드랍 분배 용도로만 쓴다. 그래서 `take_item`(인벤토리에서 꺼내기)도 만들지 않았다 —
  파티 경매의 아이템은 어느 인벤토리도 거치지 않고 드랍에서 곧바로 경매로 가기 때문.
- **경매 중 이탈 처리 없음** — 입찰해놓고 나가면 돈은 잡힌 채로 남는다.
- **보스 드랍이 항상 하나, 항상 같은 아이템** — `kBossDropName` 상수 하나.
- **끊긴 채로 보상을 받는 경우** — 60초 안에 돌아오지 않아도 그 전에 보스가 클리어되면 몫은 받는다.
  파티원이었으니 맞는 동작이라고 보지만, 알림은 못 받고 재접속해야 잔액으로 확인된다.

**검증:** 두 시나리오로 확인.

*경매 열기 (10c)*
1. 보스 클리어 → `RewardGrant`(500) → `RoomState`(`cleared` + `loot`) → `LootAuctionStarted` 순서
2. **`secondsLeft`가 실시간으로 줄어듦** — 직후 29 → 3초 뒤 25 → 다시 3초 뒤 22
   (틱과 무관하게 조회 시점 기준으로 계산)

*입찰 (10d)*
3. 잔액 초과 → `not enough currency` / `amount:0` → `amount required`
4. alice 100 입찰 → 양쪽에 `LootBidUpdate` `{"highestBid":100,"highestBidder":1}`
5. bob이 더 낮게 50 → `bid too low`
6. **bob이 200 → alice에게 `RewardGrant`(`"reason":"outbid"`, 100 환불) + `LootBidUpdate`**
7. alice 잔액이 `500 - 100 + 100 = 500`으로 정확히 복구됨 (에스크로 검증)
8. 방 밖에서 입찰 → `not in a room`

*마감 (10e)*
9. **bob이 300 걸고 30초 대기 → 아무도 아무것도 안 보냈는데** `LootAuctionClosed`
   `{"result":"sold","winnerId":2,"winningBid":300}` 수신
10. alice에게 `RewardGrant` `{"currency":300,"reason":"loot_share","balance":800}`
11. 잔액: alice `500+300=800`, bob `500-300=200` — **합이 보존됨** (돈이 생기거나 사라지지 않음)
12. **유찰**: 아무도 입찰 안 함 → `{"result":"expired","winnerId":0}`, 아이템은 어느 인벤토리에도
    들어가지 않고 소멸

*인벤토리 (10f)*
13. `InventoryList`로 경매 전 과정을 추적 — 로그인 직후 `currency:0, items:[]` →
    클리어 후 `currency:500, items:[]` → **300 입찰 직후 `currency:200`**(에스크로가 즉시 차감) →
    낙찰 후 `items:[{"id":1,"name":"Slime Core"}]`
14. 반대편도 맞물림 — alice는 `currency:800, items:[]` (돈을 받고 아이템은 못 받음)
15. bob 재접속 → 인벤토리와 재화 그대로 유지

*재접속 타임아웃 (10g)*
16. 전투 중 alice 끊김 → 30초 시점에 `members:[1,2]` 유지 → **재접속하니 `RoomState`(hp 450)**
    → 그 뒤 40초가 더 지나도 여전히 `members:[1,2]` (`clear_seat_hold`가 예약을 취소함)
17. **안 돌아오는 경우**: 25초 시점엔 자리 유지 → **65초 시점에 요청 없이 `RoomState` 푸시**,
    `members:[4]`로 정리됨. 서버 로그에 `Reconnect timeout: player 3 removed from room 2`

*테스트 도구 쪽에서 겪은 것*
18. 시나리오를 이어 붙여 돌리니 PowerShell 클라이언트가 **멈췄다.** 서버는 CPU 0.03%에
    새 연결도 정상 처리하고 있었고, 로그에는 타임아웃 처리가 제대로 찍혀 있었다 — 도구 문제.
    원인은 프레임 동기화 깨짐(위 **테스트** 절 참고). 멈추지 않고 터지게 만드는 데까지만
    손보고, 근본 수정(수신 버퍼)은 Unity 클라이언트 몫으로 남겼다.

</details>

<details>
<summary><b>Step 11 — 친구</b></summary>

**Decision:** 관계를 `Friendship{a, b, requested_by, accepted}` 하나로 표현하고 `PlayerRegistry`가
목록으로 보관한다. `a < b`로 정규화해서 누가 신청했든 같은 자리에서 찾게 하고,
신청·수락·거절·삭제·목록을 그 위에 얹는다.

**Why:** 지금까지 서버가 "사람들"을 담아둔 곳은 셋이었다 — `Room::members`(방 나가면 끝),
`match_queue_`(매칭되면 끝), `sessions_`(끊기면 끝). **전부 "지금 이 순간"의 관계**다.

친구는 다르다. **접속하지 않은 사람과도 유지**되고(→ 연결보다 오래 사는 `PlayerRegistry`에 살아야 함),
**상대가 수락해야 성립**하며(→ 한쪽만 신청한 중간 상태가 존재), **양방향**이다
(→ 한쪽만 갱신하면 "나는 친구인데 쟤는 아닌" 상태가 생김).

**Alternatives considered:**
- **각자 목록을 들고 있기** (`Player`가 `friends` / `incoming` / `outgoing` 벡터를 각각 보관):
  조회가 빠르다. 그런데 같은 사실이 양쪽에 적히므로 **수락 한 번에 네 곳을 고쳐야 한다** —
  신청자의 `outgoing`에서 빼고, 수신자의 `incoming`에서 빼고, 양쪽 `friends`에 넣기.
  하나라도 빠지면 어긋난다. Step 8에서 `Session::room_id_` 사본을 없앤 것과 같은 판단으로,
  **사실을 한 군데만 두는 쪽**을 택했다. 그 결과 수락이 `f->accepted = true;` **한 줄**이 됐다.
- **플레이어 번호로 신청**(`{"playerId": 3}`): 같은 방에서 만난 사람에게 걸 땐 편하다.
  그런데 번호를 알 경로가 제한적이라 **유저네임**으로 갔다. 이름이 곧 계정이므로(Step 9)
  `find_by_name` 하나로 찾을 수 있다.

**`a < b` 정규화:** alice(1)가 bob(2)에게 걸든 반대든 저장은 항상 `{a:1, b:2}`다.
이게 없으면 찾을 때마다 `(f.a==x && f.b==y) || (f.a==y && f.b==x)`를 확인해야 하고,
무엇보다 **같은 관계가 `{1,2}`와 `{2,1}` 두 개로 들어가는 실수**가 가능해진다.

**`requested_by`를 따로 저장하는 이유:** 정규화하면서 "누가 걸었는지"가 사라진다.
이 값이 두 곳에서 일한다.
- **자기 신청을 자기가 수락하는 걸 막는다.** 없으면 누구에게나 신청을 걸고 바로 수락해서
  **일방적으로 친구가 될 수 있다.**
- **양쪽이 동시에 신청한 상황**을 구분해서 안내한다. `"이미 관계가 있음"`이라고만 하면
  사용자는 뭘 해야 할지 모른다. `"they already sent you a request"`로 **수락하면 된다고**
  알려준다. 에러를 `already friends` / `already requested` / `they already sent you a request`
  셋으로 나눈 게 그래서다.

**`find_by_name`이 `login`과 다른 점:** `login`은 없으면 계정을 만든다(Step 9).
그걸 재사용하면 `"asdfasdf"`에게 친구 신청했을 때 **빈 계정이 생긴다.** 없으면 `nullptr`을
주는 함수를 따로 뒀다. 반환 타입도 다르다 — `login`은 항상 성공하니 참조, 이건 포인터.

**설계 판단:**
- **목록은 한 번의 순회로 셋을 나눈다** — `accepted`면 친구, 아니면서 `requested_by == me`면
  보낸 신청, 그 외는 받은 신청. 관계 하나에서 세 갈래가 파생된다.
- **`online`이 두 번째 쓸모를 찾았다** — Step 9에서 중복 로그인 판정용으로 만든 플래그가
  이제 친구 목록에 실려 "접속 중" 표시가 된다.
- **수락·삭제는 양쪽에 알린다** — 관계는 둘의 것이다. 한쪽만 알면 상대는 아직 친구라고
  믿고 있다가 나중에 엉뚱한 곳에서 알게 된다.
- **거절과 신청 취소는 알리지 않는다** — 거절당했다는 알림은 기분 나쁘고, 취소는 상대가
  아직 못 봤을 수도 있다. 신청자 입장에선 목록에서 사라질 뿐이다.
- **삭제와 취소를 한 메시지로** — `accepted` 여부로 갈라진다. 둘 다 "그 관계를 없앤다"라서
  `FriendCancel`을 따로 만들지 않았다. 클라이언트도 목록의 X 버튼 하나면 된다.
- **거절은 상태가 아니라 삭제** — `declined`를 따로 두지 않고 관계를 지운다. 없던 일이
  되므로 다시 신청할 수 있다. "거절당하면 못 건다"는 스팸 방지 요구가 생기면 그때 만든다.

**성능은 나중에:** `find_friendship`이 전체를 훑는다. 관계가 10만 개면 느리다.
지금은 몇 개 안 되고 **어긋나지 않는 게 더 중요**해서 그대로 뒀다. 느려지면
`unordered_map<uint64_t, vector<size_t>>` 색인을 옆에 두면 되는데, Step 8의 `player_to_room_`처럼
**필요해질 때** 하면 된다.

**막혔던 부분:** `handle_friend_remove`를 선언만 하고 정의를 빼먹어서 `LNK2019` 링커 에러.
컴파일은 통과한다 — 컴파일러는 선언을 보고 "어딘가 정의가 있겠지" 하고 넘어가고,
링커가 실제로 찾을 때 없다는 걸 발견하기 때문. Step 6에서 만든 "클래스 안엔 선언만,
정의는 뒤로" 패턴의 대가다. 에러 메시지가 길고 읽기 어려운 건 이름 장식(name mangling)
때문인데(`nlohmann::json`의 실제 타입이 템플릿 인자 11개짜리다), **맨 앞의
`Session::handle_friend_remove`만 보면 된다.**

**현재 한계 (의도적):**
- **귓속말 없음** — 친구가 생겼으니 다음 수순인데, 방 단위 채팅(Step 7)과 전달 경로가
  같아서 새로 배울 게 적다고 판단했다.
- **친구 초대 없음** — "내 방으로 오라"는 매칭과 방 기능을 엮어야 한다.
- **차단 없음** — 스팸 신청을 막을 방법이 없다.
- **친구 수 제한 없음** — `friendships_`가 무한히 늘어난다.
- **접속/종료 알림 없음** — 친구가 접속해도 모른다. `FriendList`를 다시 조회해야 안다.
  `online`이 바뀔 때 친구들에게 밀어주는 게 자연스러운 다음 단계.

**검증:** alice/bob/carol/dave 네 연결로 확인.

*신청 (11b)*
1. 없는 유저 → `no such player` / 자기 자신 → `cannot add yourself`
2. alice → bob 신청 → alice는 `FriendRequestSent`, bob은 `FriendRequestReceived`
3. 같은 신청 반복 → `already requested`
4. **bob이 반대로 신청 → `they already sent you a request`** (수락하라는 안내)

*수락·거절 (11c)*
5. **alice가 자기 신청을 수락 시도 → `you sent this request`** (`requested_by` 검사)
6. bob이 수락 → **양쪽 다** `FriendAdded` 수신
7. 이미 친구인데 또 신청 → `already friends`
8. carol → alice 신청 후 alice가 거절 → alice만 `FriendRespondOk`, **carol에겐 알림 없음**
9. 거절 후 다시 신청 → 정상 접수 (거절이 상태로 남지 않음)

*목록 (11d)*
10. 관계 없을 때 → `{"friends":[],"incoming":[],"outgoing":[]}`
11. **같은 관계가 양쪽에서 다르게 보임** — alice는 `outgoing`, bob은 `incoming`
12. 수락 후 → 양쪽 다 `friends`로 이동
13. bob 접속 종료 → alice 목록에서 `online:false`
14. **오프라인 dave에게 신청 → dave가 재접속하니 `incoming`에 남아 있음**
    (지금까지 모든 알림은 "접속 중인 사람에게 지금"이었는데, 친구는 나중에 확인이 기본)

*삭제 (11e)*
15. 관계 없는 사람 삭제 → `not friends`
16. 친구 삭제 → **양쪽 다** `FriendRemoved`, 양쪽 목록에서 사라짐
17. 삭제 후 다시 친구 맺기 → 정상
18. **수락 전 신청 취소** → 취소한 본인만 `FriendRemoveOk{cancelled:true}`, 상대는 조용히
    `incoming`에서 사라짐
19. 오프라인 친구 삭제 → 터지지 않음 (세션이 없으면 알림만 생략)

</details>

<details>
<summary><b>Step 12 — 도커 배포</b></summary>

**Decision:** 도커로 배포한다. 멀티 스테이지 Dockerfile로 이미지를 만들고
`restart: unless-stopped`로 상시 구동한다. 그 전에 **서버가 SIGTERM으로 정상 종료되도록** 고치고,
로그가 버퍼에 갇히지 않게 하고, 포트를 환경변수로 받게 했다.

**Why:** 개발 중엔 창을 닫으면 그만이었는데 서버는 그러면 안 된다. 지금까지 미뤄둔 것들이
배포에서 한꺼번에 청구된다.

| 문제 | 개발 중엔 | 배포하면 |
|---|---|---|
| `io.run()`이 반환 안 함 | 강제 종료하면 됨 | `docker stop`이 10초 기다렸다 SIGKILL |
| stdout 전체 버퍼링 | 불편한 정도 | 크래시하면 직전 로그가 통째로 사라짐 |
| 포트 하드코딩 | 재컴파일하면 됨 | 이미지를 다시 만들어야 함 |
| 재시작 없음 | 다시 띄우면 됨 | 새벽에 죽으면 아침까지 멈춰 있음 |

**Alternatives considered:**
- **systemd 유닛 + apt로 의존성**: 원래 계획이었다. 도커로 바꾼 이유는 **의존성이 이미지에
  고정**되기 때문. systemd 방식은 "그 파이에서만 되는" 상태가 되기 쉽고, OS를 다시 깔면
  설치 과정을 되짚어야 한다. 그리고 `restart: unless-stopped` 한 줄이 `Restart=always`를 대신한다.
- **vcpkg를 리눅스에서도**: Windows와 같은 Boost 버전을 쓸 수 있다. 그런데 ARM에서 boost를
  소스 빌드하면 몇 시간이 걸리고 메모리가 모자라 실패하기도 한다. apt는 몇 분이다.
- **PC에서 크로스 빌드**(`docker buildx --platform linux/arm64`): 파이의 빌드 시간을 아낀다.
  그런데 `main.cpp` 하나짜리라 파이에서 직접 빌드해도 몇 분이다. 레지스트리에 올리거나
  이미지 파일로 옮기는 수고가 더 크다.

**PID 1과 시그널 — 도커의 함정:**

```dockerfile
CMD ./game_server          # 쉘 형식: /bin/sh 가 PID 1
CMD ["./game_server"]      # exec 형식: 서버가 PID 1
```

`docker stop`의 SIGTERM은 **PID 1에게만** 간다. 쉘 형식으로 쓰면 `sh`가 PID 1이 되고 서버는
그 자식이 되는데, `sh`는 시그널을 자식에게 전달하지 않는다. 결국 10초 뒤 SIGKILL로 죽고
**정성껏 만든 종료 처리가 통째로 무용지물**이 된다. 게다가 강제 종료되면 로그 버퍼가 날아간다.

**`io.run()`을 끝내는 게 생각보다 어려웠다:** asio는 **할 일이 하나라도 남으면 계속 돈다.**
`stop()`을 만들고도 세 번을 더 고쳤다.

1. **`sessions_`에는 로그인한 연결만 있다** (Step 9의 결정). 접속만 하고 로그인 안 한 소켓은
   `Server`가 아예 모른다 — `do_accept`에서 만들고 `shared_ptr`을 놓아버리니까.
   그 연결의 읽기 대기가 남아서 `io.run()`이 안 끝났다.
   → `std::vector<std::weak_ptr<Session>> all_sessions_`를 추가했다.
2. **`do_accept`가 에러여도 자기를 다시 걸었다.** Step 3에서 만든 코드다.

   ```cpp
   if (!ec) { ... }
   do_accept();        // ← 에러여도 무조건
   ```

   `acceptor_.close()`를 하면 에러와 함께 콜백이 불리는데, 닫힌 acceptor에 또 걸고 또 실패하고를
   무한 반복한다. **Step 10에서 타이머엔 `if (ec) return`을 넣었으면서 acceptor엔 없었다** —
   그때는 서버를 멈출 일이 없어서 필요가 없었고, 그대로 열 스텝을 지나왔다.
3. 위 둘을 고치고 나서야 `Stopped cleanly`가 찍혔다.

**`weak_ptr`을 쓴 이유 (Step 7과 반대 선택):**

| | `sessions_` (Step 7) | `all_sessions_` (지금) |
|---|---|---|
| 담는 것 | `shared_ptr` | `weak_ptr` |
| 목적 | 번호로 **찾아서 보내기** | 종료 시 **전부 닫기** |
| 죽은 항목이 남으면 | 못 보냄 (정확해야 함) | 무해 (닫을 게 없을 뿐) |
| 정리 | `on_disconnect`에서 즉시 | 틱에서 `expired()` 걷어냄 |

Step 8 데브로그에 *"플레이어 번호를 어딘가 저장할 때마다 `on_disconnect`에 한 줄이 늘어난다"*고
적어뒀는데, **`weak_ptr` 덕에 이번엔 안 늘어났다.** 연결이 죽으면 알아서 `expired()`가 된다.

**Step 10의 조언을 정정한다:** 그때 로그 버퍼링 해결책으로
`setvbuf(stdout, nullptr, _IOLBF, 0)`을 적어뒀는데 **틀렸다.** MSVC에서 두 가지로 깨진다.
- `size`가 2 이상이어야 한다. 0을 주면 잘못된 인자로 판정돼 **디버그 빌드에서 프로세스가 abort한다.**
  최소 예제로 재현했고 종료 코드 3이 나왔다.
- 설령 통과해도 소용없다. MSVC 문서상 **`_IOLBF`는 `_IOFBF`와 동일하게** 처리된다.
  Windows에는 줄 단위 버퍼링이 없다.

리눅스(glibc)에서는 `buf`가 `NULL`이면 `size`를 무시해서 잘 돈다. **리눅스 기준으로 쓴 코드를
Windows에서 먼저 돌린 게 실수였다.** `std::cout << std::unitbuf;`로 바꿨다 — 이식성 있고,
`main()` 첫 줄에 한 번만 쓰면 되고, 이 프로젝트는 로그를 전부 `std::cout`으로 내보낸다.

**라즈베리파이 OS의 Boost 1.74 버그:** 도커 빌드가 실패했다.

```
/usr/include/boost/asio/awaitable.hpp:68: error: 'exchange' is not a member of 'std'
```

Debian Bookworm이 주는 Boost 1.74에서 `awaitable.hpp`가 `std::exchange`를 쓰면서 `<utility>`를
include하지 않은 버그다. `<boost/asio.hpp>`는 코루틴 헤더까지 전부 끌어오므로, 코루틴을 안 써도
컴파일된다. Windows는 vcpkg가 Boost 1.92를 주니 안 걸렸다.

`main.cpp`에서 `<boost/asio.hpp>`보다 **먼저** `<utility>`를 include해서 우회했다.
**라즈베리파이 OS도 Bookworm 기반이라 거기서도 똑같이 실패했을 것** — 도커가 아니었으면
파이에 올린 뒤에야 알았을 문제다. 베이스 이미지를 trixie로 올리는 것보다,
**실제 배포 대상과 같은 환경에서 되게** 만드는 쪽을 택했다.

**설계 판단:**
- **잘못된 포트면 시작하지 않는다** — `GAME_SERVER_PORT=abc`를 조용히 무시하고 7777로 뜨면,
  운영자는 8080에서 듣는다고 믿는데 실제로는 7777이다. 연결이 안 되는 이유를 한참 찾는다.
- **포트 출처를 로그에 찍는다** — `Port 7777 (from GAME_SERVER_PORT)`. 환경변수를 설정했는데
  `from default`가 보이면 전달이 안 된 것. 컨테이너에서 흔한 실수다.
- **`atoi`가 아니라 `strtol`** — `atoi("80x")`는 80을 돌려주고 뒤의 쓰레기를 조용히 무시한다.
  `strtol`은 `end` 포인터로 어디까지 읽었는지 알려줘서 검증이 가능하다.
- **`resolve_port`가 `std::optional` 반환** — Step 10에서 설명만 하고 쓸 자리가 없었던 그것.
  `bool` + 출력 파라미터보다 **시그니처만 봐도 뭐가 결과인지** 보이고, 실패했을 때
  출력 파라미터가 어떤 상태인지 고민할 필요가 없다.
- **멀티 스테이지 빌드** — Boost.Asio와 nlohmann-json이 둘 다 헤더 전용이라
  **런타임에는 `libstdc++`만 있으면 된다.** 컴파일러와 헤더를 뺀 최종 이미지는 114MB.
- **root로 안 돌린다** — 7777은 1024 이상이라 일반 사용자도 열 수 있다. 낮은 포트를 쓰려면
  추가 권한이 필요한데, 그래서도 높은 포트가 낫다.
- **`unless-stopped`이지 `always`가 아니다** — `always`는 수동으로 멈춰도 재부팅 시 살아난다.
  점검하려고 내렸는데 다시 뜨면 곤란하다.

**정리할 거리 (기록용):**
- 코드 곳곳의 `std::endl`은 이제 `'\n'`으로 되돌려도 된다. `unitbuf`가 전역으로 처리한다.
- `do_accept`의 `if (ec) return`은 일시적 에러(파일 디스크립터 부족 등)에도 accept를 멈춘다.
  엄밀히는 `operation_aborted`일 때만 멈춰야 하지만, 타이머와 같은 규칙으로 두는 쪽을 택했다.
- `stop()`에서 `sessions_.clear()`는 `all_sessions_`와 중복이다. 종료 후 상태를 명확히 하려고 남겼다.

**현재 한계 (의도적):**
- **영속성 없음** — 컨테이너를 내리면 계정·재화·아이템·친구가 전부 사라진다. 볼륨에 파일로
  저장하거나 DB를 붙여야 하는데, 이 프로젝트 범위 밖이다.
- **비밀번호 없음** — Step 5부터의 한계. **이제 인터넷에 노출되므로 위험이 실질적이다.**
  외부 공개 전 반드시 보완해야 한다.
- **TLS 없음** — 평문 TCP다. 로그인 이름이 그대로 흐른다.
- **헬스체크 없음** — 도커가 "프로세스가 살아있나"만 본다. 데드락에 빠져도 살아있다고 판단한다.
- **로그 로테이션 없음** — 도커 기본 json-file 드라이버는 무한히 쌓인다. SD카드가 찬다.
  `logging: options: max-size` 설정이 필요하다.

**검증:** x86_64 도커에서 확인 (파이의 arm64는 사용자가 배포하며 확인).

1. 이미지 빌드 성공 — 컴파일 15초, 최종 114MB
2. `docker compose up -d` → `Port 7777 (from GAME_SERVER_PORT)` + `Listening on port 7777...`이
   **즉시** 로그에 보임 (`unitbuf` 동작 확인. 안 그러면 버퍼에 갇혀 안 보인다)
3. `docker compose ps`의 `COMMAND`가 `"./game_server"` — exec 형식이라 서버가 PID 1
4. 컨테이너 안에서 로그인 → 매칭 → 보스 스폰까지 정상 동작
5. **연결 3개(1개는 미로그인)가 붙은 상태에서 `docker compose stop`**
   - `Signal 15 received` → `Shutting down...` → **`Stopped cleanly`**
   - **소요 1초** (30초 유예를 안 썼다 = SIGKILL이 없었다)
   - **종료 코드 0** (137이면 SIGKILL 당한 것)
   - 마지막 세 줄이 로그에 **남아 있다** (강제 종료였다면 버퍼째 날아갔다)

</details>

<details>
<summary><b>Step 13 — UDP 배틀 서버: 연결 계층</b></summary>

**Decision:** 전투용 서버를 `battle_server/`에 별도 CMake 프로젝트로 만든다. UDP 소켓 하나로
모든 클라이언트를 받고(13a), 11바이트 바이너리 헤더를 정의하고(13b), `endpoint → Connection`
맵과 핸드셰이크를 붙이고(13c), 하트비트/타임아웃으로 죽은 세션을 정리한다(13d).

**Why:** **TCP가 너무 잘해줘서** 전투에 못 쓴다. 패킷이 유실되면 재전송될 때까지 그 뒤 데이터를
전부 붙잡아두는데(head-of-line blocking), 초당 30번 위치를 보내는 상황에서 0.03초 전 위치를
기다리느라 지금 위치를 못 받는 건 말이 안 된다. **낡은 위치는 버리고 최신 것만 쓰면 된다.**
TCP는 "모든 바이트가 중요하다"를 전제하는데, 실시간 게임은 그 전제가 틀린 영역이다.

**Alternatives considered:**
- **로비 서버에 전투를 얹기**: 코드와 세션을 공유할 수 있다. 그런데 위의 이유로 프로토콜 자체가
  안 맞고, 무엇보다 30Hz 루프가 로비의 이벤트 기반 구조와 섞인다. 실행 파일을 나눴다.
- **길이-prefix 프레이밍을 UDP에도**: Step 4에서 만든 걸 재사용하고 싶었는데 **필요가 없다.**
  UDP는 메시지 경계를 보장한다 — 보낸 한 덩어리가 그대로 한 덩어리로 온다. TCP와 정반대다.
- **JSON 유지**: 디버깅이 편하다. 그런데 `"seq":12345`가 12바이트인데 `uint16`은 2바이트다.
  초당 120패킷 × 10배 오버헤드, 그리고 1400바이트 안에 몬스터 수백 마리를 넣어야 한다.

**로비에서 배운 것을 처음부터 적용했다:** `if (ec) return`(Step 12), `std::cout << std::unitbuf`
(Step 10), 포트를 환경변수로(Step 12), 시그널 정상 종료(Step 12), `<utility>` 우회(Step 12).
**로비는 이걸 12스텝에 걸쳐 하나씩 배웠는데 배틀 서버는 첫 커밋부터 다 들어간다.**
로비 Step 1~5에 해당하는 구간을 하루에 지난 이유다.

**파일을 처음부터 나눴고, 그 때문에 생긴 문제를 바로 만났다:** 도커 빌드가 Boost 1.74
`awaitable.hpp` 버그로 실패했다(Step 12에서 겪은 그것). 로비에서는 `main.cpp`에 `<utility>`를
넣어 해결했는데 여기선 안 통했다 — **각 `.cpp`는 독립적으로 컴파일된다.** `BattleServer.cpp`는
`BattleServer.h`만 보고, 그 헤더가 `<boost/asio.hpp>`를 끌어온다. 우회 코드가 **헤더 안에**
있어야 두 파일 모두에 적용된다. 파일이 하나였으면 못 겪었을 문제다.

**TCP와 구조적으로 다른 것들**

- **`accept`가 없다.** 소켓 하나가 모든 클라이언트를 상대하고, `async_receive_from`이 데이터와
  보낸 쪽 주소를 함께 채워준다. TCP는 연결마다 객체가 생겨서 **그 존재 자체가 신원**이었는데,
  UDP는 `(IP, 포트)` 쌍이 유일한 식별자다. `Connection`은 소켓을 갖지 않는다.
- **`udp::endpoint`에 표준 해시가 없다.** `unordered_map`의 키로 쓰려면 `EndpointHash`를 직접
  만들어야 한다. TCP에선 생각할 일도 없던 코드.
- **버퍼가 하나면 된다.** TCP는 `Session`마다 버퍼가 있었다(연결이 동시에 여러 개). UDP는
  단일 스레드에서 `handle_packet`이 끝난 뒤에야 다음 `do_receive`를 걸므로 겹칠 수 없다.
  대신 **송신용은 따로 뒀다** — 같은 버퍼를 쓰면 아직 읽고 있는 데이터를 덮어쓴다.
- **아무나 보낼 수 있다.** TCP는 연결을 맺어야 데이터가 온다. UDP 포트는 열려 있으면 누구나
  쏜다. 프로토콜 ID로 거르고, 11바이트 미만은 버리고(없으면 `read_u16(buf+10)`이 버퍼 밖을
  읽는다), **잘못된 패킷에 로그를 안 남긴다**(포트 스캐너 하나에 로그가 폭발한다).
- **모르는 주소에 응답하지 않는다.** 출발지를 위조한 패킷에 답하면 **증폭 공격**의 발판이 된다
  (DNS·NTP가 실제로 이렇게 악용된다). TCP는 3-way 핸드셰이크가 이걸 막아주는데 UDP는 없다.
- **멱등성이 필요하다.** 핸드셰이크 응답이 유실되면 클라가 다시 보내는데, 새 세션을 만들면
  같은 클라이언트에 세션이 둘 생긴다. TCP는 재전송을 커널이 처리해서 **애플리케이션이 이
  문제를 못 본다.**
- **연결 끊김이라는 사건이 없다.** TCP는 소켓이 닫히면 읽기가 에러로 깨어나 `on_disconnect()`가
  돌았다. UDP는 그냥 안 온다. **조용한 시간을 재는 것 말고는 방법이 없다** — 13d가 통째로
  이것 때문에 존재한다.

**설계 판단:**
- **`sizeof(PacketHeader)`를 쓰지 않는다** — 정렬 때문에 패딩이 들어가 실제로는 12바이트다
  (`uint32_t ack_bits`가 4바이트 경계에 맞춰진다). **메모리 레이아웃과 전송 형식은 별개**라
  상수 11을 쓰고 `write_header`/`read_header`가 변환한다. `#pragma pack(1)`로 통째로 보내는
  방법도 있지만 엔디언 문제가 그대로 남고 컴파일러마다 동작이 다르다.
- **엔디언을 직접 맞춘다** — `memcpy`로 보내면 PC에서 보낸 걸 라즈베리파이가 뒤집어 읽을 수
  있다. 로비의 길이-prefix와 같은 규칙(빅엔디안).
- **`ack`/`ack_bits`는 자리만 만들어뒀다** — Step 14에서 신뢰성 계층의 핵심이 된다. 지금은
  `ack`에 "마지막으로 받은 상대 번호"만 채우고 `ack_bits`는 0이다.
- **타임아웃이 하트비트의 5배** — 하트비트도 유실된다. 1:1이면 패킷 하나에 멀쩡한 클라이언트가
  끊긴다. 5초면 연속 4개가 유실돼도 버틴다. 로비 Step 12의 재접속 유예(60초)와 같은 저울질.
- **서버는 하트비트를 먼저 보내지 않는다** — 받은 걸 되돌려준다. 먼저 보내면 연결 수만큼
  패킷이 늘어난다. 전투 중엔 **아무 패킷이나 보내면** `last_seen`이 갱신되므로 거의 안 나간다.
- **`send_to`는 동기** — UDP 송신은 커널 버퍼에 넣고 바로 반환한다. TCP처럼 상대를 기다리지
  않으므로 `async_send_to`의 완료 핸들러·버퍼 수명 관리를 살 이유가 없다.
- **포트 7778** — TCP 7777과 프로토콜이 달라 겹쳐도 충돌하지 않지만 헷갈리지 않게 나눴다.

**막혔던 부분:**
- 소스 폴더를 `scr`로 만들어 CMake가 못 찾음. Step 1의 `maim.cpp`와 같은 종류.
- `vcpkg.json`이 없어 Boost를 못 찾음 — **vcpkg는 프로젝트마다 매니페스트가 필요하다.**
  로비 것을 쓰지 않는다.
- 첫 실패 configure가 남긴 캐시 때문에 vcpkg가 안 돌아서, `build/`를 지우고 재구성해야 했다.
- `steady_timer`는 **기본 생성자가 없다**(`io_context`를 받아야 한다). 생성자 초기화 리스트에만
  쓸 수 있다 — `socket_`, Step 6의 `Server&` 참조 멤버와 같은 부류.

**테스트 도구를 의심해야 했던 일:** 핸드셰이크 응답의 프로토콜 ID가 `0x0B47`이 아니라 `0x0047`로
보였다. 그런데 **서버는 내가 보낸 `0x0B47`을 받아들였다** — 읽을 땐 맞고 쓸 땐 틀린다는 뜻인데
같은 상수를 쓰는 코드가 그럴 수 없다. 범인은 PowerShell 테스트 스크립트였다.

```powershell
[byte]0x0B -shl 8   # = 0     (피연산자 타입의 폭 안에서 돈다)
[int]0x0B  -shl 8   # = 2816
```

Step 10에서도 비슷했다(테스트 클라이언트의 프레임 동기화가 깨져 서버를 한참 의심했는데 서버는
CPU 0.03%로 멀쩡했다). **검증 도구를 의심하는 것도 디버깅의 일부다.** 특히 직접 만들었을 때는.

**현재 한계 (의도적):**
- **신뢰성이 없다** — `ack_bits`가 0이고 재전송도 없다. **Step 14에서 해결.**
- **고정 틱이 없다** — 1초 타이머는 타임아웃 검사용이다. 30Hz 게임 루프는 Step 15.
- **payload를 해석하지 않는다** — 헤더만 읽고 본문은 크기만 센다.
- **로비와 연동이 없다** — 매칭된 파티를 배틀 서버로 보내는 경로가 아직 없다. Step 18.
- **도커 구성이 로비만** — `docker-compose.yml`에 배틀 서버가 없다. Step 15에서 둘을 같이 띄운다.

**검증:** 도커 컨테이너에서 직접 조립한 UDP 패킷으로 확인.

*헤더 파싱 (13b)*
1. `seq=12345 ack=999 ack_bits=0xDEADBEEF` → 그대로 파싱됨. **바이트 순서가 한 군데라도
   틀렸으면 `0xEFBEADDE` 같은 게 나왔을 것**
2. `seq=65535`(uint16 최대), `ack_bits=0x80000001`(양 끝 비트) → 정확히 왕복
3. payload 0 / 8 / 100바이트 → `size - kHeaderSize` 계산이 맞음
4. **잘못된 프로토콜 ID, 5바이트짜리 패킷 → 로그조차 없음** (조용히 버려짐)

*세션 (13c)*
5. 핸드셰이크 → `HandshakeAck` 수신. **서버가 처음으로 패킷을 보낸 순간**
6. 포트가 다른 클라 셋 → `conn 1` / `conn 2` / `conn 3`으로 각각 구분됨
7. Input `seq=1,2,3` 보낸 뒤 핸드셰이크 재전송 → 응답에 **`ack=3`**
   (서버가 "네 3번까지 봤다"를 알려줌). 그리고 **새 세션을 안 만들고 응답만 다시 보냄**
8. 핸드셰이크 없이 Input을 보낸 클라 → **응답 없음** (증폭 공격 방지)

*타임아웃 (13d)*
9. A는 1초마다 하트비트, B는 침묵 → **B만 5초 뒤 `Timeout: conn 2`**
10. A는 8초 뒤에도 살아서 `Input seq=99`가 처리됨
11. 하트비트마다 `ack`가 1→8로 따라 올라감

</details>

<details>
<summary><b>Step 14 — 신뢰성 계층: 선택적으로, 그리고 포기할 줄 알게</b></summary>

**Decision:** `ack`/`ack_bits`로 유실을 탐지하고(14a~14b), 타입마다 재전송 여부를 다르게 하고
(14c), RTT를 재서 창을 기다리지 않고 먼저 재전송하고(14d), 재전송 횟수에 상한을 둔다(14e).

**Why:** Step 13에서 헤더에 자리만 잡아둔 `ack`/`ack_bits`를 실제로 쓰기 시작하는 구간이다.
그런데 **TCP와 똑같이 만들면 안 된다. 똑같이 만들 거면 TCP를 쓰면 되니까.** TCP가 못 하는
건 "이 메시지는 안 와도 된다"는 구분이다. 위치는 다음 스냅샷이 덮으니 재전송이 낭비이고,
파티원 입장 알림은 놓치면 영영 모른다. **같은 연결 위에서 메시지마다 다른 보장**을 주는 것이
직접 만드는 유일한 이유다.

**Alternatives considered:**
- **전부 재전송 (TCP 흉내)**: 구현이 단순하다. 그런데 그게 바로 head-of-line blocking이고,
  UDP로 옮긴 의미가 사라진다.
- **아무것도 재전송 안 함**: 30Hz 스냅샷만 있으면 실제로 상당 부분 굴러간다. 그런데 "보스가
  등장했다" 같은 **상태 전이**는 스냅샷에 담기 어렵고(담으면 매 프레임 전체 상태를 보내야
  한다), 놓치면 클라이언트가 영영 다른 세계를 보게 된다.
- **메시지 ID + ack 목록**: 패킷이 아니라 메시지 단위로 추적하는 방식. 더 정확하지만 payload가
  아직 없다. **지금 필요하지 않은 구조**라서 Step 16으로 미뤘다.

**uint16 시퀀스의 wraparound.** 30Hz면 `seq`가 **36분마다 한 바퀴** 돈다. 65535 다음은 0인데
숫자로는 0이 더 작아서, 단순 `>` 비교는 최신 패킷을 "옛날 것"으로 판정한다. 해법은 **차이가
절반(32768) 이내면 그쪽이 최신**으로 보는 것. 30Hz에서 32768패킷이면 18분이라 그 사이에
도착할 패킷은 없다.

```cpp
std::uint16_t shift = seq - remote_sequence;   // 65535 -> 0 이면 언더플로가 1을 준다
```

Step 9에서 `uint64_t` 언더플로(`500 - 1000`이 천문학적 숫자가 됨)가 **버그**였는데, 여기서는
**같은 성질이 도구다.** 범위를 한 바퀴 도는 값을 다룰 때는 언더플로가 정확히 원하는 답을 준다.

**비트 시프트의 경계.** `1u << 32`는 **정의되지 않은 동작**이다. 32비트 타입을 32비트 이상
시프트하면 안 된다. `shift < 32` / `== 32` / `> 32`를 나눠 처리한 이유다. 경계 하나를 놓치면
컴파일러와 CPU에 따라 다르게 도는 코드가 된다 — 테스트로는 잘 안 잡히는 종류.

**0이 유효한 값이라 sentinel을 못 쓴다.** `remote_sequence`의 초기값 0과 "진짜 0번 패킷을
받음"이 구분되지 않는다. 구분 없이 가면 `diff = 0`에서 `1u << -1`이 되어 또 UB다. Step 6에서
`room_id_ = 0`을 "방 없음"으로 썼던 수법이 여기선 안 통해서 `has_received_any` 플래그를 뒀다.

**"판단 보류"라는 세 번째 상태.** 보낸 패킷은 도착/유실 둘이 아니라 셋이다. `ack`보다 최신인
패킷은 상대가 아직 못 받았을 뿐일 수 있다. 여기서 성급하게 유실로 치면 **멀쩡한 걸 다시
보낸다.** 로비의 경매(Step 10)에서 "입찰 중"과 "유찰"을 구분했던 것과 같은 종류의 실수 방지다.

**재전송에 특별한 코드가 없다.** 재전송 사본은 `take_sequence()`로 **새 번호**를 받아 일반
패킷과 똑같이 추적된다. 그래서 사본이 또 유실되면 또 걸리고 또 나간다 — 재귀적으로 동작하는데
그걸 위한 코드는 한 줄도 없다. `seq`를 **메시지가 아니라 패킷의 신분증**으로 정의한 덕이다.

여기서 공짜 이득이 하나 나왔다. TCP는 재전송에 같은 번호를 재사용해서 ack가 원본의 것인지
사본의 것인지 모른다. 틀린 쪽으로 재면 RTT가 망가지므로 **재전송은 RTT 측정에서 제외**하는
Karn's algorithm이 필요하다. 우리는 번호가 항상 다르니 **그 문제 자체가 생기지 않는다.**

**테스트가 설계 결함을 찾아냈다.** 14d까지 만들고 돌렸더니 틱당 `RETRY`가 1, 2, 3, 4, 5, 6, 7로
계속 늘었다. 클라이언트는 일정한 속도로 보내는데 **서버 송신량만 증가**하고 있었다.

```
틱1:  Event A
틱2:  A'  + Event B
틱3:  A'' + B'  + Event C
틱4:  A'''+ B'' + C' + Event D      <- 매 틱 하나씩 늘어난다
```

재전송 사본이 또 RTO를 타기 때문이다. `resent` 플래그는 **같은 기록의 중복 전송**만 막았지
**연쇄**는 못 막았다. UDP에는 혼잡 제어가 없으니 **받지 못하는 클라이언트 하나가 서버로 하여금
제 회선을 막게 만든다.** 재전송 횟수 상한(5회)을 둬서 틱당 4개로 평평해졌다.

신뢰성 계층의 목표는 "반드시 도착시킨다"가 아니라 **"합리적으로 노력하고 포기한다"**다.
상한은 타임아웃(5초)보다 짧아야 한다 — 더 오래 버티면 이미 없는 상대에게 보내는 꼴이다.

**로비와 같은 자료구조, 같은 이유.** 보낸 패킷 기록은 `std::deque`다. 앞에서 빼고(가장 오래된
것부터 판정) 뒤에 넣는(방금 보낸 것) 패턴이라, `vector`면 `pop_front`가 O(n)이고
`unordered_map`이면 "가장 오래된 것"을 찾느라 전체를 뒤져야 한다. Step 4의 `send_queue_`와
똑같은 선택이다.

다만 이번엔 **상한을 뒀다**(256개). 로비의 `send_queue_`에는 상한이 없는데, TCP가 역압을
걸어줘서 덜 드러날 뿐 같은 종류의 구멍이다.

**현재 한계 (의도적):**
- **메시지가 아니라 패킷 단위다** — payload가 없어서 "무엇을" 재전송하는지가 타입뿐이다.
  진짜 이벤트(입장/보스 등장)는 Step 16.
- **틱이 1Hz라 RTO 해상도가 1초다** — `rto_ms()`가 30ms를 돌려줘도 실제 재전송은 다음 틱까지
  기다린다. 30Hz가 되는 Step 15에서 저절로 풀린다.
- **RTT가 네트워크 지연이 아니다** — 상대가 "다음에 보낼 때" ack를 싣기 때문에 **ack 지연**을
  재고 있다. 틱이 30Hz면 33ms 바닥 위에 진짜 지연이 얹힌다. 분리해 재는 건 Step 19.
- **`Event`는 재전송 동작을 보려고 틱마다 보내는 임시 코드다** — Step 16에서 진짜 이벤트로
  바뀐다.
- **혼잡 제어가 없다** — 재전송 상한이 유일한 제동 장치다. 대역폭 추정은 Step 19.

**검증:** 도커 컨테이너 + 직접 조립한 UDP 패킷. 클라이언트가 특정 패킷을 "못 받은 척"한다.

*ack_bits 생성 (14a)*
1. 순차 수신 10, 11, 12 → `ack=12 bits=0x3`
2. 13·14를 건너뛰고 15 → `ack=15 bits=0x1c` (**유실 자리가 0으로 남음**)
3. 13이 **늦게** 도착 → `bits=0x1e`, 이어서 14 → `0x1f` (**구멍이 메워짐**)
4. 15가 **중복** 도착 → 변화 없음
5. **wraparound**: 65535 → 0에서 `ack=0`, `bits=0x31`.
   단순 `>` 비교였으면 `ack`가 65535에 멈췄을 것
6. 99칸 점프 → `bits=0x0` (창 밖 전부 폐기), 51칸 지각 → 무시

*유실 탐지 (14b)*
7. 클라가 서버 패킷 5·9·20을 버림 → **`LOST seq=5`, `LOST seq=9`만** 보고됨
8. **20번은 보고 안 됨** — `ack=43` 기준 23칸 뒤라 아직 창 안, "판단 보류"가 맞다
9. `sent=46 acked=41 lost=2 inflight=35` — `ack=43`이면 11번 미만이 정리 대상이라
   11개 처리(9 acked + 2 lost), `46 - 11 = 35`로 **산수가 맞아떨어짐**

*선택적 재전송 (14c)*
10. 클라가 `Event`를 전부 버리고 `Heartbeat`는 5·9만 버림
    → `DROP Heartbeat` / `RESEND Event`. **`RESEND Heartbeat`는 0회**
11. 재전송된 Event가 **매번 새 번호**로 도착함 (클라 로그의 7, 20, 33 사이에 41, 54, 67…)

*RTT / RTO (14d)*
12. `RESEND`가 사라지고 전부 `RETRY`로 바뀜 — **창(3초)보다 RTO(1틱)가 먼저 걸린다**
13. `rtt` 193ms → 87ms → 78ms로 **수렴**. 첫 표본(핸드셰이크)이 튀었는데 한 번에 끌려가지
    않고 서서히 내려옴 = 평활화가 동작

*재전송 상한 (14e)*
14. `try=2,3,4,5`까지만 나오고 **`try=6`은 0회**
15. 틱당 `RETRY`가 1→2→3→4→5→6→7 증가하던 것이 **4개로 평평**해짐
16. `gaveup`이 5회 시도를 마친 시점부터 1씩 오름

</details>

<details>
<summary><b>Step 15 — 30Hz 고정 틱: 서버가 먼저 말을 건다</b></summary>

**Decision:** 틱을 30Hz로 올리고 드리프트를 없애고(15a), `Input`에 본문을 붙이고(15b),
서버가 이동을 계산하고(15c), `Snapshot`을 30Hz로 브로드캐스트한다(15d).

**Why:** 여기서 서버의 성격이 바뀐다. Step 1~14의 모든 코드는 **반응**이었다 — 메시지가 오면
처리하고 응답한다. 이제 **아무도 아무것도 안 보내도 초당 30번 세계가 흘러가야** 한다.
로비 Step 10에서 경매 타이머로 "스스로 움직이는 서버"를 처음 만들었는데, 그건 1Hz였고 틀려도
상관없었다. 30Hz에서는 간격 자체가 게임의 물리가 된다.

**Alternatives considered:**
- **60Hz**: 대역폭이 두 배인데, 몬스터 수십 마리의 위치를 보내는 쪽이 프레임 수보다 중요하다.
  클라이언트가 스냅샷 사이를 보간해서 그리면 30Hz로도 부드럽다.
- **클라이언트가 위치를 보내기**: 서버 계산이 사라져서 단순하다. 그런데 **벽 통과와
  순간이동이 공짜**가 된다. 협동 게임이라 경쟁 요소가 없어도, 한 명이 보스를 즉사시키면
  나머지 셋의 판이 망가진다.
- **플레이어마다 다른 스냅샷 만들기**: 각자 주변만 보내면 대역폭이 준다. 4인에 5.3KB/s 인
  지금은 **아낄 것이 없다.** 몬스터가 늘면 그때 한다.

**`expires_after`를 쓰면 안 된다.** "지금부터 33ms"는 처리 시간이 매번 더해진다. 틱 처리에
2ms가 걸리면 실제 간격이 35ms가 되고, 1분이면 3.6초가 밀린다. **목표 시각을 박아두고
`expires_at`으로 거는 것**이 답이다. 한 틱이 늦으면 다음 틱이 그만큼 일찍 와서 스스로
보정한다. Step 10에서 경매 마감을 `ends_at`으로 둔 것과 같은 수법인데, 거기선 1초 오차가
무해했고 여기선 치명적이다.

도커 컨테이너에서 측정한 지터가 **0.04ms**였다. 33.333ms 목표에 0.1% 오차다.

**밀린 틱을 따라잡지 않는다.**

```cpp
if (next_tick_ + kTickInterval * 5 < now) { next_tick_ = now + kTickInterval; }
```

서버가 1초 멈췄다 깨어나면 밀린 틱이 30개 생긴다. 전부 처리하려 들면 그동안 또 밀리고, 더
많이 밀린다 — **죽음의 나선(spiral of death)**. 게임 루프에서는 **과거를 포기하는 게 정답**이다.
Step 14의 재전송과 정반대 판단인데, 거기선 과거를 반드시 복구해야 했다. **같은 서버 안에서도
데이터 성격에 따라 반대 결론이 나온다.**

**늦게 온 입력을 버리는 데 새 상태가 필요 없었다.**

```cpp
if (h.sequence != c->remote_sequence) return;
```

`on_packet_received`(Step 14a)가 이미 "가장 최신 번호"를 관리하고 있다. 방금 받은 패킷이
최신이었다면 둘이 같고, 옛 패킷이었다면 다르다. `last_input_seq` 멤버를 따로 둘 뻔했는데
**신뢰성 계층에서 만든 것이 그대로 답**이었다.

UDP는 순서를 안 지킨다. 50번 입력(오른쪽) 뒤에 49번 입력(왼쪽)이 올 수 있고, 그걸 적용하면
**캐릭터가 한 프레임 뒤로 튄다.** 유실보다 오히려 눈에 잘 띈다.

**고정 dt를 쓰는 세 가지 이유.** 보통 게임 엔진은 `dt = now - last_frame`을 쓴다. 서버는
반대로 항상 1/30을 쓴다.

- **결정적이다.** 같은 입력 열을 넣으면 언제 실행해도 같은 위치가 나온다. 버그 재현이 된다
- **클라이언트가 같은 계산을 할 수 있다.** 예측(prediction)을 붙이려면 서버와 정확히 같은
  수식을 돌려야 하는데, 서버의 실제 경과 시간은 클라가 알 수 없다
- **틱 간격을 흔들어 더 움직이는 치트가 막힌다**

15a에서 지터를 0.04ms로 만든 것이 여기서 값을 한다. **간격이 정확하니 고정 dt가 거짓말이
아니다.** 둘은 한 쌍이다.

**대각선이 41% 빨라지는 고전 버그.** `(127, 127)`을 그대로 쓰면 길이가 √2다.

```cpp
float len2 = ix * ix + iy * iy;
if (len2 > 1.0f) { float len = std::sqrt(len2); ix /= len; iy /= len; }
```

`len2 > 1.0f`일 때만 나누는 건 **아날로그 스틱을 살짝 민 경우를 살리려는 것**이다. 무조건
정규화하면 `(30, 0)`도 최고 속도가 되어 "살살 걷기"가 사라진다. `√a > 1`과 `a > 1`이 같은
조건이라 제곱근을 피했다.

**클라이언트가 멈추는 것과 끊는 것은 다르다.** 프로세스가 얼어붙으면 패킷이 안 오지만 연결은
살아 있다. 서버는 5초 뒤에야 정리하는데, 그동안 마지막 입력이 계속 적용되면 **캐릭터가
25유닛을 혼자 달린다.** 입력에 250ms 유효기간을 뒀다. "UDP에는 끊김이 없어서 조용한 시간을
재는 수밖에 없다"는 Step 13의 성질이 **게임 로직까지 번진 경우**다. 같은 패턴, 다른 시간
척도(5초 vs 250ms).

다만 이 유예가 **이동 거리에 그대로 더해진다.** 1초 입력하면 1.25초어치를 움직인다. 클라이언트
예측을 붙일 때 양쪽이 이 유예를 똑같이 적용하지 않으면 거기서 어긋난다.

**본문은 한 번만 직렬화한다.** `send_to`가 덮어쓰는 건 헤더(0~10)뿐이고 본문(11~)은 건드리지
않는다. 그래서 스냅샷을 한 번 만들어 같은 버퍼를 N번 보낸다. 로비의 브로드캐스트(Step 7)와
같은 구조인데, 거기선 내용이 완전히 같았고 여기선 **헤더만 다르다** — 각자의 `seq`와 `ack`가
다르니까.

**테스트 도구가 또 범인이었다.** `move_y`가 항상 0으로 읽혔다. 서버를 한참 뒤졌는데
`read_input`은 멀쩡했다. 원인은 테스트 스크립트가 **BOM 없는 UTF-8**이라 Windows PowerShell이
CP949로 읽었고, 줄 끝 한글 주석이 어긋나면서 **줄바꿈을 삼켜 다음 줄 전체를 주석으로**
만든 것이었다. 이 레포의 `tools/test-client.ps1`에 "BOM 필요"라고 적어둔 바로 그 함정에
그대로 빠졌다.

| Step | 증상 | 진짜 원인 |
|---|---|---|
| 10 | 서버가 멈춘 줄 알았음 | 테스트 클라의 프레임 동기화 깨짐 |
| 13 | 프로토콜 ID가 `0x0047`로 보임 | PowerShell `[byte] -shl 8`이 0 반환 |
| 15 | `move_y`가 항상 0 | BOM 없는 UTF-8 + 한글 주석이 다음 줄을 삼킴 |

세 번 모두 서버는 멀쩡했다. **검증 도구를 의심하는 것도 디버깅의 일부다.**

**현재 한계 (의도적):**
- **본문 있는 신뢰 패킷을 재전송하면 본문이 빠진다** — 재전송 경로가 `payload_size`를 안
  넘긴다. 지금은 그런 패킷이 없어서 안 드러난다. `Event`에 내용이 들어가는 Step 16에서
  `SentPacket`이 본문을 들고 있도록 고쳐야 한다.
- **모두에게 같은 스냅샷을 보낸다** — 관심 영역(area of interest)이 없다. 멀리 있는 것도
  전부 보낸다. 자를 때도 순서대로 자른다.
- **`Player`가 `Connection` 안에 있다** — 연결 없는 게임 객체(몬스터)가 생기는 Step 16에서
  `World`로 옮긴다. **지금 옮기면 빈 껍데기 클래스가 하나 생길 뿐이다.**
- **클라이언트 예측이 없다** — 서버 왕복을 기다려야 캐릭터가 움직인다. 유니티 클라이언트를
  만들 때 붙인다.
- **충돌이 없다** — 맵 경계 clamp 뿐이다.

**검증:** 도커 컨테이너 + 직접 조립한 UDP 패킷.

*고정 틱 (15a)*
1. 무부하 10초 → **정확히 30/s**, 지터 0.04ms (첫 틱만 0.42ms)
2. 클라이언트가 초당 14패킷 보내는 중에도 → **30/s 유지**, 지터 0.05ms
3. RTO 해상도가 1초에서 33ms로 내려오면서, 재전송 5회가 **4초에서 1초로** 단축됨
4. 부작용: `RETRY HandshakeAck`가 매 연결마다 한 번 나타남. RTT를 모를 때의 초기 RTO
   100ms가 공격적인 탓. 핸드셰이크는 멱등해서 동작은 정상 (TCP의 초기 RTO는 1초다)

*입력 payload (15b)*
5. `(127, 0)`, `(-127, 100)` 정확히 왕복. `0x81` → `-127`
6. **seq 60 뒤에 seq 50 도착 → 무시됨** (`remote_sequence` 비교만으로)
7. 본문 없는 `Input`(헤더 11바이트만) → 터지지 않고 기존 값 유지

*서버 권위 이동 (15c)*
8. 오른쪽 15초 → `13.81 → 18.81 → 23.81 → 28.81`, **초당 정확히 5.0**
9. 맵 끝 → `48.81` 다음 **`50`에서 멈춤**
10. 대각선 `(127,127)` → Δx와 Δy가 같고 **거리 기준 속도 5.07** (정규화 없으면 7.07)
11. 입력 중단 → 250ms 안에 **완전 동결**, 3초간 좌표 불변

*스냅샷 (15d)*
12. 클라 둘이 각각 오른쪽/위로 이동 → **양쪽이 완전히 같은 내용** 수신
    (`count=2 id1=(11.16,0) id2=(0,11.16)`)
13. 수신 주기 → **1초에 30개**
14. 음수 좌표 → `0xFB89` = `-1143` → **`-11.43`** (2의 보수 빅엔디안)
15. 4인 기준 서버 송신량 5.3KB/s (`float` 좌표였으면 7.4KB/s)

</details>

<details>
<summary><b>Step 16 — 몬스터: 연결 없는 객체가 생기면서 구조가 깨진다</b></summary>

**Decision:** `World` 를 만들어 `Player` 를 `Connection` 에서 꺼내고(16a), `Monster` 와 타입
필드를 더하고(16b), 최근접 플레이어 추적을 붙이고(16c), 자동 사격과 사망을 넣고(16d),
`Event` 에 본문을 담으면서 재전송 구멍을 막는다(16e).

**Why:** Step 15 에서 "`Player` 를 `Connection` 안에 둔다, 1:1 이니까"라고 적었다. 그 말이
**몬스터가 생기는 순간 거짓**이 된다. 몬스터는 연결이 없다. `ConnectionMap` 에 넣을 수 없고,
넣는다 해도 `endpoint` 가 무엇이 되나.

**필요해지기 전에 만들지 않는다는 원칙을 지켰더니, 언제 만들어야 하는지를 코드가 알려줬다.**
Step 15 에서 미리 `World` 를 만들었다면 플레이어만 든 빈 껍데기였을 것이고, 그 모양이 맞는지
판단할 근거도 없었을 것이다.

**Alternatives considered:**
- **몬스터도 `Connection` 으로 표현하기**: 코드를 안 고쳐도 된다. 그런데 `endpoint` 가
  의미를 잃고, 타임아웃 검사가 몬스터를 지우게 된다. **억지로 맞추면 더 큰 비용이 온다.**
- **`Entity` 기반 클래스 + 가상 함수**: 확장성은 좋다. 그런데 지금 공통 멤버는 `id/x/y` 뿐이고,
  플레이어는 입력을, 몬스터는 추적을 한다. **공통이 거의 없는데 상속을 쓰면 분기만 숨겨진다.**
- **발사체 만들기**: 탕탕특공대는 실제로 총알이 날아간다. 그런데 총알도 엔티티가 되고 속도·
  수명·명중 판정이 붙는다. 그 자체로 한 스텝짜리 일이라 즉시 명중으로 먼저 돌려봤다.

**리팩터링과 기능을 분리했다.** 16a 는 **기능을 하나도 더하지 않는다.** 합격 기준이
"15d 테스트가 그대로 통과하는 것"이었다. 둘을 섞었으면 뭔가 깨졌을 때 원인이 둘인데, 나누면
**16a 에서 깨진 건 리팩터링 탓밖에 없다.**

실제로 리팩터링이 새 책임을 하나 만들었다. 예전에는 `Connection` 이 지워지면 `Player` 도
**자동으로** 사라졌다. 같은 객체 안에 있었으니까. 이제는 `world_.remove_player()` 를
**명시적으로 불러야** 하고, 빠뜨리면 접속이 끊긴 플레이어가 맵에 영원히 서 있는다.
**소유권을 분리하면 해제도 분리된다.**

**연결 ID와 엔티티 ID를 나눈 것이 16b 에서 값을 했다.** 몬스터가 1~5 번을 받고 플레이어가
6 번부터 시작한다. 발급기가 하나여야 스냅샷에서 충돌하지 않는다. 로비에서 `Session` 과
`username` 을 나눈 것과 같은 구분 — **네트워크 신원과 게임 신원은 다르다.**

**제곱근을 미루는 습관.** "누가 더 가까운가"는 제곱 상태로도 답이 같다.

```cpp
float d2 = dx * dx + dy * dy;    // 비교는 제곱으로
...
float d = std::sqrt(d2);          // 정규화할 때 한 번만
```

몬스터 100마리 × 플레이어 4명 × 30Hz = **초당 12,000번 비교**다. 15c 의 `len2 > 1.0f` 와
같은 수법이고, 이런 건 습관으로 들여두는 쪽이 싸다.

**오버슈트 분기가 없으면 몬스터가 떤다.**

```cpp
if (step >= d) { m.x = target->x; m.y = target->y; continue; }
```

`dt` 가 고정이라 틱당 이동량이 일정하다. 남은 거리가 그보다 작아지는 순간부터 매 틱 목표를
넘어갔다 되돌아온다. 화면에서는 **플레이어 위에서 부들부들 떠는** 것으로 보인다.

**공격 루프 안에서 몬스터를 지우면 안 된다.** `nearest_monster` 가 `monsters_` 안을 가리키는
포인터를 돌려주는데, swap-and-pop 으로 지우면 **마지막 원소가 옮겨가면서** 다른 플레이어가
들고 있던 포인터가 엉뚱한 것을 보게 된다. 때리는 단계와 치우는 단계를 나눴다. 로비 Step 10
타임아웃의 "먼저 모아두고 그 다음에 처리"와 같은 패턴인데, 거기는 반복자였고 여기는 포인터다.

그리고 지우면서 `i` 를 올리면 안 된다.

```cpp
monsters_[i] = monsters_.back();
monsters_.pop_back();
// ++i 하지 않는다 — 방금 당겨온 것을 아직 안 봤다
```

올리면 한 틱에 둘이 연달아 죽을 때 하나를 건너뛴다. 다음 틱에 결국 지워져서 **증상이
"가끔 한 틱 늦게 죽는다"로만 나타난다.** 찾기 힘든 종류다.

**쿨다운은 대입이 아니라 더하기.** `dt` 가 1/30 이라 타이머가 정확히 0 에서 멈추지 않는다.
`= 0.5` 로 대입하면 초과분이 매번 버려져서 실제 간격이 0.5167 초가 된다. `+=` 는 넘긴 만큼을
다음 주기에서 뺀다. **15a 의 `next_tick_ += kTickInterval` 과 똑같은 문제, 똑같은 답**이다.
주기적인 일에는 늘 이 함정이 있다.

음수로 쌓이는 것도 막아야 한다. 목표가 없는 10초 동안 타이머가 -10 까지 내려가면, 적이
나타나는 순간 `+= 0.5` 를 20번 해야 양수가 되고 그동안 **20발이 한 틱에 나간다.**

**15d 에서 적어둔 구멍을 16e 에서 막았다.** 재전송 경로가 본문을 복원하지 않아서, 본문 있는
신뢰 패킷을 다시 보내면 빈 패킷이 나갔다. 그때는 본문 있는 신뢰 패킷이 하나도 없어서 드러나지
않았고, `Event` 에 내용이 들어가는 순간 진짜 버그가 됐다. **"지금은 안 드러나는 구멍"을
적어두는 것이 실제로 쓸모가 있었다.**

본문은 `std::array<8>` 로 고정했다. `vector` 면 `SentPacket` 마다 힙 할당이 생기고, 인플라이트
256개가 매 `push_back`/`pop_front` 마다 할당·해제를 반복한다. 다만 **크기를 넘기면 조용히
본문 없이 저장된다** — 나중에 더 큰 이벤트를 만들면 재전송에서만 본문이 빠져 재현이 어렵다.
지금은 넘길 수 있는 경로 자체가 없어서 미뤘지만, 좋은 설계는 아니다.

**하나의 실수가 세 군데서 다르게 보였다.** `send_to` 에서 `on_packet_sent` 가 두 번 불렸다
(옛 호출을 지우지 않음). 증상은 이랬다.

1. 재전송이 두 번씩 나감 → 송신량 2.5배 (초당 30 → 77)
2. `inflight` 가 256 상한에 눌려붙음
3. 둘 중 **본문 없이 저장된 쪽**이 빈 `Event` 로 나감

로그만 보면 "재전송 로직이 이상하다"로 읽히는데 원인은 전혀 다른 곳이었다. 테스트가
`!! Event with NO payload` 를 130번 찍어준 덕에 찾았다.

**현재 한계 (의도적):**
- **몬스터끼리 충돌하지 않는다** — 같은 목표를 같은 속도로 쫓으니 **다섯 마리가 한 점에
  겹친다.** 뱀서류의 "포위당한다"는 감각은 몬스터가 서로 밀어내며 면적을 차지할 때 생기는데,
  분리(separation)는 200×200 = 40,000번/틱이라 공간 분할이 따라온다.
- **몬스터가 플레이어를 때리지 않는다** — 닿아도 아무 일이 없다. 다운/부활은 Step 18.
- **웨이브가 없다** — 서버 시작 시 5마리를 놓는 임시 코드다. 다 죽으면 맵이 빈다. Step 17.
- **발사체가 없다** — 즉시 명중이라 거리에 따른 선입선출이 없다.
- **hp 가 스냅샷에 없다** — 클라이언트가 체력바를 못 그린다.
- **`inflight` 상한에 걸려 밀려난 패킷은 유실 판정도 못 받는다** — "보냈는데 도착도 유실도
  아닌" 구간이 통계에서 사라진다. Step 19 에서 다룬다.

**검증:** 도커 컨테이너 + 직접 조립한 UDP 패킷.

*리팩터링 (16a)*
1. 15d 테스트 그대로 → `count=2`, 좌표 동일, 30/s. **아무것도 안 바뀜**
2. 덤: 스냅샷 순서가 `unordered_map` 순회에서 `vector` 삽입 순서로 바뀌어 **결정적**이 됨
3. B 가 조용해진 뒤 7초 → A 가 보는 `count` 가 **2 → 1**. 유령 캐릭터 안 남음

*몬스터 (16b)*
4. 접속 직후 `count=6` — `P6(0,0) M1(20,0) M2(6.18,19.02) ...`
5. 배치 검증: `√(6.18² + 19.02²) = 20`. 반지름 정확
6. ID 공간: 몬스터 1~5, 첫 플레이어 **6**, 둘째 플레이어 **7**. 겹침 없음

*추적 (16c)*
7. 수렴 속도 `16.99 → 14.99 → 12.99 → 10.93 → 8.86` — **초당 정확히 2.0**
8. 방사 대칭 유지: M2 `(5.25,16.16)` 의 거리 = 16.99 = M1 과 동일
9. `(0,0)` 도달 후 **완전 정지**. 떨지 않음
10. 도망 시 격차가 초당 3.0 씩 벌어짐 (`5.0 - 2.0`)

*전투 (16d)*
11. 거리 8.79 에서는 무사, **6.79 에서 피격 시작** (사거리 8)
12. 진입 후 **약 1.5초**에 사망 (30hp ÷ 10 × 0.5초)
13. `kills` `0 → 1 → 2 → 3 → 4 → 5`, `monsters` `5 → 0`
14. 몬스터 0마리 상태에서도 터지지 않음
15. 동률 처리가 드러남 — `d2 <= best_d2` 의 `=` 때문에 **마지막 것**이 선택되어 한 마리씩
    집중 공격한다. `<` 였다면 첫 번째를 집중했을 것

*이벤트와 재전송 (16e)*
16. ack 를 전혀 안 보내는 클라 → 모든 `Event` 가 5회까지 재전송됨
17. **5회 전부 `entity_id` 정상.** 본문 빠진 패킷 **0개** (고치기 전엔 130개)
18. `RETRY` 로그의 각 `seq` 가 **정확히 1회**씩 (중복 기록 제거 확인)
19. `sent` 증가율이 초당 77 → **30** (스냅샷만큼)

</details>

<details>
<summary><b>Step 17 — 웨이브와 방: 틱이 상태를 바꾼다</b></summary>

**Decision:** 방 상태 머신과 웨이브 스폰을 넣고(17a), 문 도달 판정으로 방을 넘기고(17b),
스폰 위치·난이도·전환 처리를 다듬고(17c), 진행 상황을 클라이언트에 알린다(17d).

**Why:** 지금까지 서버에는 **게임의 흐름**이 없었다. 몬스터가 있고 죽을 뿐이었다. 이제
"몇 마리를 언제 내보낼지, 다 죽이면 뭐가 되는지, 언제 넘어갈지"가 생긴다.

로비에도 `RoomPhase` 가 있었다. 결정적인 차이는 **누가 상태를 바꾸느냐**다. 로비에서는
누가 공격 메시지를 보내야 체력이 깎이고 단계가 바뀌었다. 여기서는 **아무도 아무것도 안
보내도** 웨이브가 끝나고 카운트다운이 돌고 방이 넘어간다. 같은 열거형인데 성격이 다르다.

**Alternatives considered:**
- **쉬는 시간을 상태로 만들기** (`WaveBreak` phase): 명시적이다. 그런데 상태를 늘리면 전이
  경로가 곱으로 늘고("WaveBreak 중에 전멸하면?"), **클라이언트 입장에서는 그냥 "몬스터가
  없는 Fighting"** 이다. 숫자 하나(`wave_break`)로 충분했다.
- **방마다 타이머 객체 두기**: 정확하다. 그런데 Step 10 에서 경매마다 타이머를 달지 않은
  것과 같은 이유로 안 했다. 틱 하나가 전부를 훑으면 생명주기 관리가 사라진다.
- **난이도를 곱으로 올리기**: `base × (room+1)` 이면 5번째 방에서 수백 마리가 되고 1400바이트
  스냅샷(154개)을 넘는다. **게임 밸런스보다 프로토콜 한계가 먼저 온다.**

**설계를 틀렸고, 테스트가 잡았다.** 17a 를 처음 만들었을 때 첫 웨이브가 통째로 빠졌다.
로그가 `wave 1 (5 monsters)` 부터 시작하고 총 처치가 `5 + 6 = 11` 이었다.

원인은 `update_phase` 가 **서버 시작 직후부터 플레이어 없이도 돌았다**는 것이다. 몬스터가
없으니 바로 다음 웨이브로 넘어가버려서, 나중에 플레이어가 들어왔을 때 "첫 웨이브를 스폰한다"는
특수 케이스의 조건이 이미 거짓이었다.

그 특수 케이스는 이렇게 생겼었다.

```cpp
if (players_.size() == 1 && monsters_.empty()
    && phase == RoomPhase::Fighting && wave_index == 0) { spawn_wave(); }
```

**조건이 네 개나 되는 게 이미 신호였다.** 적으면서 "이건 임시다"라고 써놓고도 구조를 안
의심했다. 고친 방법은 특수 케이스를 **없애는** 것이었다.

1. `wave_index` 를 `-1` 로 시작한다
2. `update_phase` 는 `players_.empty()` 면 바로 돌아선다

그러면 "다음 웨이브로 간다"는 규칙 하나가 시작까지 처리한다. 덤으로 **파티가 다 나간 방이
멈춘다** — 전에는 빈 방이 혼자 웨이브를 돌리며 CPU 를 먹고 있었다.

**"시작 조건"을 따로 쓰려다 보면 대개 상태 초기값이 잘못된 것이다.**

**타이머가 도는 동안 조건을 계속 봐야 한다.** 과반수가 문에 모이면 5초 카운트다운이 시작되는데,
"조건이 만족되면 시작"만 쓰고 "조건이 깨지면 취소"를 안 쓰면 **한 명만 남아도 방이 넘어간다.**

로비 경매(Step 10)에서는 마감 시각을 박아두고 아무도 못 바꿨다. 여기서는 **사람이 움직여서
조건이 바뀐다.** 같은 타이머인데 감시해야 할 것이 하나 더 있다.

**템포를 고친 것이 마릿수를 늘린 것보다 먼저였다.** 처음 측정에서 3인 방 클리어가 108초였는데,
그 대부분이 **몬스터가 맵 가장자리에서 걸어오는 16초**였다. 죽이는 건 금방이었다.

스폰 기준점을 맵 중심에서 **파티 평균 위치**로 옮기고 반지름을 40 → 25 로 줄였다. 그랬더니
몬스터를 4배(4/5/6 → 12/16/20)로 늘렸는데도 **클리어가 56초로 절반이 됐다.**

숫자보다 중요한 건 "플레이어가 어디 있든 같다"는 점이다. 전에는 구석에 숨으면 몬스터가 오래
걸어와서 **도망이 전략이 아니라 시간 끌기**였다. 이제 어디로 가든 주변에서 나온다.

**병목을 모르고 "몬스터를 늘리자"고 했으면 더 지루해졌을 것이다.**

**1400바이트를 늘릴지 논의했고, 안 하기로 했다.** 몬스터를 수백 마리로 늘리려면 길이 셋이다.

| 수단 | 효과 | 비용 |
|---|---|---|
| 엔티티 크기 줄이기 | 9 → 5바이트 (-45%) | 비트 패킹 |
| 안 보내기 (관심 영역) | -70% 가능 | 플레이어별 스냅샷 |
| 델타 압축 | -80% | 클라별 상태 추적 |
| 패킷 쪼개기 | 개수 상한만 풂 | `part/total` 헤더 |

2800바이트를 **그냥 보내는 것**은 선택지가 아니다. IP 단편화가 일어나고 **조각 하나만 잃어도
전체가 버려진다** (유실률 2% 가정 시 3.96%). 일부 공유기·VPN·모바일 망은 조각난 UDP 를 아예
버린다. 쪼갠다면 **애플리케이션에서** 쪼개야 각 패킷이 독립적으로 쓸모 있다.

그런데 실측이 20마리에 **219바이트**였다. 1400의 16%다. 넷 다 **측정되지 않은 문제를 고치는
일**이라 전부 미뤘다. 실제로 스냅샷이 잘리거나 Step 19 측정에서 대역폭이 문제가 되면 그때 한다.

**바뀔 때 + 주기적으로.** `RoomState` 를 바뀔 때만 보내면 그 한 번을 잃었을 때 복구가 안 되고
(비신뢰), 매 틱 보내면 대부분이 같은 내용이다. 둘을 합치니 75초에 **77개**가 나갔다 — 30Hz
전송(2250개)의 3.4%다.

신뢰 패킷으로 만들 수도 있었지만, 그러면 재전송 대상이 되고 `SentPacket` 이 본문을 들어야 한다.
**"어차피 또 보낼 거라 유실돼도 된다"** 는 14a 의 `ack_bits` 와 같은 논리다.

**서버가 플레이어를 순간이동시킨다.** 방이 넘어갈 때 좌표를 입구로 덮어쓴다. Step 15 에서
서버 권위를 정한 것이 여기서 값을 한다 — 클라이언트가 위치를 들고 있었다면 **서버가 옮긴 것을
클라이언트가 되돌리는 싸움**이 난다.

나중에 클라이언트 예측을 붙일 때 이것이 함정이 된다. 예측은 "서버가 내 입력대로 움직였을 것"을
전제하는데 **방 전환은 입력과 무관한 이동**이다. 클라이언트가 "이건 보정 대상이 아니다"를
알아야 한다.

**현재 한계 (의도적):**
- **던전이 끝나지 않는다** — 방이 무한히 이어진다. 보스방과 종료 조건은 로비 연동(Step 18) 때
  같이 정해야 한다.
- **문이 하나뿐이고 위치가 고정** — 엔티티 ID 0 을 "진짜 객체가 아닌 것"의 표식으로 쓰고 있다.
  갈림길이 생기면 문도 `World` 에 실체를 두고 ID 를 발급받아야 한다.
- **몬스터가 플레이어를 때리지 않는다** — 다운/부활/전멸은 Step 18.
- **몬스터끼리 겹친다** — 16c 의 한계 그대로. 20마리가 되면서 더 잘 보인다.
- **파티가 구석에 있으면 스폰이 한쪽으로 몰린다** — 맵 밖 좌표를 가장자리로 당기기 때문.
  "열린 쪽에서 몰려온다"로 읽혀서 일단 뒀다.
- **`room` 이 `uint8`** — 255방까지. 지금 설계로는 넘을 일이 없다.

**검증:** 도커 컨테이너 + 직접 조립한 UDP 패킷, 클라이언트 3개.

*웨이브 (17a)*
1. 플레이어 없는 4초 → `wave=-1 monsters=0` 에서 **정지**. 고치기 전에는 이미 웨이브를
   스폰하고 있었다
2. 접속 후 `wave 0 (4) → wave 1 (5) → wave 2 (6) → cleared` — **정확히 3웨이브**
3. 누적 처치 `0 → 4 → 9 → 15`

*문 (17b)*
4. 1/3 도달 → 카운트다운 **없음**
5. 2/3 도달 → `majority at door (2/3) - countdown 5s`
6. 이탈 → `majority lost - countdown cancelled`
7. 재진입 → 재시작 후 `countdown done - advancing` → `--> room 1`
8. 3/3 도달 → `all 3 at door - advancing now` (카운트다운 건너뜀)
9. 처음엔 테스트가 문을 **지나쳐서** 실패했다 — 문은 `x ∈ [35,45]` 인데 계속 오른쪽을 눌러
   맵 끝 `x=50` 까지 갔다. `atdoor=1` 이 한 번 찍힌 것으로 로직이 맞는 걸 확인하고 테스트를
   고쳤다

*템포와 난이도 (17c)*
10. 방 클리어 **108초 → 56초** (몬스터는 4배)
11. 방 1 진입 시 `--> room 1 (monster hp 40)`, 웨이브 `16/20/24`
12. 방 전환 시 플레이어가 문(`x≈41`)에서 **입구(`x≈-28, -31, -31`)**로 이동, 2유닛 간격
13. 20마리 기준 스냅샷 **219바이트** (1400 의 16%)

*RoomState (17d)*
14. 카운트다운이 **정확히 1000ms/초** 감소 (`3533 → 2533 → 1533`)
15. `wave = -1` 이 `uint8` 0xFF 로 왕복해 **-1 로 복원**
16. `door=1/3`, `Transitioning` 전환 모두 **즉시** 반영
17. 75초 동안 **77개** 수신 — 30Hz 전송 대비 3.4%

</details>
