cmake_minimum_required(VERSION 3.16)
project(cpp_trading_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

# ===== apps (пока заглушки) =====
add_executable(trading_replay app/replay_main.cpp)
target_link_libraries(trading_replay PRIVATE trading_core)

add_executable(trading_bench_basic app/bench_basic_main.cpp)
target_link_libraries(trading_bench_basic PRIVATE trading_core)

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
