# Step 1 — 접속만 받아서 로그 찍기



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


---

[← Devlog 목록](README.md)
