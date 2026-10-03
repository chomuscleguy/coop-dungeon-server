#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <boost/asio.hpp>

// UDP 클라이언트 하나. TCP의 Session 에 해당하지만 소켓을 갖지 않는다.
// 소켓은 서버에 하나뿐이고, 이건 "누가 어디서 보내는가"의 기록이다.
struct Connection {
    std::uint32_t id = 0;                       // 세션 ID (서버가 발급)
    boost::asio::ip::udp::endpoint endpoint;    // 이 클라이언트의 주소

    std::uint16_t next_sequence = 0;            // 내가 보낼 다음 패킷 번호
    std::uint16_t remote_sequence = 0;          // 상대에게서 받은 가장 최근 번호

    std::chrono::steady_clock::time_point last_seen;

    // 보낼 패킷에 번호를 붙이고 하나 올린다. 65535를 넘으면 0으로 돈다.
    std::uint16_t take_sequence() { return next_sequence++; }
};

// endpoint 로 찾는 맵을 만들려면 해시가 필요하다.
// udp::endpoint 에는 표준 해시가 없어서 직접 만든다.
struct EndpointHash {
    std::size_t operator()(const boost::asio::ip::udp::endpoint& e) const {
        std::size_t h1 = std::hash<std::string>{}(e.address().to_string());
        std::size_t h2 = std::hash<std::uint16_t>{}(e.port());
        return h1 ^ (h2 << 1);
    }
};

using ConnectionMap =
std::unordered_map<boost::asio::ip::udp::endpoint, Connection, EndpointHash>;