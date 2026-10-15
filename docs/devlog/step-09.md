# Step 9 — 보스 공격 + 클리어 보상


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


---

[← Devlog 목록](README.md)
