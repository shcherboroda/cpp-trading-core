cmake_minimum_required(VERSION 3.16)

project(cpp_trading_core
    VERSION 0.1.0
    LANGUAGES CXX
)

# ========= базовые настройки =========
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# Чтобы по умолчанию был Release, если не задано явно
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ========= опции сборки (как у библиотеки) =========
option(BUILD_EXAMPLES     "Build small example/demo apps"          ON)
option(BUILD_BENCHMARKS   "Build benchmark binaries"               ON)
option(BUILD_TOOLS        "Build helper tools (replay/generator)"  ON)
option(BUILD_BYBIT_DEMOS  "Build Bybit REST/WS demo apps"          ON)

# ========= зависимости (для библиотек) =========
find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)

# ========= core library =========
# Ядро: ордербук и всё, что не зависит от конкретной биржи
add_library(trading_core STATIC
    src/order_book.cpp
)

target_include_directories(trading_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_features(trading_core PUBLIC cxx_std_20)

# Удобные алиасы в "библиотечном" стиле
add_library(trading::core ALIAS trading_core)

# ========= exchange library =========
# Обмены / инфраструктура: HTTP-клиент, Bybit REST + WS
add_library(trading_exchanges STATIC
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

add_library(trading::exchanges ALIAS trading_exchanges)

# ========= tools / утилиты =========
if (BUILD_TOOLS)
    # Генерация synthetic events
    add_executable(trading_generate
        app/generate_events_main.cpp
    )

    # Реплей по CSV/events -> OrderBook + метрики
    add_executable(trading_replay
        app/replay_main.cpp
    )
    target_link_libraries(trading_replay
        PRIVATE
            trading_core
    )

    # Live feed через stdin (используется с Python-скриптом)
    add_executable(trading_live_feed
        app/live_feed_main.cpp
    )
    target_link_libraries(trading_live_feed
        PRIVATE
            trading_core
    )
endif()

# ========= benchmarks =========
if (BUILD_BENCHMARKS)
    # ns/op бенч ордербука через std::chrono
    add_executable(trading_bench_order_book
        app/bench_order_book_main.cpp
    )
    target_link_libraries(trading_bench_order_book
        PRIVATE
            trading_core
    )

    # ns/op бенч ордербука через TSC
    add_executable(trading_bench_order_book_tsc
        app/bench_order_book_tsc_main.cpp
    )
    target_link_libraries(trading_bench_order_book_tsc
        PRIVATE
            trading_core
    )

    # Многопоточный бенч с очередью событий
    add_executable(trading_mt_bench
        app/mt_bench_main.cpp
    )
    target_link_libraries(trading_mt_bench
        PRIVATE
            trading_core
    )
    target_include_directories(trading_mt_bench
        PRIVATE
            ${PROJECT_SOURCE_DIR}/include
    )
endif()

# ========= Bybit demos (REST + WS) =========
if (BUILD_BYBIT_DEMOS)
    # Простой REST-демо (тикер/пинг и т.п.)
    add_executable(trading_bybit_rest_demo
        app/bybit_rest_demo_main.cpp
    )
    target_link_libraries(trading_bybit_rest_demo
        PRIVATE
            trading_exchanges
    )

    # REST-снапшот ордербука + бенч билдов OrderBook
    add_executable(trading_bybit_orderbook_snapshot
        app/bybit_orderbook_snapshot_main.cpp
    )
    target_link_libraries(trading_bybit_orderbook_snapshot
        PRIVATE
            trading_exchanges
            trading_core
    )

    # WS-снапшот (Python -> stdin) + бенч билдов OrderBook
    add_executable(trading_ws_orderbook_snapshot
        app/ws_orderbook_snapshot_main.cpp
    )
    target_link_libraries(trading_ws_orderbook_snapshot
        PRIVATE
            trading_exchanges
            trading_core
            nlohmann_json::nlohmann_json
    )

    # Чтение публичных трейдов с Bybit WS в C++
    add_executable(trading_bybit_ws_trades
        app/bybit_ws_trades_main.cpp
    )
    target_include_directories(trading_bybit_ws_trades
        PRIVATE
            ${PROJECT_SOURCE_DIR}/include
            ${Boost_INCLUDE_DIRS}
    )
    target_link_libraries(trading_bybit_ws_trades
        PRIVATE
            trading_exchanges
    )

    # Живой WS ордербук с обновлением OrderBook и метриками latency
    add_executable(trading_bybit_ws_orderbook_live
        app/bybit_ws_orderbook_live_main.cpp
    )
    target_include_directories(trading_bybit_ws_orderbook_live
        PRIVATE
            ${PROJECT_SOURCE_DIR}/include
            ${Boost_INCLUDE_DIRS}
    )
    target_link_libraries(trading_bybit_ws_orderbook_live
        PRIVATE
            trading_exchanges
            trading_core
    )
endif()

# ========= tests (GoogleTest) =========
include(CTest)
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
