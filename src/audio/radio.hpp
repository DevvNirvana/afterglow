// Afterglow - Internet radio and the player: stations, playback, favourites, checks.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A worker thread opens the station (net/http.hpp), splits off the ICY song
// titles (audio/icy.hpp), decodes MP3 or AAC as it arrives
// (audio/stream_decoder.hpp) and writes 48 kHz audio into a ring that the
// mixer plays on the music bus, so the visualiser hears it like any song.
// The game thread (pump) only fades the radio and the built-in songs in and
// out: mixer commands must come from one thread. A station that drops out is
// reconnected a few times before the player gives up with an error.
//
// The player is also the jukebox the screen talks to: play, pause, stop,
// next and previous work for a station (within the list it was picked from)
// and for the built-in songs alike. Favourites, recently played stations,
// the results of the station check, the songs heard and the songs liked are
// kept in one small text file; the liked songs are also written as plain
// text next to it (liked-songs.txt), to read on a PC.

#pragma once

#include "audio/mixer.hpp"
#include "audio/stream_ring.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace hui::net
{
struct OpenResult;
}

namespace hui::audio
{

class MusicPlayer;

struct Station
{
    std::string category; // the channel group: "Techno", "Hip-Hop & Rap", ...
    std::string name;
    std::string style; // sub-genres, free text ("Minimal / Hard Techno")
    std::string country;
    std::string format; // as listed: "MP3 128", "AAC 64"
    std::string url;
};

// One station per line: "Category | Name | Style | Country | Format | URL",
// optionally followed by "| Logo URL". Blank lines and lines starting with
// '#' are ignored, as are lines without six or seven fields or without an
// http(s) URL.
std::vector<Station> parse_stations(const std::string &text);
// The logos of the same list: stream address to picture address.
std::map<std::string, std::string> parse_logos(const std::string &text);

// The first stream address in a .pls or .m3u playlist, or "".
std::string playlist_target(const std::string &text);

enum class Source : std::uint8_t
{
    songs, // the built-in songs
    radio,
};

enum class RadioState : std::uint8_t
{
    off,        // no station chosen
    connecting, // opening the stream
    buffering,  // connected, filling the buffer
    playing,
    paused,  // the listener paused; play reconnects
    stopped, // the listener stopped
    failed,  // gave up; message says why
};

enum class Health : std::uint8_t
{
    unknown, // not checked yet
    ok,      // the last check heard audio
    failed,  // the last check did not
};

struct PlayerStatus
{
    Source source = Source::songs;
    RadioState state = RadioState::off;
    bool songs_paused = false;
    int station = -1;
    std::string title;    // what plays now: the station's song title, or the song
    std::string message;  // how it is connected, or why it failed
    std::string format;   // what is being decoded: "MP3 44.1 kHz", "AAC+ 48 kHz"
    double elapsed = 0.0; // seconds on this station or song
};

// A song heard on a station (the history) or kept by the listener (liked).
struct SongEntry
{
    std::string title;     // as the station sent it, usually "Artist - Title"
    std::string station;   // the station's name at the time
    std::string url;       // the station's stream, to tune in again
    std::int64_t when = 0; // seconds since 1970 (0: unknown)
};

struct CheckProgress
{
    bool running = false;
    int done = 0;
    int total = 0;
    int ok = 0;
    int failed = 0;
};

// What a screen may do with the player. The app owns it and registers it
// with set_radio(); without one (PC preview, tests) radio() is null.
class RadioControl
{
  public:
    virtual ~RadioControl() = default;
    virtual const std::vector<Station> &stations() const = 0;
    // Plays a station; next and previous then move within queue (indices
    // into stations(); empty: all of them).
    virtual void play(int index, std::vector<int> queue = {}) = 0;
    virtual void play_songs() = 0;
    virtual void toggle_pause() = 0;
    virtual void stop() = 0;
    virtual void next() = 0;
    virtual void previous() = 0;
    virtual PlayerStatus status() const = 0;

    virtual bool favorite(int index) const = 0;
    virtual void set_favorite(int index, bool on) = 0;
    // Most recent first.
    virtual std::vector<int> recents() const = 0;
    virtual Health health(int index) const = 0;
    // Tries every station once, in the background.
    virtual void start_check() = 0;
    virtual void cancel_check() = 0;
    virtual CheckProgress check_progress() const = 0;

    // A screen's own preferences, kept in the same file (keys and values are
    // short words). "" when unset. The default keeps nothing.
    virtual std::string setting(const std::string &key) const
    {
        (void)key;
        return {};
    }
    virtual void set_setting(const std::string &key, const std::string &value)
    {
        (void)key;
        (void)value;
    }

    // Songs: the last ones heard on the radio and the ones the listener
    // liked, newest first. The defaults keep nothing.
    virtual std::vector<SongEntry> history() const
    {
        return {};
    }
    virtual std::vector<SongEntry> liked() const
    {
        return {};
    }
    virtual bool is_liked(const std::string &title) const
    {
        (void)title;
        return false;
    }
    // Keeps (or forgets) a song; false when nothing changed.
    virtual bool set_liked(const SongEntry &song, bool on)
    {
        (void)song;
        (void)on;
        return false;
    }
    // The address of a station's logo, or "".
    virtual std::string logo(int index) const
    {
        (void)index;
        return {};
    }
    // The station playing a stream address, or -1.
    int station_of(const std::string &url) const
    {
        const auto &list = stations();
        for (std::size_t i = 0; i < list.size(); ++i)
            if (list[i].url == url)
                return static_cast<int>(i);
        return -1;
    }
};

// What the history keeps, and the most liked songs kept.
inline constexpr std::size_t kMaxHistory = 50;
inline constexpr std::size_t kMaxLiked = 500;

RadioControl *radio();
void set_radio(RadioControl *control);

class RadioPlayer final : public RadioControl
{
  public:
    using Opener =
        std::function<net::OpenResult(const std::string &url, const std::atomic<bool> &cancel)>;

    RadioPlayer();
    ~RadioPlayer() override;
    RadioPlayer(const RadioPlayer &) = delete;
    RadioPlayer &operator=(const RadioPlayer &) = delete;

    // Attaches the radio's two stream slots, slot and slot + 1 (call before
    // the audio thread starts): a new station starts on the other deck and
    // the two cross-fade once it sounds. music is faded out while a station
    // plays and is what play_songs() and the transport drive when the songs
    // are the source.
    void init(Mixer &mixer, std::size_t slot, MusicPlayer *music, Opener opener);
    void set_stations(std::vector<Station> stations);
    void set_logos(std::map<std::string, std::string> logos);
    // The songs found on the drives (category kLibraryCategory), replacing
    // the last ones found; the station playing keeps playing. Game thread.
    void set_library(std::vector<Station> songs, std::map<std::string, std::string> covers);
    std::string logo(int index) const override;
    // Where favourites, recents and check results are kept ("" for nowhere).
    void set_store(std::string path);

    const std::vector<Station> &stations() const override
    {
        return stations_;
    }
    void play(int index, std::vector<int> queue = {}) override;
    void play_songs() override;
    void toggle_pause() override;
    void stop() override;
    void next() override;
    void previous() override;
    PlayerStatus status() const override;

    bool favorite(int index) const override;
    void set_favorite(int index, bool on) override;
    std::vector<int> recents() const override;
    Health health(int index) const override;
    void start_check() override;
    void cancel_check() override;
    CheckProgress check_progress() const override;
    std::string setting(const std::string &key) const override;
    void set_setting(const std::string &key, const std::string &value) override;
    std::vector<SongEntry> history() const override;
    std::vector<SongEntry> liked() const override;
    bool is_liked(const std::string &title) const override;
    bool set_liked(const SongEntry &song, bool on) override;

    // Where the liked songs are also written as plain text, for reading on
    // a PC ("" for nowhere; set_store() puts it next to the store).
    const std::string &liked_export() const
    {
        return liked_export_;
    }
    static std::string export_text(const std::vector<SongEntry> &songs);

    // Game thread, once a frame: fades, notices a failed worker, saves.
    void pump(float dt);

    // Whether data looks like a playable stream: a codec this build decodes
    // and some audio out of it. Used by the station check (and tests).
    static bool sounds_like_audio(const std::string &content_type, const std::uint8_t *data,
                                  std::size_t size, std::string *why);

    // How long a station change cross-fades, from the "xfade" setting
    // (seconds; 0 cuts straight over). Three seconds by default.
    float crossfade_seconds() const;
    // Whether the previous station is still fading out (tests).
    bool fading() const
    {
        return fading_ >= 0;
    }

  private:
    // A deck: one station's worker, writing into its own ring, playing on
    // its own mixer slot.
    struct Deck
    {
        std::thread thread;
        std::atomic<bool> cancel{false};
        std::atomic<bool> sounding{false}; // enough audio buffered to be heard
        std::atomic<bool> ended{false};    // a song from a drive reached its end
    };
    void join();
    void stop_deck(int deck);
    void start_worker();
    void worker(std::string url, int deck, std::uint64_t generation);
    void set_state(RadioState state, const std::string &message);
    // As set_state, but only from the worker of the station now chosen.
    bool set_state_if(std::uint64_t generation, RadioState state, const std::string &message);
    void check_worker();
    int find(const std::string &url) const;
    void load();
    void save();

    Mixer *mixer_ = nullptr;
    std::size_t slot_ = 1;
    MusicPlayer *music_ = nullptr;
    Opener opener_;
    std::vector<Station> stations_;
    std::map<std::string, std::string> logos_; // stream address to logo address

    // Two decks, each with its ring: a new station starts on the other one,
    // so nothing buffered from the old station plays after the switch, and
    // the old one can fade out under the new one.
    StreamRing rings_[2]{StreamRing(1u << 17), StreamRing(1u << 17)};
    Deck decks_[2];
    int deck_ = 0;                 // the deck of the station chosen
    int fading_ = -1;              // the deck fading out, or -1 (game thread)
    float fade_wait_ = 0.0f;       // seconds the fade has waited for the new deck
    float fade_left_ = -1.0f;      // seconds left of a fade under way, or -1
    bool deck_up_ = false;         // the chosen deck's gain is up (game thread)
    std::uint64_t generation_ = 0; // which worker may write the status (mutex_)

    mutable std::mutex mutex_;
    PlayerStatus status_;
    std::vector<int> queue_;
    std::chrono::steady_clock::time_point started_{};
    std::string song_; // the song last seen playing (game thread)

    // Library: by URL, so an edited station list keeps them.
    std::set<std::string> favorites_;
    std::vector<std::string> recents_;
    std::map<std::string, Health> health_;
    std::map<std::string, std::string> settings_;
    std::vector<SongEntry> history_;
    std::vector<SongEntry> liked_;
    bool liked_dirty_ = false;
    std::string heard_; // the last song added to the history (game thread)
    std::string store_;
    std::string liked_export_;
    bool dirty_ = false;
    float save_timer_ = 0.0f;

    std::thread check_thread_;
    std::atomic<bool> check_cancel_{false};
    std::atomic<int> check_next_{0};
    CheckProgress check_;

    float pending_fade_ = 0.0f; // the length of the cross-fade under way
    bool songs_muted_ = false;
};

} // namespace hui::audio
