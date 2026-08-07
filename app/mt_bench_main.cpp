#include "trading/event.hpp"
#include "trading/order_book.hpp"
#include "trading/types.hpp"
#include "utils/spsc_queue.hpp"

#include <atomic>
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <random>
#include <string_view>
#include <thread>
#include <vector>

#if defined(__i386__) || defined(__x86_64__)
#include <immintrin.h>
#endif

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

using namespace trading;

using Clock      = std::chrono::steady_clock;
using TimePoint  = Clock::time_point;
using Nanoseconds = std::chrono::nanoseconds;

struct TimedEvent {
    trading::Event ev;
    std::uint64_t  id;          // порядковый номер (0..num_events-1)
    TimePoint      enqueue_ts;  // timestamp semantics selected by --latency
};

// Event generator
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
            // id not required
        } else {
            // CANCEL
            ev.type = EventType::Cancel;
            if (!active_ids_.empty()) {
                std::uniform_int_distribution<std::size_t> idx_dist(0, active_ids_.size() - 1);
                std::size_t idx = idx_dist(rng_);
                ev.id = active_ids_[idx];

                // remove id to avoid multiple cancels
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

constexpr std::size_t QUEUE_CAPACITY = 4096;
constexpr int K_WARMUP_EVENTS = 20000;

enum class LatencyMode {
    Off,
    PrePush,
    Enqueued,
};

enum class OutputFormat {
    Text,
    Csv,
};

enum class BackoffMode {
    Yield,
    Pause,
};

const char* latency_mode_name(LatencyMode mode)
{
    switch (mode) {
    case LatencyMode::Off:      return "off";
    case LatencyMode::PrePush:  return "pre-push";
    case LatencyMode::Enqueued: return "enqueued";
    }
    return "unknown";
}

const char* backoff_mode_name(BackoffMode mode)
{
    switch (mode) {
    case BackoffMode::Yield: return "yield";
    case BackoffMode::Pause: return "pause";
    }
    return "unknown";
}

void apply_backoff(BackoffMode mode)
{
    if (mode == BackoffMode::Yield) {
        std::this_thread::yield();
        return;
    }

#if defined(__i386__) || defined(__x86_64__)
    _mm_pause();
#else
    std::this_thread::yield();
#endif
}

bool parse_cpu_option(std::string_view arg, std::string_view prefix, int& cpu)
{
    if (!arg.starts_with(prefix)) {
        return false;
    }

    const auto value = arg.substr(prefix.size());
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), cpu);
    return error == std::errc{} && end == value.data() + value.size() && cpu >= 0;
}

bool parse_size_option(std::string_view arg, std::string_view prefix, std::size_t& value)
{
    if (!arg.starts_with(prefix)) {
        return false;
    }

    const auto text = arg.substr(prefix.size());
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

int pin_current_thread_to_cpu(int cpu) noexcept
{
#if defined(__linux__)
    if (cpu < 0) {
        return 0;
    }
    if (cpu >= CPU_SETSIZE) {
        return EINVAL;
    }

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    return pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
#else
    return cpu < 0 ? 0 : ENOTSUP;
#endif
}

int current_cpu() noexcept
{
#if defined(__linux__)
    return sched_getcpu();
#else
    return -1;
#endif
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: trading_mt_bench <num_events> <seed> "
                     "[--latency=off|pre-push|enqueued] [--backoff=yield|pause] "
                     "[--producer-cpu=N --consumer-cpu=N] "
                     "[--book-reserve=N] "
                     "[--format=text|csv]\n";
        return 1;
    }

    const std::size_t num_events = static_cast<std::size_t>(std::stoull(argv[1]));
    const std::uint32_t seed     = static_cast<std::uint32_t>(std::stoul(argv[2]));
    LatencyMode latency_mode = LatencyMode::PrePush;
    BackoffMode backoff_mode = BackoffMode::Yield;
    OutputFormat output_format = OutputFormat::Text;
    int producer_cpu = -1;
    int consumer_cpu = -1;
    std::size_t book_reserve = 0;

    for (int i = 3; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--latency=off") {
            latency_mode = LatencyMode::Off;
        } else if (arg == "--latency=pre-push") {
            latency_mode = LatencyMode::PrePush;
        } else if (arg == "--latency=enqueued") {
            latency_mode = LatencyMode::Enqueued;
        } else if (arg == "--backoff=yield") {
            backoff_mode = BackoffMode::Yield;
        } else if (arg == "--backoff=pause") {
            backoff_mode = BackoffMode::Pause;
        } else if (arg.starts_with("--producer-cpu=")) {
            if (!parse_cpu_option(arg, "--producer-cpu=", producer_cpu)) {
                std::cerr << "Invalid producer CPU: " << arg << "\n";
                return 1;
            }
        } else if (arg.starts_with("--consumer-cpu=")) {
            if (!parse_cpu_option(arg, "--consumer-cpu=", consumer_cpu)) {
                std::cerr << "Invalid consumer CPU: " << arg << "\n";
                return 1;
            }
        } else if (arg.starts_with("--book-reserve=")) {
            if (!parse_size_option(arg, "--book-reserve=", book_reserve)) {
                std::cerr << "Invalid book reserve: " << arg << "\n";
                return 1;
            }
        } else if (arg == "--format=text") {
            output_format = OutputFormat::Text;
        } else if (arg == "--format=csv") {
            output_format = OutputFormat::Csv;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            return 1;
        }
    }

    if ((producer_cpu < 0) != (consumer_cpu < 0)) {
        std::cerr << "--producer-cpu and --consumer-cpu must be specified together\n";
        return 1;
    }

    EventGenerator generator(num_events, seed);

    const std::size_t queue_capacity = QUEUE_CAPACITY;
    utils::SpscQueue<TimedEvent> queue(queue_capacity);

    OrderBook book;
    if (book_reserve != 0) {
        book.reserve(book_reserve);
    }

    std::atomic<bool> producer_done{false};
    std::atomic<std::size_t> consumed_count{0};

    std::vector<long long> latencies_ns;
    if (latency_mode != LatencyMode::Off) {
        latencies_ns.resize(num_events, 0);
    }

    std::atomic<unsigned int> ready_threads{0};
    std::atomic<bool> start_measurement{false};
    std::atomic<bool> cancel_start{false};
    std::atomic<int> producer_pin_error{0};
    std::atomic<int> consumer_pin_error{0};
    std::atomic<int> producer_cpu_start{-1};
    std::atomic<int> producer_cpu_end{-1};
    std::atomic<int> consumer_cpu_start{-1};
    std::atomic<int> consumer_cpu_end{-1};

    const auto pin_and_wait_for_start = [&](int requested_cpu,
                                            std::atomic<int>& pin_error,
                                            std::atomic<int>& observed_cpu) {
        pin_error.store(pin_current_thread_to_cpu(requested_cpu), std::memory_order_release);
        observed_cpu.store(current_cpu(), std::memory_order_release);
        ready_threads.fetch_add(1, std::memory_order_release);
        while (!start_measurement.load(std::memory_order_acquire)) {
            if (cancel_start.load(std::memory_order_acquire)) {
                return false;
            }
            std::this_thread::yield();
        }
        return true;
    };

    // consumer / matching thread
    std::thread consumer_thread([&]() {
        if (!pin_and_wait_for_start(consumer_cpu, consumer_pin_error, consumer_cpu_start)) {
            return;
        }
        TimedEvent tev;
        //Backoff backoff;
        for (;;) {
            // try read frrom queue
            if (!queue.pop(tev)) {
                if (producer_done.load(std::memory_order_acquire)) {
                    if (queue.empty()) {
                        break;
                    }
                }
                apply_backoff(backoff_mode);
                continue;
            }

            const auto& ev = tev.ev;

            if (ev.type == EventType::End) {
                // end of event stream
                break;
            }

            // record processing time
            auto id = tev.id;
            if (latency_mode != LatencyMode::Off && id < latencies_ns.size()) {
                const auto t1 = Clock::now();
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
                // already handled above
                break;
            }

            consumed_count.fetch_add(1, std::memory_order_relaxed);
        }
        consumer_cpu_end.store(current_cpu(), std::memory_order_release);
    });

    // producer / feed thread
    std::thread producer_thread([&]() {
        if (!pin_and_wait_for_start(producer_cpu, producer_pin_error, producer_cpu_start)) {
            return;
        }
        std::uint64_t next_id = 0;

        //Backoff backoff;

        for (;;) {
            trading::Event base_ev = generator.next();
            TimedEvent tev;

            tev.ev = base_ev;

            if (base_ev.type == EventType::End) {
                // "end marker": id not required
                tev.id         = static_cast<std::uint64_t>(-1);
            } else {
                tev.id         = next_id;
                ++next_id;
            }

            if (latency_mode == LatencyMode::PrePush) {
                tev.enqueue_ts = Clock::now();
            }

            for (;;) {
                if (latency_mode == LatencyMode::Enqueued) {
                    // The timestamp copied into the queue is from the successful push attempt.
                    tev.enqueue_ts = Clock::now();
                }
#if UTILS_SPSC_QUEUE_MOVE_TRANSFER
                if (queue.push(std::move(tev))) break;
#else
                if (queue.push(tev)) break;
#endif
                apply_backoff(backoff_mode);
            }

            if (base_ev.type == EventType::End) {
                break;
            }
        }

        producer_done.store(true, std::memory_order_release);
        producer_cpu_end.store(current_cpu(), std::memory_order_release);
    });

    while (ready_threads.load(std::memory_order_acquire) != 2) {
        std::this_thread::yield();
    }
    if (producer_pin_error.load(std::memory_order_acquire) != 0 ||
        consumer_pin_error.load(std::memory_order_acquire) != 0) {
        cancel_start.store(true, std::memory_order_release);
        start_measurement.store(true, std::memory_order_release);
        producer_thread.join();
        consumer_thread.join();
        std::cerr << "Thread affinity setup failed: producer="
                  << producer_pin_error.load(std::memory_order_relaxed)
                  << ", consumer=" << consumer_pin_error.load(std::memory_order_relaxed) << "\n";
        return 1;
    }
    const auto start_time = Clock::now();
    start_measurement.store(true, std::memory_order_release);

    producer_thread.join();
    consumer_thread.join();

    auto end_time = Clock::now();
    auto ns = std::chrono::duration_cast<Nanoseconds>(end_time - start_time).count();

    double seconds = static_cast<double>(ns) / 1e9;
    std::size_t processed = consumed_count.load(std::memory_order_relaxed);

    const double eps = seconds > 0.0 ? static_cast<double>(processed) / seconds : 0.0;
    const double ns_per_event = processed > 0
        ? static_cast<double>(ns) / static_cast<double>(processed) : 0.0;

    // === New: latency distribution ===
    std::vector<long long> samples;
    samples.reserve(processed);

    // latencies_ns may be slightly longer (by num_events),
    // and processed is the actual number of processed events.
    // Just in case, take the min.
    /*for (std::size_t i = 0; i < processed && i < latencies_ns.size(); ++i) {
        samples.push_back(latencies_ns[i]);
    }*/

    // add warmup
    for (std::size_t i = K_WARMUP_EVENTS; i < processed && i < latencies_ns.size(); ++i) {
        samples.push_back(latencies_ns[i]);
    }

    long long p50 = 0;
    long long p95 = 0;
    long long p99 = 0;
    if (!samples.empty()) {
        std::sort(samples.begin(), samples.end());
        auto N = samples.size();

        p50 = samples[static_cast<std::size_t>(0.50 * (N - 1))];
        p95 = samples[static_cast<std::size_t>(0.95 * (N - 1))];
        p99 = samples[static_cast<std::size_t>(0.99 * (N - 1))];
    }

    if (output_format == OutputFormat::Csv) {
        std::cout << processed << ',' << seed << ',' << QUEUE_CAPACITY << ',' << book_reserve << ','
                  << K_WARMUP_EVENTS << ',' << latency_mode_name(latency_mode) << ','
                  << backoff_mode_name(backoff_mode) << ',' << producer_cpu << ',' << consumer_cpu << ','
                  << producer_cpu_start.load(std::memory_order_relaxed) << ','
                  << producer_cpu_end.load(std::memory_order_relaxed) << ','
                  << consumer_cpu_start.load(std::memory_order_relaxed) << ','
                  << consumer_cpu_end.load(std::memory_order_relaxed) << ','
                  << ns << ',' << eps << ',' << ns_per_event << ',' << samples.size() << ','
                  << p50 << ',' << p95 << ',' << p99 << "\n";
    } else {
        std::cout << "mt_bench: processed " << processed << " events in "
                  << seconds << " s\n";
        std::cout << "  throughput: " << eps << " events/s\n";
        std::cout << "  mean:       " << ns_per_event << " ns/event\n";
        std::cout << "  latency mode: " << latency_mode_name(latency_mode) << "\n";
        std::cout << "  backoff mode: " << backoff_mode_name(backoff_mode) << "\n";
        std::cout << "  order-book reserve: " << book_reserve << "\n";
        std::cout << "  placement: producer requested=" << producer_cpu
                  << ", observed=" << producer_cpu_start.load(std::memory_order_relaxed)
                  << "->" << producer_cpu_end.load(std::memory_order_relaxed)
                  << "; consumer requested=" << consumer_cpu
                  << ", observed=" << consumer_cpu_start.load(std::memory_order_relaxed)
                  << "->" << consumer_cpu_end.load(std::memory_order_relaxed) << "\n";
        if (latency_mode == LatencyMode::PrePush) {
            std::cout << "Latency (pre-push -> processed; includes full-queue waiting):\n";
        } else if (latency_mode == LatencyMode::Enqueued) {
            std::cout << "Latency (successful push -> processed):\n";
        } else {
            std::cout << "Latency:\n";
        }
        if (!samples.empty()) {
            std::cout << "  samples: " << samples.size() << "\n";
            std::cout << "  p50: " << p50 << " ns\n";
            std::cout << "  p95: " << p95 << " ns\n";
            std::cout << "  p99: " << p99 << " ns\n";
        } else {
            std::cout << "  disabled\n";
        }
    }

    if (output_format == OutputFormat::Text) {
        auto bb = book.best_bid();
        auto ba = book.best_ask();
        std::cout << "Final best bid valid=" << bb.valid
                  << ", price=" << bb.price << ", qty=" << bb.qty << "\n";
        std::cout << "Final best ask valid=" << ba.valid
                  << ", price=" << ba.price << ", qty=" << ba.qty << "\n";
    }

    return 0;
}
