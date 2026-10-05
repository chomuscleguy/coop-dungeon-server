#pragma once

#include "Connection.h"   
#include "World.h"
#include "Protocol.h"     

#include <chrono>
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

    // payload 는 호출 전에 send_buffer_ 의 헤더 뒤쪽에 써둬야 한다.
    void send_to(Connection& c, protocol::PacketType type, bool reliable,
        std::uint8_t attempts = 1, std::size_t payload_size = 0);

    void broadcast_snapshot();
    void broadcast_events();

    void start_tick_timer();            
    void on_tick();

    void on_second();                   // 30틱에 한 번 하는 일

    boost::asio::ip::udp::socket socket_;
    boost::asio::steady_timer tick_timer_;

    boost::asio::ip::udp::endpoint sender_;
    std::array<std::uint8_t, 1400> buffer_;

    std::array<std::uint8_t, 1400> send_buffer_;
    ConnectionMap connections_;
    World world_;
    std::uint32_t next_connection_id_ = 1;

    // on_ack_received 가 채우는 임시 버퍼. 매번 벡터를 새로 만들지 않으려고
    // 멤버로 둔다. 30Hz × 4인이면 초당 120번 호출된다.
    std::vector<SentPacket> lost_buffer_;
    std::vector<SentPacket> retransmit_buffer_;

    std::vector<protocol::EntityState> snapshot_buffer_;
    std::vector<protocol::EventPayload> event_buffer_;

    // 다음 틱의 목표 시각. "지금부터 33ms"가 아니라 "이 시각에"로 걸어야
    // 처리 시간이 누적되지 않는다.
    std::chrono::steady_clock::time_point next_tick_;
    std::chrono::steady_clock::time_point last_tick_at_;

    std::uint64_t tick_count_ = 0;

    // 틱 간격이 목표에서 얼마나 벗어났는지 (1초마다 리셋)
    double worst_jitter_ms_ = 0.0;
    int ticks_this_second_ = 0;
};