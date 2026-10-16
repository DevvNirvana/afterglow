// Afterglow - A minimal streaming HTTP client for internet radio.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Radio streams are one endless response body, so all this needs is GET,
// redirects and a byte stream. Every request asks for ICY metadata (the
// song titles stations put between the audio); the stream reports how often
// it comes. The transport is the platform's: on the console libcurl with
// OpenSSL (platform/ps5/net_ps5.cpp), on a PC the plain-socket client below
// (http only), which the tests use too.

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace hui::net
{

class ByteStream
{
  public:
    virtual ~ByteStream() = default;
    // Up to size bytes. Returns how many, 0 at the end of the stream, -1 on
    // an error (or when cancel was raised while waiting).
    virtual int read(std::uint8_t *data, int size) = 0;
    std::string content_type; // lower case, as the server sent it
    int metaint = 0;          // ICY: audio bytes between metadata blocks; 0: none
    std::string name;         // ICY: the station's own name, if it sent one
};

struct OpenResult
{
    std::unique_ptr<ByteStream> stream;
    std::string error; // set when stream is null
    std::string via;   // which path served it ("sockets", "system https", ...)
    int status = 0;    // the HTTP status of the final answer, when one came
};

struct Url
{
    std::string scheme; // "http" or "https"
    std::string host;
    int port = 0;
    std::string path; // starts with '/'
};

// Splits an absolute http(s) URL. False if it is not one.
bool parse_url(const std::string &text, Url *url);

// Opens url for reading, following redirects. cancel may be raised from
// another thread to abandon a connect or a read. This is platform_open().
OpenResult open_url(const std::string &url, const std::atomic<bool> &cancel);

// The plain-socket client: http:// only, HTTP/1.0, up to four redirects.
// Names are looked up with getaddrinfo.
OpenResult open_with_sockets(const std::string &url, const std::atomic<bool> &cancel);

// ---- platform hook (host: net_host.cpp; console: platform/ps5/net_ps5.cpp) ----
OpenResult platform_open(const std::string &url, const std::atomic<bool> &cancel);

} // namespace hui::net
