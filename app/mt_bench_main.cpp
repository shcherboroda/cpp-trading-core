#include "trading/event.hpp"
#include "trading/order_book.hpp"
#include "trading/types.hpp"
#include "utils/spsc_queue.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

using namespace trading;

using Clock      = std::chrono::steady_clock;
using TimePoint  = Clock::time_point;
using Nanoseconds = std::chrono::nanoseconds;

struct TimedEvent {
    trading::Event ev;
    std::uint64_t  id;          // порядковый номер (0..num_events-1)
    TimePoint      enqueue_ts;  // когда продюсер положил event в очередь
};


// Генератор событий, похожий на trading_generate, но в память
class EventGenerator {
public:
    EventGenerator(std::size_t num_events, std::uint32_t seed)
        : num_events_(num_events),
          rng_(seed),
          event_type_dist_(0, 99),
          side_dist_(0, 1),
          price_dist_(95, 105),
          qty_dist_(1, 10),
          next_id_(1)
    {
        active_ids_.reserve(num_events_);
    }

    std::size_t num_events() const { return num_events_; }

    // генерит следующее событие
    Event next() {
        if (generated_ >= num_events_) {
            Event ev;
            ev.type = EventType::End;
            return ev;
        }

        int r = event_type_dist_(rng_);

        bool force_add = active_ids_.empty();
        Event ev;

        if (force_add || r < 60) {
            // ADD
            int side_val = side_dist_(rng_);
            ev.type = EventType::Add;
            ev.side = (side_val == 0) ? Side::Buy : Side::Sell;
            ev.price = price_dist_(rng_);
            ev.qty   = qty_dist_(rng_);

            ev.id = next_id_++;
            active_ids_.push_back(ev.id);
        } else if (r < 90) {
            // MKT
            int side_val = side_dist_(rng_);
            ev.type = EventType::Market;
            ev.side = (side_val == 0) ? Side::Buy : Side::Sell;
            ev.qty  = qty_dist_(rng_);
            // id не нужен
        } else {
            // CANCEL
            ev.type = EventType::Cancel;
            if (!active_ids_.empty()) {
                std::uniform_int_distribution<std::size_t> idx_dist(0, active_ids_.size() - 1);
                std::size_t idx = idx_dist(rng_);
                ev.id = active_ids_[idx];

                // убираем id, чтобы не отменять неск раз
                active_ids_[idx] = active_ids_.back();
                active_ids_.pop_back();
            } else {
                // fallback: ADD
                int side_val = side_dist_(rng_);
                ev.type = EventType::Add;
                ev.side = (side_val == 0) ? Side::Buy : Side::Sell;
                ev.price = price_dist_(rng_);
                ev.qty   = qty_dist_(rng_);
                ev.id    = next_id_++;
                active_ids_.push_back(ev.id);
            }
        }

        ++generated_;
        return ev;
    }

private:
    std::size_t num_events_;
    std::size_t generated_{0};

    std::mt19937_64 rng_;
    std::uniform_int_distribution<int> event_type_dist_;
    std::uniform_int_distribution<int> side_dist_;
    std::uniform_int_distribution<Price> price_dist_;
    std::uniform_int_distribution<Quantity> qty_dist_;

    OrderId next_id_;
    std::vector<OrderId> active_ids_;
};

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: trading_mt_bench <num_events> <seed>\n";
        return 1;
    }

    const std::size_t num_events = static_cast<std::size_t>(std::stoull(argv[1]));
    const std::uint32_t seed     = static_cast<std::uint32_t>(std::stoul(argv[2]));

    EventGenerator generator(num_events, seed);

    // capacity > num_events, чтобы не упираться в full (или можно чуть меньше и допускать спины)
    const std::size_t queue_capacity = num_events + 1;
    utils::SpscQueue<TimedEvent> queue(queue_capacity);

    OrderBook book;

    std::atomic<bool> producer_done{false};
    std::atomic<std::size_t> consumed_count{0};

    std::vector<long long> latencies_ns(num_events, 0);

    auto start_time = Clock::now();

    // consumer / matching thread
    std::thread consumer_thread([&]() {
        TimedEvent tev;
        for (;;) {
            // пробуем читать из очереди
            if (!queue.pop(tev)) {
                // пусто — можно чуть-чуть подождать, чтобы не жечь CPU
                if (producer_done.load(std::memory_order_acquire)) {
                    // если продюсер уже закончил и очередь пуста — выходим
                    if (queue.empty()) {
                        break;
                    }
                }
                std::this_thread::yield();
                continue;
            }

            const auto& ev = tev.ev;

            if (ev.type == EventType::End) {
                // конец потока событий
                break;
            }

            // фиксируем время обработки
            auto t1 = Clock::now();
            auto id = tev.id;
            // id у нас 0..num_events-1 для всех реальных событий
            if (id < latencies_ns.size()) {
                auto dt = std::chrono::duration_cast<Nanoseconds>(t1 - tev.enqueue_ts).count();
                latencies_ns[static_cast<std::size_t>(id)] = dt;
            }

            switch (ev.type) {
            case EventType::Add:
                book.add_limit_order_with_id(ev.id, ev.side, ev.price, ev.qty);
                break;
            case EventType::Market:
                (void)book.execute_market_order(ev.side, ev.qty);
                break;
            case EventType::Cancel:
                (void)book.cancel(ev.id);
                break;
            case EventType::End:
                // уже обрабатывается выше
                break;
            }

            consumed_count.fetch_add(1, std::memory_order_relaxed);
        }
    });

    // producer / feed thread
    std::thread producer_thread([&]() {
        std::uint64_t next_id = 0;

        for (;;) {
            trading::Event base_ev = generator.next();
            TimedEvent tev;

            tev.ev = base_ev;

            if (base_ev.type == EventType::End) {
                // "маркер конца": id можно не использовать
                tev.id         = static_cast<std::uint64_t>(-1);
                tev.enqueue_ts = Clock::now(); // не так важно, но можно и не ставить
            } else {
                tev.id         = next_id;
                tev.enqueue_ts = Clock::now();
                ++next_id;
            }

            // кладём в очередь (как раньше)
            for (;;) {
                if (queue.push(tev)) break;
                std::this_thread::yield();
            }

            if (base_ev.type == EventType::End) {
                break;
            }
        }

        producer_done.store(true, std::memory_order_release);
    });

    producer_thread.join();
    consumer_thread.join();

    auto end_time = Clock::now();
    auto ns = std::chrono::duration_cast<Nanoseconds>(end_time - start_time).count();

    double seconds = static_cast<double>(ns) / 1e9;
    std::size_t processed = consumed_count.load(std::memory_order_relaxed);

    std::cout << "mt_bench: processed " << processed << " events in "
            << seconds << " s\n";
    if (seconds > 0.0) {
        double eps = static_cast<double>(processed) / seconds;
        double ns_per_event = static_cast<double>(ns) / static_cast<double>(processed);
        std::cout << "  throughput: " << eps << " events/s\n";
        std::cout << "  mean:       " << ns_per_event << " ns/event\n";
    }

    // === Новое: latency distribution ===
    std::vector<long long> samples;
    samples.reserve(processed);

    // latencies_ns может быть чуть длиннее (по num_events),
    // а processed — это реальное кол-во обработанных событий.
    // На всякий случай берём min.
    for (std::size_t i = 0; i < processed && i < latencies_ns.size(); ++i) {
        samples.push_back(latencies_ns[i]);
    }

    if (!samples.empty()) {
        std::sort(samples.begin(), samples.end());
        auto N = samples.size();

        auto p50 = samples[static_cast<std::size_t>(0.50 * (N - 1))];
        auto p95 = samples[static_cast<std::size_t>(0.95 * (N - 1))];
        auto p99 = samples[static_cast<std::size_t>(0.99 * (N - 1))];

        std::cout << "Latency (enqueue -> processed):\n";
        std::cout << "  p50: " << p50 << " ns\n";
        std::cout << "  p95: " << p95 << " ns\n";
        std::cout << "  p99: " << p99 << " ns\n";
    }

    auto bb = book.best_bid();
    auto ba = book.best_ask();
    std::cout << "Final best bid valid=" << bb.valid
              << ", price=" << bb.price << ", qty=" << bb.qty << "\n";
    std::cout << "Final best ask valid=" << ba.valid
              << ", price=" << ba.price << ", qty=" << ba.qty << "\n";

    return 0;
}
