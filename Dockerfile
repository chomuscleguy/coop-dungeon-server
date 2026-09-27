# ---------- 빌드 단계 ----------
FROM debian:bookworm-slim AS build

# vcpkg 대신 apt를 쓴다. ARM에서 boost를 소스 빌드하면 몇 시간이 걸리고
# 메모리가 모자라 실패하기도 하는데, apt는 몇 분이면 끝난다.
# boost-asio는 헤더 전용이라 libboost-dev 하나로 충분하다.
RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ cmake ninja-build \
        libboost-dev nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY server/CMakeLists.txt ./CMakeLists.txt
COPY server/src ./src

RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build

# ---------- 실행 단계 ----------
FROM debian:bookworm-slim

# Boost.Asio도 nlohmann-json도 헤더 전용이라 런타임에 필요한 건 libstdc++ 뿐이다.
# 빌드 도구를 뺀 이미지는 1/10 이하로 작아진다.
RUN apt-get update && apt-get install -y --no-install-recommends \
        libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

# root로 돌릴 이유가 없다. 컨테이너가 뚫려도 피해를 줄인다.
RUN useradd --system --create-home --shell /usr/sbin/nologin gameserver
USER gameserver

WORKDIR /app
COPY --from=build /src/build/game_server ./game_server

ENV GAME_SERVER_PORT=7777
EXPOSE 7777

# exec 형식이어야 한다. 쉘 형식(CMD ./game_server)으로 쓰면 /bin/sh 가 PID 1이 되고,
# docker stop 의 SIGTERM 이 서버에게 전달되지 않아 10초 뒤 SIGKILL 로 죽는다.
CMD ["./game_server"]