#pragma once

#include "Connection.h"   
#include "World.h"
#include "LobbyReport.h"
#include "Protocol.h"     

#include <string>
#include <chrono>
#include <array>
#include <vector>
#include <cstdint>
#include <utility>
#include <random>
#include <boost/asio.hpp>

// 1초치 트래픽. on_second 에서 찍고 비운다.
struct TrafficStats {
    std::uint64_t sent_bytes = 0;
    std::uint64_t sent_packets = 0;
    std::uint64_t recv_bytes = 0;
    std::uint64_t recv_packets = 0;
    std::uint64_t recv_dropped = 0;     // 파싱 실패 또는 모르는 주소
    std::uint64_t sim_dropped_in = 0;   // 받았지만 안 받은 척
    std::uint64_t sim_dropped_out = 0;  // 보냈다 치고 안 보냄

    std::array<std::uint64_t, protocol::kPacketTypeCount> sent_by_type{};
    std::array<std::uint64_t, protocol::kPacketTypeCount> sent_count_by_type{};

    std::size_t peak_snapshot = 0;      // 이 1초의 최대 스냅샷 본문 크기

    void reset() {
        sent_bytes = sent_packets = 0;
        recv_bytes = recv_packets = recv_dropped = 0;
        sent_by_type.fill(0);
        sent_count_by_type.fill(0);
        peak_snapshot = 0;
        sim_dropped_in = sim_dropped_out = 0;
    }
};

class BattleServer {
public:
    BattleServer(boost::asio::io_context& io, unsigned short port,
        std::string lobby_host, std::string lobby_port,
        std::string secret);

    // 생성자가 아니라 세터로 받는다. 생성자 인자가 다섯을 넘으면
    // 호출부에서 순서를 틀리기 시작한다.
    void set_sim_loss(int in_percent, int out_percent);

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
    void broadcast_room_state();

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

    // 직전에 보낸 방 상태. 바뀌었을 때만 즉시 보내기 위한 비교용.
    protocol::RoomStatePayload last_room_state_{};

    // 틱 간격이 목표에서 얼마나 벗어났는지 (1초마다 리셋)
    double worst_jitter_ms_ = 0.0;
    int ticks_this_second_ = 0;

    void report_result(const char* result);

    boost::asio::io_context& io_;        // LobbyReport 에 넘겨줘야 한다
    std::string lobby_host_;
    std::string lobby_port_;
    std::string secret_;

    // 이번 틱에 Failed 로 **바뀌었는지** 보려면 직전 값이 필요하다.
    protocol::RoomPhase last_phase_ = protocol::RoomPhase::Fighting;

    TrafficStats stats_;
    std::size_t peak_snapshot_ever_ = 0;

    bool drop_roll(int percent);

    int sim_loss_in_ = 0;
    int sim_loss_out_ = 0;
    std::mt19937 sim_rng_{ 7777 };     // 고정 시드 — 같은 조건이면 같은 유실
};