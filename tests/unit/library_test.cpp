// Afterglow - Own-music tests: tags, covers, scanning, playing from a drive.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/library.hpp"
#include "audio/mixer.hpp"
#include "audio/radio.hpp"
#include "core/save_file.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace
{

using hui::audio::TrackTags;

// The test tone without the ID3 tag it comes with.
std::string tone()
{
    std::string data;
    EXPECT_TRUE(
        hui::save::read_file(HUI_SOURCE_DIR "/tests/unit/data/tone-44k-mono.mp3", &data, 1u << 20));
    if (data.size() > 10 && data.compare(0, 3, "ID3") == 0)
    {
        std::size_t size = 0;
        for (int i = 6; i < 10; ++i)
            size =
                size << 7 | (static_cast<unsigned char>(data[static_cast<std::size_t>(i)]) & 0x7f);
        data.erase(0, 10 + size);
    }
    return data;
}

std::string be32(std::uint32_t v)
{
    std::string out(4, '\0');
    for (int i = 0; i < 4; ++i)
        out[static_cast<std::size_t>(i)] = static_cast<char>((v >> (24 - 8 * i)) & 0xff);
    return out;
}

std::string syncsafe(std::uint32_t v)
{
    std::string out(4, '\0');
    for (int i = 0; i < 4; ++i)
        out[static_cast<std::size_t>(i)] = static_cast<char>((v >> (21 - 7 * i)) & 0x7f);
    return out;
}

// An ID3v2.3 or 2.4 frame.
std::string frame(int major, const std::string &id, const std::string &body)
{
    const auto size = static_cast<std::uint32_t>(body.size());
    return id + (major == 4 ? syncsafe(size) : be32(size)) + std::string(2, '\0') + body;
}

std::string tag(int major, const std::string &frames)
{
    return std::string("ID3") + static_cast<char>(major) + std::string(2, '\0') +
           syncsafe(static_cast<std::uint32_t>(frames.size() + 64)) + frames +
           std::string(64, '\0');
}

hui::audio::ReadAt reader(const std::string &data)
{
    return [&data](std::uint64_t offset, std::size_t size, std::string *out)
    {
        *out = offset < data.size() ? data.substr(static_cast<std::size_t>(offset), size) : "";
        return true;
    };
}

const std::string kCover = std::string("\xFF\xD8\xFF\xE0", 4) + "a little jpeg";

std::string tagged_mp3(const std::string &title, const std::string &artist, int track)
{
    const std::string frames =
        frame(3, "TIT2", std::string(1, '\0') + title) +
        frame(3, "TPE1", std::string(1, '\0') + artist) +
        frame(3, "TALB", std::string(1, '\0') + "Night Shift") +
        frame(3, "TRCK", std::string(1, '\0') + std::to_string(track) + "/9") +
        frame(3, "APIC",
              std::string(1, '\0') + "image/jpeg" + std::string(1, '\0') + '\x03' + "front" +
                  std::string(1, '\0') + kCover);
    return tag(3, frames) + tone();
}

TEST(Library, ReadsId3Tags)
{
    const std::string song = tagged_mp3("Concrete Halls", "Nova Kessler", 4);
    TrackTags tags;
    ASSERT_TRUE(hui::audio::read_tags(reader(song), song.size(), &tags));
    EXPECT_EQ(tags.title, "Concrete Halls");
    EXPECT_EQ(tags.artist, "Nova Kessler");
    EXPECT_EQ(tags.album, "Night Shift");
    EXPECT_EQ(tags.track, 4);
    EXPECT_TRUE(tags.has_art);
    EXPECT_EQ(hui::audio::embedded_art(reader(song)), kCover);
}

TEST(Library, ReadsUtf16Utf8AndOldTags)
{
    // 2.4 with UTF-8; 2.3 with UTF-16 and a byte order mark.
    const std::string utf8 = tag(4, frame(4, "TIT2", std::string(1, '\x03') + "Gl\xC3\xA4ss"));
    TrackTags a;
    ASSERT_TRUE(hui::audio::read_tags(reader(utf8), utf8.size(), &a));
    EXPECT_EQ(a.title, "Gl\xC3\xA4ss");
    const std::string utf16 = tag(3, frame(3, "TPE1",
                                           std::string("\x01\xFF\xFE"
                                                       "M\0i\0r\0a\0",
                                                       11)));
    TrackTags b;
    ASSERT_TRUE(hui::audio::read_tags(reader(utf16), utf16.size(), &b));
    EXPECT_EQ(b.artist, "Mira");
    // ID3v1 at the end of a file without ID3v2.
    std::string v1 = tone() + "TAG";
    v1 += std::string("Night Drive 86") + std::string(16, '\0');
    v1 += std::string("The Paper Moons") + std::string(15, '\0');
    v1 += std::string(30, '\0');
    v1 += std::string(4, '\0');
    v1 += std::string(28, '\0') + '\0' + '\x07' + '\0';
    TrackTags c;
    ASSERT_TRUE(hui::audio::read_tags(reader(v1), v1.size(), &c));
    EXPECT_EQ(c.title, "Night Drive 86");
    EXPECT_EQ(c.artist, "The Paper Moons");
    EXPECT_EQ(c.track, 7);
    // Nothing at all.
    const std::string bare = tone();
    TrackTags d;
    EXPECT_FALSE(hui::audio::read_tags(reader(bare), bare.size(), &d));
    // A tag that lies about its frame sizes is read no further.
    std::string broken = tag(3, frame(3, "TIT2", std::string(1, '\0') + "Fine"));
    broken[13] = '\x7f';
    TrackTags e;
    hui::audio::read_tags(reader(broken), broken.size(), &e);
    SUCCEED();
}

TEST(Library, ReadsVorbisComments)
{
    std::string comments = "\x03vorbis";
    const auto le = [](std::uint32_t v)
    {
        std::string out(4, '\0');
        for (int i = 0; i < 4; ++i)
            out[static_cast<std::size_t>(i)] = static_cast<char>((v >> (8 * i)) & 0xff);
        return out;
    };
    const std::vector<std::string> fields = {"TITLE=Glasshouse", "ARTIST=Mira Sol",
                                             "TRACKNUMBER=2"};
    comments += le(6) + "vendor" + le(static_cast<std::uint32_t>(fields.size()));
    for (const std::string &f : fields)
        comments += le(static_cast<std::uint32_t>(f.size())) + f;
    const std::string ogg = "OggS" + std::string(60, '\0') + comments;
    TrackTags tags;
    ASSERT_TRUE(hui::audio::read_tags(reader(ogg), ogg.size(), &tags));
    EXPECT_EQ(tags.title, "Glasshouse");
    EXPECT_EQ(tags.artist, "Mira Sol");
    EXPECT_EQ(tags.track, 2);
}

TEST(Library, AddressesAndFileNames)
{
    EXPECT_TRUE(hui::audio::is_music_file("a.MP3"));
    EXPECT_TRUE(hui::audio::is_music_file("b.ogg"));
    EXPECT_TRUE(hui::audio::is_music_file("c.opus"));
    EXPECT_FALSE(hui::audio::is_music_file("cover.jpg"));
    EXPECT_FALSE(hui::audio::is_music_file("mp3"));
    EXPECT_EQ(hui::audio::file_path("file:///mnt/usb0/a b.mp3"), "/mnt/usb0/a b.mp3");
    EXPECT_EQ(hui::audio::file_path("http://x/"), "");
    const std::string art = hui::audio::art_address("file:///mnt/usb0/a.mp3");
    EXPECT_EQ(hui::audio::art_owner(art), "file:///mnt/usb0/a.mp3");
    EXPECT_EQ(hui::audio::file_path(art), "/mnt/usb0/a.mp3");
    EXPECT_EQ(hui::audio::art_owner("https://x/#art"), "");
}

std::string temp_folder()
{
    const std::string root =
        ::testing::TempDir() + "/afterglow-usb-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    EXPECT_TRUE(hui::save::ensure_directory(root));
    return root;
}

TEST(Library, ScansFoldersInTrackOrder)
{
    const std::string root = temp_folder();
    ASSERT_TRUE(hui::save::ensure_directory(root + "/Music"));
    ASSERT_TRUE(hui::save::ensure_directory(root + "/Music/Night Shift"));
    hui::save::write_atomic(root + "/Music/Night Shift/b.mp3", tagged_mp3("Second", "Nova", 2));
    hui::save::write_atomic(root + "/Music/Night Shift/a.mp3", tagged_mp3("Tenth", "Nova", 10));
    hui::save::write_atomic(root + "/Music/Night Shift/c.mp3", tagged_mp3("First", "Nova", 1));
    hui::save::write_atomic(root + "/Music/untagged song.mp3", tone());
    hui::save::write_atomic(root + "/Music/notes.txt", "not music");
    hui::save::write_atomic(root + "/.hidden.mp3", tone());
    std::map<std::string, std::string> covers;
    const auto songs = hui::audio::scan_library({root}, &covers);
    ASSERT_EQ(songs.size(), 4u);
    EXPECT_EQ(songs[0].name, "untagged song"); // its folder sorts first
    EXPECT_EQ(songs[0].style, "Music");        // no artist: the folder
    EXPECT_EQ(songs[1].name, "First");
    EXPECT_EQ(songs[2].name, "Second");
    EXPECT_EQ(songs[3].name, "Tenth");
    EXPECT_EQ(songs[1].style, "Nova");
    EXPECT_EQ(songs[1].country, "Night Shift");
    EXPECT_EQ(songs[1].category, hui::audio::kLibraryCategory);
    EXPECT_EQ(songs[1].format, "MP3");
    EXPECT_EQ(songs[1].url, "file://" + root + "/Music/Night Shift/c.mp3");
    EXPECT_EQ(covers.size(), 3u);
    EXPECT_EQ(hui::audio::file_art(hui::audio::file_path(songs[1].url)), kCover);
    // At most so many.
    EXPECT_EQ(hui::audio::scan_library({root}, nullptr, 2).size(), 2u);
    EXPECT_TRUE(hui::audio::scan_library({root + "/nowhere"}, nullptr).empty());
}

// Pumps the player and plays its audio out (as the audio thread would).
bool wait_until(hui::audio::RadioPlayer &player, hui::audio::Mixer &mixer,
                const std::function<bool()> &done)
{
    std::vector<std::int16_t> out(1024 * 2);
    for (int i = 0; i < 600; ++i)
    {
        player.pump(1.0f / 60.0f);
        for (int k = 0; k < 4; ++k)
            mixer.render(out.data(), 1024);
        if (done())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

TEST(Library, PlaysSongsFromADriveOneAfterAnother)
{
    const std::string root = temp_folder();
    hui::save::write_atomic(root + "/1.mp3", tagged_mp3("Concrete Halls", "Nova Kessler", 1));
    hui::save::write_atomic(root + "/2.mp3", tagged_mp3("Glasshouse", "Mira Sol", 2));
    std::map<std::string, std::string> covers;
    auto songs = hui::audio::scan_library({root}, &covers);
    ASSERT_EQ(songs.size(), 2u);
    hui::audio::Mixer mixer;
    hui::audio::RadioPlayer player;
    int network = 0;
    player.init(mixer, 1, nullptr,
                [&](const std::string &, const std::atomic<bool> &)
                {
                    ++network; // never: songs are read from the file
                    return hui::net::OpenResult{};
                });
    player.set_stations({{"Techno", "A", "", "USA", "MP3", "http://a.example/"}});
    player.set_library(std::move(songs), std::move(covers));
    ASSERT_EQ(player.stations().size(), 3u);
    EXPECT_EQ(player.logo(1), hui::audio::art_address(player.stations()[1].url));
    player.set_setting("xfade", "0");
    player.play(1, {1, 2});
    ASSERT_TRUE(wait_until(player, mixer, [&]
                           { return player.status().title == "Nova Kessler - Concrete Halls"; }));
    // The song is short: it ends and the next one plays.
    ASSERT_TRUE(wait_until(player, mixer, [&] { return player.status().station == 2; }));
    ASSERT_TRUE(wait_until(player, mixer,
                           [&] { return player.status().title == "Mira Sol - Glasshouse"; }));
    EXPECT_EQ(network, 0);
    // The drive's songs change; the playing one keeps its place by address.
    std::vector<hui::audio::Station> fewer = {player.stations()[2]};
    player.set_library(std::move(fewer), {});
    EXPECT_EQ(player.stations().size(), 2u);
    EXPECT_EQ(player.status().station, 1);
    player.stop();
    // A song that is gone fails with a reason.
    player.set_library(
        {{hui::audio::kLibraryCategory, "Gone", "", "", "MP3", "file://" + root + "/gone.mp3"}},
        {});
    player.play(1);
    ASSERT_TRUE(wait_until(player, mixer, [&]
                           { return player.status().state == hui::audio::RadioState::failed; }));
    EXPECT_NE(player.status().message.find("drive"), std::string::npos);
}

} // namespace
