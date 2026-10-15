# Step 7 — 채팅 (방 단위 브로드캐스트)


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


---

[← Devlog 목록](README.md)
