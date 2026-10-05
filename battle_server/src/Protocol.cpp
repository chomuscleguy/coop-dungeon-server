#include "Protocol.h"

namespace protocol {

	void write_u8(std::uint8_t* p, std::uint8_t v) {
		p[0] = v;
	}

	void write_u16(std::uint8_t* p, std::uint16_t v) {
		p[0] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
		p[1] = static_cast<std::uint8_t>(v & 0xFF);
	}

	void write_u32(std::uint8_t* p, std::uint32_t v) {
		p[0] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
		p[1] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
		p[2] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
		p[3] = static_cast<std::uint8_t>(v & 0xFF);
	}

	std::uint8_t read_u8(const std::uint8_t* p) {
		return p[0];
	}

	std::uint16_t read_u16(const std::uint8_t* p) {
		return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) |
			static_cast<std::uint16_t>(p[1]));
	}

	std::uint32_t read_u32(const std::uint8_t* p) {
		return (static_cast<std::uint32_t>(p[0]) << 24) |
			(static_cast<std::uint32_t>(p[1]) << 16) |
			(static_cast<std::uint32_t>(p[2]) << 8) |
			static_cast<std::uint32_t>(p[3]);
	}

	void write_header(std::uint8_t* buf, const PacketHeader& h) {
		write_u16(buf + 0, h.protocol_id);
		write_u16(buf + 2, h.sequence);
		write_u16(buf + 4, h.ack);
		write_u32(buf + 6, h.ack_bits);
		write_u8(buf + 10, static_cast<std::uint8_t>(h.type));
	}

	bool read_header(const std::uint8_t* buf, std::size_t size, PacketHeader& out) {
		// 헤더도 못 채우는 패킷은 버린다. 여기서 막지 않으면
		// 아래 read_u16 들이 버퍼 밖을 읽는다.
		if (size < kHeaderSize) return false;

		out.protocol_id = read_u16(buf + 0);
		if (out.protocol_id != kProtocolId) return false;   // 우리 패킷이 아니다

		out.sequence = read_u16(buf + 2);
		out.ack = read_u16(buf + 4);
		out.ack_bits = read_u32(buf + 6);
		out.type = static_cast<PacketType>(read_u8(buf + 10));
		return true;
	}

	bool read_input(const std::uint8_t* buf, std::size_t size, InputPayload& out) {
		if (size < kInputPayloadSize) return false;

		// uint8 로 읽어서 int8 로 해석한다. 128~255 가 -128~-1 이 된다.
		out.move_x = static_cast<std::int8_t>(read_u8(buf + 0));
		out.move_y = static_cast<std::int8_t>(read_u8(buf + 1));
		return true;
	}

	std::size_t write_snapshot(std::uint8_t* buf, std::size_t capacity,
		const PlayerState* players, std::size_t count) {
		if (capacity < 1) return 0;

		// 1400바이트를 넘으면 IP 단편화가 일어난다. 들어갈 만큼만 담는다.
		// 자르는 쪽이 터지는 쪽보다 낫다.
		std::size_t max_count = (capacity - 1) / kPlayerStateSize;
		if (count > max_count) count = max_count;
		if (count > 255) count = 255;          // 개수 필드가 uint8

		write_u8(buf, static_cast<std::uint8_t>(count));

		std::size_t off = 1;
		for (std::size_t i = 0; i < count; ++i) {
			std::int16_t xi = static_cast<std::int16_t>(players[i].x * kPositionScale);
			std::int16_t yi = static_cast<std::int16_t>(players[i].y * kPositionScale);

			write_u32(buf + off + 0, players[i].id);
			write_u16(buf + off + 4, static_cast<std::uint16_t>(xi));
			write_u16(buf + off + 6, static_cast<std::uint16_t>(yi));
			off += kPlayerStateSize;
		}
		return off;
	}

	const char* to_string(PacketType t) {
		switch (t) {
		case PacketType::Handshake:    return "Handshake";
		case PacketType::HandshakeAck: return "HandshakeAck";
		case PacketType::Heartbeat:    return "Heartbeat";
		case PacketType::Input:        return "Input";
		case PacketType::Snapshot:     return "Snapshot";
		case PacketType::Event:        return "Event";
		default:                       return "Invalid";
		}
	}

} // namespace protocol