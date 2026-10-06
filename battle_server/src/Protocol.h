#pragma once

#include <cstdint>
#include <cstddef>
#include <chrono>

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
		RoomState = 7,       // 서버 -> 클라: 방 진행 상황 (비신뢰, 주기적)
	};

	struct PacketHeader {
		std::uint16_t protocol_id = kProtocolId;
		std::uint16_t sequence = 0;     // 내가 보내는 이 패킷의 번호
		std::uint16_t ack = 0;          // 내가 마지막으로 받은 상대 번호
		std::uint32_t ack_bits = 0;     // 그 이전 32개를 받았는지 (비트당 1개)
		PacketType type = PacketType::Invalid;
	};

	inline constexpr std::size_t kHeaderSize = 11;   // 2+2+2+4+1

	// Input 패킷의 본문. 헤더 바로 뒤에 붙는다.
	// 방향을 float(8바이트) 대신 int8(2바이트)로 보낸다. 이동 방향에
	// 소수점 정밀도는 필요 없고, 같은 수법을 Step 16의 위치 압축에 쓴다.
	struct InputPayload {
		std::int8_t move_x = 0;   // -127 ~ 127 을 -1.0 ~ 1.0 으로 읽는다
		std::int8_t move_y = 0;
	};

	inline constexpr std::size_t kInputPayloadSize = 2;

	enum class EntityType : std::uint8_t {
		Player = 0,
		Monster = 1,
		Door = 2,
	};

	// 플레이어의 생존 상태. 몬스터는 항상 Alive 로 보낸다.
	enum class EntityLife : std::uint8_t {
		Alive = 0,
		Down = 1,     // 쓰러짐. 파티원이 일으킬 수 있다
		Dead = 2,     // 못 일어남 (그 판 한정)
	};

	struct EntityState {
		std::uint32_t id = 0;
		EntityType type = EntityType::Player;
		float x = 0.0f;
		float y = 0.0f;
		std::uint8_t hp_pct = 100;                   // 0~100
		EntityLife life = EntityLife::Alive;
	};

	inline constexpr std::size_t kEntityStateSize = 11;   // 4+1+2+2+1+1

	enum class EventKind : std::uint8_t {
		MonsterDied = 1,
		MonsterSpawned = 2,
	};

	// Event 패킷의 본문. "한 번뿐인 사건"이라 반드시 도착해야 한다.
	struct EventPayload {
		EventKind kind = EventKind::MonsterDied;
		std::uint32_t entity_id = 0;
	};

	inline constexpr std::size_t kEventPayloadSize = 5;   // 1 + 4

	// 방의 진행 단계. 틱이 이 값을 바꾼다 (로비의 RoomPhase 는 메시지가 바꿨다).
	enum class RoomPhase : std::uint8_t {
		Fighting = 0,       // 웨이브 진행 중
		Cleared = 1,        // 전부 격파. 문이 열렸다
		Transitioning = 2,  // 과반수 도달. 카운트다운 중
		Failed = 3,         // 전멸. 던전 실패
	};

	// 방 진행 상황. 스냅샷처럼 계속 보내므로 재전송하지 않는다.
	struct RoomStatePayload {
		std::uint8_t room = 0;
		std::int8_t  wave = -1;        // -1 = 시작 전
		RoomPhase    phase = RoomPhase::Fighting;
		std::uint8_t monsters = 0;     // 남은 수 (255 이상이면 255)
		std::uint8_t at_door = 0;
		std::uint8_t players = 0;
		std::uint16_t countdown_ms = 0;
	};

	inline constexpr std::size_t kRoomStateSize = 8;   // 1+1+1+1+1+1+2

	// 좌표를 int16 으로 담을 때의 배율. 맵이 -50~50 이므로
	// 100배 하면 -5000~5000 이고 정밀도는 0.01 유닛이다.
	inline constexpr float kPositionScale = 100.0f;


	// 클라이언트가 이 간격으로 하트비트를 보낸다(서버는 참고만).
	inline constexpr int kHeartbeatSeconds = 1;

	// 이만큼 조용하면 죽은 것으로 보고 정리한다.
	// 하트비트 간격의 몇 배로 잡아야 한두 개 유실돼도 안 끊긴다.
	inline constexpr int kTimeoutSeconds = 5;

	// 게임 루프 주기. 30Hz 는 격투/슈팅 장르의 사실상 표준이다.
	// 60Hz 는 대역폭이 두 배인데, 몬스터 수십 마리의 위치를 보내는 쪽이
	// 프레임 수보다 중요하다. 클라이언트는 그 사이를 보간해서 그린다.
	inline constexpr int kTickHz = 30;

	// 1000000 / 30 = 33333 (us). 나머지 0.33us 는 틱당 오차인데
	// 10분 던전에서 6ms 라 무시한다.
	inline constexpr std::chrono::microseconds kTickInterval{ 1000000 / kTickHz };

	// 틱 간격을 초 단위 float 으로. 이동 계산에 쓴다.
	inline constexpr float kTickSeconds = 1.0f / kTickHz;

	// 초당 이동 거리. 맵은 -50 ~ 50 이라 가로지르는 데 20초.
	inline constexpr float kPlayerSpeed = 5.0f;
	inline constexpr float kMapHalfSize = 50.0f;

	// 플레이어(5.0)보다 느리다. 도망칠 수 있어야 하고, 그래야
	// "몰려오는 걸 뚫고 지나간다"가 성립한다.
	inline constexpr float kMonsterSpeed = 2.5f;        // 2.0 -> 2.5

	inline constexpr int kMonsterHp = 30;

	// 자동 사격. 조준도 발사도 입력이 아니다 — 사거리 안에 들어오면 쏜다.
	inline constexpr float kAttackRange = 8.0f;
	inline constexpr int   kAttackDamage = 10;
	inline constexpr float kAttackInterval = 0.5f;   // 초. 몬스터(30hp)는 3발.

	inline constexpr int kWavesPerRoom = 3;

	// 웨이브 사이 숨 돌릴 틈. 끝나자마자 다음이 나오면 정신없다.
	inline constexpr float kWaveBreakSeconds = 2.0f;

	// 첫 웨이브 몬스터 수. 웨이브마다, 방마다 늘어난다.
	inline constexpr int kBaseMonstersPerWave = 12;

	// 몬스터는 맵 중심이 아니라 파티 주변에 나온다. 화면 밖에서 나타나
	// 좁혀 들어오는 거리.
	inline constexpr float kSpawnRadius = 25.0f;

	// 방이 깊어질수록 단단해진다. 수를 늘리는 것보다 이쪽이 낫다 —
	// 수는 1400바이트 스냅샷에 묶여 있다.
	inline constexpr int kMonsterHpPerRoom = 10;

	// 플레이어 체력과 몬스터의 접촉 공격.
	inline constexpr int   kPlayerMaxHp = 100;
	inline constexpr float kContactRange = 1.5f;        // 이 안에 들어오면 때린다
	inline constexpr int   kMonsterDamage = 3;
	inline constexpr float kMonsterAttackInterval = 1.0f;

	// 부활은 채널링이다. 쓰러진 동료 옆에 머물러야 한다 —
	// 구하는 쪽도 그동안 위험을 감수한다.
	inline constexpr float kReviveRange = 3.0f;
	inline constexpr float kReviveSeconds = 3.0f;
	inline constexpr int   kReviveHp = 50;          // 절반만 회복

	// 이 안에 못 구하면 그 판에서는 못 일어난다.
	inline constexpr float kDownToDeadSeconds = 10.0f;

	// 새 방에 들어가면 왼쪽 입구에서 시작한다 (문은 오른쪽 x=40).
	inline constexpr float kRoomEntryX = -30.0f;

	// 문은 맵 오른쪽 끝에 생긴다. 방마다 같은 자리 (지금은).
	inline constexpr float kDoorX = 40.0f;
	inline constexpr float kDoorY = 0.0f;

	// 이 거리 안에 들어오면 "문에 도달"로 본다.
	inline constexpr float kDoorRadius = 5.0f;

	// 과반수가 모였을 때 기다려주는 시간.
	inline constexpr float kTransitionSeconds = 5.0f;

	// 이 시간 동안 입력이 안 오면 멈춘다. 30Hz 기준 7패킷 연속 유실.
	inline constexpr int kInputHoldMs = 250;

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

	// 헤더 뒤쪽을 입력으로 읽는다. buf 는 payload 시작점, size 는 남은 길이.
	bool read_input(const std::uint8_t* buf, std::size_t size, InputPayload& out);

	// buf 에 스냅샷 본문을 쓴다. capacity 를 넘지 않게 개수를 줄인다.
	// 실제로 쓴 바이트 수를 돌려준다.
	std::size_t write_snapshot(std::uint8_t* buf, std::size_t capacity,
		const EntityState* entities, std::size_t count);

	const char* to_string(PacketType t);

	void write_event(std::uint8_t* buf, const EventPayload& e);
	bool read_event(const std::uint8_t* buf, std::size_t size, EventPayload& out);
	const char* to_string(EventKind k);

	void write_room_state(std::uint8_t* buf, const RoomStatePayload& s);
	bool read_room_state(const std::uint8_t* buf, std::size_t size,
		RoomStatePayload& out);

} // namespace protocol