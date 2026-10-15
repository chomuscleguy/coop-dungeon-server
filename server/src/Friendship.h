#pragma once

#include <cstdint>

// ============================================================
// Friendship — 두 사람 사이의 관계 하나
// ============================================================
// 관계를 각자 목록으로 들고 있으면 같은 사실이 양쪽에 적힌다. 수락할 때 네 곳을
// 동시에 고쳐야 하고, 한 곳이라도 빠지면 "나는 친구인데 쟤는 아닌" 상태가 된다.
// 사실을 한 군데만 두면 어긋날 수가 없다.
struct Friendship {
    uint64_t a = 0;              // 항상 a < b 로 정규화해서 저장
    uint64_t b = 0;
    uint64_t requested_by = 0;   // 누가 신청했나 (수락 권한 판정에 쓴다)
    bool accepted = false;

    // 이 관계에 참여한 사람인가
    bool involves(uint64_t id) const { return a == id || b == id; }

    // 나 말고 상대방
    uint64_t other(uint64_t me) const { return (a == me) ? b : a; }
};
