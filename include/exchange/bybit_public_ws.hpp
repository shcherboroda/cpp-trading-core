// include/exchange/bybit_public_ws.hpp
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace exchange {

class BybitPublicWs {
public:
    using MessageHandler = std::function<void(const nlohmann::json&)>;

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

private:
    std::string host_;
    std::string port_;
    std::string path_;
};

} // namespace exchange
