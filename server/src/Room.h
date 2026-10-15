#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// ============================================================
// Item
// ============================================================
// 개체마다 고유 번호를 준다. 같은 이름의 물건이 둘이어도 서로 다른 개체라,
// 경매에 걸린 그 물건을 지목하고 추적할 수 있어야 하기 때문.
struct Item {
    uint64_t id = 0;
    std::string name;

    nlohmann::json to_json() const {
        return nlohmann::json{ {"id", id}, {"name", name} };
    }
};

// 보스가 떨구는 아이템. 종류가 늘면 데이터 파일로 뺄 자리.
inline constexpr const char* kBossDropName = "Slime Core";

// ============================================================
// Boss
// ============================================================
struct Boss {
    std::string name;
    int32_t max_hp = 0;
    int32_t hp = 0;
};

struct BossTemplate {
    const char* name;
    int32_t max_hp;
    uint64_t clear_reward;      // 클리어 시 방 전체가 나눠 갖는 금액
};
inline constexpr BossTemplate kDefaultBoss{ "Slime King", 500, 1000 };

// 공격 한 번의 데미지는 서버가 정한다. 클라이언트는 "때렸다"만 보낸다.
inline constexpr int32_t kAttackDamage = 50;

// ============================================================
// Room
// ============================================================
enum class RoomPhase { Waiting, BossFight, Cleared };

inline const char* to_string(RoomPhase phase) {
    switch (phase) {
    case RoomPhase::BossFight: return "boss_fight";
    case RoomPhase::Cleared:   return "cleared";
    default:                   return "waiting";
    }
}

// 파티 경매 — 보스 드랍을 파티원끼리 나눠 갖기 위한 것.
// 낙찰자가 아이템을 갖고, 낸 돈은 나머지 파티원이 나눠 갖는다.
inline constexpr int kAuctionSeconds = 30;

// 전투 중 끊긴 사람의 자리를 얼마나 지켜줄지. 지나면 방에서 정리한다.
inline constexpr int kReconnectGraceSeconds = 60;

struct LootAuction {
    bool active = false;
    Item item;
    uint64_t highest_bid = 0;
    uint64_t highest_bidder = 0;        // 0 = 아직 아무도 안 걸었음
    std::chrono::steady_clock::time_point ends_at;

    int seconds_left() const {
        auto now = std::chrono::steady_clock::now();
        if (now >= ends_at) return 0;
        return static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(ends_at - now).count());
    }

    nlohmann::json to_json() const {
        nlohmann::json j;
        j["item"] = item.to_json();
        j["highestBid"] = highest_bid;
        j["highestBidder"] = highest_bidder;
        j["secondsLeft"] = seconds_left();
        return j;
    }
};

struct Room {
    uint32_t id = 0;
    std::string name;
    std::vector<uint64_t> members;   // 참여자들의 player_id
    RoomPhase phase = RoomPhase::Waiting;      //bool in_battle 대신
    Boss boss;
    LootAuction loot;

    bool has_boss() const { return phase != RoomPhase::Waiting; }

    // 드랍된 아이템으로 경매를 연다. 마감 시각을 지금 계산해 박아둔다.
    void start_loot_auction(const Item& drop) {
        loot = LootAuction{};           // 이전 경매 흔적 초기화
        loot.active = true;
        loot.item = drop;
        loot.ends_at = std::chrono::steady_clock::now()
            + std::chrono::seconds(kAuctionSeconds);
    }

    // 입찰을 받는다. 성공하면 밀려난 이전 최고 입찰자를 out으로 알려준다
    // (0이면 없음). 환불은 호출하는 쪽이 한다 — Room은 재화를 모른다.
    bool place_bid(uint64_t bidder, uint64_t amount, uint64_t& outbid_player,
        uint64_t& outbid_amount) {
        if (!loot.active) return false;
        if (amount <= loot.highest_bid) return false;

        outbid_player = loot.highest_bidder;
        outbid_amount = loot.highest_bid;

        loot.highest_bidder = bidder;
        loot.highest_bid = amount;
        return true;
    }

    bool auction_expired() const {
        return loot.active && std::chrono::steady_clock::now() >= loot.ends_at;
    }

    // 경매를 닫고 결과를 남긴다. Room은 재화를 모르므로 정산은 밖에서 한다.
    void close_loot_auction() {
        loot.active = false;
    }

    void spawn_boss(const BossTemplate& t) {
        boss.name = t.name;
        boss.max_hp = t.max_hp;
        boss.hp = t.max_hp;
        phase = RoomPhase::BossFight;          
    }

    // 데미지를 적용하고, 이번 공격으로 쓰러졌으면 true.
    // 얼마나 깎을지는 호출하는 쪽이 아니라 서버가 정한 값이 넘어온다.
    bool attack_boss(int32_t damage) {
        if (phase != RoomPhase::BossFight) return false;

        boss.hp = std::max(0, boss.hp - damage);
        if (boss.hp == 0) {
            phase = RoomPhase::Cleared;
            return true;
        }
        return false;
    }

    nlohmann::json to_json() const {
        nlohmann::json j;
        j["id"] = id;
        j["name"] = name;
        j["members"] = members;
        j["state"] = to_string(phase);         
        // 보스가 없는 방에 빈 보스를 실어 보내면 클라이언트가 그걸 또 걸러야 한다.
        // state로 먼저 구분하고, 있을 때만 싣는다.
        if (has_boss()) {                      // in_battle 대신
            j["boss"] = {
                { "name", boss.name },
                { "maxHp", boss.max_hp },
                { "hp", boss.hp },
            };
        }

        if (loot.active) {
            j["loot"] = loot.to_json();
        }

        return j;
    }
};
