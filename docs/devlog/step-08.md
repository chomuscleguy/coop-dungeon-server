# Step 8 — 매칭 큐 + 보스 스폰


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


---

[← Devlog 목록](README.md)
