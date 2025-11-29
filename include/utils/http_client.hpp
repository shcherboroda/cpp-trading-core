// include/utils/http_client.hpp
#pragma once

#include <string>

namespace utils {

class HttpClient {
public:
    // base_url like "https://api.bybit.com"
    explicit HttpClient(std::string base_url);

    // Simple GET: path like "/v5/market/time",
    // query like "category=spot&symbol=BTCUSDT" (optional).
    std::string get(const std::string& path, const std::string& query = "");

private:
    std::string base_url_;
};

} // namespace utils
