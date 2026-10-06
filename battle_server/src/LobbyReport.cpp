#include "LobbyReport.h"

#include <iostream>

using boost::asio::ip::tcp;

LobbyReport::LobbyReport(boost::asio::io_context& io, std::string json)
    : resolver_(io), socket_(io) {
    // 로비의 프레이밍: [4바이트 빅엔디안 길이][UTF-8 JSON]
    std::uint32_t len = static_cast<std::uint32_t>(json.size());
    frame_.resize(4 + json.size());
    frame_[0] = static_cast<std::uint8_t>((len >> 24) & 0xFF);
    frame_[1] = static_cast<std::uint8_t>((len >> 16) & 0xFF);
    frame_[2] = static_cast<std::uint8_t>((len >> 8) & 0xFF);
    frame_[3] = static_cast<std::uint8_t>(len & 0xFF);
    for (std::size_t i = 0; i < json.size(); ++i) {
        frame_[4 + i] = static_cast<std::uint8_t>(json[i]);
    }
}

void LobbyReport::send(boost::asio::io_context& io,
    const std::string& host, const std::string& port,
    const std::string& json) {
    auto self = std::make_shared<LobbyReport>(io, json);
    self->start(host, port);
}

void LobbyReport::start(const std::string& host, const std::string& port) {
    auto self = shared_from_this();   // 콜백이 끝날 때까지 살아 있게

    resolver_.async_resolve(host, port,
        [this, self](boost::system::error_code ec, tcp::resolver::results_type eps) {
            if (ec) {
                std::cout << "[lobby] resolve failed: " << ec.message() << '\n';
                return;
            }

            boost::asio::async_connect(socket_, eps,
                [this, self](boost::system::error_code ec2, const tcp::endpoint&) {
                    if (ec2) {
                        std::cout << "[lobby] connect failed: " << ec2.message() << '\n';
                        return;
                    }

                    boost::asio::async_write(socket_, boost::asio::buffer(frame_),
                        [this, self](boost::system::error_code ec3, std::size_t n) {
                            if (ec3) {
                                std::cout << "[lobby] write failed: " << ec3.message() << '\n';
                                return;
                            }
                            std::cout << "[lobby] reported " << n << " bytes\n";

                            boost::system::error_code ignored;
                            socket_.shutdown(tcp::socket::shutdown_both, ignored);
                            socket_.close(ignored);
                        });
                });
        });
}