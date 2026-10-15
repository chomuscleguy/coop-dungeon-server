#pragma once

#include "Friendship.h"
#include "Room.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

// ============================================================
// PlayerRegistry — 계정 정보. 연결이 끊겨도 살아남는다.
// ============================================================
// 지금까지 플레이어 정보(번호, 유저네임)는 Session 안에 있었고 연결이 끊기면
// 같이 사라졌다. 보상으로 받은 재화는 그러면 안 되므로, 연결보다 오래 사는
// 정보를 담을 자리가 Session 바깥에 필요해졌다.
class PlayerRegistry {
public:
    struct Player {
        uint64_t id = 0;
        std::string username;
        uint64_t currency = 0;
        bool online = false;
        std::vector<Item> inventory;
    };

    // 처음 보는 이름이면 계정을 만들고, 아는 이름이면 그 계정을 그대로 돌려준다.
    // 비밀번호는 여전히 없다 — 이름만 대면 그 계정이 된다(Step 5의 한계 그대로).
    Player& login(const std::string& username) {
        auto it = name_to_id_.find(username);
        if (it != name_to_id_.end()) {
            return players_[it->second];
        }

        uint64_t id = next_id_++;
        Player& p = players_[id];
        p.id = id;
        p.username = username;
        name_to_id_[username] = id;
        return p;
    }

    Player* find(uint64_t player_id) {
        auto it = players_.find(player_id);
        return (it == players_.end()) ? nullptr : &it->second;
    }

    // 유저네임으로 계정을 찾는다. 없으면 nullptr (login과 달리 만들지 않는다).
    Player* find_by_name(const std::string& username) {
        auto it = name_to_id_.find(username);
        return (it == name_to_id_.end()) ? nullptr : &players_[it->second];
    }

    void grant_currency(uint64_t player_id, uint64_t amount) {
        if (Player* p = find(player_id)) p->currency += amount;
    }

    // 새 개체를 만든다. 아직 누구의 것도 아니다 — 경매에 걸릴 물건.
    Item make_item(const std::string& name) {
        return Item{ next_item_id_++, name };
    }

    // 이미 존재하는 개체를 인벤토리에 넣는다. 번호가 유지된다.
    void give_item(uint64_t player_id, const Item& item) {
        if (Player* p = find(player_id)) p->inventory.push_back(item);
    }

    // 재화를 깎는다. 잔액이 모자라면 아무 일도 안 하고 false.
    bool spend_currency(uint64_t player_id, uint64_t amount) {
        Player* p = find(player_id);
        if (!p || p->currency < amount) return false;

        p->currency -= amount;
        return true;
    }

    void set_online(uint64_t player_id, bool online) {
        if (Player* p = find(player_id)) p->online = online;
    }

    // ---- 친구 ----
    // a < b 정규화. 어느 쪽이 신청했든 같은 관계를 같은 자리에서 찾게 한다.
    Friendship* find_friendship(uint64_t x, uint64_t y) {
        uint64_t lo = std::min(x, y);
        uint64_t hi = std::max(x, y);
        for (Friendship& f : friendships_) {
            if (f.a == lo && f.b == hi) return &f;
        }
        return nullptr;
    }

    // 새 신청을 만든다. 이미 관계가 있으면(신청 중이든 친구든) nullptr.
    Friendship* add_request(uint64_t from, uint64_t to) {
        if (from == to) return nullptr;
        if (find_friendship(from, to)) return nullptr;

        Friendship f;
        f.a = std::min(from, to);
        f.b = std::max(from, to);
        f.requested_by = from;
        f.accepted = false;
        friendships_.push_back(f);
        return &friendships_.back();
    }

    void remove_friendship(uint64_t x, uint64_t y) {
        uint64_t lo = std::min(x, y);
        uint64_t hi = std::max(x, y);
        friendships_.erase(
            std::remove_if(friendships_.begin(), friendships_.end(),
                [lo, hi](const Friendship& f) { return f.a == lo && f.b == hi; }),
            friendships_.end());
    }

    // 나와 관련된 관계를 전부 모아 JSON으로. 수락된 친구와 대기 중인 신청을 나눠 담는다.
    nlohmann::json friend_list_json(uint64_t me) {
        nlohmann::json friends = nlohmann::json::array();
        nlohmann::json incoming = nlohmann::json::array();
        nlohmann::json outgoing = nlohmann::json::array();

        for (const Friendship& f : friendships_) {
            if (!f.involves(me)) continue;

            Player* p = find(f.other(me));
            if (!p) continue;

            nlohmann::json entry;
            entry["playerId"] = p->id;
            entry["username"] = p->username;
            entry["online"] = p->online;

            if (f.accepted)                    friends.push_back(entry);
            else if (f.requested_by == me)     outgoing.push_back(entry);
            else                               incoming.push_back(entry);
        }

        nlohmann::json j;
        j["friends"] = friends;
        j["incoming"] = incoming;
        j["outgoing"] = outgoing;
        return j;
    }

private:
    std::unordered_map<uint64_t, Player> players_;
    std::unordered_map<std::string, uint64_t> name_to_id_;
    uint64_t next_id_ = 1;
    uint64_t next_item_id_ = 1;
    std::vector<Friendship> friendships_;
};
