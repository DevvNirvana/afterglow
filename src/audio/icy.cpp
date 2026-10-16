// Afterglow - ICY metadata (see icy.hpp).
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/icy.hpp"

#include <algorithm>

namespace hui::audio
{

std::string icy_title(const std::string &block)
{
    const std::string key = "StreamTitle='";
    const std::size_t start = block.find(key);
    if (start == std::string::npos)
        return {};
    const std::size_t from = start + key.size();
    // The title ends at "';" (a quote inside a title is common: "Don't").
    std::size_t end = block.find("';", from);
    if (end == std::string::npos)
        end = block.rfind('\'');
    if (end == std::string::npos || end < from)
        return {};
    std::string title = block.substr(from, end - from);
    // Keep it printable and short.
    title.erase(std::remove_if(title.begin(), title.end(),
                               [](char c) { return static_cast<unsigned char>(c) < 0x20; }),
                title.end());
    if (title.size() > 160)
        title.resize(160);
    return title;
}

void IcyReader::feed(const std::uint8_t *data, std::size_t size, std::vector<std::uint8_t> &audio)
{
    if (metaint_ == 0)
    {
        audio.insert(audio.end(), data, data + size);
        return;
    }
    std::size_t at = 0;
    while (at < size)
    {
        if (meta_left_ < 0)
        {
            if (left_ > 0)
            {
                const std::size_t count = std::min(size - at, static_cast<std::size_t>(left_));
                audio.insert(audio.end(), data + at, data + at + count);
                at += count;
                left_ -= static_cast<int>(count);
                continue;
            }
            // The length byte.
            meta_left_ = data[at++] * 16;
            block_.clear();
            if (meta_left_ == 0)
                finish_block();
            continue;
        }
        const std::size_t count = std::min(size - at, static_cast<std::size_t>(meta_left_));
        block_.append(reinterpret_cast<const char *>(data + at), count);
        at += count;
        meta_left_ -= static_cast<int>(count);
        if (meta_left_ == 0)
            finish_block();
    }
}

void IcyReader::finish_block()
{
    meta_left_ = -1;
    left_ = metaint_;
    if (block_.empty())
        return;
    std::string title = icy_title(block_);
    if (!title.empty() && title != title_)
    {
        title_ = std::move(title);
        fresh_ = true;
    }
}

bool IcyReader::take_title(std::string *title)
{
    if (!fresh_)
        return false;
    fresh_ = false;
    *title = title_;
    return true;
}

} // namespace hui::audio
