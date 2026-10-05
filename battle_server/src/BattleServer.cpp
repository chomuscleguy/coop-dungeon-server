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
		<< socket_.local_endpoint().port()
		<< " (tick " << protocol::kTickHz << "Hz)" << '\n';

	next_tick_ = std::chrono::steady_clock::now();      
	last_tick_at_ = next_tick_;                          

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
		handle_handshake(from, h.sequence);
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
	c->on_packet_received(h.sequence);

	lost_buffer_.clear();
	c->on_ack_received(h.ack, h.ack_bits, lost_buffer_);

	for (const SentPacket& p : lost_buffer_) {
		if (!p.reliable) {
			// 버린다. 위치 정보라면 이미 더 최신 것이 도착해 있다.
			std::cout << "[conn " << c->id << "] DROP "
				<< protocol::to_string(p.type) << " seq=" << p.sequence << '\n';
			continue;
		}
		std::cout << "[conn " << c->id << "] RESEND "
			<< protocol::to_string(p.type) << " (lost seq=" << p.sequence << ")\n";
		send_to(*c, p.type, true);
	}


	// 하트비트는 받은 사실만으로 last_seen 이 갱신되므로 되돌려주기만 하면 된다.
	// 클라이언트도 서버가 살아있는지 알아야 한다.
	if (h.type == protocol::PacketType::Heartbeat) {
		send_to(*c, protocol::PacketType::Heartbeat, false);
		return;
	}

	if (h.type == protocol::PacketType::Input) {
		// 늦게 도착한 입력은 버린다. 적용하면 캐릭터가 뒤로 튄다.
		// on_packet_received 가 방금 갱신했으니, 이게 최신이면 둘이 같다.
		if (h.sequence != c->remote_sequence) return;

		protocol::InputPayload in;
		if (protocol::read_input(data + protocol::kHeaderSize,
			size - protocol::kHeaderSize, in)) {
			c->player.set_input(in.move_x, in.move_y);
		}
		return;      // 초당 30번 오므로 로그를 남기지 않는다
	}

	std::cout << "[conn " << c->id << "] " << protocol::to_string(h.type)
		<< " seq=" << h.sequence
		<< " | ack=" << c->remote_sequence
		<< " bits=0x" << std::hex << c->received_bits << std::dec
		<< '\n';
}

Connection* BattleServer::find_connection(const udp::endpoint& from) {
	auto it = connections_.find(from);
	return (it == connections_.end()) ? nullptr : &it->second;
}

void BattleServer::handle_handshake(const udp::endpoint& from, std::uint16_t seq) {
	Connection* existing = find_connection(from);
	if (existing) {
		existing->last_seen = std::chrono::steady_clock::now();
		existing->on_packet_received(seq);
		send_to(*existing, protocol::PacketType::HandshakeAck, true);
		return;
	}

	Connection c;
	c.id = next_connection_id_++;
	c.endpoint = from;
	c.last_seen = std::chrono::steady_clock::now();
	c.on_packet_received(seq);

	connections_.emplace(from, c);
	std::cout << "Handshake from " << from << " -> conn " << c.id << '\n';

	send_to(connections_[from], protocol::PacketType::HandshakeAck, true);
}

void BattleServer::send_to(Connection& c, protocol::PacketType type, bool reliable,
	std::uint8_t attempts, std::size_t payload_size) {    
	protocol::PacketHeader h;
	h.sequence = c.take_sequence();
	h.ack = c.remote_sequence;
	h.ack_bits = c.received_bits;
	h.type = type;
	c.on_packet_sent(h.sequence, type, reliable, attempts);

	protocol::write_header(send_buffer_.data(), h);

	boost::system::error_code ec;
	socket_.send_to(
		boost::asio::buffer(send_buffer_.data(),
			protocol::kHeaderSize + payload_size),      
		c.endpoint, 0, ec);

	if (ec) {
		std::cout << "send_to failed: " << ec.message() << '\n';
	}
}

void BattleServer::start_tick_timer() {
	next_tick_ += protocol::kTickInterval;

	// 서버가 한참 멈췄다 깨어나면 밀린 틱이 몰려서 터진다.
	// 따라잡기를 포기하고 현재 시각으로 다시 맞춘다.
	auto now = std::chrono::steady_clock::now();
	if (next_tick_ + protocol::kTickInterval * 5 < now) {
		std::cout << "Tick fell behind, resyncing\n";
		next_tick_ = now + protocol::kTickInterval;
	}

	tick_timer_.expires_at(next_tick_); 
	tick_timer_.async_wait([this](boost::system::error_code ec) {
		if (ec) return;
		on_tick();
		start_tick_timer();
		});
}

void BattleServer::on_tick() {
	auto now = std::chrono::steady_clock::now();

	// 실제 간격이 목표에서 얼마나 벗어났는지 잰다.
	double actual_ms =
		std::chrono::duration<double, std::milli>(now - last_tick_at_).count();
	double target_ms =
		std::chrono::duration<double, std::milli>(protocol::kTickInterval).count();
	double jitter = actual_ms - target_ms;
	if (jitter < 0) jitter = -jitter;
	if (jitter > worst_jitter_ms_) worst_jitter_ms_ = jitter;
	last_tick_at_ = now;

	tick_count_++;
	ticks_this_second_++;

	// 매 틱: 재전송 검사. RTO 해상도가 1초에서 33ms 로 내려왔다.
	for (auto& entry : connections_) {
		Connection& c = entry.second;

		// 게임 시뮬레이션: 마지막 입력을 고정 dt 만큼 적용한다
		c.player.apply_input(protocol::kTickSeconds);

		retransmit_buffer_.clear();
		c.collect_timeouts(retransmit_buffer_);
		for (const SentPacket& p : retransmit_buffer_) {
			std::cout << "[conn " << c.id << "] RETRY "
				<< protocol::to_string(p.type)
				<< " seq=" << p.sequence
				<< " try=" << static_cast<int>(p.attempts + 1) << '\n';
			send_to(c, p.type, true, p.attempts + 1);
		}
	}

	broadcast_snapshot();

	if (tick_count_ % protocol::kTickHz == 0) {
		on_second();
	}
}

void BattleServer::broadcast_snapshot() {
	snapshot_buffer_.clear();
	for (const auto& entry : connections_) {
		protocol::PlayerState s;
		s.id = entry.second.id;
		s.x = entry.second.player.x;
		s.y = entry.second.player.y;
		snapshot_buffer_.push_back(s);
	}
	if (snapshot_buffer_.empty()) return;

	// 본문은 한 번만 쓴다. 모두에게 같은 내용이 나간다.
	std::size_t payload_size = protocol::write_snapshot(
		send_buffer_.data() + protocol::kHeaderSize,
		send_buffer_.size() - protocol::kHeaderSize,
		snapshot_buffer_.data(), snapshot_buffer_.size());

	for (auto& entry : connections_) {
		// 헤더만 매번 다시 쓰인다 (seq/ack 가 연결마다 다르므로).
		send_to(entry.second, protocol::PacketType::Snapshot,
			false, 1, payload_size);
	}
}

void BattleServer::on_second() {
	auto now = std::chrono::steady_clock::now();
	auto limit = std::chrono::seconds(protocol::kTimeoutSeconds);

	std::cout << "[tick] " << ticks_this_second_ << "/s"
		<< " worst_jitter=" << worst_jitter_ms_ << "ms"
		<< " total=" << tick_count_ << '\n';
	worst_jitter_ms_ = 0.0;
	ticks_this_second_ = 0;

	std::vector<udp::endpoint> dead;
	for (auto& entry : connections_) {
		Connection& c = entry.second;

		
		std::cout << "[conn " << c.id << "] sent=" << c.total_sent
			<< " acked=" << c.total_acked
			<< " lost=" << c.total_lost
			<< " inflight=" << c.sent_packets.size()
			<< " rtt=" << c.rtt_ms << "ms"
			<< " rto=" << c.rto_ms() << "ms"
			<< " gaveup=" << c.total_given_up << '\n';

		std::cout << "[conn " << c.id << "] input=("
			<< static_cast<int>(c.player.input_x) << ","
			<< static_cast<int>(c.player.input_y) << ")"
			<< " pos=(" << c.player.x << "," << c.player.y << ")" << '\n';

		// 재전송 동작을 보려고 넣은 임시 코드. Step 16에서 진짜 이벤트로 바뀐다.
		send_to(c, protocol::PacketType::Event, true);

		if (now - c.last_seen > limit) {
			dead.push_back(entry.first);
		}
	}

	for (const udp::endpoint& ep : dead) {
		std::cout << "Timeout: conn " << connections_[ep].id
			<< " (" << ep << ")" << '\n';
		connections_.erase(ep);
	}
}