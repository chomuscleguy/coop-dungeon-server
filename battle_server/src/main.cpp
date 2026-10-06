#include <cstdlib>
#include <iostream>
#include <optional>
#include <utility>        // std::exchange (Boost 1.74 awaitable.hpp 우회)
#include <string>

#include <boost/asio.hpp>

#include "BattleServer.h"

std::optional<unsigned short> resolve_port(int argc, char* argv[]) {
    long value = 7778;                  // 로비(7777)와 다른 포트
    const char* source = "default";

    if (const char* env = std::getenv("BATTLE_SERVER_PORT")) {
        char* end = nullptr;
        value = std::strtol(env, &end, 10);
        if (*env == '\0' || *end != '\0') {
            std::cerr << "Invalid BATTLE_SERVER_PORT: " << env << '\n';
            return std::nullopt;
        }
        source = "BATTLE_SERVER_PORT";
    }

    if (argc > 1) {
        char* end = nullptr;
        value = std::strtol(argv[1], &end, 10);
        if (*argv[1] == '\0' || *end != '\0') {
            std::cerr << "Invalid port argument: " << argv[1] << '\n';
            return std::nullopt;
        }
        source = "argv[1]";
    }

    if (value < 1 || value > 65535) {
        std::cerr << "Port out of range (1-65535): " << value << '\n';
        return std::nullopt;
    }

    std::cout << "Port " << value << " (from " << source << ")" << '\n';
    return static_cast<unsigned short>(value);
}

static std::string env_or(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : std::string(fallback);
}

int main(int argc, char* argv[]) {
    std::cout << std::unitbuf;

    std::optional<unsigned short> port = resolve_port(argc, argv);
    if (!port) return 1;

    try {
        boost::asio::io_context io;
        std::string lobby_host = env_or("LOBBY_HOST", "lobby");
        std::string lobby_port = env_or("LOBBY_PORT", "7777");
        std::string secret = env_or("BATTLE_SECRET", "dev-secret");  
        std::cout << "Lobby at " << lobby_host << ":" << lobby_port << '\n';

        BattleServer server(io, *port, lobby_host, lobby_port, secret); 
        server.start();

        boost::asio::signal_set signals(io, SIGINT, SIGTERM);
        signals.async_wait([&server](const boost::system::error_code& ec, int sig) {
            if (ec) return;
            std::cout << "Signal " << sig << " received" << '\n';
            server.stop();
            });

        io.run();
        std::cout << "Stopped cleanly" << '\n';
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}