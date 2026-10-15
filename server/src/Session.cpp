#include "Session.h"

#include "BattleToken.h"
#include "Server.h"

#include <iostream>
#include <utility>
#include <vector>

// Server 를 쓰는 Session 메서드들. 선언은 Session.h 에 있다.

void Session::handle_login(const nlohmann::json& data) {
    if (logged_in_) {
        send_error("already logged in");
        return;
    }

    std::string username = data.value("username", "");
    if (username.empty()) {
        send_error("username required");
        return;
    }

    // 처음 보는 이름이면 계정이 새로 생기고, 아는 이름이면 그 계정으로 들어간다.
    PlayerRegistry::Player& p = server_.players().login(username);

    // 같은 계정이 이미 접속 중이면 기존 연결을 밀어낸다.
    if (p.online) {
        std::cout << "Kicking previous session of " << username << '\n';
        server_.kick(p.id);
    }
    p.online = true;

    player_id_ = p.id;
    username_ = p.username;
    logged_in_ = true;

    server_.register_session(player_id_, shared_from_this());

    std::cout << "Login: " << username_ << " (id=" << player_id_ << ")\n";

    nlohmann::json ok;
    ok["playerId"] = player_id_;
    ok["username"] = username_;
    ok["currency"] = p.currency;
    send({ {"type", "LoginOk"}, {"data", ok} });

    //자리를 비워둔 방이 있으면 현재 상태를 보내준다
    uint32_t room_id = server_.room_of(player_id_);
    if (room_id != 0) {
        Room* room = server_.find_room(room_id);
        if (room) {
            server_.clear_seat_hold(player_id_);
            std::cout << username_ << " reconnected to room " << room_id << '\n';
            send({ {"type", "RoomState"}, {"data", room->to_json()} });
        }
    }
}

void Session::handle_room_create(const nlohmann::json& data) {
    if (server_.room_of(player_id_) != 0) {
        send_error("already in a room");
        return;
    }
    std::string name = data.value("name", "");
    if (name.empty()) {
        send_error("room name required");
        return;
    }

    uint32_t id = server_.create_room(name, player_id_);
    std::cout << username_ << " created room " << id << " (" << name << ")\n";

    server_.broadcast_room_state(id);
}

void Session::handle_room_join(const nlohmann::json& data) {
    if (server_.room_of(player_id_) != 0) {
        send_error("already in a room");
        return;
    }
    uint32_t room_id = data.value("roomId", 0u);
    if (!server_.join_room(room_id, player_id_)) {
        send_error("cannot join room " + std::to_string(room_id));
        return;
    }

    std::cout << username_ << " joined room " << room_id << '\n';

    // 들어온 사람만이 아니라 원래 있던 사람들도 새 멤버 목록을 받는다
    server_.broadcast_room_state(room_id);
}

void Session::handle_room_leave() {
    uint32_t left_room = server_.leave_current_room(player_id_);
    if (left_room == 0) {
        send_error("not in a room");
        return;
    }
    std::cout << username_ << " left room " << left_room << '\n';

    // 나간 본인은 이미 방 멤버가 아니라 브로드캐스트 대상이 아님 -> 따로 회신
    send({ {"type", "RoomLeaveOk"}, {"data", nlohmann::json::object()} });
    server_.broadcast_room_state(left_room);
}

void Session::handle_room_list() {
    send({ {"type", "RoomListResult"}, {"data", server_.room_list_json()} });
}

void Session::handle_chat_send(const nlohmann::json& data) {
    uint32_t room_id = server_.room_of(player_id_);  
    if (room_id == 0) {                              
        send_error("not in a room");
        return;
    }

    std::string text = data.value("text", "");
    if (text.empty()) {
        send_error("text required");
        return;
    }
    if (text.size() > 500) {
        send_error("text too long");
        return;
    }

    // 보내는 사람이 누군지는 자기 Session이 이미 알고 있다.
    // sessions_가 필요한 건 "누가 보냈나"가 아니라 "누구에게 전달하나" 쪽.
    nlohmann::json chat;
    chat["fromId"] = player_id_;
    chat["fromName"] = username_;
    chat["text"] = text;

    server_.broadcast_to_room(room_id,
        { {"type", "ChatBroadcast"}, {"data", chat} });
}

void Session::handle_boss_attack() {
    uint32_t room_id = server_.room_of(player_id_);
    if (room_id == 0) {
        send_error("not in a room");
        return;
    }

    Room* room = server_.find_room(room_id);
    if (!room) {
        send_error("room not found");
        return;
    }
    if (room->phase == RoomPhase::Waiting) {
        send_error("no boss in this room");
        return;
    }
    if (room->phase == RoomPhase::Cleared) {
        send_error("boss already cleared");
        return;
    }

    server_.attack_boss(room_id, player_id_, username_);
}

void Session::handle_loot_bid(const nlohmann::json& data) {
    uint32_t room_id = server_.room_of(player_id_);
    if (room_id == 0) {
        send_error("not in a room");
        return;
    }

    Room* room = server_.find_room(room_id);
    if (!room || !room->loot.active) {
        send_error("no auction running");
        return;
    }

    uint64_t amount = data.value("amount", 0ull);
    if (amount == 0) {
        send_error("amount required");
        return;
    }
    if (amount <= room->loot.highest_bid) {
        send_error("bid too low");
        return;
    }

    PlayerRegistry::Player* me = server_.players().find(player_id_);
    if (!me || me->currency < amount) {
        send_error("not enough currency");
        return;
    }

    if (!server_.bid_on_loot(room_id, player_id_, amount)) {
        send_error("bid rejected");
        return;
    }
}

void Session::handle_inventory_list() {
    PlayerRegistry::Player* p = server_.players().find(player_id_);
    if (!p) {
        send_error("player not found");
        return;
    }

    nlohmann::json items = nlohmann::json::array();
    for (const Item& item : p->inventory) {
        items.push_back(item.to_json());
    }

    nlohmann::json d;
    d["items"] = items;
    d["currency"] = p->currency;
    send({ {"type", "InventoryResult"}, {"data", d} });
}

void Session::handle_friend_request(const nlohmann::json& data) {
    std::string target_name = data.value("username", "");
    if (target_name.empty()) {
        send_error("username required");
        return;
    }
    if (target_name == username_) {
        send_error("cannot add yourself");
        return;
    }

    PlayerRegistry::Player* target = server_.players().find_by_name(target_name);
    if (!target) {
        send_error("no such player");
        return;
    }

    // 이미 관계가 있으면 사유를 구분해서 알려준다
    Friendship* existing = server_.players().find_friendship(player_id_, target->id);
    if (existing) {
        if (existing->accepted) {
            send_error("already friends");
        }
        else if (existing->requested_by == player_id_) {
            send_error("already requested");
        }
        else {
            send_error("they already sent you a request");
        }
        return;
    }

    if (!server_.players().add_request(player_id_, target->id)) {
        send_error("cannot send request");
        return;
    }

    std::cout << username_ << " sent friend request to " << target_name << std::endl;

    server_.notify_friend_request(player_id_, target->id);

    nlohmann::json ok;
    ok["toId"] = target->id;
    ok["toName"] = target->username;
    send({ {"type", "FriendRequestSent"}, {"data", ok} });
}

void Session::handle_friend_respond(const nlohmann::json& data) {
    std::string from_name = data.value("username", "");
    if (from_name.empty()) {
        send_error("username required");
        return;
    }

    PlayerRegistry::Player* other = server_.players().find_by_name(from_name);
    if (!other) {
        send_error("no such player");
        return;
    }

    Friendship* f = server_.players().find_friendship(player_id_, other->id);
    if (!f) {
        send_error("no pending request");
        return;
    }
    if (f->accepted) {
        send_error("already friends");
        return;
    }
    // 내가 보낸 신청을 내가 수락할 수는 없다
    if (f->requested_by == player_id_) {
        send_error("you sent this request");
        return;
    }

    bool accept = data.value("accept", false);

    if (!accept) {
        server_.players().remove_friendship(player_id_, other->id);
        std::cout << username_ << " declined " << from_name << std::endl;

        nlohmann::json d;
        d["username"] = other->username;
        d["accepted"] = false;
        send({ {"type", "FriendRespondOk"}, {"data", d} });
        return;
    }

    f->accepted = true;                      // 수락은 이 한 줄
    std::cout << username_ << " accepted " << from_name << std::endl;

    server_.notify_friend_accepted(player_id_, other->id);
}

void Session::handle_friend_list() {
    send({ {"type", "FriendListResult"},
           {"data", server_.players().friend_list_json(player_id_)} });
}

void Session::handle_friend_remove(const nlohmann::json& data) {
    std::string target_name = data.value("username", "");
    if (target_name.empty()) {
        send_error("username required");
        return;
    }

    PlayerRegistry::Player* other = server_.players().find_by_name(target_name);
    if (!other) {
        send_error("no such player");
        return;
    }

    Friendship* f = server_.players().find_friendship(player_id_, other->id);
    if (!f) {
        send_error("not friends");
        return;
    }

    if (!f->accepted) {
        // 아직 수락 전인 신청은 취소로 처리한다
        server_.players().remove_friendship(player_id_, other->id);

        nlohmann::json d;
        d["username"] = other->username;
        d["cancelled"] = true;
        send({ {"type", "FriendRemoveOk"}, {"data", d} });
        return;
    }

    server_.players().remove_friendship(player_id_, other->id);
    std::cout << username_ << " removed friend " << target_name << std::endl;

    server_.notify_friend_removed(player_id_, other->id);
}

void Session::handle_match_enqueue() {
    if (server_.room_of(player_id_) != 0) {
        send_error("already in a room");
        return;
    }
    if (server_.in_queue(player_id_)) {
        send_error("already in queue");
        return;
    }

    // 성사되면 MatchFound가 이미 나갔으므로 여기서 더 보낼 게 없다.
    if (server_.enqueue_for_match(player_id_)) {
        return;
    }

    std::cout << username_ << " queued for match\n";

    nlohmann::json d;
    d["waiting"] = server_.queue_size();
    d["needed"] = Server::kPartySize;
    send({ {"type", "MatchQueued"}, {"data", d} });
}

void Session::handle_match_cancel() {
    if (!server_.in_queue(player_id_)) {
        send_error("not in queue");
        return;
    }
    server_.cancel_match(player_id_);
    std::cout << username_ << " cancelled matchmaking\n";

    send({ {"type", "MatchCancelOk"}, {"data", nlohmann::json::object()} });
}

void Session::handle_battle_register(const nlohmann::json& data) {
    if (data.value("secret", "") != battle_secret()) {
        std::cerr << "BattleRegister rejected: bad secret\n";
        return;
    }

    is_battle_server_ = true;
    battle_port_ = data.value("port", 0);

    server_.set_battle_link(shared_from_this(),
        static_cast<unsigned short>(battle_port_));

    std::cout << "[battle] server registered (udp port "
        << battle_port_ << ")\n";
}

void Session::handle_battle_start() {
    uint32_t room_id = server_.room_of(player_id_);
    if (room_id == 0) { send_error("not in a room"); return; }

    Room* room = server_.find_room(room_id);
    if (!room) { send_error("room is gone"); return; }

    auto link = server_.battle_link();
    if (!link) { send_error("battle server offline"); return; }

    uint32_t battle_id = server_.take_battle_id();

    // 방 전원에게 표를 하나씩 발급한다. 각자 다른 표다.
    nlohmann::json players = nlohmann::json::array();
    std::vector<std::pair<uint64_t, std::string>> grants;

    for (uint64_t pid : room->members) {
        PlayerRegistry::Player* p = server_.players().find(pid);
        if (!p) continue;

        std::string token = make_battle_token();
        players.push_back({
            {"token", token},
            {"username", p->username},
            {"player_id", pid}
            });
        grants.emplace_back(pid, token);
    }

    // 1) 배틀 서버에 "이 사람들이 간다"
    nlohmann::json grant;
    grant["type"] = "SessionGrant";
    grant["data"] = {
        {"secret", battle_secret()},
        {"battle_id", battle_id},
        {"players", players}
    };
    link->send(grant);

    // 2) 각자에게 "여기로 붙어라". 표가 다르므로 개별 전송이다.
    for (const auto& g : grants) {
        nlohmann::json ready;
        ready["type"] = "BattleReady";
        ready["data"] = {
            {"battleId", battle_id},
            {"host", battle_public_host()},
            {"port", server_.battle_udp_port()},
            {"token", g.second}
        };
        server_.send_to_player(g.first, ready);
    }

    std::cout << "[battle] start battle=" << battle_id
        << " players=" << grants.size() << '\n';
}

void Session::on_disconnect() {
    server_.cancel_match(player_id_);

    uint32_t room_id = server_.room_of(player_id_);
    Room* room = server_.find_room(room_id);

    // 전투 중이면 자리를 비워둔다 — 같은 계정으로 돌아오면 그대로 복귀
    bool keep_seat = (room && room->phase == RoomPhase::BossFight);

    if (!keep_seat) {
        uint32_t left_room = server_.leave_current_room(player_id_);
        if (left_room != 0) {
            server_.broadcast_room_state(left_room);
        }
    }
    else {
        server_.hold_seat(player_id_); 
    }

    if (logged_in_) {
        server_.players().set_online(player_id_, false);
        server_.unregister_session(player_id_, this);
        logged_in_ = false;
    }
}
