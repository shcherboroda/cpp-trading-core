// src/exchange/bybit_public_ws.cpp
#include "exchange/bybit_public_ws.hpp"

#include <algorithm>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace exchange {

namespace beast = boost::beast;
namespace http  = beast::http;
namespace websocket = beast::websocket;
namespace net   = boost::asio;
namespace ssl   = net::ssl;
using     tcp   = net::ip::tcp;

using json = nlohmann::json;

BybitPublicWs::BybitPublicWs(std::string host,
                             std::string port,
                             std::string path)
    : host_(std::move(host))
    , port_(std::move(port))
    , path_(std::move(path))
{
}

void BybitPublicWs::run(const std::vector<std::string>& channels,
                        const MessageHandler& handler,
                        int max_messages)
{
    run_impl(channels, [&handler](std::string_view text) {
        try {
            handler(json::parse(text));
            return true;
        } catch (const std::exception& ex) {
            std::cerr << "[BybitPublicWs] JSON parse error: " << ex.what() << "\n";
            return false;
        }
    }, max_messages);
}

void BybitPublicWs::run_text(const std::vector<std::string>& channels,
                             const TextMessageHandler& handler,
                             int max_messages)
{
    run_impl(channels, [&handler](std::string_view text) { handler(text); return true; }, max_messages);
}

void BybitPublicWs::run_impl(const std::vector<std::string>& channels,
                             const FrameHandler& handler,
                             int max_messages)
{
    try {
        read_wait_stats_.clear();
        callback_stats_.clear();
        buffered_read_count_ = 0;
        max_buffered_read_streak_ = 0;
        net::io_context ioc;

        ssl::context ctx{ssl::context::tls_client};
        ctx.set_default_verify_paths();
        ctx.set_verify_mode(ssl::verify_peer);

        tcp::resolver resolver{ioc};
        auto const results = resolver.resolve(host_, port_);

        beast::ssl_stream<beast::tcp_stream> ssl_stream{ioc, ctx};

        // SNI (Server Name Indication), чтобы TLS знал хост
        if (! ::SSL_set_tlsext_host_name(ssl_stream.native_handle(), host_.c_str())) {
            beast::error_code ec{static_cast<int>(::ERR_get_error()),
                                 net::error::get_ssl_category()};
            throw beast::system_error{ec};
        }

        // TCP connect
        beast::get_lowest_layer(ssl_stream).connect(results);

        // TLS handshake
        ssl_stream.handshake(ssl::stream_base::client);

        // WebSocket handshake поверх TLS
        websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws{std::move(ssl_stream)};
        ws.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
        ws.set_option(websocket::stream_base::decorator(
            [this](websocket::request_type& req) {
                req.set(http::field::host, host_);
                req.set(http::field::user_agent, std::string("cpp-trading-core/bybit-ws"));
            }));
        ws.handshake(host_, path_);

        // Формируем subscribe сообщение
        json sub_msg;
        sub_msg["op"]   = "subscribe";
        sub_msg["args"] = channels;

        auto sub_str = sub_msg.dump();
        ws.write(net::buffer(sub_str));

        // Чтение сообщений
        beast::flat_buffer buffer;
        std::size_t count = 0;
        std::size_t buffered_read_streak = 0;

        for (;;) {
            buffer.clear();
            beast::error_code ec;
            const auto read_start = std::chrono::steady_clock::now();
            ws.read(buffer, ec);
            const auto read_end = std::chrono::steady_clock::now();
            const auto read_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(read_end - read_start).count();
            read_wait_stats_.add(read_ns);
            if (ec == websocket::error::closed) {
                break;
            }
            if (ec) {
                throw beast::system_error{ec};
            }
            if (read_ns < 100'000) {
                ++buffered_read_count_;
                max_buffered_read_streak_ = std::max(max_buffered_read_streak_, ++buffered_read_streak);
            } else {
                buffered_read_streak = 0;
            }

            const auto data = buffer.data();
            const std::string_view text{static_cast<const char*>(data.data()), data.size()};
            const auto callback_start = std::chrono::steady_clock::now();
            const bool handled = handler(text);
            const auto callback_end = std::chrono::steady_clock::now();
            callback_stats_.add(std::chrono::duration_cast<std::chrono::nanoseconds>(callback_end - callback_start).count());
            if (!handled) {
                continue;
            }

            ++count;
            if (max_messages >= 0 && static_cast<int>(count) >= max_messages) {
                break;
            }
        }

        beast::error_code ec;
        ws.close(websocket::close_code::normal, ec);
        // ignore ec on close

    } catch (const std::exception& ex) {
        std::cerr << "[BybitPublicWs] exception: " << ex.what() << "\n";
        throw; // или можно не пробрасывать, но для отладки пока оставим
    }
}

} // namespace exchange
