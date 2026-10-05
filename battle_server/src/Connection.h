#pragma once

#include "Protocol.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <boost/asio.hpp>

// 내가 보낸 패킷의 기록. 상대의 ack 가 올 때까지 들고 있는다.
struct SentPacket {
	std::uint16_t sequence = 0;
	std::chrono::steady_clock::time_point sent_at;
	bool acked = false;

	// 유실됐을 때 다시 보낼지. 위치 같은 건 false (다음 것이 덮으니까).
	bool reliable = false;
	protocol::PacketType type = protocol::PacketType::Invalid;

	// 재전송하려면 본문도 들고 있어야 한다. 신뢰 패킷만 채워진다.
	// 지금 본문 있는 신뢰 패킷은 Event(5바이트)뿐이라 작게 잡았다.
	std::array<std::uint8_t, 8> payload{};
	std::uint8_t payload_size = 0;

	bool resent = false;    // 이 기록으로 이미 다시 보냈는가 (중복 재전송 방지)
	std::uint8_t attempts = 1;
};

// UDP 클라이언트 하나. TCP의 Session 에 해당하지만 소켓을 갖지 않는다.
// 소켓은 서버에 하나뿐이고, 이건 "누가 어디서 보내는가"의 기록이다.
struct Connection {
	std::uint32_t id = 0;                       // 세션 ID (서버가 발급)
	boost::asio::ip::udp::endpoint endpoint;    // 이 클라이언트의 주소

	std::uint16_t next_sequence = 0;            // 내가 보낼 다음 패킷 번호
	std::uint16_t remote_sequence = 0;          // 상대에게서 받은 가장 최근 번호

	// remote_sequence 이전 32개를 받았는지. 비트 0 = (remote_sequence - 1).
	std::uint32_t received_bits = 0;

	bool has_received_any = false;              // 첫 패킷인지 구분

	std::chrono::steady_clock::time_point last_seen;

	// 이 연결이 조종하는 캐릭터의 엔티티 ID. 실체는 World 에 있다.
	// 0 이면 아직 캐릭터가 없다는 뜻 (World 는 1부터 발급한다).
	std::uint32_t entity_id = 0;

	std::uint16_t take_sequence() { return next_sequence++; }

	// 패킷을 받았을 때 호출. ack/ack_bits 의 재료를 갱신한다.
	void on_packet_received(std::uint16_t seq) {
		if (!has_received_any) {
			remote_sequence = seq;
			received_bits = 0;
			has_received_any = true;
			return;
		}

		if (protocol::sequence_greater_than(seq, remote_sequence)) {
			// 더 최신이 왔다. 기존 비트를 밀고, 옛 remote_sequence 자리를 세운다.
			std::uint16_t shift = seq - remote_sequence;   // wraparound 포함
			if (shift < 32) {
				received_bits = (received_bits << shift) | (1u << (shift - 1));
			}
			else if (shift == 32) {
				received_bits = (1u << 31);                // 창 끝에 겨우 걸침
			}
			else {
				received_bits = 0;                          // 32개 넘게 건너뜀
			}
			remote_sequence = seq;
		}
		else if (seq != remote_sequence) {
			// 늦게 도착한 옛 패킷. 해당 비트만 세운다.
			std::uint16_t diff = remote_sequence - seq;
			if (diff <= 32) {
				received_bits |= (1u << (diff - 1));
			}
			// 32개보다 오래된 건 기록할 자리가 없다. 그냥 버린다.
		}
		// seq == remote_sequence 는 중복 수신. 할 일 없음.
	}

	// 보낸 순서대로 쌓인다. 앞쪽부터 판정이 끝나면 빠진다.
	std::deque<SentPacket> sent_packets;

	std::uint32_t total_sent = 0;
	std::uint32_t total_acked = 0;
	std::uint32_t total_lost = 0;

	// 평활화된 왕복 시간. 표본 하나가 튀어도 흔들리지 않게 조금씩만 반영한다.
	double rtt_ms = 0.0;
	bool has_rtt = false;

	static constexpr double kRttSmoothing = 0.1;   // 새 표본을 10%만 반영
	static constexpr double kMinRtoMs = 30.0;  // 아무리 빨라도 이보다 짧겐 안 기다린다
	static constexpr double kRtoMultiplier = 2.0;

	// 이 횟수를 넘기면 포기한다. 상대가 못 받는 상태라면 더 보내봐야
	// 내 회선만 막는다. UDP 에는 혼잡 제어가 없으니 직접 멈춰야 한다.
	static constexpr std::uint8_t kMaxAttempts = 5;

	std::uint32_t total_given_up = 0;

	// 재전송 대기 시간(Retransmission TimeOut).
	double rto_ms() const {
		if (!has_rtt) return 100.0;                // 아직 모르면 보수적으로
		double rto = rtt_ms * kRtoMultiplier;
		return rto < kMinRtoMs ? kMinRtoMs : rto;
	}

	// 패킷을 보낼 때마다 호출.
	void on_packet_sent(std::uint16_t seq,
		protocol::PacketType type, bool reliable,
		std::uint8_t attempts = 1,
		const std::uint8_t* body = nullptr, std::size_t body_size = 0) {
		SentPacket p;
		p.sequence = seq;
		p.sent_at = std::chrono::steady_clock::now();
		p.reliable = reliable;
		p.type = type;
		p.attempts = attempts;

		// 비신뢰 패킷은 다시 보낼 일이 없으니 본문을 안 들고 있는다.
		// 30Hz 스냅샷을 전부 복사하면 그게 더 큰 낭비다.
		if (reliable && body && body_size <= p.payload.size()) {
			for (std::size_t i = 0; i < body_size; i++) p.payload[i] = body[i];
			p.payload_size = static_cast<std::uint8_t>(body_size);
		}

		sent_packets.push_back(p);
		total_sent++;

		if (sent_packets.size() > 256) {
			sent_packets.pop_front();
		}
	}

	// seq 가 (ack, ack_bits) 에 "도착했다"고 적혀 있는가.
	static bool is_acked_by(std::uint16_t seq,
		std::uint16_t ack, std::uint32_t ack_bits) {
		if (seq == ack) return true;

		// ack 보다 최신이면 상대가 아직 못 받았을 수 있다. 판단하지 않는다.
		if (protocol::sequence_greater_than(seq, ack)) return false;

		std::uint16_t diff = ack - seq;
		if (diff > 32) return false;          // 창 밖
		return (ack_bits & (1u << (diff - 1))) != 0;
	}

	void on_ack_received(std::uint16_t ack, std::uint32_t ack_bits,
		std::vector<SentPacket>& lost) {
		auto now = std::chrono::steady_clock::now();  

		for (SentPacket& p : sent_packets) {
			if (!p.acked && is_acked_by(p.sequence, ack, ack_bits)) {
				p.acked = true;
				++total_acked;

				//RTT 표본. 보낸 시각과 지금의 차이가 왕복 시간이다.
				double sample =
					std::chrono::duration<double, std::milli>(now - p.sent_at).count();
				if (!has_rtt) { rtt_ms = sample; has_rtt = true; }
				else { rtt_ms += (sample - rtt_ms) * kRttSmoothing; }
			}
		}

		while (!sent_packets.empty()) {
			const SentPacket& f = sent_packets.front();

			if (!protocol::sequence_greater_than(ack, f.sequence)) break;
			if (static_cast<std::uint16_t>(ack - f.sequence) <= 32) break;

			if (!f.acked) {
				total_lost++;
				if (!f.resent && f.attempts < kMaxAttempts) lost.push_back(f);
			}

			sent_packets.pop_front();
		}
	}

	// RTO 가 지났는데 아직 ack 가 없는 신뢰 패킷을 모은다.
	// 창(32칸)이 밀리기를 기다리지 않고 먼저 다시 보내기 위한 것.
	void collect_timeouts(std::vector<SentPacket>& out) {
		auto now = std::chrono::steady_clock::now();
		auto rto = std::chrono::duration<double, std::milli>(rto_ms());

		for (SentPacket& p : sent_packets) {
			if (p.acked || p.resent || !p.reliable) continue;
			if (now - p.sent_at < rto) continue;

			p.resent = true;        // 이 기록으로는 한 번만

			if (p.attempts >= kMaxAttempts) {
				total_given_up++;  
				continue;
			}
			out.push_back(p);
		}
	}
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