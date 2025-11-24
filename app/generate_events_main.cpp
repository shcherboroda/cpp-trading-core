#include <iostream>
#include <random>
#include <vector>
#include <string>
#include <cstdint>

using OrderId = std::uint64_t;

enum class Side { Buy, Sell };

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: trading_generate <num_events> <seed>\n";
        return 1;
    }

    const std::size_t num_events = static_cast<std::size_t>(std::stoull(argv[1]));
    const std::uint32_t seed     = static_cast<std::uint32_t>(std::stoul(argv[2]));

    std::mt19937_64 rng(seed);

    // Вероятности типов событий:
    // 0..59  -> ADD (60%)
    // 60..89 -> MKT (30%)
    // 90..99 -> CANCEL (10%)
    std::uniform_int_distribution<int> event_type_dist(0, 99);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<std::int64_t> price_dist(95, 105);
    std::uniform_int_distribution<std::int64_t> qty_dist(1, 10);

    std::vector<OrderId> active_ids;
    active_ids.reserve(num_events);

    OrderId next_id_for_cancel = 1; // логический счётчик ADD'ов

    // Комментарий-хедер (replay его пропустит, т.к. строка начинается с '#')
    std::cout << "# type,side,price,qty,id\n";

    for (std::size_t i = 0; i < num_events; ++i) {
        int r = event_type_dist(rng);

        // Если активных ордеров нет, CANCEL бессмысленен → чуть bias в сторону ADD.
        bool force_add = active_ids.empty();

        if (force_add || r < 60) {
            // ADD
            int side_val = side_dist(rng);
            std::string side_str = (side_val == 0) ? "BUY" : "SELL";
            auto price = price_dist(rng);
            auto qty   = qty_dist(rng);

            // логический id ордера
            OrderId id = next_id_for_cancel++;

            std::cout << "ADD," << side_str << "," << price << "," << qty << "," << id << "\n";

            active_ids.push_back(id);
        } else if (r < 90) {
            // MKT
            int side_val = side_dist(rng);
            std::string side_str = (side_val == 0) ? "BUY" : "SELL";
            auto qty = qty_dist(rng);

            std::cout << "MKT," << side_str << "," << qty << "\n";
        } else {
            // CANCEL
            if (!active_ids.empty()) {
                // Обновляем распределение под текущий размер
                std::uniform_int_distribution<std::size_t> idx_dist(0, active_ids.size() - 1);
                std::size_t idx = idx_dist(rng);
                OrderId id = active_ids[idx];

                std::cout << "CANCEL," << id << "\n";

                // Убираем id, чтобы не отменять один и тот же много раз
                active_ids[idx] = active_ids.back();
                active_ids.pop_back();
            } else {
                // Нет активных ордеров — откат к ADD с корректным форматом
                int side_val = side_dist(rng);
                std::string side_str = (side_val == 0) ? "BUY" : "SELL";
                auto price = price_dist(rng);
                auto qty   = qty_dist(rng);

                OrderId id = next_id_for_cancel++;

                std::cout << "ADD," << side_str << "," << price << "," << qty << "," << id << "\n";
                active_ids.push_back(id);
            }
        }
    }

    return 0;
}
