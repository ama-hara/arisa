cmake_minimum_required(VERSION 3.25)
project(ArisaEngine VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ARISA_SOURCES
    src/main.cpp
    src/core/engine.cpp
    src/core/scheduler.cpp
    src/io/iocp_engine.cpp
    src/net/http_client.cpp
    src/rpc/rpc_server.cpp
)

add_executable(ars ${ARISA_SOURCES})

target_include_directories(ars PRIVATE src/include)

if(WIN32)
    target_compile_definitions(ars PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
    )
    target_link_libraries(ars PRIVATE ws2_32 mswsock winhttp)
endif()

if(MSVC)
    target_compile_options(ars PRIVATE /W4 /Zc:__cplusplus)
else()
    target_compile_options(ars PRIVATE -Wall -Wextra -Wpedantic)
endif()