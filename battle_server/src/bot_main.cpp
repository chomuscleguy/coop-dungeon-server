#include "bot.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <functional>

using boost::asio::ip::udp;

static std::string env_or(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : std::string(fallback);
}

int main(int argc, char** argv) {
    std::cout << std::unitbuf;

    int count = (argc > 1) ? std::atoi(argv[1]) : 4;
    int seconds = (argc > 2) ? std::atoi(argv[2]) : 20;
    std::string host = env_or("BOT_HOST", "127.0.0.1");
    std::string port = env_or("BOT_PORT", "7778");

    std::cout << "bots=" << count << " seconds=" << seconds
        << " target=" << host << ":" << port << '\n';

    auto t0 = std::chrono::steady_clock::now();
    // 마이크로초로 잰다. 루프백은 1ms 미만이라 ms 로는 전부 0이 된다.
    // uint32 는 약 71분에 한 바퀴 도는데, 측정에는 충분하다.
    auto now_us = [&]() -> std::uint32_t {
        return static_cast<std::uint32_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - t0).count());
        };

    boost::asio::io_context io;
    udp::resolver resolver(io);
    udp::endpoint server = *resolver.resolve(udp::v4(), host, port).begin();

    std::vector<std::unique_ptr<Bot>> bots;
    for (int i = 0; i < count; ++i) {
        bots.push_back(std::make_unique<Bot>(io, server));
        bots.back()->send(protocol::PacketType::Handshake, 0);
    }

    std::mt19937 rng(12345);     // 고정 시드 — 돌릴 때마다 같은 움직임
    std::uniform_int_distribution<int> pick(-127, 127);

    // 수신은 비동기로 계속 받는다.
    // recv 를 main 스코프에 두는 것이 중요하다 — 루프 안에 두면 한 바퀴가
    // 끝날 때 죽고, 콜백이 죽은 함수를 부른다(세그폴트).
    std::function<void(Bot*)> start_recv;
    start_recv = [&](Bot* p) {
        p->socket.async_receive_from(
            boost::asio::buffer(p->buf), p->from,
            [&, p](boost::system::error_code ec, std::size_t n) {
                if (ec) return;
                p->recv_packets++;
                p->recv_bytes += n;

                protocol::PacketHeader h;
                if (protocol::read_header(p->buf.data(), n, h)) {
                    p->note_received(h.sequence);
                    p->connected = true;        // 서버가 나에게 보냈다 = 등록됨
                    std::size_t payload = n - protocol::kHeaderSize;
                    if (payload > p->max_payload) p->max_payload = payload;

                    if (h.type == protocol::PacketType::Heartbeat &&
                        payload >= protocol::kHeartbeatPayloadSize) {
                        std::uint32_t sent_at =
                            protocol::read_u32(p->buf.data() + protocol::kHeaderSize);
                        double rtt = (now_us() - sent_at) / 1000.0;   // ms 로 환산
                        p->rtt_samples++;
                        p->rtt_sum += rtt;
                        if (rtt < p->rtt_min) p->rtt_min = rtt;
                        if (rtt > p->rtt_max) p->rtt_max = rtt;
                    }

                    if (h.type == protocol::PacketType::Snapshot && payload >= 1) {
                        p->snapshots++;
                        p->entity_total += p->buf[protocol::kHeaderSize];
                    }
                }
                start_recv(p);
            });
        };

    for (auto& b : bots) {
        start_recv(b.get());
    }

    // 30Hz 로 모든 봇이 입력을 보낸다.
    boost::asio::steady_timer timer(io);
    auto next = std::chrono::steady_clock::now();
    int ticks = 0;
    int total_ticks = seconds * protocol::kTickHz;

    std::function<void()> tick = [&]() {
        if (ticks++ >= total_ticks) { io.stop(); return; }

        for (auto& b : bots) {
            // 아직 등록 안 됐으면 핸드셰이크를 다시 보낸다.
            // 서버가 멱등하게 만들어둔 걸(Step 13) 클라가 쓰는 자리다.
            if (!b->connected) {
                if (ticks % 10 == 0) b->send(protocol::PacketType::Handshake, 0);
                continue;
            }

            if (--b->dir_ticks <= 0) {
                b->dir_x = static_cast<std::int8_t>(pick(rng));
                b->dir_y = static_cast<std::int8_t>(pick(rng));
                b->dir_ticks = 30 + (rng() % 60);
            }
            b->out[protocol::kHeaderSize + 0] = static_cast<std::uint8_t>(b->dir_x);
            b->out[protocol::kHeaderSize + 1] = static_cast<std::uint8_t>(b->dir_y);
            b->send(protocol::PacketType::Input, protocol::kInputPayloadSize);

            // 15틱(0.5초)마다 왕복 시간을 잰다.
            if (ticks % 15 == 0) {
                protocol::write_u32(b->out.data() + protocol::kHeaderSize, now_us());
                b->send(protocol::PacketType::Heartbeat,
                    protocol::kHeartbeatPayloadSize);
            }
        }

        next += protocol::kTickInterval;
        timer.expires_at(next);
        timer.async_wait([&](boost::system::error_code ec) { if (!ec) tick(); });
        };
    tick();

    io.run();

    std::cout << "\n--- 결과 ---\n";
    std::uint64_t sp = 0, rp = 0, rb = 0, sn = 0, et = 0, gp = 0;
    std::size_t mx = 0;

    for (auto& b : bots) {                     
        sp += b->sent_packets; rp += b->recv_packets; rb += b->recv_bytes;
        sn += b->snapshots;    et += b->entity_total; gp += b->gaps;
        if (b->max_payload > mx) mx = b->max_payload;
    }

    double rs = 0; std::uint64_t rn = 0; double rmin = 1e9, rmax = 0;
    for (auto& b : bots) {
        rs += b->rtt_sum; rn += b->rtt_samples;
        if (b->rtt_samples && b->rtt_min < rmin) rmin = b->rtt_min;
        if (b->rtt_max > rmax) rmax = b->rtt_max;
    }
    std::cout << "true rtt  " << (rn ? rs / rn : 0) << "ms 평균"
        << " (min " << (rn ? rmin : 0) << " / max " << rmax << ")"
        << " 표본 " << rn << '\n';

    double secs = static_cast<double>(seconds);
    std::cout << "sent      " << sp << " pkt (" << (sp / secs / count) << "/s/bot)\n";
    std::cout << "recv      " << rp << " pkt, " << rb << " B"
        << " (" << (rb / secs / 1024.0) << " KB/s 전체)\n";
    std::cout << "snapshots " << sn << " (" << (sn / secs / count) << "/s/bot)\n";
    std::cout << "entities  " << (sn ? (double)et / sn : 0) << " 평균\n";
    std::cout << "gaps      " << gp << " ("
        << (rp ? 100.0 * gp / (rp + gp) : 0) << "% 추정 유실)\n";
    std::cout << "max payload " << mx << "B\n";
    return 0;
}