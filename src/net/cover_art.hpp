// Afterglow - Album art for what the radio plays.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stations send "Artist - Title" between the audio (ICY). This looks the
// song up in MusicBrainz (the open music encyclopedia) and fetches the front
// cover of a matching release from the Cover Art Archive, the same free
// sources CD players and media centres use. A worker thread does the network
// and the JPEG/PNG decoding; the game thread uploads finished pictures as
// textures (pump) and asks for them by song (view).
//
// Manners: MusicBrainz asks for at most one request a second and a
// User-Agent that says who is asking; both are kept. Pictures are cached on
// disk, so a song heard again costs nothing, and songs without art are
// remembered too. A cover is only used when both the artist and the title
// match, so a wrong cover is unlikely; anything unclear shows the station's
// generated cover instead.

#pragma once

#include "gfx/draw_list.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace hui::net
{

enum class ArtState : std::uint8_t
{
    none,      // nothing asked (or the song has no artist and title)
    searching, // being looked up
    ready,     // texture holds the cover
    missing,   // no cover found
};

struct ArtView
{
    ArtState state = ArtState::none;
    std::uint32_t texture = 0;
    gfx::Color dominant{0.0f, 0.0f, 0.0f, 0.0f}; // the cover's most common colour
    gfx::Color vivid{0.0f, 0.0f, 0.0f, 0.0f};    // its most striking colour
    float age = 0.0f;                            // seconds since it became ready
    int width = 0;                               // the picture's size, in pixels
    int height = 0;
};

// What a CoverArt looks up: album art for songs, or (for station logos)
// pictures by their own address.
enum class ArtSource : std::uint8_t
{
    songs,
    images,
};

enum class FetchResult : std::uint8_t
{
    ok,
    not_found, // the server answered 404: there is nothing there
    failed,    // anything else: try again another time
};

struct ArtImage
{
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    gfx::Color dominant;
    gfx::Color vivid;
};

class CoverArt
{
  public:
    using Fetch = std::function<FetchResult(const std::string &url, std::string *body,
                                            const std::atomic<bool> &cancel)>;
    using Upload = std::function<std::uint32_t(int width, int height, const std::uint8_t *rgba)>;
    using Release = std::function<void(std::uint32_t texture)>;

    CoverArt() = default;
    ~CoverArt();
    CoverArt(const CoverArt &) = delete;
    CoverArt &operator=(const CoverArt &) = delete;

    // cache_dir may be "" (no disk cache). min_interval: seconds between
    // MusicBrainz requests (1.1 by default; tests pass 0). With
    // ArtSource::images, request() and view() take a picture's address (a
    // station's logo): fetched as it is, more kept at once, small ones
    // (favicons) refused as too blurry to show.
    void init(Fetch fetch, Upload upload, Release release, std::string cache_dir,
              double min_interval = 1.1, ArtSource source = ArtSource::songs);
    void shutdown();

    // Game thread. Starts a lookup for song if it is new.
    void request(const std::string &song);
    ArtView view(const std::string &song) const;
    // Game thread, once a frame: uploads finished covers, ages them, frees
    // the least recently shown ones beyond a dozen.
    void pump(float dt);

    // ---- the pieces, public for tests ----
    // "Artist - Title" into its halves, cleaned for searching; false if it
    // does not look like a song (no separator, an empty half, an advert).
    static bool split_song(const std::string &song, std::string *artist, std::string *title);
    static std::string search_url(const std::string &artist, const std::string &title);
    // Cover Art Archive addresses for the recordings in a MusicBrainz search
    // answer that match artist and title, best first (at most four).
    static std::vector<std::string> cover_urls(const std::string &json, const std::string &artist,
                                               const std::string &title);
    // JPEG, PNG, GIF, BMP or ICO (PNG or 24/32-bit entries) to RGBA, at most
    // 256 pixels a side, with its colours.
    static bool decode(const std::string &bytes, ArtImage *out);
    static std::string key(const std::string &song);
    // The smallest logo worth showing, in pixels a side.
    static constexpr int kMinLogo = 48;

  private:
    struct Entry
    {
        ArtState state = ArtState::searching;
        std::uint32_t texture = 0;
        ArtImage pending; // decoded, waiting for the game thread
        bool has_pending = false;
        gfx::Color dominant;
        gfx::Color vivid;
        float age = 0.0f;
        int width = 0;
        int height = 0;
        std::uint64_t used = 0;
    };

    std::string key_for(const std::string &what) const;
    void fetch_image(const std::string &key, const std::string &url);
    void worker();
    void look_up(const std::string &key, const std::string &song);
    void finish(const std::string &key, ArtState state, ArtImage *image);
    FetchResult fetch_rate_limited(const std::string &url, std::string *body);
    std::string cache_path(const std::string &key, const char *extension) const;
    void remember(const std::string &key);

    Fetch fetch_;
    Upload upload_;
    Release release_;
    std::string cache_dir_;
    double min_interval_ = 1.1;
    ArtSource source_ = ArtSource::songs;
    double last_request_ = -1e9;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::pair<std::string, std::string>> queue_; // key, song
    mutable std::map<std::string, Entry> entries_;          // view() marks what it shows
    mutable std::uint64_t tick_ = 0;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    bool started_ = false;
};

CoverArt *cover_art();
void set_cover_art(CoverArt *art);
// The station logos (an ArtSource::images instance), or null.
CoverArt *station_logos();
void set_station_logos(CoverArt *logos);

} // namespace hui::net
