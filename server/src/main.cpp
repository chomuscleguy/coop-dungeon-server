#include "Server.h"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <boost/asio.hpp>

// 포트 우선순위: 명령행 인자 > 환경변수 > 기본값.
// systemd/docker는 환경변수가 편하고, 손으로 돌려볼 땐 인자가 편하다.
// 잘못된 값은 조용히 무시하지 않는다 — 요청한 포트와 다른 데서 듣는 게 더 나쁘다.
std::optional<unsigned short> resolve_port(int argc, char* argv[]) {
    long value = 7777;
    const char* source = "default";

    if (const char* env = std::getenv("GAME_SERVER_PORT")) {
        char* end = nullptr;
        value = std::strtol(env, &end, 10);
        if (*env == '\0' || *end != '\0') {
            std::cerr << "Invalid GAME_SERVER_PORT: " << env << '\n';
            return std::nullopt;
        }
        source = "GAME_SERVER_PORT";
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

// ============================================================
int main(int argc, char* argv[]) {
    std::cout << std::unitbuf;

    std::optional<unsigned short> port = resolve_port(argc, argv);
    if (!port) {
        return 1;
    }

    try {
        boost::asio::io_context io;
        Server server(io, *port); 
        server.start();

        // SIGTERM(docker stop, systemctl stop), SIGINT(Ctrl+C)를 받으면 정상 종료
        boost::asio::signal_set signals(io, SIGINT, SIGTERM);
        signals.async_wait([&server](const boost::system::error_code& ec, int sig) {
            if (ec) return;
            std::cout << "Signal " << sig << " received" << std::endl;
            server.stop();
            });

        io.run();
        std::cout << "Stopped cleanly" << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
