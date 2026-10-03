#pragma once

#include "Connection.h"   
#include "Protocol.h"     

#include <array>
#include <vector>
#include <cstdint>
#include <utility>
#include <boost/asio.hpp>

class BattleServer {
public:
    BattleServer(boost::asio::io_context& io, unsigned short port);

    void start();
    void stop();

private:
    void do_receive();
    void handle_packet(const boost::asio::ip::udp::endpoint& from,
        const std::uint8_t* data, std::size_t size);

    void handle_handshake(const boost::asio::ip::udp::endpoint& from, std::uint16_t seq);
    Connection* find_connection(const boost::asio::ip::udp::endpoint& from);

    // 헤더만 있는 패킷을 보낸다 (payload 없음)
    void send_to(Connection& c, protocol::PacketType type, bool reliable,
        std::uint8_t attempts = 1);

    void start_tick_timer();            
    void on_tick();

    boost::asio::ip::udp::socket socket_;
    boost::asio::steady_timer tick_timer_;

    boost::asio::ip::udp::endpoint sender_;
    std::array<std::uint8_t, 1400> buffer_;

    std::array<std::uint8_t, 1400> send_buffer_;
    ConnectionMap connections_;
    std::uint32_t next_connection_id_ = 1;

    // on_ack_received 가 채우는 임시 버퍼. 매번 벡터를 새로 만들지 않으려고
    // 멤버로 둔다. 30Hz × 4인이면 초당 120번 호출된다.
    std::vector<SentPacket> lost_buffer_;
    std::vector<SentPacket> retransmit_buffer_;
};