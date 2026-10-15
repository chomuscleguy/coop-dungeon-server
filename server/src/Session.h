#pragma once

#include "BattleToken.h"
#include "PlayerRegistry.h"
#include "Room.h"

#include <cstdint>
#include <deque>
#include <iostream>
#include <memory>
#include <string>
#include <boost/asio.hpp>
#include <nlohmann/json.hpp>

// 한 프로젝트 안에서만 쓰는 헤더라 using 을 둔다.
// Server.h 가 이 헤더를 포함하므로 거기서도 쓸 수 있다.
using boost::asio::ip::tcp;

class Server;   // 전방 선언 — Session 이 Server& 를 갖기 위해 필요

// ============================================================
// Session — 연결 하나를 담당
// ============================================================
class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket socket, Server& server)
        : socket_(std::move(socket)), server_(server) {
    }

    void start() {
        do_read_header();
    }

    void send(const nlohmann::json& message) {
        std::string body = message.dump();
        uint32_t len = static_cast<uint32_t>(body.size());

        std::string frame;
        frame.reserve(4 + body.size());
        frame.push_back(static_cast<char>((len >> 24) & 0xFF));
        frame.push_back(static_cast<char>((len >> 16) & 0xFF));
        frame.push_back(static_cast<char>((len >> 8) & 0xFF));
        frame.push_back(static_cast<char>(len & 0xFF));
        frame += body;

        bool write_in_progress = !write_queue_.empty();
        write_queue_.push_back(std::move(frame));
        if (!write_in_progress) {
            do_write();
        }
    }

    void close() {
        boost::system::error_code ec;
        socket_.shutdown(tcp::socket::shutdown_both, ec);
        socket_.close(ec);
    }

private:
    // ---- 네트워크 ----
    void do_write() {
        std::shared_ptr<Session> self(shared_from_this());
        boost::asio::async_write(socket_, boost::asio::buffer(write_queue_.front()),
            [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                if (ec) {
                    return;
                }
                write_queue_.pop_front();
                if (!write_queue_.empty()) {
                    do_write();
                }
            });
    }

    void do_read_header() {
        std::shared_ptr<Session> self(shared_from_this());
        boost::asio::async_read(socket_, boost::asio::buffer(header_, 4),
            [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                if (ec) {
                    if (ec == boost::asio::error::eof) {
                        std::cout << "Client disconnected" << '\n';
                    }
                    on_disconnect();
                    return;
                }

                uint32_t body_length =
                    (static_cast<uint32_t>(header_[0]) << 24) |
                    (static_cast<uint32_t>(header_[1]) << 16) |
                    (static_cast<uint32_t>(header_[2]) << 8) |
                    (static_cast<uint32_t>(header_[3]));

                if (body_length == 0 || body_length > 1024 * 1024) {
                    std::cout << "Invalid body length: " << body_length << '\n';
                    on_disconnect();
                    return;
                }
                do_read_body(body_length);
            });
    }

    void do_read_body(uint32_t body_length) {
        body_.resize(body_length);
        std::shared_ptr<Session> self(shared_from_this());
        boost::asio::async_read(socket_, boost::asio::buffer(body_.data(), body_length),
            [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                if (ec) {
                    if (ec == boost::asio::error::eof) {
                        std::cout << "Client disconnected" << '\n';
                    }
                    on_disconnect();
                    return;
                }

                handle_message(body_);
                do_read_header();
            });
    }

    // ---- 메시지 라우팅 ----
    void handle_message(const std::string& raw) {
        try {
            nlohmann::json msg = nlohmann::json::parse(raw);

            std::string type = msg.value("type", "");
            nlohmann::json data = msg.value("data", nlohmann::json::object());

            // Login은 로그인 전에도 허용
            if (type == "Login") {
                handle_login(data);
                return;
            }

            // 배틀 서버가 보내는 결과. 사람이 아니라 서버라 로그인할 수 없다.
            if (type == "DungeonResult") {
                handle_dungeon_result(data);
                return;
            }

            if (type == "BattleRegister") {
                handle_battle_register(data);
                return;
            }

            // 그 외 모든 메시지는 로그인 필수
            if (!logged_in_) {
                send_error("login required");
                return;
            }

            if (type == "Ping") {
                send({ {"type", "Pong"}, {"data", nlohmann::json::object()} });
                return;
            }

            if (type == "RoomCreate") { handle_room_create(data); return; }
            if (type == "RoomJoin") { handle_room_join(data);   return; }
            if (type == "RoomLeave") { handle_room_leave();      return; }
            if (type == "RoomList") { handle_room_list();       return; }
            if (type == "ChatSend") { handle_chat_send(data);   return; }
            if (type == "MatchEnqueue") { handle_match_enqueue(); return; } 
            if (type == "MatchCancel") { handle_match_cancel();  return; } 
            if (type == "BossAttack") { handle_boss_attack();    return; }
            if (type == "LootBid") { handle_loot_bid(data);      return; }
            if (type == "InventoryList") { handle_inventory_list(); return; }
            if (type == "FriendRequest") { handle_friend_request(data); return; }
            if (type == "FriendRespond") { handle_friend_respond(data); return; }
            if (type == "FriendList") { handle_friend_list(); return; }
            if (type == "FriendRemove") { handle_friend_remove(data); return; }
            if (type == "BattleStart") { handle_battle_start(); return; }
            
            send_error("unknown message type: " + type);
        }
        catch (const std::exception& e) {
            std::cerr << "JSON parse error: " << e.what() << '\n';
        }
    }

    void handle_dungeon_result(const nlohmann::json& data) {
        // 로그인 없이 받는 경로라, 공유 시크릿으로 막는다.
        // 이게 없으면 누구나 7777 에 붙어 가짜 결과를 밀어 넣을 수 있다.
        if (data.value("secret", "") != battle_secret()) {
            std::cerr << "DungeonResult rejected: bad secret\n";
            return;      // 응답도 안 한다. 맞췄는지 알려줄 이유가 없다
        }

        std::string names;
        for (const auto& n : data.value("players", nlohmann::json::array())) {
            if (!n.is_string()) continue;
            if (!names.empty()) names += ", ";
            names += n.get<std::string>();
        }

        std::cout << "[dungeon] battle=" << data.value("battleId", 0)
            << " result=" << data.value("result", "?")
            << " room=" << data.value("room", 0)
            << " wave=" << data.value("wave", 0)
            << " kills=" << data.value("kills", 0)
            << " players=[" << names << "]\n";
    }

    // Server 를 쓰므로 선언만 둔다. 정의는 파일 아래쪽 —
    // Session 이 Server 보다 먼저 정의되어 있어서 여기서는 쓸 수 없다.
    void handle_battle_register(const nlohmann::json& data);
    void handle_battle_start();

    void send_error(const std::string& message) {
        nlohmann::json err;
        err["message"] = message;
        send({ {"type", "Error"}, {"data", err} });
    }

    // ---- Server를 사용하는 함수들: 선언만, 정의는 Server 뒤에 ----
    void handle_login(const nlohmann::json& data);
    void handle_room_create(const nlohmann::json& data);
    void handle_room_join(const nlohmann::json& data);
    void handle_room_leave();
    void handle_room_list();
    void handle_chat_send(const nlohmann::json& data);
    void handle_match_enqueue();      
    void handle_match_cancel();      
    void handle_boss_attack();
    void handle_loot_bid(const nlohmann::json& data);
    void handle_inventory_list();
    void handle_friend_request(const nlohmann::json& data);
    void handle_friend_respond(const nlohmann::json& data);
    void handle_friend_list();
    void handle_friend_remove(const nlohmann::json& data);

    void on_disconnect();

    // ---- 멤버 ----
    tcp::socket socket_;
    unsigned char header_[4];
    std::string body_;
    std::deque<std::string> write_queue_;

    uint64_t player_id_ = 0;
    std::string username_;
    bool logged_in_ = false;

    // 사람이 아니라 배틀 서버가 붙은 연결.
    bool is_battle_server_ = false;
    int battle_port_ = 0;

    // 방 소속(room_id_)은 Server가 들고 있다. 매칭처럼 "남을 방에 넣는" 동작이 생기면
    // 각 Session이 자기 소속을 따로 들고 있는 구조로는 갱신할 방법이 없다.

    Server& server_;
};
