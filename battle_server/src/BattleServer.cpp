#include "BattleServer.h"
#include "Protocol.h"  
#include <iostream>
#include <vector>

using boost::asio::ip::udp;

BattleServer::BattleServer(boost::asio::io_context& io, unsigned short port)
    : socket_(io, udp::endpoint(udp::v4(), port)),
    tick_timer_(io) {
}

void BattleServer::start() {
    std::cout << "Battle server listening on UDP "
        << socket_.local_endpoint().port() << '\n';
    do_receive();
    start_tick_timer();                 
}

void BattleServer::stop() {
    std::cout << "Shutting down..." << '\n';
    boost::system::error_code ec;
    socket_.close(ec);
    tick_timer_.cancel();
}

void BattleServer::do_receive() {
    socket_.async_receive_from(
        boost::asio::buffer(buffer_), sender_,
        [this](boost::system::error_code ec, std::size_t size) {
            // 소켓이 닫히면(stop) 에러와 함께 불린다. 여기서 끝내야
            // io.run() 이 반환한다. (로비 Step 12에서 배운 것)
            if (ec) return;

            handle_packet(sender_, buffer_.data(), size);
            do_receive();
        });
}

void BattleServer::handle_packet(const udp::endpoint& from,
    const std::uint8_t* data, std::size_t size) {
    protocol::PacketHeader h;
    if (!protocol::read_header(data, size, h)) {
        return;
    }

    // 핸드셰이크는 아직 세션이 없는 상태에서 온다. 유일한 예외.
    if (h.type == protocol::PacketType::Handshake) {
        handle_handshake(from);
        return;
    }

    // 그 외는 등록된 주소에서 온 것만 받는다.
    Connection* c = find_connection(from);
    if (!c) {
        // 모르는 주소. 핸드셰이크부터 하라고 알려줄 수도 있지만,
        // 아무나 보낼 수 있는 UDP에서 그건 증폭 공격의 발판이 된다.
        return;
    }

    c->last_seen = std::chrono::steady_clock::now();
    c->remote_sequence = h.sequence;

    // 하트비트는 받은 사실만으로 last_seen 이 갱신되므로 되돌려주기만 하면 된다.
    // 클라이언트도 서버가 살아있는지 알아야 한다.
    if (h.type == protocol::PacketType::Heartbeat) {
        send_to(*c, protocol::PacketType::Heartbeat);
        return;
    }

    std::cout << "[conn " << c->id << "] " << protocol::to_string(h.type)
        << " seq=" << h.sequence
        << " payload=" << (size - protocol::kHeaderSize) << "B"
        << '\n';
}

Connection* BattleServer::find_connection(const udp::endpoint& from) {
    auto it = connections_.find(from);
    return (it == connections_.end()) ? nullptr : &it->second;
}

void BattleServer::handle_handshake(const udp::endpoint& from) {
    Connection* existing = find_connection(from);
    if (existing) {
        // 재전송된 핸드셰이크. UDP라 응답이 유실됐을 수 있으니 다시 보내준다.
        // 새로 만들면 같은 클라이언트에 세션이 둘 생긴다.
        existing->last_seen = std::chrono::steady_clock::now();
        send_to(*existing, protocol::PacketType::HandshakeAck);
        return;
    }

    Connection c;
    c.id = next_connection_id_++;
    c.endpoint = from;
    c.last_seen = std::chrono::steady_clock::now();

    connections_.emplace(from, c);
    std::cout << "Handshake from " << from << " -> conn " << c.id << '\n';

    send_to(connections_[from], protocol::PacketType::HandshakeAck);
}

void BattleServer::send_to(Connection& c, protocol::PacketType type) {
    protocol::PacketHeader h;
    h.sequence = c.take_sequence();
    h.ack = c.remote_sequence;
    h.ack_bits = 0;                 // 14에서 채운다
    h.type = type;

    protocol::write_header(send_buffer_.data(), h);

    boost::system::error_code ec;
    socket_.send_to(
        boost::asio::buffer(send_buffer_.data(), protocol::kHeaderSize),
        c.endpoint, 0, ec);

    if (ec) {
        std::cout << "send_to failed: " << ec.message() << '\n';
    }
}

void BattleServer::start_tick_timer() {
    tick_timer_.expires_after(std::chrono::seconds(1));
    tick_timer_.async_wait([this](boost::system::error_code ec) {
        if (ec) return;
        on_tick();
        start_tick_timer();
        });
}

void BattleServer::on_tick() {
    auto now = std::chrono::steady_clock::now();
    auto limit = std::chrono::seconds(protocol::kTimeoutSeconds);

    // 순회 중에 지울 수 없으니 먼저 모아두고 그 다음에 처리한다.
    std::vector<udp::endpoint> dead;
    for (const auto& entry : connections_) {
        if (now - entry.second.last_seen > limit) {
            dead.push_back(entry.first);
        }
    }

    for (const udp::endpoint& ep : dead) {
        std::cout << "Timeout: conn " << connections_[ep].id
            << " (" << ep << ")" << '\n';
        connections_.erase(ep);
    }
}