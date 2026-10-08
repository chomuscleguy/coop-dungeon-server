#include "LobbyLink.h"

#include <iostream>

using boost::asio::ip::tcp;

LobbyLink::LobbyLink(boost::asio::io_context& io,
	std::string host, std::string port)
	: resolver_(io), socket_(io), retry_timer_(io),
	host_(std::move(host)), port_(std::move(port)) {
}

void LobbyLink::start() { do_connect(); }

void LobbyLink::stop() {
	stopping_ = true;
	boost::system::error_code ec;
	retry_timer_.cancel();
	socket_.close(ec);
}

void LobbyLink::drop(const char* why) {
	if (!connected_ && !socket_.is_open()) return;
	std::cout << "[lobby] disconnected: " << why << '\n';
	connected_ = false;
	writing_ = false;
	send_queue_.clear();
	boost::system::error_code ec;
	socket_.close(ec);
	schedule_retry();
}

void LobbyLink::schedule_retry() {
	if (stopping_) return;
	retry_timer_.expires_after(std::chrono::seconds(3));
	retry_timer_.async_wait([this](boost::system::error_code ec) {
		if (!ec) do_connect();
		});
}

void LobbyLink::do_connect() {
	if (stopping_) return;

	resolver_.async_resolve(host_, port_,
		[this](boost::system::error_code ec, tcp::resolver::results_type eps) {
			if (ec) { schedule_retry(); return; }

			socket_ = tcp::socket(resolver_.get_executor());
			boost::asio::async_connect(socket_, eps,
				[this](boost::system::error_code ec2, const tcp::endpoint&) {
					if (ec2) { schedule_retry(); return; }

					connected_ = true;
					std::cout << "[lobby] connected to "
						<< host_ << ":" << port_ << '\n';
					if (on_connected) on_connected();
					do_read_header();
				});
		});
}

void LobbyLink::send(const std::string& json) {
	if (!connected_) {
		// 끊겨 있으면 버린다. 쌓아두면 재연결 순간에 몰려 나가고,
		// 그중 대부분은 이미 낡은 소식이다.
		std::cout << "[lobby] dropped (not connected): " << json << '\n';
		return;
	}

	std::uint32_t len = static_cast<std::uint32_t>(json.size());
	std::vector<std::uint8_t> frame(4 + json.size());
	frame[0] = static_cast<std::uint8_t>((len >> 24) & 0xFF);
	frame[1] = static_cast<std::uint8_t>((len >> 16) & 0xFF);
	frame[2] = static_cast<std::uint8_t>((len >> 8) & 0xFF);
	frame[3] = static_cast<std::uint8_t>(len & 0xFF);
	for (std::size_t i = 0; i < json.size(); ++i) {
		frame[4 + i] = static_cast<std::uint8_t>(json[i]);
	}

	send_queue_.push_back(std::move(frame));
	if (!writing_) do_write();
}

void LobbyLink::do_write() {
	if (send_queue_.empty()) { writing_ = false; return; }
	writing_ = true;

	boost::asio::async_write(socket_,
		boost::asio::buffer(send_queue_.front()),
		[this](boost::system::error_code ec, std::size_t) {
			if (ec) { drop("write failed"); return; }
			send_queue_.pop_front();
			do_write();
		});
}

void LobbyLink::do_read_header() {
	boost::asio::async_read(socket_, boost::asio::buffer(head_),
		[this](boost::system::error_code ec, std::size_t) {
			if (ec) { drop("read header failed"); return; }

			std::uint32_t len =
				(static_cast<std::uint32_t>(head_[0]) << 24) |
				(static_cast<std::uint32_t>(head_[1]) << 16) |
				(static_cast<std::uint32_t>(head_[2]) << 8) |
				static_cast<std::uint32_t>(head_[3]);

			// 로비가 보낼 리 없는 크기면 프레임이 어긋난 것이다.
			// 그대로 읽으면 메모리를 통째로 먹는다. (Step 4 와 같은 방어)
			if (len == 0 || len > 64 * 1024) { drop("bad frame length"); return; }

			do_read_body(len);
		});
}

void LobbyLink::do_read_body(std::uint32_t len) {
	body_.assign(len, 0);
	boost::asio::async_read(socket_, boost::asio::buffer(body_),
		[this](boost::system::error_code ec, std::size_t) {
			if (ec) { drop("read body failed"); return; }

			if (on_message) on_message(std::string(body_.begin(), body_.end()));
			do_read_header();
		});
}