#include "BattleServer.h"
#include "Protocol.h"  

#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <utility>
#include <vector>

using boost::asio::ip::udp;

BattleServer::BattleServer(boost::asio::io_context& io, unsigned short port,
	std::string lobby_host, std::string lobby_port, std::string secret)
	: socket_(io, udp::endpoint(udp::v4(), port)),
	tick_timer_(io),
	lobby_(io, std::move(lobby_host), std::move(lobby_port)),
	secret_(std::move(secret)) {
}

void BattleServer::set_sim_loss(int in_percent, int out_percent) {
	sim_loss_in_ = in_percent;
	sim_loss_out_ = out_percent;
	if (in_percent > 0 || out_percent > 0) {
		std::cout << "SIM LOSS in=" << in_percent << "% out="
			<< out_percent << "%\n";
	}
}

bool BattleServer::drop_roll(int percent) {
	if (percent <= 0) return false;
	return (sim_rng_() % 100) < static_cast<unsigned>(percent);
}

void BattleServer::start() {
	std::cout << "Battle server listening on UDP "
		<< socket_.local_endpoint().port()
		<< " (tick " << protocol::kTickHz << "Hz)" << '\n';

	next_tick_ = std::chrono::steady_clock::now();
	last_tick_at_ = next_tick_;

	lobby_.on_message = [this](const std::string& json) {
		handle_lobby_message(json);
		};

	// 연결되자마자 자기소개를 한다. 로비는 이걸 받아야 이 연결이
	// 사람이 아니라 배틀 서버라는 걸 안다.
	lobby_.on_connected = [this]() {
		std::ostringstream os;
		os << "{\"type\":\"BattleRegister\",\"data\":{"
			<< "\"secret\":\"" << secret_ << "\","
			<< "\"port\":" << socket_.local_endpoint().port()
			<< "}}";
		std::cout << "[lobby] -> " << os.str() << '\n';
		lobby_.send(os.str());
		};

	lobby_.start();
	do_receive();
	start_tick_timer();
}

void BattleServer::stop() {
	std::cout << "Shutting down..." << '\n';
	lobby_.stop();
	boost::system::error_code ec;
	socket_.close(ec);
	tick_timer_.cancel();
}

void BattleServer::do_receive() {
	socket_.async_receive_from(
		boost::asio::buffer(buffer_), sender_,
		[this](boost::system::error_code ec, std::size_t size) {
			if (ec) return;

			// 업링크 유실 흉내. 받았지만 못 받은 척한다.
			if (drop_roll(sim_loss_in_)) {
				stats_.sim_dropped_in += 1;
				do_receive();
				return;
			}

			stats_.recv_bytes += size;
			stats_.recv_packets += 1;

			handle_packet(sender_, buffer_.data(), size);
			do_receive();
		});
}

void BattleServer::handle_packet(const udp::endpoint& from,
	const std::uint8_t* data, std::size_t size) {
	protocol::PacketHeader h;
	if (!protocol::read_header(data, size, h)) {
		stats_.recv_dropped += 1;
		return;
	}

	// 핸드셰이크는 아직 세션이 없는 상태에서 온다. 유일한 예외.
	if (h.type == protocol::PacketType::Handshake) {
		handle_handshake(from, h.sequence, data, size);
		return;
	}

	// 그 외는 등록된 주소에서 온 것만 받는다.
	Connection* c = find_connection(from);
	if (!c) {
		stats_.recv_dropped += 1;
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
		// 본문이 있으면 해석하지 않고 그대로 돌려준다.
		// 서버는 이 값이 뭔지 모르고 알 필요도 없다.
		std::size_t payload = size - protocol::kHeaderSize;
		if (payload > protocol::kHeartbeatPayloadSize) {
			payload = protocol::kHeartbeatPayloadSize;
		}
		for (std::size_t i = 0; i < payload; ++i) {
			send_buffer_[protocol::kHeaderSize + i] = data[protocol::kHeaderSize + i];
		}
		send_to(*c, protocol::PacketType::Heartbeat, false, 1, payload);
		return;
	}

	if (h.type == protocol::PacketType::Input) {
		// 늦게 도착한 입력은 버린다. 적용하면 캐릭터가 뒤로 튄다.
		// on_packet_received 가 방금 갱신했으니, 이게 최신이면 둘이 같다.
		if (h.sequence != c->remote_sequence) return;

		protocol::InputPayload in;
		if (protocol::read_input(data + protocol::kHeaderSize,
			size - protocol::kHeaderSize, in)) {
			Player* p = world_.find_player(c->entity_id);
			if (p) p->set_input(in.move_x, in.move_y);
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

void BattleServer::handle_handshake(const udp::endpoint& from, std::uint16_t seq,
	const std::uint8_t* data, std::size_t size) {

	Connection* existing = find_connection(from);
	if (existing) {
		// 이미 들어온 사람. 토큰을 다시 보지 않는다 — 재전송된
		// 핸드셰이크일 뿐이고, 토큰은 이미 소비됐다. (Step 13 멱등성)
		existing->last_seen = std::chrono::steady_clock::now();
		existing->on_packet_received(seq);
		send_to(*existing, protocol::PacketType::HandshakeAck, true);
		return;
	}

	std::string username;
	std::uint32_t lobby_id = 0;
	std::uint32_t grant_battle = 0;

	std::size_t payload = size - protocol::kHeaderSize;
	if (payload >= protocol::kTokenSize) {
		std::string token(reinterpret_cast<const char*>(data + protocol::kHeaderSize),
			protocol::kTokenSize);

		auto it = pending_.find(token);
		if (it != pending_.end()) {
			username = it->second.username;
			lobby_id = it->second.lobby_player_id;
			grant_battle = it->second.battle_id;
			// 한 번 쓰면 사라진다. 같은 표로 둘이 들어올 수 없다.
			pending_.erase(it);
		}
		else if (!allow_anonymous_) {
			// 모르는 표. 응답하지 않는다 — 틀렸다고 알려줄 이유가 없다.
			std::cout << "Handshake rejected (unknown token) from " << from << '\n';
			return;
		}
	}
	else if (!allow_anonymous_) {
		std::cout << "Handshake rejected (no token) from " << from << '\n';
		return;
	}

	Connection c;
	c.id = next_connection_id_++;
	c.endpoint = from;
	c.last_seen = std::chrono::steady_clock::now();
	c.on_packet_received(seq);
	c.entity_id = world_.add_player();
	c.username = username;
	c.lobby_player_id = lobby_id;

	connections_.emplace(from, c);

	// 명단에 올린다. 중간에 끊겨도 여기서는 안 지운다 —
	// 결과는 "지금 접속해 있는 사람"이 아니라 "이 판을 돈 사람"이다.
	if (!username.empty()) {
		battle_id_ = grant_battle;
		bool known = false;
		for (const auto& u : roster_) { if (u == username) { known = true; break; } }
		if (!known) roster_.push_back(username);
	}

	std::cout << "Handshake from " << from
		<< " -> conn " << c.id
		<< " (entity " << c.entity_id
		<< ", user " << (username.empty() ? "<anon>" : username) << ")" << '\n';

	send_to(connections_[from], protocol::PacketType::HandshakeAck, true);
}

void BattleServer::send_to(Connection& c, protocol::PacketType type, bool reliable,
	std::uint8_t attempts, std::size_t payload_size) {
	protocol::PacketHeader h;
	h.sequence = c.take_sequence();
	h.ack = c.remote_sequence;
	h.ack_bits = c.received_bits;
	h.type = type;

	protocol::write_header(send_buffer_.data(), h);

	c.on_packet_sent(h.sequence, type, reliable, attempts,
		send_buffer_.data() + protocol::kHeaderSize, payload_size);

	std::size_t total = protocol::kHeaderSize + payload_size;

	// 다운링크 유실 흉내. on_packet_sent 는 이미 불렀으므로
	// 신뢰성 계층은 "보냈다"고 믿고, ack 가 안 오면 유실로 판정한다.
	// 바로 그게 시험하려는 동작이다.
	if (drop_roll(sim_loss_out_)) {
		stats_.sim_dropped_out += 1;
		return;
	}

	stats_.sent_bytes += total;
	stats_.sent_packets += 1;
	std::size_t ti = static_cast<std::size_t>(type);
	if (ti < protocol::kPacketTypeCount) {
		stats_.sent_by_type[ti] += total;
		stats_.sent_count_by_type[ti] += 1;
	}

	boost::system::error_code ec;
	socket_.send_to(
		boost::asio::buffer(send_buffer_.data(), total),
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

	// 게임을 한 틱 진행한다. 네트워크와 분리된 부분.
	world_.update(protocol::kTickSeconds);

	// 매 틱: 재전송 검사. RTO 해상도가 1초에서 33ms 로 내려왔다.
	for (auto& entry : connections_) {
		Connection& c = entry.second;

		retransmit_buffer_.clear();
		c.collect_timeouts(retransmit_buffer_);
		for (const SentPacket& p : retransmit_buffer_) {
			std::cout << "[conn " << c.id << "] RETRY "
				<< protocol::to_string(p.type)
				<< " seq=" << p.sequence
				<< " try=" << static_cast<int>(p.attempts + 1) << '\n';

			// 본문을 버퍼에 되살린 뒤 보낸다. 이게 없으면 빈 패킷이 나간다.
			for (std::size_t i = 0; i < p.payload_size; ++i) {
				send_buffer_[protocol::kHeaderSize + i] = p.payload[i];
			}
			send_to(c, p.type, true, p.attempts + 1, p.payload_size);
		}
	}

	broadcast_events();
	broadcast_room_state();
	broadcast_snapshot();

	// 상태가 "바뀐" 순간에만 한 번 보낸다. 매 틱 보내면 로비가 터진다.
	if (world_.phase != last_phase_) {
		if (world_.phase == protocol::RoomPhase::Failed) {
			report_result("wipe");
		}
		last_phase_ = world_.phase;
	}

	if (tick_count_ % protocol::kTickHz == 0) {
		on_second();
	}
}

void BattleServer::broadcast_events() {
	if (connections_.empty()) return;
	if (world_.died_this_tick.empty()) return;

	for (std::uint32_t id : world_.died_this_tick) {
		protocol::EventPayload e;
		e.kind = protocol::EventKind::MonsterDied;
		e.entity_id = id;

		protocol::write_event(send_buffer_.data() + protocol::kHeaderSize, e);

		for (auto& entry : connections_) {
			send_to(entry.second, protocol::PacketType::Event,
				true, 1, protocol::kEventPayloadSize);
		}
	}
}

void BattleServer::broadcast_room_state() {
	if (connections_.empty()) return;

	protocol::RoomStatePayload s;
	s.room = static_cast<std::uint8_t>(world_.room_index);
	s.wave = static_cast<std::int8_t>(world_.wave_index);
	s.phase = world_.phase;
	s.monsters = static_cast<std::uint8_t>(
		world_.monster_count() > 255 ? 255 : world_.monster_count());
	s.at_door = static_cast<std::uint8_t>(world_.at_door);
	s.players = static_cast<std::uint8_t>(world_.player_count());
	s.countdown_ms = static_cast<std::uint16_t>(
		world_.transition_timer > 0.0f ? world_.transition_timer * 1000.0f : 0);

	// 카운트다운은 매 틱 바뀌므로 비교에서 뺀다. 그것만 바뀌었으면
	// 1초에 한 번만 보내면 된다 — 클라가 자기 시계로 세면 되니까.
	bool changed =
		s.room != last_room_state_.room ||
		s.wave != last_room_state_.wave ||
		s.phase != last_room_state_.phase ||
		s.at_door != last_room_state_.at_door ||
		s.players != last_room_state_.players;

	bool periodic = (tick_count_ % protocol::kTickHz == 0);
	if (!changed && !periodic) return;

	last_room_state_ = s;

	protocol::write_room_state(send_buffer_.data() + protocol::kHeaderSize, s);
	for (auto& entry : connections_) {
		send_to(entry.second, protocol::PacketType::RoomState,
			false, 1, protocol::kRoomStateSize);
	}
}

void BattleServer::report_result(const char* result) {
	// 18d 에서는 손으로 만들었다. 전부 정수와 고정 문자열이었으니까.
	// 이제 사용자 이름이 들어온다 — 따옴표나 역슬래시가 섞이면 손으로 만든
	// JSON 은 깨진다. 조건이 바뀌었으니 방식도 바꾼다.
	nlohmann::json msg;
	msg["type"] = "DungeonResult";
	msg["data"] = {
		{"secret", secret_},
		{"battleId", battle_id_},
		{"result", result},
		{"room", world_.final_room},
		{"wave", world_.final_wave},
		{"kills", world_.total_kills},
		{"players", roster_}
	};

	std::string body = msg.dump();
	std::cout << "[lobby] -> " << body << '\n';
	lobby_.send(body);
}

void BattleServer::handle_lobby_message(const std::string& json) {
	try {
		nlohmann::json msg = nlohmann::json::parse(json);
		std::string type = msg.value("type", "");
		nlohmann::json data = msg.value("data", nlohmann::json::object());

		if (type != "SessionGrant") {
			std::cout << "[lobby] <- unknown: " << type << '\n';
			return;
		}

		if (data.value("secret", "") != secret_) {
			std::cerr << "[lobby] SessionGrant rejected: bad secret\n";
			return;
		}

		std::uint32_t battle_id = data.value("battle_id", 0u);
		int added = 0;

		for (const auto& p : data.value("players", nlohmann::json::array())) {
			std::string token = p.value("token", "");
			if (token.size() != protocol::kTokenSize) continue;

			PendingPlayer pp;
			pp.username = p.value("username", "");
			pp.lobby_player_id = p.value("player_id", 0u);
			pp.battle_id = battle_id;
			pending_[token] = pp;
			++added;
		}

		std::cout << "[lobby] SessionGrant battle=" << battle_id
			<< " players=" << added << '\n';
	}
	catch (const std::exception& e) {
		// 로비는 우리 편이지만 파싱은 방어적이어야 한다.
		std::cerr << "[lobby] parse error: " << e.what() << '\n';
	}
}

void BattleServer::broadcast_snapshot() {
	if (connections_.empty()) return;

	world_.collect_states(snapshot_buffer_);
	if (snapshot_buffer_.empty()) return;

	std::size_t payload_size = protocol::write_snapshot(
		send_buffer_.data() + protocol::kHeaderSize,
		send_buffer_.size() - protocol::kHeaderSize,
		snapshot_buffer_.data(), snapshot_buffer_.size());

	if (payload_size > stats_.peak_snapshot) stats_.peak_snapshot = payload_size;
	if (payload_size > peak_snapshot_ever_) peak_snapshot_ever_ = payload_size;

	for (auto& entry : connections_) {
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

	{
		std::uint64_t wire_sent =
			stats_.sent_bytes + stats_.sent_packets * protocol::kWireOverhead;
		std::uint64_t wire_recv =
			stats_.recv_bytes + stats_.recv_packets * protocol::kWireOverhead;

		std::cout << "[net] sent " << stats_.sent_bytes << "B"
			<< " (" << stats_.sent_packets << " pkt, wire " << wire_sent << "B)"
			<< " | recv " << stats_.recv_bytes << "B"
			<< " (" << stats_.recv_packets << " pkt, wire " << wire_recv << "B"
			<< ", dropped " << stats_.recv_dropped << ")"
			<< " | sim in=" << stats_.sim_dropped_in
			<< " out=" << stats_.sim_dropped_out
			<< " | snap peak " << stats_.peak_snapshot
			<< "/" << peak_snapshot_ever_ << "B"
			<< " | conns " << connections_.size() << '\n';

		std::cout << "[net]  ";
		for (std::size_t i = 0; i < protocol::kPacketTypeCount; ++i) {
			if (stats_.sent_count_by_type[i] == 0) continue;
			std::cout << " " << protocol::to_string(static_cast<protocol::PacketType>(i))
				<< "=" << stats_.sent_by_type[i] << "B/"
				<< stats_.sent_count_by_type[i];
		}
		std::cout << '\n';

		stats_.reset();
	}

	const char* phase_name = "?";
	switch (world_.phase) {
	case protocol::RoomPhase::Fighting:      phase_name = "Fighting"; break;
	case protocol::RoomPhase::Cleared:       phase_name = "Cleared"; break;
	case protocol::RoomPhase::Transitioning: phase_name = "Transitioning"; break;
	case protocol::RoomPhase::Failed:        phase_name = "FAILED"; break;
	}

	std::cout << "[world] room=" << world_.room_index
		<< " wave=" << world_.wave_index
		<< " phase=" << phase_name
		<< " monsters=" << world_.monster_count()
		<< " atdoor=" << world_.at_door
		<< " kills=" << world_.total_kills << '\n';

	worst_jitter_ms_ = 0.0;
	ticks_this_second_ = 0;

	std::vector<udp::endpoint> dead;
	for (auto& entry : connections_) {
		Connection& c = entry.second;

		std::cout << "[conn " << c.id << "] sent=" << c.total_sent
			<< " acked=" << c.total_acked
			<< " lost=" << c.total_lost
			<< " inflight=" << c.sent_packets.size()
			<< " ackrtt=" << c.rtt_ms << "ms"
			<< " rto=" << c.rto_ms() << "ms"
			<< " gaveup=" << c.total_given_up << '\n';

		const Player* p = world_.find_player(c.entity_id);
		if (p) {
			std::cout << "[conn " << c.id << "] entity=" << p->id
				<< " input=(" << static_cast<int>(p->input_x) << ","
				<< static_cast<int>(p->input_y) << ")"
				<< " pos=(" << p->x << "," << p->y << ")" << '\n';
		}

		if (now - c.last_seen > limit) {
			dead.push_back(entry.first);
		}
	}

	for (const udp::endpoint& ep : dead) {
		Connection& c = connections_[ep];
		std::cout << "Timeout: conn " << c.id
			<< " (entity " << c.entity_id << ", " << ep << ")" << '\n';
		world_.remove_player(c.entity_id);
		connections_.erase(ep);
	}
}