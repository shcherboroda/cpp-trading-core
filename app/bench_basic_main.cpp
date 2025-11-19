#include "trading/order_book.hpp"
#include <iostream>
#include <chrono>

int main() {
    using namespace trading;

    std::cout << "trading_bench_basic: TODO - implement basic micro-benchmark\n";

    OrderBook book;

    // TODO: сгенерировать N ордеров, добавить в книгу, замерить время.
    // Например:
    // auto start = std::chrono::high_resolution_clock::now();
    // ... add_limit_order(...) в цикле ...
    // auto end = std::chrono::high_resolution_clock::now();
    // std::cout << "Elapsed ns per order: " << ... << "\n";

    return 0;
}
