#include "World.h"
#include <iostream>

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
    m.id = next_entity_id_++;
    m.x = x;
    m.y = y;
    m.hp = monster_hp_for_room(room_index); 
    m.max_hp = m.hp;
    monsters_.push_back(m);
    return m.id;
}

int World::monsters_for_wave(int room, int wave, int players) const {
    if (players < 1) players = 1;

    // 인원이 늘면 받는 피해는 1/N 로 줄고 주는 피해는 N 배가 된다.
    // 마릿수를 그대로 두면 난이도가 인원의 제곱으로 쉬워진다.
    // 1인 기준 12/16/20 을 유지하면서 인원에 비례해 늘린다.
    int base = protocol::kBaseMonstersPerWave + room * 4 + wave * 4;
    return base * players;
}

int World::monster_hp_for_room(int room) const {
    return protocol::kMonsterHp + room * protocol::kMonsterHpPerRoom;
}

void World::spawn_wave() {
    // 파티 한가운데를 중심으로 삼는다. 맵 중심이 아니다 —
    // 구석에 몰려 있으면 반대편 몬스터가 80유닛을 걸어오게 된다.
    float cx = 0.0f;
    float cy = 0.0f;
    if (!players_.empty()) {
        for (const Player& p : players_) { cx += p.x; cy += p.y; }
        cx /= players_.size();
        cy /= players_.size();
    }

    int count = monsters_for_wave(room_index, wave_index,
        static_cast<int>(players_.size()));

    for (int i = 0; i < count; ++i) {
        float angle = 6.2831853f * i / count;
        float x = cx + std::cos(angle) * protocol::kSpawnRadius;
        float y = cy + std::sin(angle) * protocol::kSpawnRadius;

        // 맵 밖은 가장자리로 당긴다. 파티가 구석에 있으면 한쪽에
        // 몰려서 나오는데, "열린 쪽에서 몰려온다"로 읽혀서 자연스럽다.
        if (x > protocol::kMapHalfSize) x = protocol::kMapHalfSize;
        if (x < -protocol::kMapHalfSize) x = -protocol::kMapHalfSize;
        if (y > protocol::kMapHalfSize) y = protocol::kMapHalfSize;
        if (y < -protocol::kMapHalfSize) y = -protocol::kMapHalfSize;
        
        spawn_monster(x, y);
    }
}

void World::reset_player_positions() {
    int n = static_cast<int>(players_.size());
    if (n == 0) return;

    // 입구 근처에 작은 원으로 흩어 놓는다. 한 점에 겹치면
    // 몬스터가 전부 한 사람만 쫓는다.
    for (int i = 0; i < n; ++i) {
        float angle = 6.2831853f * i / n;
        players_[i].x = protocol::kRoomEntryX + std::cos(angle) * 2.0f;
        players_[i].y = std::sin(angle) * 2.0f;
    }
}

int World::count_at_door() const {
    int n = 0;
    float r2 = protocol::kDoorRadius * protocol::kDoorRadius;

    for (const Player& p : players_) {
        float dx = p.x - protocol::kDoorX;
        float dy = p.y - protocol::kDoorY;
        if (dx * dx + dy * dy <= r2) ++n;
    }
    return n;
}

void World::update_door(float dt) {
    at_door = count_at_door();
    int total = static_cast<int>(players_.size());
    if (total == 0) return;

    // 전원 도달 -> 기다릴 이유가 없다.
    if (at_door == total) {
        std::cout << "[world] all " << total << " at door - advancing now\n";
        next_room();
        return;
    }

    // 과반수 판정. 4명이면 3명부터, 3명이면 2명부터, 1명이면 1명.
    // at_door * 2 > total 로 쓰면 나눗셈과 소수점을 피할 수 있다.
    bool majority = (at_door * 2 > total);

    if (!majority) {
        // 빠져나갔다. 카운트다운을 취소한다.
        if (phase == protocol::RoomPhase::Transitioning) {
            std::cout << "[world] majority lost - countdown cancelled\n";
            phase = protocol::RoomPhase::Cleared;
            transition_timer = 0.0f;
        }
        return;
    }

    if (phase == protocol::RoomPhase::Cleared) {
        phase = protocol::RoomPhase::Transitioning;
        transition_timer = protocol::kTransitionSeconds;
        std::cout << "[world] majority at door (" << at_door << "/" << total
            << ") - countdown " << transition_timer << "s\n";
        return;
    }

    transition_timer -= dt;
    if (transition_timer <= 0.0f) {
        std::cout << "[world] countdown done - advancing\n";
        next_room();
    }
}

void World::update_phase(float dt) {
    if (phase == protocol::RoomPhase::Failed) return;
    // 아무도 없으면 던전이 흐르지 않는다. 첫 플레이어가 들어오면
    // 다음 틱에 wave_index 가 -1 에서 0 이 되면서 시작된다.
    if (players_.empty()) return;

    // 클리어 이후의 단계들은 여기서 처리하고 끝낸다.
    if (phase == protocol::RoomPhase::Cleared ||
        phase == protocol::RoomPhase::Transitioning) {
        update_door(dt);
        return;
    }

    // 아직 몬스터가 남아 있으면 할 일이 없다.
    if (!monsters_.empty()) return;

    // 웨이브 사이 쉬는 중이면 시간만 깎는다.
    if (wave_break > 0.0f) {
        wave_break -= dt;
        if (wave_break > 0.0f) return;
    }

    wave_index++;

    if (wave_index >= protocol::kWavesPerRoom) {
        phase = protocol::RoomPhase::Cleared;
        std::cout << "[world] room " << room_index << " cleared\n";
        return;
    }

    spawn_wave();
    wave_break = protocol::kWaveBreakSeconds;
    std::cout << "[world] room " << room_index
        << " wave " << wave_index
        << " (" << monsters_.size() << " monsters)\n";
}

void World::update(float dt) {
    died_this_tick.clear();

    // 실패한 던전은 흐르지 않는다. 몬스터도 멈추고 타이머도 안 간다.
    // 클라이언트가 결과 화면을 띄우는 동안 상태가 바뀌면 안 된다.
    if (phase == protocol::RoomPhase::Failed) return;

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
    resolve_monster_attacks(dt);
    update_downed(dt);

    // 전멸 검사는 다운 처리 뒤에 한다 — 이번 틱에 마지막 한 명이
    // 쓰러졌다면 그것까지 반영해서 판정해야 한다.
    if (phase != protocol::RoomPhase::Failed && all_down()) {
        phase = protocol::RoomPhase::Failed;
        final_room = static_cast<std::uint32_t>(room_index);
        final_wave = static_cast<std::uint32_t>(wave_index < 0 ? 0 : wave_index);
        std::cout << "[world] WIPED at room " << final_room
            << " wave " << final_wave
            << " (kills " << total_kills << ")\n";
    }

    update_phase(dt);
}

const Player* World::nearest_player(float x, float y) const {
    const Player* best = nullptr;
    float best_d2 = 0.0f;

    for (const Player& p : players_) {
        if (!p.is_alive()) continue;
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
        if (!p.is_alive()) continue;
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

void World::resolve_monster_attacks(float dt) {
    float r2 = protocol::kContactRange * protocol::kContactRange;

    for (Monster& m : monsters_) {
        if (m.attack_timer > 0.0f) m.attack_timer -= dt;
        if (m.attack_timer > 0.0f) continue;

        // 닿아 있는 산 플레이어를 찾는다.
        Player* victim = nullptr;
        for (Player& p : players_) {
            if (!p.is_alive()) continue;
            float dx = p.x - m.x;
            float dy = p.y - m.y;
            if (dx * dx + dy * dy <= r2) { victim = &p; break; }
        }
        if (!victim) continue;

        victim->hp -= protocol::kMonsterDamage;
        m.attack_timer += protocol::kMonsterAttackInterval;

        if (victim->hp <= 0) {
            victim->hp = 0;
            victim->life = protocol::EntityLife::Down;
            victim->down_timer = protocol::kDownToDeadSeconds; 
            victim->revive_progress = 0.0f;                       
            std::cout << "[world] player " << victim->id << " is DOWN\n";
        }
    }
}

void World::update_downed(float dt) {
    float r2 = protocol::kReviveRange * protocol::kReviveRange;

    for (Player& p : players_) {
        if (p.life != protocol::EntityLife::Down) continue;

        // 사망 타이머는 구조 중에도 멈추지 않는다.
        // 멈추면 "일단 붙기만 하면 안전"이 되어 긴장이 사라진다.
        p.down_timer -= dt;

        // 근처에 살아 있는 동료가 있나.
        bool helper = false;
        for (const Player& q : players_) {
            if (&q == &p) continue;
            if (!q.is_alive()) continue;
            float dx = q.x - p.x;
            float dy = q.y - p.y;
            if (dx * dx + dy * dy <= r2) { helper = true; break; }
        }

        if (helper) {
            p.revive_progress += dt;
            if (p.revive_progress >= protocol::kReviveSeconds) {
                p.life = protocol::EntityLife::Alive;
                p.hp = protocol::kReviveHp;
                p.revive_progress = 0.0f;
                p.down_timer = 0.0f;
                std::cout << "[world] player " << p.id << " REVIVED\n";
                continue;
            }
        }
        else {
            // 구하던 사람이 떠나면 진행도가 깎인다. 0 으로 날리지는 않는다 —
            // 한 발짝 물러섰다가 돌아오는 플레이가 아예 불가능해진다.
            p.revive_progress -= dt;
            if (p.revive_progress < 0.0f) p.revive_progress = 0.0f;
        }

        if (p.down_timer <= 0.0f) {
            p.life = protocol::EntityLife::Dead;
            p.revive_progress = 0.0f;
            std::cout << "[world] player " << p.id << " is DEAD\n";
        }
    }
}

bool World::all_down() const {
    // 아무도 없는 것과 전멸은 다르다. 빈 월드는 실패가 아니다.
    if (players_.empty()) return false;

    for (const Player& p : players_) {
        if (p.is_alive()) return false;
    }
    return true;
}

void World::collect_states(std::vector<protocol::EntityState>& out) const {
    out.clear();

    for (const Player& p : players_) {
        protocol::EntityState s;
        s.id = p.id;
        s.type = protocol::EntityType::Player;
        s.x = p.x;
        s.y = p.y;
        s.hp_pct = p.hp_percent();                    
        s.life = p.life;                            
        out.push_back(s);
    }

    for (const Monster& m : monsters_) {
        protocol::EntityState s;
        s.id = m.id;
        s.type = protocol::EntityType::Monster;
        s.x = m.x;
        s.y = m.y;
        s.hp_pct = static_cast<std::uint8_t>(
            m.max_hp > 0 ? (m.hp * 100 / m.max_hp) : 0);   
        s.life = protocol::EntityLife::Alive;              
        out.push_back(s);
    }

    // 문은 클리어된 뒤에만 보인다. 엔티티 ID 0 은 아무도 안 쓰므로
    // "진짜 객체가 아닌 것"의 표식으로 쓴다.
    if (phase != protocol::RoomPhase::Fighting) {
        protocol::EntityState s;
        s.id = 0;
        s.type = protocol::EntityType::Door;
        s.x = protocol::kDoorX;
        s.y = protocol::kDoorY;
        out.push_back(s);
    }
}

void World::next_room() {
    ++room_index;
    wave_index = -1;
    phase = protocol::RoomPhase::Fighting;
    transition_timer = 0.0f;
    wave_break = 0.0f;
    at_door = 0;

    monsters_.clear();

    reset_player_positions();

    // 방을 넘어가면 숨을 돌린다. 쓰러진 사람도 일어난다.
    for (Player& p : players_) {
        p.hp = protocol::kPlayerMaxHp;
        p.life = protocol::EntityLife::Alive;
        p.down_timer = 0.0f;          
        p.revive_progress = 0.0f;
    }

    std::cout << "[world] --> room " << room_index
        << " (monster hp " << monster_hp_for_room(room_index) << ")\n";
}