#pragma once

#include <cstdint>
#include <cstddef>

// 모든 패킷은 이 헤더로 시작한다. 바이트 단위로 직접 찍고 읽는다.
//
// JSON을 쓰지 않는 이유: 초당 30번, 4인이면 120패킷이 오간다.
// {"seq":12345} 는 12바이트인데 uint16 은 2바이트다.
// 1400바이트 안에 몬스터 수백 마리를 넣으려면 선택지가 없다.
namespace protocol {

	// 우리 패킷인지 확인하는 표식. 포트 스캐너나 다른 프로그램의
	// 패킷이 섞여 들어와도 여기서 걸러진다.
	inline constexpr std::uint16_t kProtocolId = 0xB47L & 0xFFFF;   // 임의의 값

	enum class PacketType : std::uint8_t {
		Invalid = 0,
		Handshake = 1,       // 클라 -> 서버: 입장 요청
		HandshakeAck = 2,    // 서버 -> 클라: 세션 ID 발급
		Heartbeat = 3,       // 양방향: 살아있음
		Input = 4,           // 클라 -> 서버: 이동 입력
		Snapshot = 5,        // 서버 -> 클라: 월드 상태
		Event = 6,           // 서버 -> 클라: 한 번뿐인 사건 (신뢰)
	};

	struct PacketHeader {
		std::uint16_t protocol_id = kProtocolId;
		std::uint16_t sequence = 0;     // 내가 보내는 이 패킷의 번호
		std::uint16_t ack = 0;          // 내가 마지막으로 받은 상대 번호
		std::uint32_t ack_bits = 0;     // 그 이전 32개를 받았는지 (비트당 1개)
		PacketType type = PacketType::Invalid;
	};

	inline constexpr std::size_t kHeaderSize = 11;   // 2+2+2+4+1

	// 클라이언트가 이 간격으로 하트비트를 보낸다(서버는 참고만).
	inline constexpr int kHeartbeatSeconds = 1;

	// 이만큼 조용하면 죽은 것으로 보고 정리한다.
	// 하트비트 간격의 몇 배로 잡아야 한두 개 유실돼도 안 끊긴다.
	inline constexpr int kTimeoutSeconds = 5;

	// 시퀀스는 uint16 이라 65535 다음이 0이다. 단순 비교로는 어느 쪽이 최신인지
	// 알 수 없어서, "차이가 절반(32768) 이내면 그쪽이 최신"으로 판정한다.
	// 30Hz 에서 32768 패킷이면 18분이라, 그 사이에 도착할 패킷은 없다.
	inline bool sequence_greater_than(std::uint16_t s1, std::uint16_t s2) {
		return ((s1 > s2) && (s1 - s2 <= 32768)) ||
			((s1 < s2) && (s2 - s1 > 32768));
	}

	// 네트워크 바이트 순서(빅엔디안)로 쓴다. 로비의 길이-prefix와 같은 규칙.
	// CPU마다 메모리에 숫자를 놓는 순서가 달라서(엔디언), 한쪽으로 통일해야 한다.
	void write_u8(std::uint8_t* p, std::uint8_t v);
	void write_u16(std::uint8_t* p, std::uint16_t v);
	void write_u32(std::uint8_t* p, std::uint32_t v);

	std::uint8_t  read_u8(const std::uint8_t* p);
	std::uint16_t read_u16(const std::uint8_t* p);
	std::uint32_t read_u32(const std::uint8_t* p);

	// 버퍼에 헤더를 쓴다. 버퍼는 최소 kHeaderSize 바이트여야 한다.
	void write_header(std::uint8_t* buf, const PacketHeader& h);

	// 버퍼에서 헤더를 읽는다. 크기가 모자라거나 프로토콜 ID가 다르면 false.
	bool read_header(const std::uint8_t* buf, std::size_t size, PacketHeader& out);

	const char* to_string(PacketType t);

} // namespace protocol