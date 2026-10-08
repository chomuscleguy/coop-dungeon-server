#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include <boost/asio.hpp>

// 로비(TCP)와의 지속 연결. 배틀 서버가 클라이언트 쪽이다.
//
// 18d 의 LobbyReport 는 한 번 보내고 끊었다. 이제 로비가 역방향으로
// 밀어 넣어야 해서 연결을 유지한다. 끊기면 알아서 다시 붙는다 —
// 로비가 재시작해도 배틀은 계속 돌아야 한다.
class LobbyLink {
public:
    LobbyLink(boost::asio::io_context& io,
        std::string host, std::string port);

    // 받은 JSON 한 통. 호출자가 파싱한다.
    std::function<void(const std::string&)> on_message;

    // 연결이 (다시) 맺어질 때마다 불린다. 재연결 때도 불러야
    // 로비가 재시작한 뒤 자기소개가 다시 간다.
    std::function<void()> on_connected;

    void start();
    void stop();
    void send(const std::string& json);

    bool connected() const { return connected_; }

private:
    void do_connect();
    void schedule_retry();
    void do_read_header();
    void do_read_body(std::uint32_t len);
    void do_write();
    void drop(const char* why);

    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::ip::tcp::socket socket_;
    boost::asio::steady_timer retry_timer_;

    std::string host_;
    std::string port_;
    bool connected_ = false;
    bool stopping_ = false;

    std::array<std::uint8_t, 4> head_{};
    std::vector<char> body_;

    // 송신 큐. 로비의 Session 과 같은 이유 — async_write 가 끝나기 전에
    // 또 쓰면 두 메시지가 섞인다. (Step 4)
    std::deque<std::vector<std::uint8_t>> send_queue_;
    bool writing_ = false;
};