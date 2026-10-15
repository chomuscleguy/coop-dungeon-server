#pragma once

#include "PlayerRegistry.h"
#include "Room.h"
#include "Session.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
#include <boost/asio.hpp>
#include <nlohmann/json.hpp>

// ============================================================
// Server — 공유 상태(방 목록)와 접속 수락을 담당
// ============================================================
class Server {
public:
    Server(boost::asio::io_context& io, unsigned short port)
        : acceptor_(io, tcp::endpoint(tcp::v4(), port)), tick_timer_(io) {
    }

    void start() {
        std::cout << "Listening on port "
            << acceptor_.local_endpoint().port() << "...\n";
        do_accept();
        start_tick_timer();
    }

    // 새 연결을 그만 받고 타이머를 멈춘다. 진행 중인 비동기 작업이 끝나면
    // io_context에 할 일이 없어지므로 io.run()이 스스로 반환한다.
    void stop() {
        std::cout << "Shutting down..." << '\n';

        boost::system::error_code ec;
        acceptor_.close(ec);
        tick_timer_.cancel();

        // 로그인 여부와 무관하게 모든 연결을 닫는다.
        // 하나라도 읽기 대기가 남으면 io.run()이 반환하지 않는다.
        for (const std::weak_ptr<Session>& w : all_sessions_) {
            if (auto s = w.lock()) s->close();
        }
        all_sessions_.clear();
        sessions_.clear();
    }

    // ---- 방 관리 ----
    // "누가 어느 방에 있나"는 rooms_[].members에도 들어있지만, 번호로 거꾸로 찾으려면
    // 방 전체를 뒤져야 한다. 역방향 색인을 따로 둬서 O(1)로 찾는다.
    // 같은 사실이 두 군데 있으므로, 넣고 빼는 건 반드시 이 클래스 안에서만 한다.
    uint32_t room_of(uint64_t player_id) const {
        auto it = player_to_room_.find(player_id);
        return (it == player_to_room_.end()) ? 0 : it->second;   // 0 = 어느 방에도 없음
    }

    uint32_t create_room(const std::string& name, uint64_t player_id) {
        uint32_t id = next_room_id_++;
        Room room;
        room.id = id;
        room.name = name;
        room.members.push_back(player_id);
        rooms_.emplace(id, std::move(room));
        player_to_room_[player_id] = id;
        return id;
    }

    Room* find_room(uint32_t room_id) {
        auto it = rooms_.find(room_id);
        return (it == rooms_.end()) ? nullptr : &it->second;
    }

    bool join_room(uint32_t room_id, uint64_t player_id) {
        Room* room = find_room(room_id);
        if (!room) return false;

        auto& m = room->members;
        if (std::find(m.begin(), m.end(), player_id) != m.end()) return false;

        m.push_back(player_id);
        player_to_room_[player_id] = room_id;
        return true;
    }

    // 어느 방에 있었는지를 돌려준다(없었으면 0). 호출한 쪽이 그 방에 브로드캐스트할 수 있게.
    uint32_t leave_current_room(uint64_t player_id) {
        uint32_t room_id = room_of(player_id);
        if (room_id == 0) return 0;

        player_to_room_.erase(player_id);

        Room* room = find_room(room_id);
        if (room) {
            auto& m = room->members;
            m.erase(std::remove(m.begin(), m.end(), player_id), m.end());
            if (m.empty()) {
                rooms_.erase(room_id);   // 아무도 없는 방은 삭제
            }
        }
        return room_id;
    }

    nlohmann::json room_list_json() const {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& entry : rooms_) {
            arr.push_back(entry.second.to_json());
        }
        return arr;
    }

    void kick(uint64_t player_id) {
        auto it = sessions_.find(player_id);
        if (it == sessions_.end()) return;

        it->second->close();
    }

    // 방 전체가 아니라 한 사람에게만 보낸다. 입장권은 각자 다르다.
    void send_to_player(uint64_t player_id, const nlohmann::json& message) {
        auto it = sessions_.find(player_id);
        if (it == sessions_.end()) return;      // 그 사이 끊겼으면 그냥 버린다
        it->second->send(message);
    }

    // ---- 배틀 서버 링크 ----
    // weak_ptr 인 이유: 배틀 서버가 끊기면 Session 이 사라져야 하는데,
    // Server 가 shared_ptr 로 잡고 있으면 영영 안 죽는다. (Step 12)
    void set_battle_link(std::shared_ptr<Session> s, unsigned short port) {
        battle_link_ = s;
        battle_udp_port_ = port;
    }

    std::shared_ptr<Session> battle_link() { return battle_link_.lock(); }
    unsigned short battle_udp_port() const { return battle_udp_port_; }
    uint32_t take_battle_id() { return next_battle_id_++; }

    // ---- 세션 레지스트리 ----
    // 방은 멤버를 player_id로만 들고 있어서, 그 번호만으로는 메시지를 보낼 수 없다.
    // "번호 -> 그 사람의 연결"을 이어주는 표가 여기 필요해짐.
    void register_session(uint64_t player_id, std::shared_ptr<Session> session) {
        sessions_[player_id] = std::move(session);
    }

    // 쫓겨난 세션이 뒤늦게 정리에 들어올 수 있다. 그 사이 같은 번호로 새 연결이
    // 등록됐다면, 지금 맵에 있는 건 남의 것이므로 건드리면 안 된다.
    void unregister_session(uint64_t player_id, const Session* who) {
        auto it = sessions_.find(player_id);
        if (it == sessions_.end()) return;
        if (it->second.get() != who) return;   // 이미 새 세션이 자리를 차지함

        sessions_.erase(it);
    }

    // ---- 브로드캐스트 ----
    void broadcast_to_room(uint32_t room_id, const nlohmann::json& message) {
        Room* room = find_room(room_id);
        if (!room) return;

        for (uint64_t member_id : room->members) {
            auto it = sessions_.find(member_id);
            if (it != sessions_.end()) {
                it->second->send(message);
            }
        }
    }

    // 방이 이미 삭제됐으면(마지막 사람이 나감) 아무에게도 안 보냄
    void broadcast_room_state(uint32_t room_id) {
        Room* room = find_room(room_id);
        if (!room) return;

        broadcast_to_room(room_id,
            { {"type", "RoomState"}, {"data", room->to_json()} });
    }

    // ---- 전투 ----
    // 데미지는 서버가 정한다(kAttackDamage). 호출하는 쪽은 "누가 때렸다"만 전달.
    void attack_boss(uint32_t room_id, uint64_t attacker_id,
        const std::string& attacker_name) {
        Room* room = find_room(room_id);
        if (!room) return;

        bool defeated = room->attack_boss(kAttackDamage);

        nlohmann::json d;
        d["hp"] = room->boss.hp;
        d["maxHp"] = room->boss.max_hp;
        d["damage"] = kAttackDamage;
        d["attackerId"] = attacker_id;
        d["attackerName"] = attacker_name;
        broadcast_to_room(room_id, { {"type", "BossState"}, {"data", d} });

        if (defeated) {
            std::cout << "Boss cleared in room " << room_id << std::endl;
            distribute_clear_reward(*room);

            // 드랍 아이템으로 파티 경매 시작
            Item drop = players_.make_item(kBossDropName);
            room->start_loot_auction(drop);

            broadcast_room_state(room_id);   // state:"cleared" + loot 포함
            broadcast_to_room(room_id,
                { {"type", "LootAuctionStarted"}, {"data", room->loot.to_json()} });
        }
    }

    // 입찰: 돈을 먼저 잡아두고(에스크로), 밀려난 사람에게는 돌려준다.
   // 실패 사유는 호출한 Session이 판정해서 회신한다.
    bool bid_on_loot(uint32_t room_id, uint64_t bidder, uint64_t amount) {
        Room* room = find_room(room_id);
        if (!room || !room->loot.active) return false;
        if (amount <= room->loot.highest_bid) return false;

        // 돈부터 잡아둔다. 모자라면 여기서 실패.
        if (!players_.spend_currency(bidder, amount)) return false;

        uint64_t outbid_player = 0;
        uint64_t outbid_amount = 0;
        if (!room->place_bid(bidder, amount, outbid_player, outbid_amount)) {
            players_.grant_currency(bidder, amount);   // 되돌리기
            return false;
        }

        // 밀려난 사람에게 환불
        if (outbid_player != 0) {
            players_.grant_currency(outbid_player, outbid_amount);
            auto it = sessions_.find(outbid_player);
            if (it != sessions_.end()) {
                PlayerRegistry::Player* p = players_.find(outbid_player);
                nlohmann::json d;
                d["currency"] = outbid_amount;
                d["reason"] = "outbid";
                d["balance"] = p ? p->currency : 0;
                it->second->send({ {"type", "RewardGrant"}, {"data", d} });
            }
        }

        broadcast_to_room(room_id,
            { {"type", "LootBidUpdate"}, {"data", room->loot.to_json()} });
        return true;
    }

    // 마감 처리: 낙찰이면 아이템을 주고 낙찰금을 나머지 파티원이 나눠 갖는다.
    // 아무도 안 걸었으면 아이템은 소멸한다.
    void close_auction(Room& room) {
        LootAuction result = room.loot;     // 닫기 전에 결과를 복사해둔다
        room.close_loot_auction();

        nlohmann::json d;
        d["item"] = result.item.to_json();

        if (result.highest_bidder == 0) {
            d["winnerId"] = 0;
            d["winningBid"] = 0;
            d["result"] = "expired";        // 유찰 — 아이템 소멸
            broadcast_to_room(room.id,
                { {"type", "LootAuctionClosed"}, {"data", d} });
            return;
        }

        // 낙찰자에게 아이템
        players_.give_item(result.highest_bidder, result.item);

        // 낙찰금은 낙찰자를 뺀 나머지가 균등 분배 (돈은 입찰 때 이미 잡아뒀다)
        std::vector<uint64_t> others;
        for (uint64_t pid : room.members) {
            if (pid != result.highest_bidder) others.push_back(pid);
        }

        if (!others.empty()) {
            uint64_t share = result.highest_bid / others.size();
            for (uint64_t pid : others) {
                players_.grant_currency(pid, share);

                auto it = sessions_.find(pid);
                if (it == sessions_.end()) continue;

                PlayerRegistry::Player* p = players_.find(pid);
                nlohmann::json r;
                r["currency"] = share;
                r["reason"] = "loot_share";
                r["balance"] = p ? p->currency : 0;
                it->second->send({ {"type", "RewardGrant"}, {"data", r} });
            }
        }

        d["winnerId"] = result.highest_bidder;
        d["winningBid"] = result.highest_bid;
        d["result"] = "sold";
        broadcast_to_room(room.id,
            { {"type", "LootAuctionClosed"}, {"data", d} });
    }

    // ---- 친구 ----
    // 신청을 만들고, 상대가 접속 중이면 바로 알려준다.
    void notify_friend_request(uint64_t from, uint64_t to) {
        auto it = sessions_.find(to);
        if (it == sessions_.end()) return;      // 접속 중이 아니면 나중에 목록으로 본다

        PlayerRegistry::Player* sender = players_.find(from);

        nlohmann::json d;
        d["fromId"] = from;
        d["fromName"] = sender ? sender->username : "";
        it->second->send({ {"type", "FriendRequestReceived"}, {"data", d} });
    }

    // 수락됐음을 양쪽에 알린다. 관계는 둘의 것이므로 통지도 둘 다 받아야 한다.
    void notify_friend_accepted(uint64_t x, uint64_t y) {
        send_friend_accepted_to(x, y);
        send_friend_accepted_to(y, x);
    }

    // 삭제도 양쪽에 알린다. 한쪽만 알면 상대 목록엔 남아있다고 착각한다.
    void notify_friend_removed(uint64_t x, uint64_t y) {
        send_friend_removed_to(x, y);
        send_friend_removed_to(y, x);
    }

    void send_friend_removed_to(uint64_t who, uint64_t former_friend) {
        auto it = sessions_.find(who);
        if (it == sessions_.end()) return;

        PlayerRegistry::Player* f = players_.find(former_friend);
        if (!f) return;

        nlohmann::json d;
        d["playerId"] = f->id;
        d["username"] = f->username;
        it->second->send({ {"type", "FriendRemoved"}, {"data", d} });
    }

    void send_friend_accepted_to(uint64_t who, uint64_t friend_id) {
        auto it = sessions_.find(who);
        if (it == sessions_.end()) return;

        PlayerRegistry::Player* f = players_.find(friend_id);
        if (!f) return;

        nlohmann::json d;
        d["playerId"] = f->id;
        d["username"] = f->username;
        d["online"] = f->online;
        it->second->send({ {"type", "FriendAdded"}, {"data", d} });
    }

    // 전투 중 끊김 — 자리를 지켜주되 마감 시각을 박아둔다.
    void hold_seat(uint64_t player_id) {
        pending_reconnect_[player_id] = std::chrono::steady_clock::now()
            + std::chrono::seconds(kReconnectGraceSeconds);
    }

    void clear_seat_hold(uint64_t player_id) {
        pending_reconnect_.erase(player_id);
    }

    // 클리어 보상을 방 인원수로 균등 분배한다. 나머지는 버린다.
    void distribute_clear_reward(const Room& room) {
        if (room.members.empty()) return;

        uint64_t share = kDefaultBoss.clear_reward / room.members.size();

        for (uint64_t pid : room.members) {
            players_.grant_currency(pid, share);

            auto it = sessions_.find(pid);
            if (it == sessions_.end()) continue;   // 끊긴 사람은 알림만 생략

            PlayerRegistry::Player* p = players_.find(pid);

            nlohmann::json d;
            d["currency"] = share;
            d["reason"] = "boss_clear";
            d["balance"] = p ? p->currency : 0;
            it->second->send({ {"type", "RewardGrant"}, {"data", d} });
        }
    }

    // ---- 매칭 큐 ----
    static constexpr std::size_t kPartySize = 2;

    bool in_queue(uint64_t player_id) const {
        return std::find(match_queue_.begin(), match_queue_.end(), player_id)
            != match_queue_.end();
    }

    std::size_t queue_size() const { return match_queue_.size(); }

    void cancel_match(uint64_t player_id) {
        match_queue_.erase(
            std::remove(match_queue_.begin(), match_queue_.end(), player_id),
            match_queue_.end());
    }

    // ---- 계정 ----
    PlayerRegistry& players() { return players_; }

    // 매칭이 성사되면 방을 만들어 전원을 넣고 MatchFound까지 보낸 뒤 true.
    // 이 함수만 "남의 방 소속을 바꾸는" 일을 한다 — Session은 자기 것밖에 못 바꾼다.
    bool enqueue_for_match(uint64_t player_id) {
        match_queue_.push_back(player_id);
        if (match_queue_.size() < kPartySize) {
            return false;
        }

        std::vector<uint64_t> party(match_queue_.begin(),
            match_queue_.begin() + kPartySize);
        match_queue_.erase(match_queue_.begin(), match_queue_.begin() + kPartySize);

        uint32_t id = next_room_id_++;
        Room room;
        room.id = id;
        room.name = "Matchmade Room " + std::to_string(id);
        room.members = party;
        room.spawn_boss(kDefaultBoss);
        rooms_.emplace(id, std::move(room));

        for (uint64_t pid : party) {
            player_to_room_[pid] = id;
        }

        std::cout << "Match found -> room " << id << " (" << party.size() << " players)\n";

        broadcast_to_room(id,
            { {"type", "MatchFound"}, {"data", find_room(id)->to_json()} });
        return true;
    }

private:
    // 붙어 있는 배틀 서버 세션. 지금은 한 대만 상정한다.
    std::weak_ptr<Session> battle_link_;
    unsigned short battle_udp_port_ = 0;
    uint32_t next_battle_id_ = 1;

    void do_accept() {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket) {
                // acceptor가 닫히면(stop()) 에러와 함께 불린다. 여기서 끝내야
                // io_context에 할 일이 없어져 io.run()이 반환한다.
                if (ec) return;

                std::cout << "Client connected: "
                    << socket.remote_endpoint() << '\n';
                auto session = std::make_shared<Session>(std::move(socket), *this);
                all_sessions_.push_back(session);
                session->start();

                do_accept();
            });
    }

    void start_tick_timer() {
        tick_timer_.expires_after(std::chrono::seconds(1));
        tick_timer_.async_wait([this](boost::system::error_code ec) {
            if (ec) return;        // 타이머가 취소됨 (서버 종료 등)
            on_tick();
            start_tick_timer();    // 다음 틱 예약
            });
    }

    void on_tick() {
        // 마감된 경매를 찾아 정산한다. 여기가 "아무도 안 보냈는데 서버가 움직이는" 곳.
        for (auto& entry : rooms_) {
            if (entry.second.auction_expired()) {
                close_auction(entry.second);
            }
        }

        // 돌아오지 않은 사람의 자리를 정리한다.
        // 순회 중에 지울 수 없으니 먼저 모아두고 그 다음에 처리한다.
        auto now = std::chrono::steady_clock::now();
        std::vector<uint64_t> expired;
        for (const auto& entry : pending_reconnect_) {
            if (now >= entry.second) expired.push_back(entry.first);
        }

        for (uint64_t pid : expired) {
            pending_reconnect_.erase(pid);

            uint32_t left_room = leave_current_room(pid);
            if (left_room != 0) {
                std::cout << "Reconnect timeout: player " << pid
                    << " removed from room " << left_room << std::endl;
                broadcast_room_state(left_room);
            }
        }

        // 죽은 연결의 weak_ptr을 걷어낸다. 안 그러면 목록이 계속 길어진다.
        all_sessions_.erase(
            std::remove_if(all_sessions_.begin(), all_sessions_.end(),
                [](const std::weak_ptr<Session>& w) { return w.expired(); }),
            all_sessions_.end());
    }

    tcp::acceptor acceptor_;
    boost::asio::steady_timer tick_timer_;
    uint32_t next_room_id_ = 1;
    std::unordered_map<uint32_t, Room> rooms_;
    std::unordered_map<uint64_t, uint32_t> player_to_room_;
    std::unordered_map<uint64_t, std::shared_ptr<Session>> sessions_;  // 로그인한 연결만
    // 로그인 여부와 무관하게 살아있는 모든 연결. 종료할 때 전부 닫아야
    // io.run()이 반환한다. 소유는 async 핸들러 체인이 하므로 weak_ptr로 본다.
    std::vector<std::weak_ptr<Session>> all_sessions_;
    std::vector<uint64_t> match_queue_;
    // 전투 중 끊겨서 자리를 비워둔 사람들. 값은 "이때까지 안 오면 정리" 시각.
    std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> pending_reconnect_;
    PlayerRegistry players_;
};
