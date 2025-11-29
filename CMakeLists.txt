cmake_minimum_required(VERSION 3.16)
project(cpp_trading_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)

if (MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# ===== core library =====
add_library(trading_core
    src/order_book.cpp
)

target_include_directories(trading_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ===== exchange library =====
add_library(trading_exchanges
    src/utils/http_client.cpp
    src/exchange/bybit_public_rest.cpp
    src/exchange/bybit_public_ws.cpp
)

target_include_directories(trading_exchanges
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(trading_exchanges
    PUBLIC
        CURL::libcurl
        nlohmann_json::nlohmann_json
        OpenSSL::SSL
        OpenSSL::Crypto
        Boost::system
)

# ===== apps =====
add_executable(trading_replay app/replay_main.cpp)
target_link_libraries(trading_replay PRIVATE trading_core)

add_executable(trading_generate app/generate_events_main.cpp)

add_executable(trading_bench_order_book
    app/bench_order_book_main.cpp
)
target_link_libraries(trading_bench_order_book
    PRIVATE
        trading_core
)

add_executable(trading_mt_bench app/mt_bench_main.cpp)
target_link_libraries(trading_mt_bench PRIVATE trading_core)
target_include_directories(trading_mt_bench PRIVATE include)

add_executable(trading_bench_order_book_tsc
    app/bench_order_book_tsc_main.cpp
)
target_link_libraries(trading_bench_order_book_tsc
    PRIVATE trading_core
)

add_executable(trading_live_feed
    app/live_feed_main.cpp
)
target_link_libraries(trading_live_feed
    PRIVATE trading_core
)

add_executable(trading_bybit_rest_demo
    app/bybit_rest_demo_main.cpp
)

target_link_libraries(trading_bybit_rest_demo
    PRIVATE
        trading_exchanges
)

add_executable(trading_bybit_orderbook_snapshot
    app/bybit_orderbook_snapshot_main.cpp
)

target_link_libraries(trading_bybit_orderbook_snapshot
    PRIVATE
        trading_exchanges   # твоя либка с BybitPublicRest и HttpClient
        trading_core        # где лежит OrderBook
)

add_executable(trading_ws_orderbook_snapshot
    app/ws_orderbook_snapshot_main.cpp
)

target_link_libraries(trading_ws_orderbook_snapshot
    PRIVATE
        trading_core        # твоя библиотека с OrderBook
        trading_exchanges   # где лежит OrderBookSnapshot / HttpClient / etc.
        nlohmann_json::nlohmann_json
)

add_executable(trading_bybit_ws_trades
    app/bybit_ws_trades_main.cpp
)

target_include_directories(trading_bybit_ws_trades
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${Boost_INCLUDE_DIRS}
)

target_link_libraries(trading_bybit_ws_trades
    PRIVATE
        trading_exchanges
)

add_executable(trading_bybit_ws_orderbook_live
    app/bybit_ws_orderbook_live_main.cpp
)

target_include_directories(trading_bybit_ws_orderbook_live
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${Boost_INCLUDE_DIRS}
)

target_link_libraries(trading_bybit_ws_orderbook_live
    PRIVATE
        trading_exchanges
        trading_core
)

# ===== tests (GoogleTest) =====
include(CTest)          # включает BUILD_TESTING и ctest-интеграцию
enable_testing()

if (BUILD_TESTING)
    include(FetchContent)

    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
    )
    # Для MSVC, чтобы не ругался на CRT
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(googletest)

    add_executable(order_book_basic_tests
        tests/order_book_basic_tests.cpp
    )

    target_link_libraries(order_book_basic_tests
        PRIVATE
            trading_core
            GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(order_book_basic_tests)
endif()
