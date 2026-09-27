cmake_minimum_required(VERSION 3.20)
project(coop_dungeon_server CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

# nlohmann-json: vcpkg도 apt(nlohmann-json3-dev)도 CONFIG 파일을 제공한다.
find_package(nlohmann_json CONFIG REQUIRED)

# Boost.Asio는 헤더 전용이다. vcpkg는 boost_asio 타깃을 주고,
# apt(libboost-dev)는 그런 타깃이 없어서 헤더 경로만 받아 쓴다.
find_package(boost_asio CONFIG QUIET)
if(boost_asio_FOUND)
    set(BOOST_ASIO_TARGET Boost::asio)
else()
    find_package(Boost REQUIRED)
    if(TARGET Boost::headers)
        set(BOOST_ASIO_TARGET Boost::headers)
    elseif(TARGET Boost::boost)
        set(BOOST_ASIO_TARGET Boost::boost)
    else()
        # 아주 오래된 FindBoost: 타깃 없이 경로만 준다
        set(BOOST_ASIO_TARGET "")
        include_directories(${Boost_INCLUDE_DIRS})
    endif()
endif()

add_executable(game_server src/main.cpp)

target_link_libraries(game_server PRIVATE
    ${BOOST_ASIO_TARGET}
    Threads::Threads
    nlohmann_json::nlohmann_json
)

if(MSVC)
    target_compile_definitions(game_server PRIVATE _WIN32_WINNT=0x0A00)
    # 소스는 UTF-8인데 MSVC는 BOM이 없으면 시스템 코드페이지(한국어 환경이면 949)로 읽는다.
    # 한글 주석의 특정 바이트가 뒤따르는 문자를 삼켜서 코드가 사라지는 사고를 막기 위함.
    target_compile_options(game_server PRIVATE "/utf-8")
endif()