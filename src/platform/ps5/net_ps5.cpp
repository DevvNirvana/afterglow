// Afterglow - Console transport for the radio: libcurl with OpenSSL.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The same path BlackBearReloaded's console-tested apps take (ProsperoRadio,
// the boilerplate's update check): PacBrew's libcurl, with console_curl.c
// for what a native title lacks (name lookup on sceNetResolver, an fcntl
// that works on sockets, non-blocking sockets, the console's own list of
// certificate authorities). One stream is one curl transfer on its own
// multi handle, driven by the thread that reads it.

#include "net/http.hpp"
#include "platform/ps5/console_curl.h"
#include "platform/ps5/system.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>

namespace hui::net
{

namespace
{

constexpr long kConnectMs = 8000;
constexpr int kHeaderSeconds = 15; // connect, redirects, TLS and the first answer
constexpr long kStallSeconds = 20; // no byte for this long ends the stream
constexpr int kPollMs = 100;

std::once_flag g_curl_once;
bool g_curl_ok = false;

std::string lower(std::string text)
{
    for (char &c : text)
        c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return text;
}

std::string trim(const std::string &text)
{
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

class CurlStream final : public ByteStream
{
  public:
    explicit CurlStream(const std::atomic<bool> &cancel) : cancel_(cancel)
    {
    }

    ~CurlStream() override
    {
        if (multi_ != nullptr && easy_ != nullptr)
            curl_multi_remove_handle(multi_, easy_);
        if (easy_ != nullptr)
            curl_easy_cleanup(easy_);
        if (multi_ != nullptr)
            curl_multi_cleanup(multi_);
        if (headers_ != nullptr)
            curl_slist_free_all(headers_);
    }

    // Starts the transfer and waits for the final answer's header.
    long status() const
    {
        return status_;
    }

    bool open(const std::string &url, std::string *error)
    {
        multi_ = curl_multi_init();
        easy_ = curl_easy_init();
        if (multi_ == nullptr || easy_ == nullptr)
        {
            *error = "the network library did not start";
            return false;
        }
        headers_ = curl_slist_append(headers_, "Icy-MetaData: 1");
        curl_easy_setopt(easy_, CURLOPT_URL, url.c_str());
        curl_easy_setopt(easy_, CURLOPT_USERAGENT,
                         "Afterglow/1.1 ( https://github.com/DevvNirvana )");
        curl_easy_setopt(easy_, CURLOPT_HTTPHEADER, headers_);
        curl_easy_setopt(easy_, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
        curl_easy_setopt(easy_, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(easy_, CURLOPT_MAXREDIRS, 6L);
        curl_easy_setopt(easy_, CURLOPT_PROTOCOLS_STR, "http,https");
        curl_easy_setopt(easy_, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
        curl_easy_setopt(easy_, CURLOPT_CONNECTTIMEOUT_MS, kConnectMs);
        curl_easy_setopt(easy_, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(easy_, CURLOPT_LOW_SPEED_TIME, kStallSeconds);
        curl_easy_setopt(easy_, CURLOPT_BUFFERSIZE, 64L * 1024L);
        // Old SHOUTcast servers answer "ICY 200 OK", an HTTP/0.9-like line.
        curl_easy_setopt(easy_, CURLOPT_HTTP09_ALLOWED, 1L);
        curl_easy_setopt(easy_, CURLOPT_HEADERFUNCTION, &CurlStream::on_header);
        curl_easy_setopt(easy_, CURLOPT_HEADERDATA, this);
        curl_easy_setopt(easy_, CURLOPT_WRITEFUNCTION, &CurlStream::on_body);
        curl_easy_setopt(easy_, CURLOPT_WRITEDATA, this);
        curl_easy_setopt(easy_, CURLOPT_ERRORBUFFER, error_);
        console_curl_setup(easy_); // NOSIGNAL, the certificate list, SO_NBIO
        if (curl_multi_add_handle(multi_, easy_) != CURLM_OK)
        {
            *error = "the network library did not start";
            return false;
        }

        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(kHeaderSeconds);
        while (!answered_ && !done_)
        {
            if (cancel_.load(std::memory_order_relaxed))
            {
                *error = "cancelled";
                return false;
            }
            if (std::chrono::steady_clock::now() > deadline)
            {
                *error = "the station did not answer in time";
                return false;
            }
            drive();
        }
        if (done_ && result_ != CURLE_OK && buffer_.empty())
        {
            *error = describe();
            return false;
        }
        if (status_ != 200)
        {
            *error = "the server answered " + std::to_string(status_);
            return false;
        }
        return true;
    }

    int read(std::uint8_t *data, int size) override
    {
        while (taken_ >= buffer_.size())
        {
            buffer_.clear();
            taken_ = 0;
            if (cancel_.load(std::memory_order_relaxed))
                return -1;
            if (done_)
                return result_ == CURLE_OK ? 0 : -1;
            drive();
        }
        const std::size_t count = std::min(static_cast<std::size_t>(size), buffer_.size() - taken_);
        std::memcpy(data, buffer_.data() + taken_, count);
        taken_ += count;
        return static_cast<int>(count);
    }

    std::string describe() const
    {
        std::string text = error_[0] != '\0' ? error_ : curl_easy_strerror(result_);
        return text.empty() ? "the connection failed" : text;
    }

  private:
    void drive()
    {
        int running = 0;
        curl_multi_perform(multi_, &running);
        int left = 0;
        while (CURLMsg *message = curl_multi_info_read(multi_, &left))
        {
            if (message->msg == CURLMSG_DONE)
            {
                done_ = true;
                result_ = message->data.result;
            }
        }
        if (!done_ && buffer_.size() <= taken_)
            curl_multi_poll(multi_, nullptr, 0, kPollMs, nullptr);
    }

    static std::size_t on_header(char *data, std::size_t size, std::size_t count, void *user)
    {
        auto *self = static_cast<CurlStream *>(user);
        const std::size_t total = size * count;
        const std::string line(data, total);
        const std::string low = lower(trim(line));
        if (low.rfind("http/", 0) == 0 || low.rfind("icy ", 0) == 0)
        {
            // A new answer (after a redirect, the next one) starts over.
            self->status_ = 0;
            const std::size_t space = low.find(' ');
            if (space != std::string::npos)
                self->status_ = std::atoi(low.c_str() + space + 1);
            self->content_type.clear();
            self->metaint = 0;
            self->name.clear();
        }
        else if (low.empty())
        {
            // The blank line ends a header; a redirect's is followed by another.
            if (self->status_ < 300 || self->status_ >= 400)
                self->answered_ = true;
        }
        else
        {
            const std::size_t colon = low.find(':');
            if (colon != std::string::npos)
            {
                const std::string key = trim(low.substr(0, colon));
                const std::string value = trim(line.substr(colon + 1));
                if (key == "content-type")
                    self->content_type = lower(value);
                else if (key == "icy-metaint")
                    self->metaint = std::max(0, std::atoi(value.c_str()));
                else if (key == "icy-name")
                    self->name = value;
            }
        }
        return total;
    }

    static std::size_t on_body(char *data, std::size_t size, std::size_t count, void *user)
    {
        auto *self = static_cast<CurlStream *>(user);
        const std::size_t total = size * count;
        self->answered_ = true; // an "ICY" server may send no blank line we see
        if (self->status_ == 0)
            self->status_ = 200;
        self->buffer_.append(data, total);
        return total;
    }

    const std::atomic<bool> &cancel_;
    CURLM *multi_ = nullptr;
    CURL *easy_ = nullptr;
    curl_slist *headers_ = nullptr;
    std::string buffer_;
    std::size_t taken_ = 0;
    long status_ = 0;
    bool answered_ = false;
    bool done_ = false;
    CURLcode result_ = CURLE_OK;
    char error_[CURL_ERROR_SIZE] = {};
};

} // namespace

OpenResult platform_open(const std::string &url, const std::atomic<bool> &cancel)
{
    std::call_once(g_curl_once,
                   []
                   {
                       g_curl_ok = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
                       sys::log("[AG] curl %s, certificates at %s", g_curl_ok ? "ready" : "failed",
                                console_curl_ca_file());
                   });
    OpenResult result;
    if (!g_curl_ok)
    {
        result.error = "the network library did not start";
        return result;
    }
    auto stream = std::make_unique<CurlStream>(cancel);
    if (!stream->open(url, &result.error))
    {
        result.status = static_cast<int>(stream->status());
        sys::log("[AG] radio open failed: %s (%s)", url.c_str(), result.error.c_str());
        return result;
    }
    result.via = url.rfind("https", 0) == 0 ? "https" : "http";
    result.stream = std::move(stream);
    return result;
}

} // namespace hui::net
