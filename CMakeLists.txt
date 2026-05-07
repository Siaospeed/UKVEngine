cmake_minimum_required(VERSION 3.25)
project(UKVEngine)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -mtune=native")

include_directories(include)

set(SHARED_SRC
        src/cache/lru_cache.cc
        src/cache/sharded_lru_cache.cc
        src/protocol/resp_parser.cc
)

add_executable(ukvd
        src/server/ukvd_main.cc
        src/server/ukv_server.cc
        ${SHARED_SRC})


add_executable(ukv
        src/client/ukv_main.cc
        src/client/ukv_client.cc
        ${SHARED_SRC}
)
