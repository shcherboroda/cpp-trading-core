// src/utils/http_client.cpp
#include "utils/http_client.hpp"

#include <curl/curl.h>
#include <stdexcept>
#include <sstream>
#include <utility>

namespace utils {
namespace {

struct CurlGlobal {
    CurlGlobal() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }
    ~CurlGlobal() {
        curl_global_cleanup();
    }
};

size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

} // namespace

HttpClient::HttpClient(std::string base_url)
    : base_url_(std::move(base_url)) {
    static CurlGlobal curl_global_guard;
}

std::string HttpClient::get(const std::string& path, const std::string& query) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("curl_easy_init() failed");
    }

    std::string url = base_url_;
    url += path;
    if (!query.empty()) {
        url += "?";
        url += query;
    }

    std::string response;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "cpp-trading-core/0.1");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error(
            std::string("curl_easy_perform() failed: ") +
            curl_easy_strerror(res));
    }
    if (http_code != 200) {
        std::ostringstream oss;
        oss << "HTTP " << http_code << " for URL " << url;
        throw std::runtime_error(oss.str());
    }

    return response;
}

} // namespace utils
