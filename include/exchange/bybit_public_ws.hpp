// include/exchange/bybit_public_ws.hpp
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include "utils/latency_stats.hpp"

namespace exchange {

class BybitPublicWs {
public:
    using MessageHandler = std::function<void(const nlohmann::json&)>;
    using TextMessageHandler = std::function<void(std::string_view)>;

    BybitPublicWs(std::string host = "stream.bybit.com",
                  std::string port = "443",
                  std::string path = "/v5/public/spot");

    // Блокирующий запуск: подключиться, подписаться, читать сообщения,
    // вызывать handler для каждого JSON.
    //
    // max_messages < 0  -> читать бесконечно
    // max_messages >= 0 -> остановиться после обработки max_messages сообщений
    void run(const std::vector<std::string>& channels,
             const MessageHandler& handler,
             int max_messages = -1);
    void run_text(const std::vector<std::string>& channels,
                  const TextMessageHandler& handler,
                  int max_messages = -1);
    const utils::LatencyStats& read_wait_stats() const noexcept { return read_wait_stats_; }
    const utils::LatencyStats& callback_stats() const noexcept { return callback_stats_; }
    std::size_t buffered_read_count() const noexcept { return buffered_read_count_; }
    std::size_t max_buffered_read_streak() const noexcept { return max_buffered_read_streak_; }

private:
    using FrameHandler = std::function<bool(std::string_view)>;
    void run_impl(const std::vector<std::string>& channels,
                  const FrameHandler& handler,
                  int max_messages);
    std::string host_;
    std::string port_;
    std::string path_;
    utils::LatencyStats read_wait_stats_;
    utils::LatencyStats callback_stats_;
    std::size_t buffered_read_count_{};
    std::size_t max_buffered_read_streak_{};
};

} // namespace exchange
