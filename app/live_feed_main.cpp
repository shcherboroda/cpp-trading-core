#include "trading/order_book.hpp"
#include "trading/event.hpp"
#include "trading/types.hpp"
#include "utils/spsc_queue.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

using namespace trading;

namespace {

// Simple line parser: ts_ns,type,side,price_int,qty_int
bool parse_event_line(const std::string& line, Event& ev) {
    if (line.empty()) return false;

    std::stringstream ss(line);
    std::string token;

    std::string ts_str;
    std::string type_str;
    std::string side_str;
    std::string price_str;
    std::string qty_str;

    if (!std::getline(ss, ts_str, ','))   return false;
    if (!std::getline(ss, type_str, ',')) return false;
    if (!std::getline(ss, side_str, ',')) return false;
    if (!std::getline(ss, price_str, ','))return false;
    if (!std::getline(ss, qty_str, ','))  return false;

    try {
        ev.ts_ns = std::stoll(ts_str);
        // type
        if (type_str == "T") {
            ev.type = EventType::Market;
        } else if (type_str == "A") {
            ev.type = EventType::Add;
        } else if (type_str == "C") {
            ev.type = EventType::Cancel;
        } else {
            return false;
        }

        // side
        if (!side_str.empty() && (side_str[0] == 'B' || side_str[0] == 'b')) {
            ev.side = Side::Buy;
        } else {
            ev.side = Side::Sell;
        }

        ev.price = static_cast<Price>(std::stoll(price_str));
        ev.qty   = static_cast<Quantity>(std::stoll(qty_str));

        // For now we do not support real CANCEL by id in the line format,
        // so ev.id stays 0 (no-op for cancel).
        ev.id = 0;

    } catch (...) {
        return false;
    }

    return true;
}

} // namespace

int main(int argc, char** argv) {
    // Optional limit: max events to read, 0 = unlimited.
    std::size_t max_events = 0;
    if (argc > 1) {
        max_events = std::strtoull(argv[1], nullptr, 10);
    }

    constexpr std::size_t QUEUE_CAPACITY = 4096;
    utils::SpscQueue<Event> queue(QUEUE_CAPACITY);

    std::atomic<bool> done{false};
    std::atomic<std::size_t> processed{0};
    OrderBook book;

    // Engine thread: consume events and apply to OrderBook
    std::thread engine_thread([&]() {
        Event ev;
        while (!done.load(std::memory_order_acquire) || !queue.empty()) {
            if (queue.pop(ev)) {
                switch (ev.type) {
                case EventType::Add:
                    book.add_limit_order(ev.side, ev.price, ev.qty);
                    break;
                case EventType::Market:
                    book.execute_market_order(ev.side, ev.qty);
                    break;
                case EventType::Cancel:
                    // No real id in the current format -> ignore for now.
                    // You can extend line format later to pass order_id.
                    break;
                case EventType::End:
                    // Sentinel used by synthetic generators (mt_bench).
                    // Live feed should never receive this, just ignore.
                    break;
                }

                processed.fetch_add(1, std::memory_order_relaxed);
            } else {
                std::this_thread::yield();
            }
        }
    });

    // Producer: read lines from stdin and push into SPSC queue
    std::size_t read_count = 0;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }
        if (max_events > 0 && read_count >= max_events) {
            break;
        }

        Event ev;
        if (!parse_event_line(line, ev)) {
            continue;
        }

        ++read_count;
        // Backpressure: if queue is full, yield until there is space
        while (!queue.push(ev)) {
            std::this_thread::yield();
        }
    }

    done.store(true, std::memory_order_release);
    engine_thread.join();

    auto best_bid = book.best_bid();
    auto best_ask = book.best_ask();

    std::cout << "Live feed summary:\n";
    std::cout << "  lines read:      " << read_count << "\n";
    std::cout << "  events processed:" << processed.load() << "\n";

    std::cout << "  final best bid:  ";
    if (best_bid.valid) {
        std::cout << best_bid.price << " x " << best_bid.qty << "\n";
    } else {
        std::cout << "none\n";
    }

    std::cout << "  final best ask:  ";
    if (best_ask.valid) {
        std::cout << best_ask.price << " x " << best_ask.qty << "\n";
    } else {
        std::cout << "none\n";
    }

    return 0;
}
