// Afterglow - The streaming HTTP client over plain sockets (see http.hpp).
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net/http.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

namespace hui::net
{

namespace
{

constexpr int kMaxRedirects = 4;
constexpr int kMaxHeaderBytes = 16 * 1024;
// Reads wake up this often to look at the cancel flag...
constexpr int kPollSeconds = 1;
// ...and give up after this long without a byte.
constexpr int kStallSeconds = 15;

std::string lower(std::string text)
{
    for (char &c : text)
        c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return text;
}

class SocketStream final : public ByteStream
{
  public:
    SocketStream(int fd, std::vector<std::uint8_t> leftover, const std::atomic<bool> &cancel)
        : fd_(fd), leftover_(std::move(leftover)), cancel_(cancel)
    {
    }
    ~SocketStream() override
    {
        close(fd_);
    }

    int read(std::uint8_t *data, int size) override
    {
        if (offset_ < leftover_.size())
        {
            const auto count = std::min(static_cast<std::size_t>(size), leftover_.size() - offset_);
            std::memcpy(data, leftover_.data() + offset_, count);
            offset_ += count;
            return static_cast<int>(count);
        }
        int waited = 0;
        for (;;)
        {
            if (cancel_.load(std::memory_order_relaxed))
                return -1;
            const ssize_t got = recv(fd_, data, static_cast<std::size_t>(size), 0);
            if (got >= 0)
                return static_cast<int>(got);
            if ((errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) &&
                waited < kStallSeconds)
            {
                waited += kPollSeconds;
                continue;
            }
            return -1;
        }
    }

  private:
    int fd_;
    std::vector<std::uint8_t> leftover_;
    std::size_t offset_ = 0;
    const std::atomic<bool> &cancel_;
};

// One GET. On a redirect, location is set and the result has no stream.
OpenResult get_once(const Url &url, const std::atomic<bool> &cancel, std::string *location)
{
    OpenResult result;
    std::uint32_t address = 0;
    {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo *found = nullptr;
        const int status = getaddrinfo(url.host.c_str(), nullptr, &hints, &found);
        if (status != 0 || found == nullptr)
        {
            result.error = "cannot find " + url.host;
            return result;
        }
        address = reinterpret_cast<const sockaddr_in *>(found->ai_addr)->sin_addr.s_addr;
        freeaddrinfo(found);
    }

    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        result.error = "socket() failed (" + std::to_string(errno) + ")";
        return result;
    }
    timeval timeout{};
    timeout.tv_sec = kPollSeconds;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    timeval send_timeout{};
    send_timeout.tv_sec = 10;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));

    sockaddr_in peer{};
    peer.sin_family = AF_INET;
    peer.sin_port = htons(static_cast<std::uint16_t>(url.port));
    peer.sin_addr.s_addr = address;
    if (connect(fd, reinterpret_cast<const sockaddr *>(&peer), sizeof(peer)) != 0)
    {
        result.error = "cannot connect to " + url.host + " (" + std::to_string(errno) + ")";
        close(fd);
        return result;
    }

    const std::string request = "GET " + url.path + " HTTP/1.0\r\nHost: " + url.host +
                                "\r\nUser-Agent: Afterglow/1.0\r\nAccept: */*\r\n"
                                "Icy-MetaData: 1\r\nConnection: close\r\n\r\n";
    if (send(fd, request.data(), request.size(), 0) != static_cast<ssize_t>(request.size()))
    {
        result.error = "sending the request failed";
        close(fd);
        return result;
    }

    // Read until the blank line that ends the header.
    std::vector<std::uint8_t> received;
    std::size_t header_end = std::string::npos;
    int waited = 0;
    while (header_end == std::string::npos)
    {
        std::uint8_t chunk[2048];
        const ssize_t got = recv(fd, chunk, sizeof(chunk), 0);
        if (got > 0)
        {
            received.insert(received.end(), chunk, chunk + got);
            const std::string view(received.begin(), received.end());
            header_end = view.find("\r\n\r\n");
            if (header_end == std::string::npos && received.size() > kMaxHeaderBytes)
                break;
            continue;
        }
        if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) &&
            waited < kStallSeconds && !cancel.load(std::memory_order_relaxed))
        {
            waited += kPollSeconds;
            continue;
        }
        break;
    }
    if (header_end == std::string::npos)
    {
        result.error = "no answer from " + url.host;
        close(fd);
        return result;
    }

    const std::string header(received.begin(),
                             received.begin() + static_cast<std::ptrdiff_t>(header_end));
    int status = 0;
    char protocol[16] = {};
    if (std::sscanf(header.c_str(), "%15s %d", protocol, &status) != 2)
    {
        result.error = "not an HTTP answer";
        close(fd);
        return result;
    }

    std::string content_type;
    std::string station_name;
    int metaint = 0;
    std::size_t line_start = header.find("\r\n");
    while (line_start != std::string::npos)
    {
        line_start += 2;
        const std::size_t line_end = header.find("\r\n", line_start);
        const std::string line = header.substr(
            line_start, line_end == std::string::npos ? std::string::npos : line_end - line_start);
        const std::size_t colon = line.find(':');
        if (colon != std::string::npos)
        {
            const std::string name = lower(line.substr(0, colon));
            std::size_t value_start = colon + 1;
            while (value_start < line.size() && line[value_start] == ' ')
                ++value_start;
            const std::string value = line.substr(value_start);
            if (name == "location")
                *location = value;
            else if (name == "content-type")
                content_type = lower(value);
            else if (name == "icy-metaint")
                metaint = std::atoi(value.c_str());
            else if (name == "icy-name")
                station_name = value;
        }
        line_start = line_end;
    }

    if (status >= 300 && status < 400 && !location->empty())
    {
        close(fd);
        return result;
    }
    location->clear();
    if (status != 200)
    {
        result.error = "the server answered " + std::to_string(status);
        result.status = status;
        close(fd);
        return result;
    }
    std::vector<std::uint8_t> body(received.begin() + static_cast<std::ptrdiff_t>(header_end + 4),
                                   received.end());
    auto stream = std::make_unique<SocketStream>(fd, std::move(body), cancel);
    stream->content_type = content_type;
    stream->metaint = std::max(0, metaint);
    stream->name = station_name;
    result.stream = std::move(stream);
    result.via = "sockets";
    return result;
}

} // namespace

bool parse_url(const std::string &text, Url *url)
{
    const std::size_t scheme_end = text.find("://");
    if (scheme_end == std::string::npos)
        return false;
    url->scheme = lower(text.substr(0, scheme_end));
    if (url->scheme != "http" && url->scheme != "https")
        return false;
    const std::size_t host_start = scheme_end + 3;
    const std::size_t path_start = text.find('/', host_start);
    std::string authority = text.substr(
        host_start, path_start == std::string::npos ? std::string::npos : path_start - host_start);
    url->path = path_start == std::string::npos ? "/" : text.substr(path_start);
    url->port = url->scheme == "https" ? 443 : 80;
    const std::size_t at = authority.rfind('@');
    if (at != std::string::npos)
        authority = authority.substr(at + 1); // user info is not supported; drop it
    const std::size_t colon = authority.rfind(':');
    if (colon != std::string::npos)
    {
        const std::string port = authority.substr(colon + 1);
        if (port.empty() || port.size() > 5 ||
            port.find_first_not_of("0123456789") != std::string::npos)
            return false;
        url->port = std::stoi(port);
        authority = authority.substr(0, colon);
    }
    url->host = authority;
    return !url->host.empty() && url->port > 0 && url->port < 65536;
}

OpenResult open_url(const std::string &text, const std::atomic<bool> &cancel)
{
    return platform_open(text, cancel);
}

OpenResult open_with_sockets(const std::string &text, const std::atomic<bool> &cancel)
{
    std::string current = text;
    for (int hop = 0; hop <= kMaxRedirects; ++hop)
    {
        Url url;
        if (!parse_url(current, &url))
        {
            OpenResult result;
            result.error = "not an http(s) address";
            return result;
        }
        if (url.scheme == "https")
        {
            OpenResult result;
            result.error = "https stations play on the console only";
            return result;
        }
        std::string location;
        OpenResult result = get_once(url, cancel, &location);
        if (location.empty())
            return result;
        if (location.rfind("http", 0) != 0) // a relative redirect
            location = url.scheme + "://" + url.host + ":" + std::to_string(url.port) +
                       (location.empty() || location[0] != '/' ? "/" : "") + location;
        current = location;
    }
    OpenResult result;
    result.error = "too many redirects";
    return result;
}

} // namespace hui::net
