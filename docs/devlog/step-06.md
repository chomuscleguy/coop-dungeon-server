# Step 6 — 방 생성/참여/퇴장


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


---

[← Devlog 목록](README.md)
