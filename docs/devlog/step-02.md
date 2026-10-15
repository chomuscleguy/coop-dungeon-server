# Step 2 — echo 서버



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


---

[← Devlog 목록](README.md)
