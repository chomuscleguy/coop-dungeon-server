#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Protocol.h"

// 플레이어 캐릭터. Connection 에서 꺼내왔다.
// 연결(네트워크)과 캐릭터(게임)는 생명주기가 다르다 — 재접속하면
// 연결은 새로 생기지만 캐릭터는 이어져야 한다. 지금은 아니지만.
struct Player {
    std::uint32_t id = 0;
    float x = 0.0f;
    float y = 0.0f;

    std::int8_t input_x = 0;
    std::int8_t input_y = 0;
    std::chrono::steady_clock::time_point last_input_at;

    // 다음 사격까지 남은 시간(초). 0 이하면 쏠 수 있다.
    float attack_timer = 0.0f;

    int hp = protocol::kPlayerMaxHp;
    protocol::EntityLife life = protocol::EntityLife::Alive;

    float down_timer = 0.0f;        // Dead 까지 남은 시간
    float revive_progress = 0.0f;   // 0 ~ kReviveSeconds

    bool is_alive() const { return life == protocol::EntityLife::Alive; }

    // 쓰러진 동안에는 이 칸이 부활 진행도를 나른다.
    // 체력은 어차피 0이라 쓸 자리가 비어 있다. 
    std::uint8_t hp_percent() const {
        if (life == protocol::EntityLife::Down) {
            int pct = static_cast<int>(
                revive_progress * 100.0f / protocol::kReviveSeconds);
            return static_cast<std::uint8_t>(pct > 100 ? 100 : (pct < 0 ? 0 : pct));
        }
        if (hp <= 0) return 0;
        int pct = hp * 100 / protocol::kPlayerMaxHp;
        return static_cast<std::uint8_t>(pct > 100 ? 100 : pct);
    }

    void set_input(std::int8_t ix, std::int8_t iy) {
        input_x = ix;
        input_y = iy;
        last_input_at = std::chrono::steady_clock::now();
    }

    void apply_input(float dt) {
        if (!is_alive()) return;
        auto since = std::chrono::steady_clock::now() - last_input_at;
        if (since > std::chrono::milliseconds(protocol::kInputHoldMs)) {
            input_x = 0;
            input_y = 0;
        }

        float ix = input_x / 127.0f;
        float iy = input_y / 127.0f;

        float len2 = ix * ix + iy * iy;
        if (len2 > 1.0f) {
            float len = std::sqrt(len2);
            ix /= len;
            iy /= len;
        }

        x += ix * protocol::kPlayerSpeed * dt;
        y += iy * protocol::kPlayerSpeed * dt;

        if (x > protocol::kMapHalfSize) x = protocol::kMapHalfSize;
        if (x < -protocol::kMapHalfSize) x = -protocol::kMapHalfSize;
        if (y > protocol::kMapHalfSize) y = protocol::kMapHalfSize;
        if (y < -protocol::kMapHalfSize) y = -protocol::kMapHalfSize;
    }
};

struct Monster {
    std::uint32_t id = 0;
    float x = 0.0f;
    float y = 0.0f;
    int hp = protocol::kMonsterHp;
    int max_hp = protocol::kMonsterHp;  
    float attack_timer = 0.0f;    
};

// 전투 공간. 플레이어와 (곧) 몬스터가 여기 산다.
// BattleServer 는 네트워크를, World 는 게임을 맡는다.
class World {
public:
    // 새 캐릭터를 만들고 그 엔티티 ID 를 돌려준다.
    std::uint32_t add_player();
    void remove_player(std::uint32_t id);
    Player* find_player(std::uint32_t id);

    std::uint32_t spawn_monster(float x, float y);

    // 한 틱 진행한다.
    void update(float dt);

    // 이 월드에서 지금까지 죽은 몬스터 수. 로그용.
    std::uint32_t total_kills = 0;

    // --- 방 진행 상태 ---
    protocol::RoomPhase phase = protocol::RoomPhase::Fighting;
    int room_index = 0;         // 0부터. 깊어질수록 어려워진다
    int wave_index = -1;        // -1 = 아직 시작 안 함. 첫 틱에 0이 된다
    float wave_break = 0.0f;    // 다음 웨이브까지 남은 시간

    float transition_timer = 0.0f;   // 남은 카운트다운
    int at_door = 0;                 // 문에 서 있는 인원 (로그용)

    // 이번 틱에 죽은 몬스터들. BattleServer 가 읽고 비운다.
    std::vector<std::uint32_t> died_this_tick;

    std::size_t monster_count() const { return monsters_.size(); }
    std::size_t player_count() const { return players_.size(); }

    // 전멸 시각. 로비에 결과를 보낼 때 쓴다 (18d).
    std::uint32_t final_room = 0;
    std::uint32_t final_wave = 0;

    // 스냅샷에 담을 상태를 모은다.
    void collect_states(std::vector<protocol::EntityState>& out) const;

private:
    std::vector<Player> players_;
    std::vector<Monster> monsters_;

    // 가장 가까운 플레이어를 찾는다. 아무도 없으면 nullptr.
    const Player* nearest_player(float x, float y) const;

    // 사거리 안에서 가장 가까운 몬스터. 없으면 nullptr.
    Monster* nearest_monster(float x, float y, float max_range);

    void resolve_attacks(float dt);

    // 엔티티 ID 는 World 가 발급한다. 몬스터도 같은 공간을 쓸 것이므로
    // 연결 ID(BattleServer 가 발급)와는 별개여야 한다.
    std::uint32_t next_entity_id_ = 1;

    // 이 방/웨이브에 몇 마리를 낼지
    int monsters_for_wave(int room, int wave, int players) const;

    void spawn_wave();
    void update_phase(float dt);

    // 문 앞에 서 있는 플레이어 수를 센다.
    int count_at_door() const;

    void update_door(float dt);

    void next_room();

    int monster_hp_for_room(int room) const;
    void reset_player_positions();

    void resolve_monster_attacks(float dt);

    void update_downed(float dt);

    bool all_down() const;
};