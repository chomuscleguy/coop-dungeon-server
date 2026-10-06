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
		const EntityState* entities, std::size_t count) {
		if (capacity < 1) return 0;

		std::size_t max_count = (capacity - 1) / kEntityStateSize;
		if (count > max_count) count = max_count;
		if (count > 255) count = 255;

		write_u8(buf, static_cast<std::uint8_t>(count));

		std::size_t off = 1;

		for (std::size_t i = 0; i < count; ++i) {
			std::int16_t xi = static_cast<std::int16_t>(entities[i].x * kPositionScale);
			std::int16_t yi = static_cast<std::int16_t>(entities[i].y * kPositionScale);

			write_u32(buf + off + 0, entities[i].id);
			write_u8(buf + off + 4, static_cast<std::uint8_t>(entities[i].type));
			write_u16(buf + off + 5, static_cast<std::uint16_t>(xi));
			write_u16(buf + off + 7, static_cast<std::uint16_t>(yi));
			write_u8(buf + off + 9, entities[i].hp_pct);                        
			write_u8(buf + off + 10, static_cast<std::uint8_t>(entities[i].life)); 
			off += kEntityStateSize;
		}

		return off;
	}

	void write_room_state(std::uint8_t* buf, const RoomStatePayload& s) {
		write_u8(buf + 0, s.room);
		write_u8(buf + 1, static_cast<std::uint8_t>(s.wave));   // -1 은 0xFF
		write_u8(buf + 2, static_cast<std::uint8_t>(s.phase));
		write_u8(buf + 3, s.monsters);
		write_u8(buf + 4, s.at_door);
		write_u8(buf + 5, s.players);
		write_u16(buf + 6, s.countdown_ms);
	}

	bool read_room_state(const std::uint8_t* buf, std::size_t size,
		RoomStatePayload& out) {
		if (size < kRoomStateSize) return false;
		out.room = read_u8(buf + 0);
		out.wave = static_cast<std::int8_t>(read_u8(buf + 1));
		out.phase = static_cast<RoomPhase>(read_u8(buf + 2));
		out.monsters = read_u8(buf + 3);
		out.at_door = read_u8(buf + 4);
		out.players = read_u8(buf + 5);
		out.countdown_ms = read_u16(buf + 6);
		return true;
	}

	const char* to_string(PacketType t) {
		switch (t) {
		case PacketType::Handshake:    return "Handshake";
		case PacketType::HandshakeAck: return "HandshakeAck";
		case PacketType::Heartbeat:    return "Heartbeat";
		case PacketType::Input:        return "Input";
		case PacketType::Snapshot:     return "Snapshot";
		case PacketType::Event:        return "Event";
		case PacketType::RoomState:    return "RoomState";
		default:                       return "Invalid";
		}
	}

	void write_event(std::uint8_t* buf, const EventPayload& e) {
		write_u8(buf + 0, static_cast<std::uint8_t>(e.kind));
		write_u32(buf + 1, e.entity_id);
	}

	bool read_event(const std::uint8_t* buf, std::size_t size, EventPayload& out) {
		if (size < kEventPayloadSize) return false;
		out.kind = static_cast<EventKind>(read_u8(buf + 0));
		out.entity_id = read_u32(buf + 1);
		return true;
	}

	const char* to_string(EventKind k) {
		switch (k) {
		case EventKind::MonsterDied:    return "MonsterDied";
		case EventKind::MonsterSpawned: return "MonsterSpawned";
		default:                        return "Unknown";
		}
	}

} // namespace protocol