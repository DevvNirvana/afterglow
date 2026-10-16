// Afterglow - Internet radio and the player (see radio.hpp).
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/radio.hpp"

#include "audio/icy.hpp"
#include "audio/library.hpp"
#include "audio/music.hpp"
#include "audio/stream_decoder.hpp"
#include "core/save_file.hpp"
#include "net/http.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace hui::audio
{

namespace
{

constexpr int kMaxAttempts = 4;     // connections in a row before giving up
constexpr int kMaxPlaylistHops = 2; // a playlist pointing at a playlist...
constexpr std::size_t kSniffBytes = 4096;
constexpr std::size_t kNotAudioBytes = 256 * 1024; // this much without a frame: not audio
constexpr std::size_t kStartFrames = kSampleRate;  // one second buffered before it sounds
constexpr float kRadioGain = 0.9f;
constexpr float kFadeSeconds = 0.6f;
constexpr float kDefaultCrossfade = 3.0f;
constexpr float kMaxCrossfade = 10.0f;
// How long the old station keeps playing while the new one connects; past
// this it fades out anyway (the new one is slow, or will not answer).
constexpr float kCrossfadeWait = 8.0f;
constexpr std::size_t kMaxRecents = 12;
constexpr float kSaveDelay = 2.0f; // seconds after a change before it is written
// The station check: how much it reads, and for how long at most.
constexpr std::size_t kCheckBytes = 48 * 1024;
constexpr int kCheckSeconds = 10;

RadioControl *g_radio = nullptr;

// Song lines in the store are "|"-separated and one line each: a title from
// the stream cannot be allowed to break them.
std::string one_line(const std::string &text)
{
    std::string out = text;
    for (char &c : out)
        if (c == '|' || c == '\n' || c == '\r')
            c = c == '|' ? '/' : ' ';
    return out;
}

SongEntry clean_song(const SongEntry &song)
{
    SongEntry out = song;
    out.title = one_line(song.title);
    out.station = one_line(song.station);
    out.url = one_line(song.url);
    const std::size_t first = out.title.find_first_not_of(' ');
    const std::size_t last = out.title.find_last_not_of(' ');
    out.title =
        first == std::string::npos ? std::string() : out.title.substr(first, last - first + 1);
    return out;
}

std::string song_line(const char *kind, const SongEntry &song)
{
    return std::string(kind) + "|" + std::to_string(song.when) + "|" + song.url + "|" +
           song.station + "|" + song.title + "\n";
}

// "when|url|station|title" back into a song; false if it is not one.
bool parse_song(const std::string &rest, SongEntry *song)
{
    std::size_t at = 0;
    std::string parts[3];
    for (std::string &part : parts)
    {
        const std::size_t bar = rest.find('|', at);
        if (bar == std::string::npos)
            return false;
        part = rest.substr(at, bar - at);
        at = bar + 1;
    }
    song->when = std::atoll(parts[0].c_str());
    song->url = parts[1];
    song->station = parts[2];
    song->title = rest.substr(at);
    return !song->title.empty();
}

std::string trim(const std::string &text)
{
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string lower(std::string text)
{
    for (char &c : text)
        c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return text;
}

bool starts_with_http(const std::string &text)
{
    const std::string head = lower(text.substr(0, 8));
    return head.rfind("http://", 0) == 0 || head.rfind("https://", 0) == 0;
}

bool looks_like_playlist(const std::string &head)
{
    const std::string start = lower(trim(head.substr(0, 64)));
    return start.rfind("[playlist]", 0) == 0 || start.rfind("#extm3u", 0) == 0 ||
           starts_with_http(start);
}

// Sleeps up to seconds, waking early when cancel is raised.
void nap(const std::atomic<bool> &cancel, double seconds)
{
    const auto until = std::chrono::steady_clock::now() +
                       std::chrono::milliseconds(static_cast<int>(seconds * 1000.0));
    while (!cancel.load(std::memory_order_relaxed) && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

std::string describe(const StreamDecoder &decoder)
{
    char text[48];
    std::snprintf(text, sizeof(text), "%s %.1f kHz%s", decoder.name(),
                  static_cast<double>(decoder.rate()) / 1000.0,
                  decoder.channels() == 1 ? " mono" : "");
    return text;
}

} // namespace

RadioControl *radio()
{
    return g_radio;
}

void set_radio(RadioControl *control)
{
    g_radio = control;
}

namespace
{

// The station lines of a list, split at '|' and trimmed (6 or 7 fields, an
// http(s) stream, a name and a category).
std::vector<std::vector<std::string>> station_lines(const std::string &text)
{
    std::vector<std::vector<std::string>> lines;
    std::size_t start = 0;
    while (start <= text.size())
    {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        const std::string line = trim(text.substr(start, end - start));
        start = end + 1;
        if (line.empty() || line[0] == '#')
            continue;
        std::vector<std::string> fields;
        std::size_t field_start = 0;
        for (;;)
        {
            const std::size_t bar = line.find('|', field_start);
            fields.push_back(trim(line.substr(
                field_start, bar == std::string::npos ? std::string::npos : bar - field_start)));
            if (bar == std::string::npos)
                break;
            field_start = bar + 1;
        }
        if ((fields.size() != 6 && fields.size() != 7) || !starts_with_http(fields[5]) ||
            fields[1].empty() || fields[0].empty())
            continue;
        lines.push_back(std::move(fields));
    }
    return lines;
}

} // namespace

std::vector<Station> parse_stations(const std::string &text)
{
    std::vector<Station> stations;
    for (const std::vector<std::string> &fields : station_lines(text))
    {
        Station station;
        station.category = fields[0];
        station.name = fields[1];
        station.style = fields[2];
        station.country = fields[3];
        station.format = fields[4];
        station.url = fields[5];
        stations.push_back(std::move(station));
    }
    return stations;
}

std::map<std::string, std::string> parse_logos(const std::string &text)
{
    std::map<std::string, std::string> logos;
    for (const std::vector<std::string> &fields : station_lines(text))
        if (fields.size() == 7 && starts_with_http(fields[6]))
            logos[fields[5]] = fields[6];
    return logos;
}

std::string playlist_target(const std::string &text)
{
    std::size_t start = 0;
    while (start < text.size())
    {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        std::string line = trim(text.substr(start, end - start));
        start = end + 1;
        // .pls: "File1=http://..."; .m3u: the address on a line of its own.
        const std::size_t equals = line.find('=');
        if (equals != std::string::npos && lower(line.substr(0, 4)) == "file")
            line = trim(line.substr(equals + 1));
        if (starts_with_http(line))
            return line;
    }
    return {};
}

// ---- RadioPlayer ----

RadioPlayer::RadioPlayer() : started_(std::chrono::steady_clock::now())
{
}

RadioPlayer::~RadioPlayer()
{
    cancel_check();
    join();
    if (g_radio == this)
        g_radio = nullptr;
}

void RadioPlayer::init(Mixer &mixer, std::size_t slot, MusicPlayer *music, Opener opener)
{
    mixer_ = &mixer;
    slot_ = slot;
    music_ = music;
    opener_ = std::move(opener);
    for (int d = 0; d < 2; ++d)
    {
        mixer.attach_stream(slot + static_cast<std::size_t>(d), &rings_[d]);
        mixer.set_stream_gain(slot + static_cast<std::size_t>(d), 0.0f, 0.0f);
    }
}

void RadioPlayer::set_stations(std::vector<Station> stations)
{
    stations_ = std::move(stations);
}

void RadioPlayer::set_logos(std::map<std::string, std::string> logos)
{
    logos_ = std::move(logos);
}

void RadioPlayer::set_library(std::vector<Station> songs, std::map<std::string, std::string> covers)
{
    // Stations are referred to by index elsewhere: carry the one playing and
    // the queue across by address.
    std::string playing;
    std::vector<std::string> queue;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (status_.station >= 0 && status_.station < static_cast<int>(stations_.size()))
            playing = stations_[static_cast<std::size_t>(status_.station)].url;
        for (int index : queue_)
            if (index >= 0 && index < static_cast<int>(stations_.size()))
                queue.push_back(stations_[static_cast<std::size_t>(index)].url);
    }
    stations_.erase(std::remove_if(stations_.begin(), stations_.end(),
                                   [](const Station &s) { return s.category == kLibraryCategory; }),
                    stations_.end());
    for (auto it = logos_.begin(); it != logos_.end();)
        it = it->first.rfind("file://", 0) == 0 ? logos_.erase(it) : std::next(it);
    for (Station &song : songs)
        stations_.push_back(std::move(song));
    for (auto &[url, cover] : covers)
        logos_[url] = cover;
    const std::lock_guard<std::mutex> lock(mutex_);
    status_.station = find(playing);
    queue_.clear();
    for (const std::string &url : queue)
        if (const int index = find(url); index >= 0)
            queue_.push_back(index);
}

std::string RadioPlayer::logo(int index) const
{
    if (index < 0 || index >= static_cast<int>(stations_.size()))
        return {};
    const auto it = logos_.find(stations_[static_cast<std::size_t>(index)].url);
    return it == logos_.end() ? std::string() : it->second;
}

void RadioPlayer::set_store(std::string path)
{
    store_ = std::move(path);
    const std::size_t slash = store_.find_last_of('/');
    liked_export_ = store_.empty()               ? std::string()
                    : slash == std::string::npos ? std::string("liked-songs.txt")
                                                 : store_.substr(0, slash + 1) + "liked-songs.txt";
    load();
}

// ---- songs ----

std::vector<SongEntry> RadioPlayer::history() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return history_;
}

std::vector<SongEntry> RadioPlayer::liked() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return liked_;
}

bool RadioPlayer::is_liked(const std::string &title) const
{
    if (title.empty())
        return false;
    const std::lock_guard<std::mutex> lock(mutex_);
    return std::any_of(liked_.begin(), liked_.end(),
                       [&](const SongEntry &song) { return song.title == title; });
}

bool RadioPlayer::set_liked(const SongEntry &song, bool on)
{
    const SongEntry clean = clean_song(song);
    if (clean.title.empty())
        return false;
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = std::find_if(liked_.begin(), liked_.end(),
                                    [&](const SongEntry &s) { return s.title == clean.title; });
    if (on == (found != liked_.end()))
        return false;
    if (on)
    {
        liked_.insert(liked_.begin(), clean);
        if (liked_.size() > kMaxLiked)
            liked_.resize(kMaxLiked);
    }
    else
    {
        liked_.erase(found);
    }
    dirty_ = true;
    liked_dirty_ = true;
    return true;
}

std::string RadioPlayer::export_text(const std::vector<SongEntry> &songs)
{
    std::string text = "Afterglow: songs you liked, newest first\n"
                       "Song  /  station  /  when (the console's clock)\n\n";
    for (const SongEntry &song : songs)
    {
        text += song.title;
        if (!song.station.empty())
            text += "  /  " + song.station;
        if (song.when > 0)
        {
            const auto when = static_cast<std::time_t>(song.when);
            char date[32] = "";
            if (const std::tm *local = std::localtime(&when))
                std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M", local);
            if (date[0] != '\0')
                text += "  /  " + std::string(date);
        }
        text += "\n";
    }
    return text;
}

void RadioPlayer::stop_deck(int deck)
{
    Deck &d = decks_[deck];
    d.cancel.store(true, std::memory_order_relaxed);
    if (d.thread.joinable())
        d.thread.join();
    d.cancel.store(false, std::memory_order_relaxed);
    d.sounding.store(false, std::memory_order_relaxed);
    d.ended.store(false, std::memory_order_relaxed);
    if (mixer_ != nullptr)
        mixer_->set_stream_gain(slot_ + static_cast<std::size_t>(deck), 0.0f, kFadeSeconds);
    if (deck == fading_)
    {
        fading_ = -1;
        fade_left_ = -1.0f;
    }
    if (deck == deck_)
        deck_up_ = false;
}

void RadioPlayer::join()
{
    stop_deck(0);
    stop_deck(1);
}

void RadioPlayer::set_state(RadioState state, const std::string &message)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    status_.state = state;
    status_.message = message;
}

bool RadioPlayer::set_state_if(std::uint64_t generation, RadioState state,
                               const std::string &message)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    if (generation != generation_)
        return false; // an old station, fading out: the status is the new one's
    status_.state = state;
    status_.message = message;
    return true;
}

float RadioPlayer::crossfade_seconds() const
{
    const std::string value = setting("xfade");
    if (value.empty())
        return kDefaultCrossfade;
    char *end = nullptr;
    const float seconds = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || !std::isfinite(seconds))
        return kDefaultCrossfade;
    return std::clamp(seconds, 0.0f, kMaxCrossfade);
}

PlayerStatus RadioPlayer::status() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    PlayerStatus status = status_;
    status.elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count();
    return status;
}

int RadioPlayer::find(const std::string &url) const
{
    for (std::size_t i = 0; i < stations_.size(); ++i)
        if (stations_[i].url == url)
            return static_cast<int>(i);
    return -1;
}

void RadioPlayer::play(int index, std::vector<int> queue)
{
    if (mixer_ == nullptr || !opener_ || index < 0 || index >= static_cast<int>(stations_.size()))
        return;
    if (queue.empty())
    {
        queue.resize(stations_.size());
        for (std::size_t i = 0; i < queue.size(); ++i)
            queue[i] = static_cast<int>(i);
    }
    if (std::find(queue.begin(), queue.end(), index) == queue.end())
        queue.push_back(index);
    const std::string &url = stations_[static_cast<std::size_t>(index)].url;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        queue_ = std::move(queue);
        status_.source = Source::radio;
        status_.station = index;
        recents_.erase(std::remove(recents_.begin(), recents_.end(), url), recents_.end());
        recents_.insert(recents_.begin(), url);
        if (recents_.size() > kMaxRecents)
            recents_.resize(kMaxRecents);
        dirty_ = true;
    }
    start_worker();
}

void RadioPlayer::start_worker()
{
    // A station that is sounding keeps playing on its deck while the new
    // one connects on the other, then the two cross-fade. Anything still
    // fading on that other deck goes now.
    const int old = deck_;
    const int next = old ^ 1;
    stop_deck(next);
    PlayerStatus before;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        before = status_;
    }
    const bool old_sounding = decks_[old].thread.joinable() &&
                              decks_[old].sounding.load(std::memory_order_relaxed) &&
                              before.state == RadioState::playing;
    const float fade = crossfade_seconds();
    if (old_sounding && fade > 0.0f)
    {
        fading_ = old;
        fade_wait_ = 0.0f;
        fade_left_ = -1.0f;
        pending_fade_ = fade;
    }
    else
    {
        stop_deck(old);
        pending_fade_ = 0.0f;
    }
    deck_ = next;
    deck_up_ = false;
    rings_[next].drain();
    std::string url;
    std::uint64_t generation = 0;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        generation = ++generation_;
        status_.state = RadioState::connecting;
        status_.message.clear();
        status_.title.clear();
        status_.format.clear();
        started_ = std::chrono::steady_clock::now();
        url = stations_[static_cast<std::size_t>(status_.station)].url;
    }
    decks_[next].thread = std::thread(&RadioPlayer::worker, this, url, next, generation);
}

void RadioPlayer::play_songs()
{
    join();
    const std::lock_guard<std::mutex> lock(mutex_);
    status_.source = Source::songs;
    status_.state = RadioState::off;
    status_.message.clear();
    status_.title = song_;
    status_.format.clear();
    status_.songs_paused = false;
    started_ = std::chrono::steady_clock::now();
    if (music_ != nullptr)
        music_->set_paused(false);
}

void RadioPlayer::toggle_pause()
{
    const PlayerStatus now = status();
    if (now.source == Source::songs)
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        status_.songs_paused = !status_.songs_paused;
        if (music_ != nullptr)
            music_->set_paused(status_.songs_paused);
        return;
    }
    if (now.state == RadioState::connecting || now.state == RadioState::buffering ||
        now.state == RadioState::playing)
    {
        join();
        set_state(RadioState::paused, "");
    }
    else if (now.station >= 0 && mixer_ != nullptr)
    {
        // Live radio does not wait: play picks the station up where it is now.
        start_worker();
    }
}

void RadioPlayer::stop()
{
    const PlayerStatus now = status();
    if (now.source == Source::songs)
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        status_.songs_paused = true;
        if (music_ != nullptr)
            music_->set_paused(true);
        return;
    }
    join();
    set_state(RadioState::stopped, "");
}

void RadioPlayer::next()
{
    const PlayerStatus now = status();
    if (now.source == Source::songs)
    {
        if (music_ != nullptr)
        {
            music_->skip(1);
            music_->set_paused(false);
        }
        const std::lock_guard<std::mutex> lock(mutex_);
        status_.songs_paused = false;
        return;
    }
    std::vector<int> queue;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        queue = queue_;
    }
    if (queue.empty())
        return;
    const auto at = std::find(queue.begin(), queue.end(), now.station);
    const auto index =
        at == queue.end() ? std::size_t{0} : static_cast<std::size_t>(at - queue.begin());
    play(queue[(index + 1) % queue.size()], queue);
}

void RadioPlayer::previous()
{
    const PlayerStatus now = status();
    if (now.source == Source::songs)
    {
        if (music_ != nullptr)
        {
            music_->skip(-1);
            music_->set_paused(false);
        }
        const std::lock_guard<std::mutex> lock(mutex_);
        status_.songs_paused = false;
        return;
    }
    std::vector<int> queue;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        queue = queue_;
    }
    if (queue.empty())
        return;
    const auto at = std::find(queue.begin(), queue.end(), now.station);
    const auto index =
        at == queue.end() ? std::size_t{0} : static_cast<std::size_t>(at - queue.begin());
    play(queue[(index + queue.size() - 1) % queue.size()], queue);
}

bool RadioPlayer::favorite(int index) const
{
    if (index < 0 || index >= static_cast<int>(stations_.size()))
        return false;
    const std::lock_guard<std::mutex> lock(mutex_);
    return favorites_.count(stations_[static_cast<std::size_t>(index)].url) != 0;
}

void RadioPlayer::set_favorite(int index, bool on)
{
    if (index < 0 || index >= static_cast<int>(stations_.size()))
        return;
    const std::lock_guard<std::mutex> lock(mutex_);
    const std::string &url = stations_[static_cast<std::size_t>(index)].url;
    if (on)
        favorites_.insert(url);
    else
        favorites_.erase(url);
    dirty_ = true;
}

std::vector<int> RadioPlayer::recents() const
{
    std::vector<int> list;
    const std::lock_guard<std::mutex> lock(mutex_);
    for (const std::string &url : recents_)
    {
        const int index = find(url);
        if (index >= 0)
            list.push_back(index);
    }
    return list;
}

Health RadioPlayer::health(int index) const
{
    if (index < 0 || index >= static_cast<int>(stations_.size()))
        return Health::unknown;
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = health_.find(stations_[static_cast<std::size_t>(index)].url);
    return found == health_.end() ? Health::unknown : found->second;
}

void RadioPlayer::start_check()
{
    if (!opener_ || stations_.empty())
        return;
    cancel_check();
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        check_ = CheckProgress{};
        check_.running = true;
        check_.total = static_cast<int>(stations_.size());
    }
    check_next_.store(0);
    check_thread_ = std::thread(&RadioPlayer::check_worker, this);
}

void RadioPlayer::cancel_check()
{
    check_cancel_.store(true, std::memory_order_relaxed);
    if (check_thread_.joinable())
        check_thread_.join();
    check_cancel_.store(false, std::memory_order_relaxed);
    const std::lock_guard<std::mutex> lock(mutex_);
    check_.running = false;
}

CheckProgress RadioPlayer::check_progress() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return check_;
}

bool RadioPlayer::sounds_like_audio(const std::string &content_type, const std::uint8_t *data,
                                    std::size_t size, std::string *why)
{
    if (is_unsupported_type(content_type))
    {
        *why = "unsupported format (" + content_type + ")";
        return false;
    }
    const Codec codec = detect_codec(content_type, data, size);
    std::unique_ptr<StreamDecoder> decoder = make_decoder(codec);
    if (decoder == nullptr)
    {
        *why = codec == Codec::aac ? "AAC is not available here" : "not an audio stream";
        return false;
    }
    std::vector<float> out;
    decoder->feed(data, size);
    decoder->decode(out, true);
    if (out.empty())
    {
        *why = "no audio in the first bytes";
        return false;
    }
    return true;
}

void RadioPlayer::check_worker()
{
    std::vector<std::string> urls;
    urls.reserve(stations_.size());
    for (const Station &station : stations_)
        urls.push_back(station.url);
    std::vector<std::uint8_t> chunk(8192);
    for (;;)
    {
        const int index = check_next_.fetch_add(1);
        if (index >= static_cast<int>(urls.size()) || check_cancel_.load())
            break;
        const std::string &url = urls[static_cast<std::size_t>(index)];
        bool ok = false;
        std::string target = url;
        for (int hop = 0; hop <= kMaxPlaylistHops && !ok && !check_cancel_.load(); ++hop)
        {
            net::OpenResult opened = opener_(target, check_cancel_);
            if (!opened.stream)
                break;
            std::vector<std::uint8_t> raw;
            const auto until =
                std::chrono::steady_clock::now() + std::chrono::seconds(kCheckSeconds);
            while (raw.size() < kCheckBytes && std::chrono::steady_clock::now() < until &&
                   !check_cancel_.load())
            {
                const int got = opened.stream->read(chunk.data(), static_cast<int>(chunk.size()));
                if (got <= 0)
                    break;
                raw.insert(raw.end(), chunk.begin(), chunk.begin() + got);
            }
            const std::string head(raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(std::min(
                                                                  raw.size(), kSniffBytes)));
            if (opened.stream->metaint == 0 && looks_like_playlist(head))
            {
                target = playlist_target(head);
                if (target.empty())
                    break;
                continue;
            }
            IcyReader icy(opened.stream->metaint);
            std::vector<std::uint8_t> audio;
            icy.feed(raw.data(), raw.size(), audio);
            std::string why;
            ok = !audio.empty() &&
                 sounds_like_audio(opened.stream->content_type, audio.data(), audio.size(), &why);
            break;
        }
        if (check_cancel_.load())
            break;
        const std::lock_guard<std::mutex> lock(mutex_);
        health_[url] = ok ? Health::ok : Health::failed;
        ++check_.done;
        ++(ok ? check_.ok : check_.failed);
        dirty_ = true;
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    check_.running = false;
}

void RadioPlayer::pump(float dt)
{
    if (mixer_ == nullptr)
        return;
    const PlayerStatus now = status();
    const bool radio = now.source == Source::radio;
    const bool live =
        radio && (now.state == RadioState::connecting || now.state == RadioState::buffering ||
                  now.state == RadioState::playing);
    // The station is up as soon as it is chosen: until it sends sound its
    // ring is silent anyway. The songs are silent whenever the radio is the
    // source, playing or not.
    const std::size_t slot = slot_ + static_cast<std::size_t>(deck_);
    const bool sounding = decks_[deck_].sounding.load(std::memory_order_relaxed);
    if (fading_ >= 0)
    {
        // The old station waits for the new one to sound (or for a while,
        // or for it to give up), then the two cross over.
        fade_wait_ += dt;
        if (fade_left_ < 0.0f && (sounding || fade_wait_ > kCrossfadeWait || !live))
        {
            mixer_->set_stream_gain(slot_ + static_cast<std::size_t>(fading_), 0.0f, pending_fade_);
            fade_left_ = pending_fade_ + 0.1f;
        }
        if (fade_left_ >= 0.0f)
        {
            fade_left_ -= dt;
            if (fade_left_ <= 0.0f)
                stop_deck(fading_);
        }
    }
    // A song from a drive has ended: on to the next one. With a cross-fade
    // the next starts while this one's last seconds still play; without, as
    // they run out.
    if (radio && decks_[deck_].ended.load(std::memory_order_relaxed) &&
        (crossfade_seconds() > 0.0f || rings_[deck_].available() < kSampleRate / 10))
    {
        decks_[deck_].ended.store(false, std::memory_order_relaxed);
        next();
        return;
    }
    // The chosen station comes up as soon as it is chosen (its ring is
    // silent until it sends sound), or, under a cross-fade, as it sounds.
    const bool up = live && (fading_ < 0 || sounding);
    if (up != deck_up_)
    {
        deck_up_ = up;
        const float ramp = up && pending_fade_ > 0.0f ? pending_fade_ : kFadeSeconds;
        mixer_->set_stream_gain(slot, up ? kRadioGain : 0.0f, ramp);
    }
    if (radio != songs_muted_)
    {
        songs_muted_ = radio;
        if (music_ != nullptr)
            music_->set_muted(radio);
    }
    if (now.state == RadioState::failed && decks_[deck_].thread.joinable())
        stop_deck(deck_); // the worker has returned; collect it

    // The song's name is the title while the songs play.
    if (!radio && music_ != nullptr && music_->current() != song_)
    {
        song_ = music_->current();
        const std::lock_guard<std::mutex> lock(mutex_);
        status_.title = song_;
        status_.format = "OGG Vorbis 48.0 kHz";
        started_ = std::chrono::steady_clock::now();
    }

    // A new song on the radio joins the history (a repeat of the last one,
    // or the station announcing its own name, does not).
    if (radio && now.state == RadioState::playing && !now.title.empty() && now.title != heard_ &&
        now.station >= 0 && now.station < static_cast<int>(stations_.size()))
    {
        heard_ = now.title;
        const Station &station = stations_[static_cast<std::size_t>(now.station)];
        SongEntry song;
        song.title = now.title;
        song.station = station.name;
        song.url = station.url;
        song.when = static_cast<std::int64_t>(std::time(nullptr));
        song = clean_song(song);
        if (!song.title.empty() && song.title != song.station)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (history_.empty() || history_.front().title != song.title)
            {
                history_.insert(history_.begin(), song);
                if (history_.size() > kMaxHistory)
                    history_.resize(kMaxHistory);
                dirty_ = true;
            }
        }
    }

    bool dirty = false;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        dirty = dirty_;
    }
    if (dirty)
    {
        save_timer_ += dt;
        if (save_timer_ >= kSaveDelay)
        {
            save_timer_ = 0.0f;
            save();
        }
    }
}

std::string RadioPlayer::setting(const std::string &key) const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = settings_.find(key);
    return it == settings_.end() ? std::string() : it->second;
}

void RadioPlayer::set_setting(const std::string &key, const std::string &value)
{
    // Keys and values are single-line words; anything else would break the file.
    if (key.empty() || key.find_first_of("=|\n") != std::string::npos ||
        value.find_first_of("|\n") != std::string::npos)
        return;
    const std::lock_guard<std::mutex> lock(mutex_);
    auto &slot = settings_[key];
    if (slot == value)
        return;
    slot = value;
    dirty_ = true;
}

void RadioPlayer::load()
{
    if (store_.empty())
        return;
    std::string text;
    if (!save::read_file(store_, &text, 1u << 20))
        return;
    const std::lock_guard<std::mutex> lock(mutex_);
    std::size_t start = 0;
    while (start < text.size())
    {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        const std::string line = text.substr(start, end - start);
        start = end + 1;
        const std::size_t bar = line.find('|');
        if (bar == std::string::npos)
            continue;
        const std::string kind = line.substr(0, bar);
        const std::string url = line.substr(bar + 1);
        if (kind == "set")
        {
            // "set|key=value": a screen's own preferences.
            const std::size_t equals = url.find('=');
            if (equals != std::string::npos && equals > 0)
                settings_[url.substr(0, equals)] = url.substr(equals + 1);
            continue;
        }
        if (kind == "song" || kind == "like")
        {
            // "song|when|url|station|title": the history and the liked songs.
            SongEntry song;
            std::vector<SongEntry> &into = kind == "song" ? history_ : liked_;
            const std::size_t most = kind == "song" ? kMaxHistory : kMaxLiked;
            if (parse_song(url, &song) && into.size() < most)
                into.push_back(song);
            continue;
        }
        if (!starts_with_http(url))
            continue;
        if (kind == "fav")
            favorites_.insert(url);
        else if (kind == "recent" && recents_.size() < kMaxRecents)
            recents_.push_back(url);
        else if (kind == "ok")
            health_[url] = Health::ok;
        else if (kind == "failed")
            health_[url] = Health::failed;
    }
}

void RadioPlayer::save()
{
    std::string text = "# Afterglow radio: favourites, recently played, last check, songs\n";
    std::string liked_text;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        dirty_ = false;
        if (liked_dirty_)
        {
            liked_dirty_ = false;
            liked_text = export_text(liked_);
        }
        for (const SongEntry &song : history_)
            text += song_line("song", song);
        for (const SongEntry &song : liked_)
            text += song_line("like", song);
        for (const std::string &url : favorites_)
            text += "fav|" + url + "\n";
        for (const std::string &url : recents_)
            text += "recent|" + url + "\n";
        for (const auto &[url, health] : health_)
        {
            if (health != Health::unknown)
                text += std::string(health == Health::ok ? "ok|" : "failed|") + url + "\n";
        }
        for (const auto &[key, value] : settings_)
            text += "set|" + key + "=" + value + "\n";
    }
    if (!store_.empty())
        save::write_atomic(store_, text);
    if (!liked_text.empty() && !liked_export_.empty())
        save::write_atomic(liked_export_, liked_text);
}

void RadioPlayer::worker(std::string url, int deck, std::uint64_t generation)
{
    StreamRing &ring = rings_[deck];
    const std::atomic<bool> &cancel = decks_[deck].cancel;
    std::vector<std::uint8_t> chunk(8192);
    std::vector<std::uint8_t> audio;
    std::vector<float> decoded;
    int attempts = 0;
    int playlist_hops = 0;
    std::string last_error;
    while (!cancel.load(std::memory_order_relaxed))
    {
        if (attempts >= kMaxAttempts)
        {
            set_state_if(generation, RadioState::failed,
                         last_error.empty() ? "the station is not answering" : last_error);
            return;
        }
        if (attempts > 0)
            nap(cancel, 1.0 + attempts * 1.5);
        if (cancel.load(std::memory_order_relaxed))
            return;
        ++attempts;
        set_state_if(generation, RadioState::connecting, attempts > 1 ? "reconnecting" : "");

        // A song on a drive is read straight from the file.
        const bool local = !file_path(url).empty();
        net::OpenResult opened = local ? open_file(url) : opener_(url, cancel);
        if (!opened.stream)
        {
            last_error = opened.error;
            if (local)
            {
                set_state_if(generation, RadioState::failed, last_error);
                return;
            }
            continue;
        }
        if (local)
        {
            // Its tags are its title, as a station's ICY title would be.
            const TrackTags tags = file_tags(file_path(url));
            std::string title = tags.title;
            if (!tags.artist.empty() && !title.empty())
                title = tags.artist + " - " + title;
            const std::lock_guard<std::mutex> lock(mutex_);
            if (generation == generation_)
                status_.title = title;
        }
        const std::string type = opened.stream->content_type;
        if (is_unsupported_type(type) && type.find("mpegurl") == std::string::npos)
        {
            set_state_if(generation, RadioState::failed,
                         "this station sends " + type + ", which does not play yet");
            return;
        }
        set_state_if(generation, RadioState::buffering, opened.via);

        IcyReader icy(opened.stream->metaint);
        std::unique_ptr<StreamDecoder> decoder;
        Codec codec = Codec::unknown;
        std::string head;
        bool sniffed = false;
        bool redirected = false;
        bool playing = false;
        const auto follow = [&]()
        {
            const std::string target = playlist_target(head);
            if (target.empty() || playlist_hops >= kMaxPlaylistHops)
            {
                set_state_if(generation, RadioState::failed, "a playlist without a stream in it");
                return false;
            }
            ++playlist_hops;
            url = target;
            redirected = true;
            return true;
        };
        while (!cancel.load(std::memory_order_relaxed))
        {
            const int got = opened.stream->read(chunk.data(), static_cast<int>(chunk.size()));
            if (got == 0 && local)
            {
                // The end of the song: the decoder's tail, then the next song
                // (pump starts it as this one runs out).
                if (decoder != nullptr)
                {
                    decoded.clear();
                    decoder->decode(decoded, true);
                    std::size_t written = 0;
                    const std::size_t frames = decoded.size() / 2;
                    while (written < frames && !cancel.load(std::memory_order_relaxed))
                    {
                        written += ring.write(decoded.data() + written * 2, frames - written);
                        if (written < frames)
                            std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                }
                decks_[deck].sounding.store(true, std::memory_order_relaxed);
                decks_[deck].ended.store(true, std::memory_order_relaxed);
                if (!playing)
                    set_state_if(generation, RadioState::playing, opened.via);
                return;
            }
            if (got <= 0)
            {
                if (!sniffed && !head.empty() && opened.stream->metaint == 0 &&
                    looks_like_playlist(head))
                {
                    if (!follow())
                        return;
                    break;
                }
                last_error = got == 0 ? "the station stopped sending" : "the connection dropped";
                break;
            }
            audio.clear();
            if (!sniffed)
            {
                head.append(reinterpret_cast<const char *>(chunk.data()),
                            static_cast<std::size_t>(got));
                if (opened.stream->metaint == 0 && looks_like_playlist(head))
                {
                    if (head.size() < kSniffBytes)
                        continue; // read the whole (small) playlist
                    if (!follow())
                        return;
                    break;
                }
                if (head.size() < 2048)
                    continue; // enough bytes to tell the format
                icy.feed(reinterpret_cast<const std::uint8_t *>(head.data()), head.size(), audio);
                codec = detect_codec(type, audio.data(), audio.size());
                decoder = make_decoder(codec);
                if (decoder == nullptr)
                {
                    set_state_if(generation, RadioState::failed,
                                 codec == Codec::aac ? "AAC stations play on the console only"
                                                     : "this station's format does not play yet");
                    return;
                }
                sniffed = true;
            }
            else
            {
                icy.feed(chunk.data(), static_cast<std::size_t>(got), audio);
            }
            std::string title;
            if (icy.take_title(&title) && !local)
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                if (generation == generation_)
                    status_.title = title;
            }
            if (audio.empty())
                continue;
            decoder->feed(audio.data(), audio.size());
            decoded.clear();
            decoder->decode(decoded);
            // Ogg streams carry their song titles in the audio itself.
            if (std::string tagged; decoder->take_title(&tagged))
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                if (generation == generation_)
                    status_.title = tagged;
            }
            if (decoder->rate() == 0 && decoder->skipped() > kNotAudioBytes)
            {
                set_state_if(generation, RadioState::failed,
                             "no audio could be decoded from this station");
                return;
            }
            std::size_t written = 0;
            const std::size_t frames = decoded.size() / 2;
            while (written < frames && !cancel.load(std::memory_order_relaxed))
            {
                written += ring.write(decoded.data() + written * 2, frames - written);
                if (written < frames)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (!playing && ring.available() >= kStartFrames)
            {
                playing = true;
                attempts = 0; // a station that played earns fresh retries
                decks_[deck].sounding.store(true, std::memory_order_relaxed);
                const std::lock_guard<std::mutex> lock(mutex_);
                if (generation == generation_)
                {
                    status_.state = RadioState::playing;
                    status_.message = opened.via;
                    status_.format = describe(*decoder);
                }
            }
        }
        if (redirected)
        {
            attempts = 0;
            continue;
        }
    }
}

} // namespace hui::audio
