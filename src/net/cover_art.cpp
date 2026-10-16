// Afterglow - Album art for what the radio plays (see cover_art.hpp).
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net/cover_art.hpp"

#include "core/save_file.hpp"
#include "net/json.hpp"
#include "third_party/stb/image.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace hui::net
{

namespace
{

CoverArt *g_art = nullptr;
CoverArt *g_logos = nullptr;

constexpr std::size_t kMaxTextures = 12;
constexpr std::size_t kMaxLogoTextures = 48; // a list of stations shows many at once
constexpr std::size_t kMaxLogoQueue = 32;
constexpr int kMaxSide = 256;
constexpr std::size_t kMaxCached = 400; // files on disk before the oldest go
constexpr double kMinScore = 80.0;

double now_seconds()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string lower_ascii(std::string text)
{
    for (char &c : text)
        c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return text;
}

std::string trim(const std::string &text)
{
    const std::size_t first = text.find_first_not_of(" \t\r\n-|/");
    if (first == std::string::npos)
        return {};
    const std::size_t last = text.find_last_not_of(" \t\r\n-|/");
    return text.substr(first, last - first + 1);
}

// Removes "(Original Mix)", "[Label]" and the like.
std::string strip_brackets(const std::string &text)
{
    std::string out;
    int depth = 0;
    for (char c : text)
    {
        if (c == '(' || c == '[' || c == '{')
            ++depth;
        else if ((c == ')' || c == ']' || c == '}') && depth > 0)
            --depth;
        else if (depth == 0)
            out += c;
    }
    return trim(out);
}

// Letters and digits only, lower case (UTF-8 bytes kept): for comparing.
std::string fold(const std::string &text)
{
    std::string out;
    for (char c : lower_ascii(text))
    {
        const auto u = static_cast<unsigned char>(c);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || u >= 0x80)
            out += c;
    }
    return out;
}

// Words for a search: anything Lucene treats as syntax becomes a space.
std::string search_words(const std::string &text)
{
    std::string out;
    for (char c : text)
    {
        const auto u = static_cast<unsigned char>(c);
        const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || u >= 0x80 || c == '\'' || c == '.';
        out += keep ? c : ' ';
    }
    // Collapse the spaces.
    std::string collapsed;
    for (char c : out)
        if (c != ' ' || (!collapsed.empty() && collapsed.back() != ' '))
            collapsed += c;
    return trim(collapsed);
}

std::string url_encode(const std::string &text)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (char c : text)
    {
        const auto u = static_cast<unsigned char>(c);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~')
        {
            out += c;
        }
        else
        {
            out += '%';
            out += kHex[u >> 4];
            out += kHex[u & 15];
        }
    }
    return out;
}

// The first artist of "A feat. B", "A & B", "A x B", "A, B", "A vs B".
std::string first_artist(const std::string &artist)
{
    static constexpr const char *kJoins[] = {" feat. ",     " feat ", " ft. ", " ft ",
                                             " featuring ", " & ",    " x ",   " vs. ",
                                             " vs ",        ", ",     " and ", " with "};
    const std::string low = lower_ascii(artist);
    std::size_t cut = artist.size();
    for (const char *join : kJoins)
    {
        const std::size_t at = low.find(join);
        if (at != std::string::npos && at > 0)
            cut = std::min(cut, at);
    }
    return trim(artist.substr(0, cut));
}

bool contains_either(const std::string &a, const std::string &b)
{
    if (a.empty() || b.empty())
        return false;
    return a.find(b) != std::string::npos || b.find(a) != std::string::npos;
}

std::string hex_hash(const std::string &text)
{
    std::uint64_t hash = 1469598103934665603ull;
    for (char c : text)
        hash = (hash ^ static_cast<unsigned char>(c)) * 1099511628211ull;
    char out[17];
    std::snprintf(out, sizeof(out), "%016llx", static_cast<unsigned long long>(hash));
    return out;
}

// The cover's colours: the most common one, and the most striking one
// (frequent and saturated), from a coarse histogram.
void colours(ArtImage *image)
{
    struct Bucket
    {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        int count = 0;
    };
    std::vector<Bucket> buckets(4096);
    const std::size_t pixels = image->rgba.size() / 4;
    for (std::size_t i = 0; i < pixels; i += 3)
    {
        const std::uint8_t *p = &image->rgba[i * 4];
        const int index = (p[0] >> 4) << 8 | (p[1] >> 4) << 4 | (p[2] >> 4);
        Bucket &bucket = buckets[static_cast<std::size_t>(index)];
        bucket.r += static_cast<float>(p[0]);
        bucket.g += static_cast<float>(p[1]);
        bucket.b += static_cast<float>(p[2]);
        ++bucket.count;
    }
    float best_common = -1.0f;
    float best_vivid = -1.0f;
    gfx::Color common{0.2f, 0.2f, 0.2f, 1.0f};
    gfx::Color vivid{0.9f, 0.5f, 0.3f, 1.0f};
    for (const Bucket &bucket : buckets)
    {
        if (bucket.count == 0)
            continue;
        const float n = static_cast<float>(bucket.count) * 255.0f;
        const gfx::Color c{bucket.r / n, bucket.g / n, bucket.b / n, 1.0f};
        const float hi = std::max(c.r, std::max(c.g, c.b));
        const float lo = std::min(c.r, std::min(c.g, c.b));
        const float saturation = hi > 0.0f ? (hi - lo) / hi : 0.0f;
        const float count = static_cast<float>(bucket.count);
        if (count > best_common)
        {
            best_common = count;
            common = c;
        }
        const float score = std::sqrt(count) * (saturation * saturation + 0.02f) *
                            std::clamp(hi * 1.4f - 0.15f, 0.0f, 1.0f);
        if (score > best_vivid)
        {
            best_vivid = score;
            vivid = c;
        }
    }
    image->dominant = common;
    image->vivid = vivid;
}

} // namespace

CoverArt *cover_art()
{
    return g_art;
}

void set_cover_art(CoverArt *art)
{
    g_art = art;
}

CoverArt *station_logos()
{
    return g_logos;
}

void set_station_logos(CoverArt *logos)
{
    g_logos = logos;
}

namespace
{

std::uint32_t le32(const std::string &b, std::size_t at)
{
    return static_cast<std::uint32_t>(static_cast<unsigned char>(b[at])) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(b[at + 1])) << 8 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(b[at + 2])) << 16 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(b[at + 3])) << 24;
}

std::uint16_t le16(const std::string &b, std::size_t at)
{
    return static_cast<std::uint16_t>(static_cast<unsigned char>(b[at]) |
                                      static_cast<unsigned char>(b[at + 1]) << 8);
}

// A Windows icon (most station favicons): the largest picture in it, either
// a PNG inside (decoded by stb_image) or a 24- or 32-bit bitmap.
bool ico_to_rgba(const std::string &b, std::vector<std::uint8_t> *rgba, int *width, int *height)
{
    if (b.size() < 22 || le16(b, 0) != 0 || le16(b, 2) != 1)
        return false;
    const int count = le16(b, 4);
    if (count <= 0 || b.size() < 6 + static_cast<std::size_t>(count) * 16)
        return false;
    int best = -1;
    int best_side = 0;
    for (int i = 0; i < count; ++i)
    {
        const std::size_t e = 6 + static_cast<std::size_t>(i) * 16;
        const int side = b[e] == 0 ? 256 : static_cast<unsigned char>(b[e]);
        if (side > best_side)
        {
            best_side = side;
            best = i;
        }
    }
    const std::size_t e = 6 + static_cast<std::size_t>(best) * 16;
    const std::uint32_t size = le32(b, e + 8);
    const std::uint32_t at = le32(b, e + 12);
    if (at >= b.size() || size > b.size() - at || size < 40)
        return false;
    const std::string data = b.substr(at, size);
    if (data.compare(0, 4, "\x89PNG") == 0)
    {
        int channels = 0;
        stbi_uc *png =
            stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(data.data()),
                                  static_cast<int>(data.size()), width, height, &channels, 4);
        if (png == nullptr)
            return false;
        rgba->assign(png, png + static_cast<std::size_t>(*width) *
                                    static_cast<std::size_t>(*height) * 4);
        stbi_image_free(png);
        return true;
    }
    // A bitmap: a BITMAPINFOHEADER, rows bottom up, height doubled (the AND
    // mask follows the colours).
    const auto w = static_cast<std::int32_t>(le32(data, 4));
    const auto h2 = static_cast<std::int32_t>(le32(data, 8));
    const int bits = le16(data, 14);
    const std::uint32_t header = le32(data, 0);
    const int h = h2 / 2;
    if (w <= 0 || h <= 0 || w > 512 || h > 512 || (bits != 32 && bits != 24) ||
        header > data.size())
        return false;
    const std::size_t stride =
        ((static_cast<std::size_t>(w) * static_cast<std::size_t>(bits) + 31) / 32) * 4;
    const std::size_t mask_stride = ((static_cast<std::size_t>(w) + 31) / 32) * 4;
    const std::size_t colours = header + stride * static_cast<std::size_t>(h);
    if (colours > data.size())
        return false;
    const bool has_mask = colours + mask_stride * static_cast<std::size_t>(h) <= data.size();
    rgba->assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
    for (int y = 0; y < h; ++y)
    {
        const std::size_t row = header + stride * static_cast<std::size_t>(h - 1 - y);
        for (int x = 0; x < w; ++x)
        {
            const std::size_t p =
                row + static_cast<std::size_t>(x) * static_cast<std::size_t>(bits / 8);
            const std::size_t o = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                   static_cast<std::size_t>(x)) *
                                  4;
            (*rgba)[o] = static_cast<std::uint8_t>(data[p + 2]);
            (*rgba)[o + 1] = static_cast<std::uint8_t>(data[p + 1]);
            (*rgba)[o + 2] = static_cast<std::uint8_t>(data[p]);
            std::uint8_t alpha = bits == 32 ? static_cast<std::uint8_t>(data[p + 3]) : 255;
            if (bits == 24 && has_mask)
            {
                const std::size_t m = colours + mask_stride * static_cast<std::size_t>(h - 1 - y) +
                                      static_cast<std::size_t>(x / 8);
                if ((static_cast<unsigned char>(data[m]) >> (7 - x % 8) & 1) != 0)
                    alpha = 0;
            }
            (*rgba)[o + 3] = alpha;
        }
    }
    *width = w;
    *height = h;
    return true;
}

} // namespace

CoverArt::~CoverArt()
{
    shutdown();
}

void CoverArt::init(Fetch fetch, Upload upload, Release release, std::string cache_dir,
                    double min_interval, ArtSource source)
{
    source_ = source;
    fetch_ = std::move(fetch);
    upload_ = std::move(upload);
    release_ = std::move(release);
    cache_dir_ = std::move(cache_dir);
    min_interval_ = min_interval;
    if (!cache_dir_.empty())
        save::ensure_directory(cache_dir_);
    stop_ = false;
    thread_ = std::thread([this] { worker(); });
    started_ = true;
}

void CoverArt::shutdown()
{
    if (started_)
    {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
        if (thread_.joinable())
            thread_.join();
        started_ = false;
    }
    for (auto &[key, entry] : entries_)
        if (entry.texture != 0 && release_)
            release_(entry.texture);
    entries_.clear();
}

std::string CoverArt::key(const std::string &song)
{
    std::string artist;
    std::string title;
    if (!split_song(song, &artist, &title))
        return {};
    return fold(first_artist(artist)) + "|" + fold(title);
}

bool CoverArt::split_song(const std::string &song, std::string *artist, std::string *title)
{
    const std::size_t dash = song.find(" - ");
    if (dash == std::string::npos)
        return false;
    *artist = trim(song.substr(0, dash));
    *title = strip_brackets(trim(song.substr(dash + 3)));
    // A second " - " is usually a mix name or a label: keep the first part.
    const std::size_t again = title->find(" - ");
    if (again != std::string::npos)
        *title = trim(title->substr(0, again));
    if (artist->empty() || title->empty() || fold(*artist).empty() || fold(*title).empty())
        return false;
    // Station talk dressed up as a song.
    static constexpr const char *kNotSongs[] = {"advert",  "commercial", "jingle", "station id",
                                                "unknown", "live set",   "news",   "weather"};
    const std::string low = lower_ascii(*artist + " " + *title);
    for (const char *word : kNotSongs)
        if (low.find(word) != std::string::npos)
            return false;
    return true;
}

std::string CoverArt::search_url(const std::string &artist, const std::string &title)
{
    const std::string query = "recording:(" + search_words(title) + ") AND artist:(" +
                              search_words(first_artist(artist)) + ")";
    return "https://musicbrainz.org/ws/2/recording/?fmt=json&limit=8&query=" + url_encode(query);
}

std::vector<std::string> CoverArt::cover_urls(const std::string &json, const std::string &artist,
                                              const std::string &title)
{
    std::vector<std::string> urls;
    Json root;
    if (!Json::parse(json, &root))
        return urls;
    const std::string want_artist = fold(first_artist(artist));
    const std::string want_title = fold(title);
    std::vector<std::string> groups;
    std::vector<std::string> releases;
    for (const Json &recording : root["recordings"].items())
    {
        if (recording["score"].number() < kMinScore)
            continue;
        if (!contains_either(fold(strip_brackets(recording["title"].str())), want_title))
            continue;
        bool artist_ok = false;
        for (const Json &credit : recording["artist-credit"].items())
        {
            if (contains_either(fold(credit["name"].str()), want_artist) ||
                contains_either(fold(credit["artist"]["name"].str()), want_artist))
                artist_ok = true;
        }
        if (!artist_ok)
            continue;
        // Official releases first; compilations last (their covers rarely
        // belong to the song).
        for (int pass = 0; pass < 2; ++pass)
        {
            for (const Json &release : recording["releases"].items())
            {
                const bool official = release["status"].str() == "Official";
                const Json &group = release["release-group"];
                const bool compilation = group["secondary-types"].size() > 0 &&
                                         group["secondary-types"].at(0).str() == "Compilation";
                const bool good = official && !compilation;
                if ((pass == 0) != good)
                    continue;
                const std::string &group_id = group["id"].str();
                if (!group_id.empty() &&
                    std::find(groups.begin(), groups.end(), group_id) == groups.end())
                    groups.push_back(group_id);
                const std::string &release_id = release["id"].str();
                if (!release_id.empty() &&
                    std::find(releases.begin(), releases.end(), release_id) == releases.end())
                    releases.push_back(release_id);
            }
        }
    }
    for (const std::string &id : groups)
        if (urls.size() < 3)
            urls.push_back("https://coverartarchive.org/release-group/" + id + "/front-250");
    for (const std::string &id : releases)
        if (urls.size() < 4)
            urls.push_back("https://coverartarchive.org/release/" + id + "/front-250");
    return urls;
}

bool CoverArt::decode(const std::string &bytes, ArtImage *out)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    std::vector<std::uint8_t> icon;
    stbi_uc *pixels = nullptr;
    if (ico_to_rgba(bytes, &icon, &width, &height))
    {
        // stb_image does not read icons: the RGBA comes from ico_to_rgba,
        // copied into memory stbi_image_free can release.
        pixels = static_cast<stbi_uc *>(std::malloc(icon.size()));
        if (pixels != nullptr)
            std::memcpy(pixels, icon.data(), icon.size());
    }
    else
    {
        pixels =
            stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(bytes.data()),
                                  static_cast<int>(bytes.size()), &width, &height, &channels, 4);
    }
    if (pixels == nullptr || width <= 0 || height <= 0)
    {
        if (pixels != nullptr)
            stbi_image_free(pixels);
        return false;
    }
    // Box-filter down to at most kMaxSide a side.
    const int factor = std::max(1, (std::max(width, height) + kMaxSide - 1) / kMaxSide);
    out->width = std::max(1, width / factor);
    out->height = std::max(1, height / factor);
    out->rgba.assign(
        static_cast<std::size_t>(out->width) * static_cast<std::size_t>(out->height) * 4, 0);
    for (int y = 0; y < out->height; ++y)
    {
        for (int x = 0; x < out->width; ++x)
        {
            unsigned sum[4] = {0, 0, 0, 0};
            for (int dy = 0; dy < factor; ++dy)
            {
                for (int dx = 0; dx < factor; ++dx)
                {
                    const std::size_t i = (static_cast<std::size_t>(y * factor + dy) *
                                               static_cast<std::size_t>(width) +
                                           static_cast<std::size_t>(x * factor + dx)) *
                                          4;
                    for (int c = 0; c < 4; ++c)
                        sum[c] += pixels[i + static_cast<std::size_t>(c)];
                }
            }
            const auto n = static_cast<unsigned>(factor * factor);
            const std::size_t o =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(out->width) +
                 static_cast<std::size_t>(x)) *
                4;
            for (int c = 0; c < 4; ++c)
                out->rgba[o + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(sum[c] / n);
        }
    }
    stbi_image_free(pixels);
    colours(out);
    return true;
}

// ---- the game thread ----

std::string CoverArt::key_for(const std::string &what) const
{
    if (source_ == ArtSource::songs)
        return key(what);
    const bool web = what.rfind("http://", 0) == 0 || what.rfind("https://", 0) == 0 ||
                     what.rfind("file://", 0) == 0; // a cover inside a song on a drive
    return web ? "img|" + what : std::string();
}

void CoverArt::request(const std::string &song)
{
    const std::string k = key_for(song);
    if (k.empty() || !started_)
        return;
    const std::lock_guard<std::mutex> lock(mutex_);
    if (entries_.count(k) != 0)
        return;
    Entry &entry = entries_[k];
    entry.used = ++tick_;
    // Only the newest songs matter: an old request still waiting is dropped.
    // Logos are asked for a screenful at a time, so more of them wait.
    const std::size_t limit = source_ == ArtSource::songs ? 2 : kMaxLogoQueue;
    while (queue_.size() >= limit)
    {
        entries_.erase(queue_.front().first);
        queue_.pop_front();
    }
    queue_.emplace_back(k, song);
    wake_.notify_one();
}

ArtView CoverArt::view(const std::string &song) const
{
    ArtView view;
    const std::string k = key_for(song);
    if (k.empty())
        return view;
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = entries_.find(k);
    if (it == entries_.end())
        return view;
    Entry &entry = it->second;
    entry.used = ++tick_; // shown counts as used (for the eviction in pump)
    view.state = entry.texture != 0               ? ArtState::ready
                 : entry.state == ArtState::ready ? ArtState::searching
                                                  : entry.state;
    view.texture = entry.texture;
    view.dominant = entry.dominant;
    view.vivid = entry.vivid;
    view.age = entry.age;
    view.width = entry.width;
    view.height = entry.height;
    return view;
}

void CoverArt::pump(float dt)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    std::size_t textures = 0;
    for (auto &[key, entry] : entries_)
    {
        if (entry.has_pending)
        {
            entry.has_pending = false;
            if (upload_)
                entry.texture =
                    upload_(entry.pending.width, entry.pending.height, entry.pending.rgba.data());
            entry.width = entry.pending.width;
            entry.height = entry.pending.height;
            entry.dominant = entry.pending.dominant;
            entry.vivid = entry.pending.vivid;
            entry.pending = ArtImage{};
            entry.age = 0.0f;
            if (entry.texture == 0)
                entry.state = ArtState::missing;
        }
        if (entry.texture != 0)
        {
            entry.age += dt;
            ++textures;
        }
    }
    while (textures > (source_ == ArtSource::songs ? kMaxTextures : kMaxLogoTextures))
    {
        auto oldest = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it)
            if (it->second.texture != 0 &&
                (oldest == entries_.end() || it->second.used < oldest->second.used))
                oldest = it;
        if (oldest == entries_.end())
            break;
        if (release_)
            release_(oldest->second.texture);
        entries_.erase(oldest);
        --textures;
    }
}

// ---- the worker ----

void CoverArt::worker()
{
    while (true)
    {
        std::pair<std::string, std::string> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stop_.load() || !queue_.empty(); });
            if (stop_)
                return;
            job = queue_.back();
            queue_.pop_back();
        }
        look_up(job.first, job.second);
    }
}

std::string CoverArt::cache_path(const std::string &k, const char *extension) const
{
    return cache_dir_ + "/" + hex_hash(k) + extension;
}

void CoverArt::remember(const std::string &k)
{
    // A list of what is cached, oldest first; past the limit the oldest
    // quarter is deleted.
    const std::string list_path = cache_dir_ + "/cached.txt";
    std::string list;
    save::read_file(list_path, &list, 1u << 20);
    list += hex_hash(k) + "\n";
    std::vector<std::string> names;
    std::size_t start = 0;
    while (start < list.size())
    {
        const std::size_t end = list.find('\n', start);
        names.push_back(list.substr(start, end - start));
        start = end == std::string::npos ? list.size() : end + 1;
    }
    if (names.size() > kMaxCached)
    {
        const std::size_t drop = kMaxCached / 4;
        for (std::size_t i = 0; i < drop; ++i)
        {
            std::remove((cache_dir_ + "/" + names[i] + ".img").c_str());
            std::remove((cache_dir_ + "/" + names[i] + ".none").c_str());
        }
        names.erase(names.begin(), names.begin() + static_cast<std::ptrdiff_t>(drop));
        list.clear();
        for (const std::string &name : names)
            list += name + "\n";
    }
    save::write_atomic(list_path, list);
}

FetchResult CoverArt::fetch_rate_limited(const std::string &url, std::string *body)
{
    const double wait = last_request_ + min_interval_ - now_seconds();
    if (wait > 0.0)
        std::this_thread::sleep_for(std::chrono::duration<double>(wait));
    last_request_ = now_seconds();
    return fetch_(url, body, stop_);
}

void CoverArt::finish(const std::string &k, ArtState state, ArtImage *image)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = entries_.find(k);
    if (it == entries_.end())
        return; // dropped while it was looked up
    it->second.state = state;
    if (state == ArtState::ready && image != nullptr)
    {
        it->second.pending = std::move(*image);
        it->second.has_pending = true;
    }
}

void CoverArt::fetch_image(const std::string &k, const std::string &url)
{
    const bool cache = !cache_dir_.empty();
    std::string bytes;
    ArtImage image;
    const auto usable = [&](const std::string &data)
    { return decode(data, &image) && std::min(image.width, image.height) >= kMinLogo; };
    if (cache)
    {
        if (save::read_file(cache_path(k, ".img"), &bytes, 4u << 20) && usable(bytes))
        {
            finish(k, ArtState::ready, &image);
            return;
        }
        std::string none;
        if (save::read_file(cache_path(k, ".none"), &none, 64))
        {
            finish(k, ArtState::missing, nullptr);
            return;
        }
    }
    bytes.clear();
    const FetchResult result = fetch_ ? fetch_(url, &bytes, stop_) : FetchResult::failed;
    if (result == FetchResult::ok && usable(bytes))
    {
        if (cache && save::write_atomic(cache_path(k, ".img"), bytes).empty())
            remember(k);
        finish(k, ArtState::ready, &image);
        return;
    }
    // An answer that is not a picture we can show (an SVG, a 16-pixel
    // favicon) will not become one: remembered, unlike a network failure.
    if (cache && result != FetchResult::failed &&
        save::write_atomic(cache_path(k, ".none"), "none").empty())
        remember(k);
    finish(k, ArtState::missing, nullptr);
}

void CoverArt::look_up(const std::string &k, const std::string &song)
{
    if (source_ == ArtSource::images)
    {
        fetch_image(k, song);
        return;
    }
    std::string artist;
    std::string title;
    if (!split_song(song, &artist, &title) || !fetch_)
    {
        finish(k, ArtState::missing, nullptr);
        return;
    }
    const bool cache = !cache_dir_.empty();
    std::string bytes;
    ArtImage image;
    if (cache)
    {
        if (save::read_file(cache_path(k, ".img"), &bytes, 4u << 20) && decode(bytes, &image))
        {
            finish(k, ArtState::ready, &image);
            return;
        }
        std::string none;
        if (save::read_file(cache_path(k, ".none"), &none, 64))
        {
            finish(k, ArtState::missing, nullptr);
            return;
        }
    }

    std::string answer;
    if (fetch_rate_limited(search_url(artist, title), &answer) != FetchResult::ok)
    {
        finish(k, ArtState::missing, nullptr); // not remembered: maybe next time
        return;
    }
    const std::vector<std::string> urls = cover_urls(answer, artist, title);
    bool definite = true; // every miss was a clear "no cover"
    for (const std::string &url : urls)
    {
        if (stop_)
            return;
        bytes.clear();
        const FetchResult result = fetch_(url, &bytes, stop_);
        if (result == FetchResult::ok && decode(bytes, &image))
        {
            if (cache && save::write_atomic(cache_path(k, ".img"), bytes).empty())
                remember(k);
            finish(k, ArtState::ready, &image);
            return;
        }
        if (result == FetchResult::failed)
            definite = false;
    }
    if (cache && definite && save::write_atomic(cache_path(k, ".none"), "none").empty())
        remember(k);
    finish(k, ArtState::missing, nullptr);
}

} // namespace hui::net
