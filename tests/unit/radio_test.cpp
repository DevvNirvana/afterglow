// Afterglow - Tests for the internet radio: stations, decoding, ICY titles, the player.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/mixer.hpp"
#include "audio/icy.hpp"
#include "audio/radio.hpp"
#include "audio/stream_decoder.hpp"
#include "core/save_file.hpp"
#include "net/http.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <set>
#include <cmath>
#include <cstring>
#include <thread>

#ifndef HUI_SOURCE_DIR
#define HUI_SOURCE_DIR "."
#endif

namespace
{

using hui::audio::Mp3Stream;
using hui::audio::RadioState;

std::string tone_mp3()
{
    std::string data;
    EXPECT_TRUE(
        hui::save::read_file(HUI_SOURCE_DIR "/tests/unit/data/tone-44k-mono.mp3", &data, 1u << 20));
    return data;
}

TEST(Stations, ALogoMayFollowTheStream)
{
    const std::string text =
        "Techno | A | Techno | UK | MP3 | http://a.example/ | https://a.example/logo.png\n"
        "Techno | B | Techno | UK | MP3 | http://b.example/\n"
        "Techno | C | Techno | UK | MP3 | http://c.example/ | not a link\n"
        "Techno | D | Techno | UK | MP3 | http://d.example/ | x | y\n";
    const auto stations = hui::audio::parse_stations(text);
    ASSERT_EQ(stations.size(), 3u); // eight fields is not a station
    const auto logos = hui::audio::parse_logos(text);
    ASSERT_EQ(logos.size(), 1u);
    EXPECT_EQ(logos.at("http://a.example/"), "https://a.example/logo.png");
    hui::audio::RadioPlayer player;
    player.set_stations(stations);
    player.set_logos(logos);
    EXPECT_EQ(player.logo(0), "https://a.example/logo.png");
    EXPECT_EQ(player.logo(1), "");
    EXPECT_EQ(player.logo(7), "");
}

TEST(Stations, ParsesLinesAndSkipsWhatIsNotAStation)
{
    const auto stations = hui::audio::parse_stations(
        "# a comment\n"
        "\n"
        "Techno | Techno.FM | Minimal / Techno | United Kingdom | MP3 320 | http://a.example/x\n"
        "  House|Deep|Deep House|Netherlands|AAC 64|https://b.example/y  \r\n"
        "Rap | No address | Rap | USA | MP3 | ftp://c.example\n"
        "Rap | Too | Few | USA | MP3\n"
        "Rap |  | Nameless | USA | MP3 | http://d.example\n"
        " | No category | x | USA | MP3 | http://e.example\n");
    ASSERT_EQ(stations.size(), 2u);
    EXPECT_EQ(stations[0].category, "Techno");
    EXPECT_EQ(stations[0].name, "Techno.FM");
    EXPECT_EQ(stations[0].style, "Minimal / Techno");
    EXPECT_EQ(stations[0].country, "United Kingdom");
    EXPECT_EQ(stations[0].format, "MP3 320");
    EXPECT_EQ(stations[0].url, "http://a.example/x");
    EXPECT_EQ(stations[1].name, "Deep");
    EXPECT_EQ(stations[1].url, "https://b.example/y");
}

TEST(Stations, TheBundledListLeadsWithElectronicMusicAndIsLarge)
{
    std::string text;
    ASSERT_TRUE(hui::save::read_file(HUI_SOURCE_DIR "/assets/radio/stations.txt", &text, 1u << 20));
    const auto stations = hui::audio::parse_stations(text);
    EXPECT_GE(stations.size(), 100u);
    ASSERT_FALSE(stations.empty());
    EXPECT_EQ(stations.front().category, "Techno");
    std::set<std::string> categories;
    std::set<std::string> urls;
    for (const auto &station : stations)
    {
        categories.insert(station.category);
        EXPECT_TRUE(urls.insert(station.url).second) << "listed twice: " << station.name;
        hui::net::Url url;
        EXPECT_TRUE(hui::net::parse_url(station.url, &url)) << station.name;
        EXPECT_EQ(station.url.find("somafm"), std::string::npos) << "SomaFM forbids apps";
        EXPECT_EQ(station.url.find(".m3u8"), std::string::npos) << "HLS does not play";
        EXPECT_FALSE(station.country.empty()) << station.name;
    }
    for (const char *wanted :
         {"Techno", "House", "Trance", "Drum & Bass", "Hard Dance", "EDM & Dance", "Pop & Hits",
          "Hip-Hop & Rap", "Bollywood & Desi", "Latin", "K-Pop", "Afrobeats", "Arabic"})
        EXPECT_EQ(categories.count(wanted), 1u) << wanted;
}

TEST(Stations, PlaylistsPointAtTheirFirstStream)
{
    EXPECT_EQ(hui::audio::playlist_target("[playlist]\nNumberOfEntries=1\n"
                                          "File1=http://s.example:8000/live\nTitle1=x\n"),
              "http://s.example:8000/live");
    EXPECT_EQ(hui::audio::playlist_target("#EXTM3U\n#EXTINF:-1,x\nhttps://s.example/a.mp3\n"),
              "https://s.example/a.mp3");
    EXPECT_EQ(hui::audio::playlist_target("[playlist]\nTitle1=nothing\n"), "");
}

TEST(Url, SplitsHostPortAndPath)
{
    hui::net::Url url;
    ASSERT_TRUE(hui::net::parse_url("http://Host.example:8030/", &url));
    EXPECT_EQ(url.scheme, "http");
    EXPECT_EQ(url.host, "Host.example");
    EXPECT_EQ(url.port, 8030);
    EXPECT_EQ(url.path, "/");
    ASSERT_TRUE(hui::net::parse_url("https://a.example", &url));
    EXPECT_EQ(url.port, 443);
    EXPECT_EQ(url.path, "/");
    EXPECT_FALSE(hui::net::parse_url("ftp://a.example/", &url));
    EXPECT_FALSE(hui::net::parse_url("http://a.example:99999/", &url));
}

TEST(Resampler, Makes48kHzFrom44_1kHz)
{
    hui::audio::Resampler resampler;
    resampler.set_rate(44100);
    std::vector<float> in(441 * 2, 0.25f);
    std::vector<float> out;
    for (int i = 0; i < 100; ++i) // one second in small pieces
        resampler.process(in.data(), 441, out);
    EXPECT_NEAR(static_cast<double>(out.size() / 2), 48000.0, 2.0);
    for (float sample : out)
        EXPECT_FLOAT_EQ(sample, 0.25f);
}

TEST(Mp3Stream, DecodesAToneFedInPieces)
{
    const std::string data = tone_mp3();
    ASSERT_FALSE(data.empty());
    Mp3Stream mp3;
    std::vector<float> out;
    const auto *bytes = reinterpret_cast<const std::uint8_t *>(data.data());
    for (std::size_t at = 0; at < data.size(); at += 997) // odd pieces, as a network gives them
    {
        mp3.feed(bytes + at, std::min<std::size_t>(997, data.size() - at));
        mp3.decode(out);
    }
    mp3.decode(out, true);
    EXPECT_EQ(mp3.rate(), 44100);
    EXPECT_EQ(mp3.channels(), 1);
    const std::size_t frames = out.size() / 2;
    EXPECT_NEAR(static_cast<double>(frames), 96000.0, 4000.0); // two seconds at 48 kHz

    // The middle second is a 440 Hz tone: count the rising zero crossings.
    int crossings = 0;
    for (std::size_t i = 24000; i < 72000; ++i)
    {
        EXPECT_FLOAT_EQ(out[i * 2], out[i * 2 + 1]); // mono doubled
        crossings += out[i * 2] < 0.0f && out[(i + 1) * 2] >= 0.0f ? 1 : 0;
    }
    EXPECT_NEAR(crossings, 440, 3);
}

TEST(Mp3Stream, NoiseIsNotTakenForMusic)
{
    Mp3Stream mp3;
    std::vector<std::uint8_t> noise(64 * 1024);
    std::uint32_t state = 12345;
    for (auto &byte : noise)
    {
        state = state * 1664525u + 1013904223u;
        byte = static_cast<std::uint8_t>(state >> 24);
    }
    std::vector<float> out;
    mp3.feed(noise.data(), noise.size());
    mp3.decode(out, true);
    EXPECT_LT(out.size(), 4u * 1152u); // at most a stray frame
}

TEST(Codec, FromTheTypeOrTheBytes)
{
    using hui::audio::Codec;
    using hui::audio::detect_codec;
    const std::uint8_t nothing[16] = {};
    EXPECT_EQ(detect_codec("audio/mpeg", nothing, 0), Codec::mp3);
    EXPECT_EQ(detect_codec("audio/aacp", nothing, 0), Codec::aac);
    EXPECT_EQ(detect_codec("audio/AAC", nothing, 0), Codec::aac);
    const std::string mp3 = tone_mp3();
    EXPECT_EQ(detect_codec("application/octet-stream",
                           reinterpret_cast<const std::uint8_t *>(mp3.data()), mp3.size()),
              Codec::mp3);
    // Two ADTS frames back to back, 16 bytes each.
    std::vector<std::uint8_t> adts;
    for (int i = 0; i < 3; ++i)
    {
        const std::uint8_t header[7] = {0xFF, 0xF1, 0x50, 0x80, 0x02, 0x1F, 0xFC};
        adts.insert(adts.end(), header, header + 7);
        adts.resize(adts.size() + 9, 0);
    }
    EXPECT_EQ(detect_codec("", adts.data(), adts.size()), Codec::aac);
    EXPECT_EQ(detect_codec("", nothing, sizeof(nothing)), Codec::unknown);
    EXPECT_FALSE(hui::audio::is_unsupported_type("audio/ogg"));
    EXPECT_FALSE(hui::audio::is_unsupported_type("audio/opus"));
    EXPECT_TRUE(hui::audio::is_unsupported_type("audio/flac"));
    EXPECT_TRUE(hui::audio::is_unsupported_type("application/vnd.apple.mpegurl"));
    EXPECT_EQ(detect_codec("audio/ogg", nothing, 0), Codec::ogg);
    EXPECT_EQ(detect_codec("application/ogg", nothing, 0), Codec::ogg);
    const std::uint8_t ogg[8] = {'O', 'g', 'g', 'S', 0, 2, 0, 0};
    EXPECT_EQ(detect_codec("", ogg, sizeof(ogg)), Codec::ogg);
    EXPECT_FALSE(hui::audio::is_unsupported_type("audio/mpeg"));
}

std::string data_file(const char *name)
{
    std::string data;
    EXPECT_TRUE(hui::save::read_file(std::string(HUI_SOURCE_DIR "/tests/unit/data/") + name, &data,
                                     1u << 20));
    return data;
}

// Feeds a stream in uneven pieces, as a network would.
std::vector<float> decode_in_pieces(hui::audio::StreamDecoder &decoder, const std::string &bytes)
{
    std::vector<float> out;
    std::size_t at = 0;
    std::size_t piece = 37;
    while (at < bytes.size())
    {
        const std::size_t n = std::min(piece, bytes.size() - at);
        decoder.feed(reinterpret_cast<const std::uint8_t *>(bytes.data() + at), n);
        decoder.decode(out);
        at += n;
        piece = piece * 3 % 1777 + 11;
    }
    decoder.decode(out, true);
    return out;
}

// Rising zero crossings of the left channel over half a second from the
// middle: the pitch of a test tone, halved.
int crossings(const std::vector<float> &stereo)
{
    const std::size_t frames = stereo.size() / 2;
    const std::size_t from = frames / 2 - 12000;
    int count = 0;
    for (std::size_t i = from; i < from + 24000; ++i)
        count += stereo[i * 2] <= 0.0f && stereo[(i + 1) * 2] > 0.0f ? 1 : 0;
    return count;
}

double rms(const std::vector<float> &samples)
{
    double sum = 0.0;
    for (float s : samples)
        sum += static_cast<double>(s) * s;
    return samples.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(samples.size()));
}

TEST(OggStream, DecodesOpusWithItsTitle)
{
    const std::string opus = data_file("tone.opus");
    auto decoder = hui::audio::make_decoder(hui::audio::Codec::ogg);
    ASSERT_NE(decoder, nullptr);
    const std::vector<float> out = decode_in_pieces(*decoder, opus);
    // 1.5 seconds of stereo at 48 kHz (the warm-up skipped), clearly a tone.
    EXPECT_NEAR(static_cast<double>(out.size()) / 2.0, 72000.0, 2000.0);
    EXPECT_GT(rms(out), 0.04);           // the test tones are at -18 dBFS
    EXPECT_NEAR(crossings(out), 220, 3); // 440 Hz
    EXPECT_EQ(decoder->rate(), 48000);
    EXPECT_EQ(decoder->channels(), 2);
    EXPECT_STREQ(decoder->name(), "Opus");
    std::string title;
    ASSERT_TRUE(decoder->take_title(&title));
    EXPECT_EQ(title, "Nova Kessler - Concrete Halls");
    EXPECT_FALSE(decoder->take_title(&title)); // once
    EXPECT_LT(decoder->skipped(), 64u);
}

TEST(OggStream, DecodesVorbisAndResamplesTo48k)
{
    const std::string vorbis = data_file("tone-44k.ogg");
    auto decoder = hui::audio::make_decoder(hui::audio::Codec::ogg);
    const std::vector<float> out = decode_in_pieces(*decoder, vorbis);
    EXPECT_NEAR(static_cast<double>(out.size()) / 2.0, 72000.0, 3000.0);
    EXPECT_GT(rms(out), 0.04);           // the test tones are at -18 dBFS
    EXPECT_NEAR(crossings(out), 330, 3); // 660 Hz, after resampling
    EXPECT_EQ(decoder->rate(), 44100);
    EXPECT_EQ(decoder->channels(), 1);
    EXPECT_STREQ(decoder->name(), "Vorbis");
    std::string title;
    ASSERT_TRUE(decoder->take_title(&title));
    EXPECT_EQ(title, "Mira Sol - Glasshouse");
}

TEST(OggStream, FollowsAChainOfSongsAndSurvivesDamage)
{
    // Icecast sends one logical stream per song: Opus, then Vorbis, then
    // Opus again, with garbage and a damaged page in between.
    const std::string opus = data_file("tone.opus");
    const std::string vorbis = data_file("tone-44k.ogg");
    std::string damaged = opus;
    damaged[200] = static_cast<char>(damaged[200] ^ 0x55); // its CRC no longer matches
    const std::string chain = opus + "junk between pages" + vorbis + damaged;
    auto decoder = hui::audio::make_decoder(hui::audio::Codec::ogg);
    std::vector<std::string> titles;
    std::vector<float> out;
    std::size_t at = 0;
    while (at < chain.size())
    {
        const std::size_t n = std::min<std::size_t>(512, chain.size() - at);
        decoder->feed(reinterpret_cast<const std::uint8_t *>(chain.data() + at), n);
        decoder->decode(out);
        std::string title;
        if (decoder->take_title(&title))
            titles.push_back(title);
        at += n;
    }
    ASSERT_GE(titles.size(), 2u);
    EXPECT_EQ(titles[0], "Nova Kessler - Concrete Halls");
    EXPECT_EQ(titles[1], "Mira Sol - Glasshouse");
    // Most of three songs came out.
    EXPECT_GT(static_cast<double>(out.size()) / 2.0, 150000.0);
    EXPECT_GT(decoder->skipped(), 10u);
    // Not Ogg at all: nothing, no crash.
    auto other = hui::audio::make_decoder(hui::audio::Codec::ogg);
    std::vector<float> none;
    const std::string mp3 = tone_mp3();
    other->feed(reinterpret_cast<const std::uint8_t *>(mp3.data()), mp3.size());
    EXPECT_EQ(other->decode(none), 0);
}

TEST(Icy, SplitsTitlesFromTheAudio)
{
    // metaint 4: four audio bytes, a length byte, the block, and again.
    std::string block = "StreamTitle='Daft Punk - Don't Stop';StreamUrl='';";
    block.resize((block.size() + 15) / 16 * 16, '\0');
    std::string wire = "AAAA";
    wire += static_cast<char>(block.size() / 16);
    wire += block;
    wire += "BBBB";
    wire += '\0'; // an empty block: no change
    wire += "CCCC";
    hui::audio::IcyReader reader(4);
    std::vector<std::uint8_t> audio;
    // In pieces of three bytes, the way a network may cut it.
    for (std::size_t at = 0; at < wire.size(); at += 3)
        reader.feed(reinterpret_cast<const std::uint8_t *>(wire.data()) + at,
                    std::min<std::size_t>(3, wire.size() - at), audio);
    EXPECT_EQ(std::string(audio.begin(), audio.end()), "AAAABBBBCCCC");
    std::string title;
    ASSERT_TRUE(reader.take_title(&title));
    EXPECT_EQ(title, "Daft Punk - Don't Stop");
    EXPECT_FALSE(reader.take_title(&title)); // once
}

TEST(Icy, WithoutMetadataEverythingIsAudio)
{
    hui::audio::IcyReader reader(0);
    std::vector<std::uint8_t> audio;
    const std::uint8_t data[5] = {1, 2, 3, 4, 5};
    reader.feed(data, 5, audio);
    EXPECT_EQ(audio.size(), 5u);
    EXPECT_EQ(hui::audio::icy_title("nothing here"), "");
}

// Serves bytes like a socket would, then a stall that ends on cancel.
class FakeStream final : public hui::net::ByteStream
{
  public:
    FakeStream(std::string data, const std::atomic<bool> &cancel, bool ends)
        : data_(std::move(data)), cancel_(cancel), ends_(ends)
    {
    }
    int read(std::uint8_t *out, int size) override
    {
        if (at_ >= data_.size())
        {
            if (ends_)
                return 0; // the server closed the connection
            while (!cancel_.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            return -1;
        }
        const auto count = std::min(static_cast<std::size_t>(size), data_.size() - at_);
        std::memcpy(out, data_.data() + at_, count);
        at_ += count;
        return static_cast<int>(count);
    }

  private:
    std::string data_;
    std::size_t at_ = 0;
    const std::atomic<bool> &cancel_;
    bool ends_;
};

bool wait_for(hui::audio::RadioPlayer &player, RadioState state)
{
    for (int i = 0; i < 300; ++i)
    {
        player.pump(1.0f / 60.0f);
        if (player.status().state == state)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

TEST(OggStream, ThePlayerPlaysAnOggStation)
{
    const std::string opus = data_file("tone.opus");
    hui::audio::Mixer mixer;
    hui::audio::RadioPlayer player;
    player.init(mixer, 1, nullptr,
                [&](const std::string &, const std::atomic<bool> &cancel)
                {
                    hui::net::OpenResult result;
                    result.stream = std::make_unique<FakeStream>(opus + opus, cancel, false);
                    result.stream->content_type = "application/ogg";
                    result.via = "fake";
                    return result;
                });
    player.set_stations({{"Techno", "Opus FM", "", "USA", "OPUS", "http://opus.example/"}});
    player.play(0);
    ASSERT_TRUE(wait_for(player, RadioState::playing));
    EXPECT_EQ(player.status().format, "Opus 48.0 kHz");
    EXPECT_EQ(player.status().title, "Nova Kessler - Concrete Halls");
    player.stop();
}

TEST(RadioPlayer, FollowsAPlaylistThenPlaysTheStream)
{
    hui::audio::Mixer mixer;
    hui::audio::RadioPlayer player;
    std::vector<std::string> opened;
    const std::string mp3 = tone_mp3();
    player.init(mixer, 1, nullptr,
                [&](const std::string &url, const std::atomic<bool> &cancel)
                {
                    opened.push_back(url);
                    hui::net::OpenResult result;
                    // A playlist the server sends and closes, then an endless stream.
                    const bool list = url == "http://list.example/";
                    const std::string body =
                        list ? "[playlist]\nFile1=http://stream.example/\n" : mp3 + mp3 + mp3;
                    result.stream = std::make_unique<FakeStream>(body, cancel, list);
                    result.via = "fake";
                    return result;
                });
    player.set_stations({{"Test", "Test FM", "Tones", "USA", "MP3", "http://list.example/"}});
    player.play(0);
    ASSERT_TRUE(wait_for(player, RadioState::playing));
    EXPECT_EQ(player.status().station, 0);
    EXPECT_EQ(player.status().message, "fake");
    ASSERT_EQ(opened.size(), 2u);
    EXPECT_EQ(opened[1], "http://stream.example/");
    player.stop();
    EXPECT_EQ(player.status().state, RadioState::stopped);
}

TEST(RadioPlayer, AStationThatNeverAnswersFailsWithItsReason)
{
    hui::audio::Mixer mixer;
    hui::audio::RadioPlayer player;
    int attempts = 0;
    player.init(mixer, 1, nullptr,
                [&](const std::string &, const std::atomic<bool> &)
                {
                    ++attempts;
                    hui::net::OpenResult result;
                    result.error = "cannot find nowhere.example";
                    return result;
                });
    player.set_stations({{"Test", "Gone FM", "Silence", "USA", "MP3", "http://nowhere.example/"}});
    player.play(0);
    // Four tries with growing pauses: about ten seconds.
    bool failed = false;
    for (int i = 0; i < 1500 && !failed; ++i)
    {
        player.pump(1.0f / 60.0f);
        failed = player.status().state == RadioState::failed;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(failed);
    EXPECT_EQ(attempts, 4);
    EXPECT_EQ(player.status().message, "cannot find nowhere.example");
}

TEST(RadioPlayer, TransportPausesResumesAndMovesThroughTheQueue)
{
    hui::audio::Mixer mixer;
    hui::audio::RadioPlayer player;
    const std::string mp3 = tone_mp3();
    std::vector<std::string> opened;
    player.init(mixer, 1, nullptr,
                [&](const std::string &url, const std::atomic<bool> &cancel)
                {
                    opened.push_back(url);
                    hui::net::OpenResult result;
                    result.stream = std::make_unique<FakeStream>(mp3 + mp3 + mp3, cancel, false);
                    result.via = "fake";
                    return result;
                });
    player.set_stations({{"Techno", "A", "", "USA", "MP3", "http://a.example/"},
                         {"Techno", "B", "", "USA", "MP3", "http://b.example/"},
                         {"House", "C", "", "USA", "MP3", "http://c.example/"}});
    player.play(0, {0, 2}); // picked from a list of A and C
    ASSERT_TRUE(wait_for(player, RadioState::playing));
    player.toggle_pause();
    EXPECT_EQ(player.status().state, RadioState::paused);
    player.toggle_pause(); // live radio reconnects
    ASSERT_TRUE(wait_for(player, RadioState::playing));
    player.next();
    EXPECT_EQ(player.status().station, 2); // C, not B: the queue is A and C
    player.next();
    EXPECT_EQ(player.status().station, 0); // round again
    player.previous();
    EXPECT_EQ(player.status().station, 2);
    player.stop();
    EXPECT_EQ(player.status().state, RadioState::stopped);
    EXPECT_EQ(player.status().source, hui::audio::Source::radio);
    player.play_songs();
    EXPECT_EQ(player.status().source, hui::audio::Source::songs);
    const std::vector<int> recents = player.recents();
    ASSERT_GE(recents.size(), 2u);
    EXPECT_EQ(recents[0], 2); // most recent first
    EXPECT_EQ(recents[1], 0);
}

TEST(RadioPlayer, AStationChangeCrossFadesFromTheOldDeck)
{
    hui::audio::Mixer mixer;
    hui::audio::RadioPlayer player;
    const std::string mp3 = tone_mp3();
    player.init(mixer, 1, nullptr,
                [&](const std::string &, const std::atomic<bool> &cancel)
                {
                    hui::net::OpenResult result;
                    result.stream = std::make_unique<FakeStream>(mp3 + mp3 + mp3, cancel, false);
                    result.via = "fake";
                    return result;
                });
    player.set_stations({{"Techno", "A", "", "USA", "MP3", "http://a.example/"},
                         {"Techno", "B", "", "USA", "MP3", "http://b.example/"}});
    EXPECT_FLOAT_EQ(player.crossfade_seconds(), 3.0f);
    player.set_setting("xfade", "1");
    EXPECT_FLOAT_EQ(player.crossfade_seconds(), 1.0f);
    player.play(0);
    ASSERT_TRUE(wait_for(player, RadioState::playing));
    EXPECT_FALSE(player.fading());
    player.play(1);
    // A keeps playing under B until B sounds, then fades out.
    EXPECT_TRUE(player.fading());
    EXPECT_EQ(player.status().station, 1);
    ASSERT_TRUE(wait_for(player, RadioState::playing));
    bool done = false;
    for (int i = 0; i < 300 && !done; ++i)
    {
        player.pump(1.0f / 60.0f);
        done = !player.fading();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_TRUE(done);
    EXPECT_EQ(player.status().state, RadioState::playing);
    // Off: a straight cut.
    player.set_setting("xfade", "0");
    player.play(0);
    EXPECT_FALSE(player.fading());
    ASSERT_TRUE(wait_for(player, RadioState::playing));
    // Nonsense in the setting means the default.
    player.set_setting("xfade", "loud");
    EXPECT_FLOAT_EQ(player.crossfade_seconds(), 3.0f);
    player.set_setting("xfade", "99");
    EXPECT_FLOAT_EQ(player.crossfade_seconds(), 10.0f);
    player.stop();
    EXPECT_FALSE(player.fading());
}

TEST(RadioPlayer, AStationChangeWhilePausedDoesNotFade)
{
    hui::audio::Mixer mixer;
    hui::audio::RadioPlayer player;
    const std::string mp3 = tone_mp3();
    player.init(mixer, 1, nullptr,
                [&](const std::string &, const std::atomic<bool> &cancel)
                {
                    hui::net::OpenResult result;
                    result.stream = std::make_unique<FakeStream>(mp3 + mp3 + mp3, cancel, false);
                    return result;
                });
    player.set_stations({{"Techno", "A", "", "USA", "MP3", "http://a.example/"},
                         {"Techno", "B", "", "USA", "MP3", "http://b.example/"}});
    player.play(0);
    ASSERT_TRUE(wait_for(player, RadioState::playing));
    player.toggle_pause();
    player.play(1);
    EXPECT_FALSE(player.fading()); // nothing was sounding
    ASSERT_TRUE(wait_for(player, RadioState::playing));
}

TEST(RadioPlayer, FavouritesAndChecksAreKeptInTheStore)
{
    const std::string path = ::testing::TempDir() + "afterglow-radio-store.txt";
    std::remove(path.c_str());
    const std::string mp3 = tone_mp3();
    const std::vector<hui::audio::Station> stations = {
        {"Techno", "Good", "", "USA", "MP3", "http://good.example/"},
        {"Techno", "Bad", "", "USA", "MP3", "http://bad.example/"}};
    {
        hui::audio::Mixer mixer;
        hui::audio::RadioPlayer player;
        player.init(mixer, 1, nullptr,
                    [&](const std::string &url, const std::atomic<bool> &cancel)
                    {
                        hui::net::OpenResult result;
                        if (url == "http://good.example/")
                            result.stream = std::make_unique<FakeStream>(mp3 + mp3, cancel, true);
                        else
                            result.error = "no";
                        return result;
                    });
        player.set_stations(stations);
        player.set_store(path);
        player.set_favorite(1, true);
        player.set_setting("scene", "12");
        player.set_setting("bad|key", "x"); // refused: would break the file
        player.set_setting("note", "a\nb"); // refused too
        EXPECT_EQ(player.setting("scene"), "12");
        EXPECT_EQ(player.setting("bad|key"), "");
        EXPECT_TRUE(player.favorite(1));
        EXPECT_FALSE(player.favorite(0));
        player.start_check();
        for (int i = 0; i < 500 && player.check_progress().running; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const auto progress = player.check_progress();
        EXPECT_EQ(progress.done, 2);
        EXPECT_EQ(progress.ok, 1);
        EXPECT_EQ(player.health(0), hui::audio::Health::ok);
        EXPECT_EQ(player.health(1), hui::audio::Health::failed);
        for (int i = 0; i < 200; ++i)
            player.pump(1.0f / 60.0f); // the store is written a moment after a change
    }
    hui::audio::Mixer mixer;
    hui::audio::RadioPlayer again;
    again.init(mixer, 1, nullptr, [](const std::string &, const std::atomic<bool> &)
               { return hui::net::OpenResult{}; });
    again.set_stations(stations);
    again.set_store(path);
    EXPECT_TRUE(again.favorite(1));
    EXPECT_EQ(again.setting("scene"), "12");
    EXPECT_EQ(again.setting("note"), "");
    EXPECT_EQ(again.health(0), hui::audio::Health::ok);
    EXPECT_EQ(again.health(1), hui::audio::Health::failed);
    std::remove(path.c_str());
}

// Audio with an ICY title in its first metadata block, as a station sends it.
std::string with_title(const std::string &audio, int metaint, const std::string &title)
{
    std::string block = "StreamTitle='" + title + "';";
    block.resize((block.size() + 15) / 16 * 16, '\0');
    std::string out;
    const auto step = static_cast<std::size_t>(metaint);
    for (std::size_t at = 0; at + step <= audio.size(); at += step)
    {
        out += audio.substr(at, step);
        if (at == 0)
        {
            out += static_cast<char>(block.size() / 16);
            out += block;
        }
        else
        {
            out += '\0';
        }
    }
    return out;
}

TEST(RadioPlayer, KeepsTheSongsHeardAndLiked)
{
    const std::string path = ::testing::TempDir() + "afterglow-radio-songs.txt";
    const std::string exported = ::testing::TempDir() + "liked-songs.txt";
    std::remove(path.c_str());
    std::remove(exported.c_str());
    const std::string mp3 = tone_mp3();
    const std::vector<hui::audio::Station> stations = {
        {"Techno", "Warehouse FM", "", "USA", "MP3", "http://warehouse.example/"}};
    const std::string song = "Nova Kessler - Concrete Halls";
    {
        hui::audio::Mixer mixer;
        hui::audio::RadioPlayer player;
        player.init(mixer, 1, nullptr,
                    [&](const std::string &, const std::atomic<bool> &cancel)
                    {
                        hui::net::OpenResult result;
                        result.stream = std::make_unique<FakeStream>(
                            with_title(mp3 + mp3 + mp3, 1024, song), cancel, false);
                        result.stream->metaint = 1024;
                        return result;
                    });
        player.set_stations(stations);
        player.set_store(path);
        EXPECT_EQ(player.liked_export(), exported);
        player.play(0);
        ASSERT_TRUE(wait_for(player, RadioState::playing));
        for (int i = 0; i < 300 && player.history().empty(); ++i)
        {
            player.pump(1.0f / 60.0f);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const std::vector<hui::audio::SongEntry> heard = player.history();
        ASSERT_EQ(heard.size(), 1u);
        EXPECT_EQ(heard[0].title, song);
        EXPECT_EQ(heard[0].station, "Warehouse FM");
        EXPECT_EQ(heard[0].url, "http://warehouse.example/");
        EXPECT_GT(heard[0].when, 0);
        for (int i = 0; i < 20; ++i)
            player.pump(1.0f / 60.0f); // the same song again is not a new entry
        EXPECT_EQ(player.history().size(), 1u);

        EXPECT_FALSE(player.is_liked(song));
        EXPECT_TRUE(player.set_liked(heard[0], true));
        EXPECT_FALSE(player.set_liked(heard[0], true)); // already kept
        EXPECT_TRUE(player.is_liked(song));
        // A title that would break a line of the store is made safe.
        hui::audio::SongEntry odd;
        odd.title = "A | B - C\nD";
        EXPECT_TRUE(player.set_liked(odd, true));
        EXPECT_TRUE(player.is_liked("A / B - C D"));
        EXPECT_FALSE(player.set_liked(hui::audio::SongEntry{}, true)); // no title
        ASSERT_EQ(player.liked().size(), 2u);
        EXPECT_EQ(player.liked()[0].title, "A / B - C D"); // newest first
        player.stop();
        for (int i = 0; i < 200; ++i)
            player.pump(1.0f / 60.0f);
    }
    std::string text;
    ASSERT_TRUE(hui::save::read_file(exported, &text));
    EXPECT_NE(text.find(song + "  /  Warehouse FM  /  "), std::string::npos);

    hui::audio::Mixer mixer;
    hui::audio::RadioPlayer again;
    again.init(mixer, 1, nullptr, [](const std::string &, const std::atomic<bool> &)
               { return hui::net::OpenResult{}; });
    again.set_stations(stations);
    again.set_store(path);
    ASSERT_EQ(again.history().size(), 1u);
    EXPECT_EQ(again.history()[0].title, song);
    ASSERT_EQ(again.liked().size(), 2u);
    EXPECT_EQ(again.liked()[1].station, "Warehouse FM");
    EXPECT_EQ(again.station_of(again.liked()[1].url), 0);
    EXPECT_EQ(again.station_of("http://gone.example/"), -1);
    EXPECT_TRUE(again.set_liked(again.liked()[1], false));
    EXPECT_FALSE(again.is_liked(song));
    std::remove(path.c_str());
    std::remove(exported.c_str());
}

} // namespace
