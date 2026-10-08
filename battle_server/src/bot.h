#pragma once

#include <cstdint>
#include <random>
#include <utility>
#include <vector>
#include <boost/asio.hpp>

#include "Protocol.h"

// 봇 하나. 서버의 Connection 과 대칭이지만 코드를 공유하지 않는다 —
// 같은 코드를 쓰면 서버가 틀렸을 때 봇도 같이 틀려서 못 잡는다.
struct Bot {
    boost::asio::ip::udp::socket socket;
    boost::asio::ip::udp::endpoint server;
    boost::asio::ip::udp::endpoint from;
    std::array<std::uint8_t, 1400> buf{};
    std::array<std::uint8_t, 1400> out{};

    std::uint16_t next_seq = 0;
    std::uint16_t remote_seq = 0;
    std::uint32_t recv_bits = 0;
    bool got_any = false;

    // 이동 방향. 일정 시간마다 바꾼다.
    std::int8_t dir_x = 0;
    std::int8_t dir_y = 0;
    int dir_ticks = 0;

    bool connected = false;

    // --- 통계 ---
    std::uint64_t sent_packets = 0;
    std::uint64_t recv_packets = 0;
    std::uint64_t recv_bytes = 0;
    std::uint64_t snapshots = 0;
    std::uint64_t entity_total = 0;     // 스냅샷당 엔티티 수의 합
    std::uint64_t gaps = 0;             // 건너뛴 서버 시퀀스 (유실 추정)
    std::size_t   max_payload = 0;

    // 진짜 왕복 시간. 서버가 되돌려준 토큰으로 직접 잰다.
    std::uint64_t rtt_samples = 0;
    double rtt_sum = 0.0;
    double rtt_min = 1e9;
    double rtt_max = 0.0;

    Bot(boost::asio::io_context& io, boost::asio::ip::udp::endpoint ep)
        : socket(io, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0)),
        server(std::move(ep)) {
    }

    // 서버가 보낸 번호를 기록한다. 서버의 on_packet_received 와 같은 일을
    // 하지만 따로 짠 코드다.
    void note_received(std::uint16_t seq) {
        if (!got_any) {
            remote_seq = seq;
            got_any = true;
            return;
        }
        if (protocol::sequence_greater_than(seq, remote_seq)) {
            std::uint16_t shift = seq - remote_seq;
            if (shift > 1) gaps += (shift - 1);    // 사이가 비었다
            if (shift < 32) {
                recv_bits = (recv_bits << shift) | (1u << (shift - 1));
            }
            else if (shift == 32) {
                recv_bits = (1u << 31);
            }
            else {
                recv_bits = 0;
            }
            remote_seq = seq;
        }
        else if (seq != remote_seq) {
            std::uint16_t diff = remote_seq - seq;
            if (diff <= 32) {
                recv_bits |= (1u << (diff - 1));
                if (gaps > 0) --gaps;              // 늦게 왔을 뿐이었다
            }
        }
    }

    void send(protocol::PacketType type, std::size_t payload_size) {
        protocol::PacketHeader h;
        h.sequence = next_seq++;
        h.ack = remote_seq;
        h.ack_bits = recv_bits;
        h.type = type;
        protocol::write_header(out.data(), h);

        boost::system::error_code ec;
        socket.send_to(
            boost::asio::buffer(out.data(), protocol::kHeaderSize + payload_size),
            server, 0, ec);
        if (!ec) ++sent_packets;
    }
};