#include "World.h"

std::uint32_t World::add_player() {
    Player p;
    p.id = next_entity_id_++;
    p.last_input_at = std::chrono::steady_clock::now();
    players_.push_back(p);
    return p.id;
}

void World::remove_player(std::uint32_t id) {
    for (std::size_t i = 0; i < players_.size(); ++i) {
        if (players_[i].id != id) continue;

        // 순서가 의미 없으므로 마지막 것과 바꿔치고 뒤에서 뺀다.
        // 중간에서 erase 하면 뒤쪽 전체가 밀린다.
        players_[i] = players_.back();
        players_.pop_back();
        return;
    }
}

Player* World::find_player(std::uint32_t id) {
    for (Player& p : players_) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

std::uint32_t World::spawn_monster(float x, float y) {
    Monster m;
    m.id = next_entity_id_++;      // 플레이어와 같은 ID 공간을 쓴다
    m.x = x;
    m.y = y;
    monsters_.push_back(m);
    return m.id;
}

void World::spawn_initial_monsters() {
    // 반지름 20 원 위에 다섯 마리. 임시 코드다.
    const int count = 5;
    for (int i = 0; i < count; ++i) {
        float angle = 6.2831853f * i / count;
        spawn_monster(std::cos(angle) * 20.0f, std::sin(angle) * 20.0f);
    }
}

void World::update(float dt) {
    for (Player& p : players_) {
        p.apply_input(dt);
    }

    for (Monster& m : monsters_) {
        const Player* target = nearest_player(m.x, m.y);
        if (!target) continue;             // 아무도 없으면 가만히 있는다

        float dx = target->x - m.x;
        float dy = target->y - m.y;
        float d2 = dx * dx + dy * dy;

        // 거의 겹쳐 있으면 움직이지 않는다. 0으로 나누는 것도 막는다.
        if (d2 < 0.0001f) continue;

        float d = std::sqrt(d2);
        float step = protocol::kMonsterSpeed * dt;

        // 이번 틱에 목표를 지나칠 만큼 가까우면 딱 붙인다.
        // 안 그러면 목표를 중심으로 진동한다.
        if (step >= d) {
            m.x = target->x;
            m.y = target->y;
            continue;
        }

        m.x += (dx / d) * step;
        m.y += (dy / d) * step;
    }

    resolve_attacks(dt);
}

const Player* World::nearest_player(float x, float y) const {
    const Player* best = nullptr;
    float best_d2 = 0.0f;

    for (const Player& p : players_) {
        float dx = p.x - x;
        float dy = p.y - y;
        float d2 = dx * dx + dy * dy;      // 제곱근은 비교에 필요 없다

        if (!best || d2 < best_d2) {
            best = &p;
            best_d2 = d2;
        }
    }
    return best;
}

Monster* World::nearest_monster(float x, float y, float max_range) {
    Monster* best = nullptr;
    float best_d2 = max_range * max_range;      // 사거리 밖은 애초에 제외

    for (Monster& m : monsters_) {
        float dx = m.x - x;
        float dy = m.y - y;
        float d2 = dx * dx + dy * dy;

        if (d2 <= best_d2) {
            best = &m;
            best_d2 = d2;
        }
    }
    return best;
}

void World::resolve_attacks(float dt) {
    died_this_tick.clear();

    for (Player& p : players_) {
        // 쏠 준비가 됐으면 더 깎지 않는다. 목표가 없는 동안 음수로
        // 쌓이면, 나중에 적이 나타났을 때 한꺼번에 터진다.
        if (p.attack_timer > 0.0f) p.attack_timer -= dt;
        if (p.attack_timer > 0.0f) continue;

        Monster* target = nearest_monster(p.x, p.y, protocol::kAttackRange);
        if (!target) continue;

        target->hp -= protocol::kAttackDamage;

        // 대입이 아니라 더하기다. 넘긴 만큼을 다음 주기에서 빼야
        // 장기적으로 정확히 0.5초마다 쏜다.
        p.attack_timer += protocol::kAttackInterval;
    }

    // 죽은 것을 치운다. 공격 루프 안에서 지우면 포인터가 깨진다.
    for (std::size_t i = 0; i < monsters_.size(); ) {
        if (monsters_[i].hp > 0) {
            i++;
            continue;
        }
        std::uint32_t dead_id = monsters_[i].id;
        monsters_[i] = monsters_.back();
        monsters_.pop_back();
        total_kills++;
        died_this_tick.push_back(dead_id);
        // i 를 올리지 않는다. 방금 당겨온 것을 아직 안 봤다.
    }
}

void World::collect_states(std::vector<protocol::EntityState>& out) const {
    out.clear();

    for (const Player& p : players_) {
        protocol::EntityState s;
        s.id = p.id;
        s.type = protocol::EntityType::Player;
        s.x = p.x;
        s.y = p.y;
        out.push_back(s);
    }

    for (const Monster& m : monsters_) {
        protocol::EntityState s;
        s.id = m.id;
        s.type = protocol::EntityType::Monster;
        s.x = m.x;
        s.y = m.y;
        out.push_back(s);
    }
}