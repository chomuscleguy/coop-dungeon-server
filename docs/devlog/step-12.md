# Step 12 — 도커 배포


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


---

[← Devlog 목록](README.md)
