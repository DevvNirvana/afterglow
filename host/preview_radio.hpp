// Afterglow - A stand-in radio and album art for the PC preview.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The preview has no network and no mixer, so pictures of the radio would
// only ever show it silent. PreviewRadio plays nothing but reports what a
// playing station reports (state, song title, time), and the art fetcher
// answers from canned data: an invented MusicBrainz answer and invented
// covers from tests/unit/data. The songs and artists are made up.

#pragma once

#include "audio/radio.hpp"
#include "core/save_file.hpp"
#include "net/cover_art.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace hui::host
{

struct PreviewSong
{
    const char *song; // "Artist - Title", as a station sends it
    const char *group;
};

inline constexpr PreviewSong kPreviewSongs[] = {
    {"Nova Kessler - Concrete Halls", "aaaaaaaa-0000-4000-8000-000000000001"},
    {"Mira Sol - Glasshouse (Extended Mix)", "bbbbbbbb-0000-4000-8000-000000000002"},
    {"The Paper Moons - Night Drive 86", "cccccccc-0000-4000-8000-000000000003"},
    // A long one, to see titles wrap and shrink.
    {"Nova Kessler & The Paper Moons feat. Mira Sol - Concrete Halls of the Midnight Sun (Sunrise "
     "Extended Club Mix)",
     "dddddddd-0000-4000-8000-000000000004"},
};

class PreviewRadio final : public audio::RadioControl
{
  public:
    explicit PreviewRadio(std::vector<audio::Station> stations) : stations_(std::move(stations))
    {
        // A third of the list "answered the last check".
        for (std::size_t i = 0; i < stations_.size(); i += 3)
            ok_.insert(static_cast<int>(i));
    }

    const std::vector<audio::Station> &stations() const override
    {
        return stations_;
    }
    void play(int index, std::vector<int> queue = {}) override
    {
        if (index < 0 || index >= static_cast<int>(stations_.size()))
            return;
        queue_ = std::move(queue);
        status_.source = audio::Source::radio;
        status_.state = audio::RadioState::playing;
        status_.station = index;
        status_.elapsed = 0.0;
        status_.format = "MP3 44.1 kHz stereo";
        status_.title =
            kPreviewSongs[static_cast<std::size_t>(index) % std::size(kPreviewSongs)].song;
        recents_.insert(recents_.begin(), index);
        audio::SongEntry heard;
        heard.title = status_.title;
        heard.station = stations_[static_cast<std::size_t>(index)].name;
        heard.url = stations_[static_cast<std::size_t>(index)].url;
        heard.when = 1792000000 + static_cast<std::int64_t>(history_.size()) * 240;
        if (history_.empty() || history_.front().title != heard.title)
            history_.insert(history_.begin(), heard);
    }
    std::vector<audio::SongEntry> history() const override
    {
        return history_;
    }
    std::vector<audio::SongEntry> liked() const override
    {
        return liked_;
    }
    bool is_liked(const std::string &title) const override
    {
        return std::any_of(liked_.begin(), liked_.end(),
                           [&](const audio::SongEntry &s) { return s.title == title; });
    }
    bool set_liked(const audio::SongEntry &song, bool on) override
    {
        if (song.title.empty() || is_liked(song.title) == on)
            return false;
        if (on)
            liked_.insert(liked_.begin(), song);
        else
            liked_.erase(std::remove_if(liked_.begin(), liked_.end(), [&](const audio::SongEntry &s)
                                        { return s.title == song.title; }),
                         liked_.end());
        return true;
    }
    // Every other station has a logo (an invented picture, see snapshot_main).
    std::string logo(int index) const override
    {
        return index >= 0 && index % 2 == 0 ? "https://logo.example/" + std::to_string(index)
                                            : std::string();
    }
    void play_songs() override
    {
        status_ = audio::PlayerStatus{};
    }
    void toggle_pause() override
    {
        if (status_.source == audio::Source::radio)
            status_.state = status_.state == audio::RadioState::playing
                                ? audio::RadioState::paused
                                : audio::RadioState::playing;
        else
            status_.songs_paused = !status_.songs_paused;
    }
    void stop() override
    {
        status_.state = audio::RadioState::stopped;
    }
    void next() override
    {
        step(1);
    }
    void previous() override
    {
        step(-1);
    }
    audio::PlayerStatus status() const override
    {
        return status_;
    }
    bool favorite(int index) const override
    {
        return favorites_.count(index) != 0;
    }
    void set_favorite(int index, bool on) override
    {
        if (on)
            favorites_.insert(index);
        else
            favorites_.erase(index);
    }
    std::vector<int> recents() const override
    {
        return recents_;
    }
    audio::Health health(int index) const override
    {
        return ok_.count(index) != 0 ? audio::Health::ok : audio::Health::unknown;
    }
    void start_check() override
    {
    }
    void cancel_check() override
    {
    }
    audio::CheckProgress check_progress() const override
    {
        return {};
    }

    std::string setting(const std::string &key) const override
    {
        const auto it = settings_.find(key);
        return it == settings_.end() ? std::string() : it->second;
    }
    void set_setting(const std::string &key, const std::string &value) override
    {
        settings_[key] = value;
    }

    void tick(float dt)
    {
        if (status_.state == audio::RadioState::playing)
            status_.elapsed += static_cast<double>(dt);
    }

  private:
    void step(int direction)
    {
        if (status_.source != audio::Source::radio)
            return;
        std::vector<int> order = queue_;
        if (order.empty())
            for (std::size_t i = 0; i < stations_.size(); ++i)
                order.push_back(static_cast<int>(i));
        auto it = std::find(order.begin(), order.end(), status_.station);
        int at = it == order.end() ? 0 : static_cast<int>(it - order.begin());
        at = (at + direction + static_cast<int>(order.size())) % static_cast<int>(order.size());
        const std::vector<int> keep = queue_;
        play(order[static_cast<std::size_t>(at)], keep);
    }

    std::vector<audio::Station> stations_;
    audio::PlayerStatus status_;
    std::vector<int> queue_;
    std::vector<int> recents_;
    std::set<int> favorites_;
    std::set<int> ok_;
    std::map<std::string, std::string> settings_;
    std::vector<audio::SongEntry> history_;
    std::vector<audio::SongEntry> liked_;
};

// Answers like MusicBrainz and the Cover Art Archive would, from files.
inline net::FetchResult preview_fetch(const std::string &root, const std::string &url,
                                      std::string *body)
{
    if (url.find("musicbrainz.org") != std::string::npos)
    {
        *body = R"({"recordings":[)";
        bool first = true;
        for (const PreviewSong &song : kPreviewSongs)
        {
            std::string artist;
            std::string title;
            net::CoverArt::split_song(song.song, &artist, &title);
            *body += std::string(first ? "" : ",") + R"({"score":100,"title":")" + title +
                     R"(","artist-credit":[{"name":")" + artist + R"("}],"releases":[{"id":"r-)" +
                     song.group + R"(","status":"Official","release-group":{"id":")" + song.group +
                     R"("}}]})";
            first = false;
        }
        *body += "]}";
        return net::FetchResult::ok;
    }
    const char *files[] = {"cover-a.jpg", "cover-b.jpg", "cover-c.jpg", "cover-a.jpg"};
    for (std::size_t i = 0; i < std::size(kPreviewSongs); ++i)
        if (url.find(kPreviewSongs[i].group) != std::string::npos)
            return save::read_file(root + "/tests/unit/data/" + files[i], body)
                       ? net::FetchResult::ok
                       : net::FetchResult::failed;
    return net::FetchResult::not_found;
}

} // namespace hui::host
