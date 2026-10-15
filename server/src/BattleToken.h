#pragma once

// 배틀 서버와 주고받는 값들. 헤더에 두므로 static 이 아니라 inline 이다 —
// static 이면 번역 단위마다 복사본이 생기고, make_battle_token 의 난수기도
// 파일마다 따로 생긴다.

#include <cstdint>
#include <cstdlib>
#include <random>
#include <string>

static std::string battle_secret() {
    const char* v = std::getenv("BATTLE_SECRET");
    return (v && *v) ? std::string(v) : std::string("dev-secret");
}

// 클라이언트가 접속할 배틀 서버 주소. 배틀 서버는 자기 공인 주소를
// 모르므로(컨테이너 안에서는 172.17.x.x) 로비가 환경변수로 알고 있는다.
static std::string battle_public_host() {
    const char* v = std::getenv("BATTLE_PUBLIC_HOST");
    return (v && *v) ? std::string(v) : std::string("127.0.0.1");
}

// 입장권. 추측할 수 없어야 하므로 난수를 쓴다.
// 16자 16진수 = 64비트. 무작위로 맞힐 확률은 사실상 0이다.
static std::string make_battle_token() {
    static std::mt19937_64 rng{ std::random_device{}() };
    static const char* hex = "0123456789abcdef";

    std::uint64_t v = rng();
    std::string s(16, '0');
    for (int i = 0; i < 16; ++i) {
        s[15 - i] = hex[(v >> (i * 4)) & 0xF];
    }
    return s;
}
