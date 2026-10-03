#pragma once

#include <array>
#include <cstdint>
#include <utility>
#include <boost/asio.hpp>

#include "Connection.h"   
#include "Protocol.h"     

class BattleServer {
public:
    BattleServer(boost::asio::io_context& io, unsigned short port);

    void start();
    void stop();

private:
    void do_receive();
    void handle_packet(const boost::asio::ip::udp::endpoint& from,
        const std::uint8_t* data, std::size_t size);

    // ★ 아래 넷 추가
    void handle_handshake(const boost::asio::ip::udp::endpoint& from);
    Connection* find_connection(const boost::asio::ip::udp::endpoint& from);

    // 헤더만 있는 패킷을 보낸다 (payload 없음)
    void send_to(Connection& c, protocol::PacketType type);

    void start_tick_timer();            
    void on_tick();

    boost::asio::ip::udp::socket socket_;
    boost::asio::steady_timer tick_timer_;

    boost::asio::ip::udp::endpoint sender_;
    std::array<std::uint8_t, 1400> buffer_;

    std::array<std::uint8_t, 1400> send_buffer_;
    ConnectionMap connections_;
    std::uint32_t next_connection_id_ = 1;
};