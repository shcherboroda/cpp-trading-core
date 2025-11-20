#include "trading/order_book.hpp"
#include "trading/types.hpp"
#include "utils/benchmark.hpp"

#include <random>
#include <vector>
#include <iostream>

using namespace trading;

struct AddParams {
    Side     side;
    Price    price;
    Quantity qty;
};

int main(int argc, char** argv) {
    using bench::run_benchmark_with_percentiles_batched;
    using bench::run_multi_benchmark;
    using bench::print_multi;

    // ---- Параметры бенча: можно задать из командной строки ----
    std::size_t iterations = (argc > 1) ? std::stoull(argv[1]) : 200'000; // N операций
    std::size_t runs       = (argc > 2) ? std::stoull(argv[2]) : 5;       // число прогонов
    std::size_t batch_size = (argc > 3) ? std::stoull(argv[3]) : 128;     // размер батча

    if (iterations == 0) {
        std::cerr << "iterations must be > 0\n";
        return 1;
    }
    if (runs == 0) {
        std::cerr << "runs must be > 0\n";
        return 1;
    }

    std::size_t warmup = iterations / 10; // можно подвинуть по вкусу

    std::cout << "Config:\n"
              << "  iterations = " << iterations << "\n"
              << "  runs       = " << runs << "\n"
              << "  batch_size = " << batch_size << "\n"
              << "  warmup     = " << warmup << "\n\n";

    std::mt19937_64 rng(42);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<std::int64_t> price_dist(95, 105);
    std::uniform_int_distribution<std::int64_t> qty_dist(1, 10);

    // ---------- Подготовка данных ----------

    // Для ADD и MKT будем использовать заранее сгенерированные параметры,
    // чтобы в измерение не попадала генерация случайных чисел.

    std::vector<AddParams> add_params;
    add_params.reserve(iterations);
    for (std::size_t i = 0; i < iterations; ++i) {
        Side side   = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
        Price price = price_dist(rng);
        Quantity qty = qty_dist(rng);
        add_params.push_back(AddParams{side, price, qty});
    }

    struct MktParams {
        Side     side;
        Quantity qty;
    };

    std::vector<MktParams> mkt_params;
    mkt_params.reserve(iterations);
    for (std::size_t i = 0; i < iterations; ++i) {
        Side side    = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
        Quantity qty = qty_dist(rng);
        mkt_params.push_back(MktParams{side, qty});
    }

    // Отдельные ордера для начальной ликвидности книги под market-бенч
    constexpr std::size_t INIT_ORDERS = 50'000;
    std::vector<AddParams> init_orders;
    init_orders.reserve(INIT_ORDERS);
    for (std::size_t i = 0; i < INIT_ORDERS; ++i) {
        Side side   = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
        Price price = price_dist(rng);
        Quantity qty = qty_dist(rng);
        init_orders.push_back(AddParams{side, price, qty});
    }

    // ---------- empty_loop (оверход бенч-харнесса) ----------

    auto empty_summary = run_multi_benchmark(
        "empty_loop",
        runs,
        [&](std::size_t /*run_idx*/) {
            return run_benchmark_with_percentiles_batched(
                "empty_loop_single",
                iterations,
                batch_size,
                [](std::size_t) {
                    // пустой бенч — измеряем только цикл и clock::now
                },
                warmup
            );
        }
    );

    print_multi(empty_summary);
    std::cout << "\n";

    // ---------- OrderBook::add_limit_order ----------

    auto add_summary = run_multi_benchmark(
        "OrderBook::add_limit_order",
        runs,
        [&](std::size_t /*run_idx*/) {
            OrderBook book;

            return run_benchmark_with_percentiles_batched(
                "OrderBook::add_limit_order_single",
                iterations,
                batch_size,
                [&](std::size_t i) {
                    const auto& p = add_params[i];
                    book.add_limit_order(p.side, p.price, p.qty);
                },
                warmup
            );
        }
    );

    print_multi(add_summary);
    std::cout << "\n";

    // ---------- OrderBook::execute_market_order ----------

    auto mkt_summary = run_multi_benchmark(
        "OrderBook::execute_market_order",
        runs,
        [&](std::size_t /*run_idx*/) {
            OrderBook book;

            // заливаем ликвидность
            for (const auto& p : init_orders) {
                book.add_limit_order(p.side, p.price, p.qty);
            }

            return run_benchmark_with_percentiles_batched(
                "OrderBook::execute_market_order_single",
                iterations,
                batch_size,
                [&](std::size_t i) {
                    const auto& p = mkt_params[i];
                    book.execute_market_order(p.side, p.qty);
                },
                warmup
            );
        }
    );

    print_multi(mkt_summary);
    std::cout << "\n";

    return 0;
}
