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

    void set_input(std::int8_t ix, std::int8_t iy) {
        input_x = ix;
        input_y = iy;
        last_input_at = std::chrono::steady_clock::now();
    }

    void apply_input(float dt) {
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

// 몬스터. 연결이 없다 — 이 게임에서 처음 등장하는, 네트워크 없는 객체다.
struct Monster {
    std::uint32_t id = 0;
    float x = 0.0f;
    float y = 0.0f;
    int hp = protocol::kMonsterHp;
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

    // 서버 시작 시 한 번. Step 17 에서 웨이브로 바뀐다.
    void spawn_initial_monsters();

    // 한 틱 진행한다.
    void update(float dt);

    // 이 월드에서 지금까지 죽은 몬스터 수. 로그용.
    std::uint32_t total_kills = 0;

    // 이번 틱에 죽은 몬스터들. BattleServer 가 읽고 비운다.
    std::vector<std::uint32_t> died_this_tick;

    std::size_t monster_count() const { return monsters_.size(); }

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
};