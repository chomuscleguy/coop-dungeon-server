
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <boost/asio.hpp>

// 로비(TCP)에 한 통 보내고 끊는다. 한 번 쓰고 버리는 객체다.
//
// 30Hz 틱을 막으면 안 되므로 전부 비동기다. 로비가 꺼져 있거나
// 느려도 전투는 그대로 돌아가야 한다 — 그래서 실패해도 로그만 남긴다.
class LobbyReport : public std::enable_shared_from_this<LobbyReport> {
public:
    // 호출 즉시 반환한다. 실제 전송은 io_context 위에서 일어난다.
    static void send(boost::asio::io_context& io,
        const std::string& host, const std::string& port,
        const std::string& json);

    LobbyReport(boost::asio::io_context& io, std::string json);

private:
    void start(const std::string& host, const std::string& port);

    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::ip::tcp::socket socket_;
    std::vector<std::uint8_t> frame_;   // 4바이트 길이 + JSON
};