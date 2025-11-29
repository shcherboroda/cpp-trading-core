// app/bybit_rest_demo_main.cpp
#include "exchange/bybit_public_rest.hpp"

#include <chrono>
#include <ctime>
#include <iostream>

int main() {
    try {
        exchange::BybitPublicRest client;

        auto server_ms = client.get_server_time_ms();
        std::time_t t = server_ms / 1000;
        std::cout << "Bybit server time (ms): " << server_ms << "\n";
        std::cout << "Bybit server time (UTC): "
                  << std::asctime(std::gmtime(&t)); // has trailing '\n'

        auto ticker = client.get_spot_ticker("BTCUSDT");
        std::cout << "\nSpot ticker BTCUSDT:\n";
        std::cout << "  symbol:    " << ticker.symbol << "\n";
        std::cout << "  last:      " << ticker.last_price << "\n";
        std::cout << "  best bid:  " << ticker.best_bid << "\n";
        std::cout << "  best ask:  " << ticker.best_ask << "\n";
    } catch (const std::exception& ex) {
        std::cerr << "bybit_rest_demo error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
