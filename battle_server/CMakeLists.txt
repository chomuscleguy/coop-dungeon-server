cmake_minimum_required(VERSION 3.20)
project(battle_server CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

# 로비 서버와 달리 JSON을 안 쓴다. 30Hz로 보내는 패킷에
# {"type":"..."} 를 실을 수는 없다. 바이너리로 직접 찍는다.
find_package(boost_asio CONFIG QUIET)
if(boost_asio_FOUND)
    set(BOOST_ASIO_TARGET Boost::asio)
else()
    find_package(Boost REQUIRED)
    if(TARGET Boost::headers)
        set(BOOST_ASIO_TARGET Boost::headers)
    elseif(TARGET Boost::boost)
        set(BOOST_ASIO_TARGET Boost::boost)
    endif()
endif()

add_executable(battle_server
    src/main.cpp
    src/BattleServer.cpp
    src/Protocol.cpp 
    src/World.cpp
)

target_include_directories(battle_server PRIVATE src)

target_link_libraries(battle_server PRIVATE
    ${BOOST_ASIO_TARGET}
    Threads::Threads
)

if(MSVC)
    target_compile_definitions(battle_server PRIVATE _WIN32_WINNT=0x0A00)
    target_compile_options(battle_server PRIVATE "/utf-8")
endif()

