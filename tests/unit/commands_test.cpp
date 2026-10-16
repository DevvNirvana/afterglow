// Afterglow - Command parser tests: the words people use, on the real list.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/library.hpp"
#include "audio/radio.hpp"
#include "concepts/commands.hpp"
#include "core/save_file.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{

using hui::concepts::CommandKind;
using hui::concepts::parse_command;

const std::vector<hui::audio::Station> &stations()
{
    static const std::vector<hui::audio::Station> kStations = []
    {
        std::string text;
        hui::save::read_file(std::string(HUI_SOURCE_DIR) + "/assets/radio/stations.txt", &text,
                             1u << 20);
        return hui::audio::parse_stations(text);
    }();
    return kStations;
}

const std::vector<hui::concepts::SceneRef> &scenes()
{
    static const std::vector<hui::concepts::SceneRef> kScenes = {
        {"Paint Swirl", "AFTERGLOW"}, {"Wormhole", "TRIP"},     {"Kaleidoscope", "TRIP"},
        {"Tide Pool", "CALM"},        {"Paper Cut", "GALLERY"}, {"Skyline", "RETRO"}};
    return kScenes;
}

hui::concepts::Command parse(const char *text)
{
    return parse_command(text, stations(), scenes());
}

std::string station_name(const hui::concepts::Command &command)
{
    return command.station >= 0 ? stations()[static_cast<std::size_t>(command.station)].name : "";
}

TEST(Commands, CleansTheWords)
{
    EXPECT_EQ(hui::concepts::normalize_command("Hey, can you PLAY some Bollywood Nineties?"),
              "play bollywood 90s");
    EXPECT_EQ(hui::concepts::normalize_command("R&B"), "r and b");
    EXPECT_EQ(hui::concepts::normalize_command("90's hits"), "90s hits");
    EXPECT_EQ(hui::concepts::normalize_command("   "), "");
}

TEST(Commands, PlaysAStationByItsWords)
{
    auto c = parse("play bollywood 90s");
    ASSERT_EQ(c.kind, CommandKind::play_station);
    EXPECT_EQ(station_name(c), "Radio Bollywood 90s");
    c = parse("Play Bollywood nineties please");
    ASSERT_EQ(c.kind, CommandKind::play_station);
    EXPECT_EQ(station_name(c), "Radio Bollywood 90s");
    c = parse("put on arijit singh");
    ASSERT_EQ(c.kind, CommandKind::play_station);
    EXPECT_EQ(station_name(c), "Arijit Singh Radio");
    EXPECT_TRUE(c.asked);
    c = parse("hindi retro");
    ASSERT_EQ(c.kind, CommandKind::play_station);
    EXPECT_EQ(station_name(c), "Hindi Retro");
    EXPECT_FALSE(c.asked); // a bare name: a search box searches
    c = parse("play technobase");
    ASSERT_EQ(c.kind, CommandKind::play_station);
    EXPECT_EQ(station_name(c), "TechnoBase.FM");
}

TEST(Commands, PlaysAGenreByItsEverydayName)
{
    for (const auto &[words, genre] : std::vector<std::pair<const char *, const char *>>{
             {"play techno", "Techno"},
             {"play some k-pop", "K-Pop"},
             {"put on reggaeton", "Latin"},
             {"play hindi songs", "Bollywood & Desi"},
             {"dnb", "Drum & Bass"},
             {"play drum & bass", "Drum & Bass"},
             {"play amapiano", "Afrobeats"},
             {"play arabic music", "Arabic"},
             {"play lo-fi", "Chill & Electronica"},
             {"play r&b", "R&B & Soul"},
             {"play hip hop", "Hip-Hop & Rap"}})
    {
        const auto c = parse(words);
        ASSERT_EQ(c.kind, CommandKind::play_genre) << words;
        EXPECT_EQ(c.genre, genre) << words;
        ASSERT_FALSE(c.queue.empty()) << words;
        EXPECT_EQ(c.station, c.queue.front());
        for (int index : c.queue)
            EXPECT_EQ(stations()[static_cast<std::size_t>(index)].category, genre);
    }
}

TEST(Commands, TheTransportAndTheSong)
{
    EXPECT_EQ(parse("next station").kind, CommandKind::next);
    EXPECT_EQ(parse("skip").kind, CommandKind::next);
    EXPECT_TRUE(parse("skip").asked);
    EXPECT_EQ(parse("change the station").kind, CommandKind::next);
    EXPECT_EQ(parse("something else").kind, CommandKind::next);
    EXPECT_EQ(parse("previous station").kind, CommandKind::previous);
    EXPECT_EQ(parse("go back").kind, CommandKind::previous);
    EXPECT_EQ(parse("pause").kind, CommandKind::pause);
    EXPECT_EQ(parse("stop").kind, CommandKind::stop);
    EXPECT_EQ(parse("resume").kind, CommandKind::resume);
    EXPECT_EQ(parse("play").kind, CommandKind::resume);
    EXPECT_EQ(parse("like this song").kind, CommandKind::like);
    EXPECT_EQ(parse("I love this").kind, CommandKind::like);
    EXPECT_EQ(parse("unlike").kind, CommandKind::unlike);
    // "love" inside a request to play is not a like.
    EXPECT_NE(parse("play love songs").kind, CommandKind::like);
}

TEST(Commands, TheVisualsAndTheRoom)
{
    auto c = parse("show trippy scenes");
    ASSERT_EQ(c.kind, CommandKind::scene_pack);
    EXPECT_EQ(c.pack, "TRIP");
    c = parse("calm visuals");
    ASSERT_EQ(c.kind, CommandKind::scene_pack);
    EXPECT_EQ(c.pack, "CALM");
    c = parse("show paper cut");
    ASSERT_EQ(c.kind, CommandKind::scene);
    EXPECT_EQ(c.scene, "Paper Cut");
    c = parse("scene wormhole");
    ASSERT_EQ(c.kind, CommandKind::scene);
    EXPECT_EQ(c.scene, "Wormhole");
    EXPECT_EQ(parse("show me a unicorn scene").kind, CommandKind::none);
    c = parse("party mode");
    ASSERT_EQ(c.kind, CommandKind::display);
    EXPECT_EQ(c.display, 1);
    EXPECT_EQ(parse("ambient mode").display, 2);
    c = parse("auto vj off");
    ASSERT_EQ(c.kind, CommandKind::auto_vj);
    EXPECT_FALSE(c.on);
    EXPECT_TRUE(parse("start auto vj").on);
    EXPECT_EQ(parse("hide").kind, CommandKind::hide);
    EXPECT_EQ(parse("show controls").kind, CommandKind::show);
    EXPECT_EQ(parse("turn it up").kind, CommandKind::louder);
    EXPECT_EQ(parse("volume down").kind, CommandKind::quieter);
    c = parse("sleep in 45 minutes");
    ASSERT_EQ(c.kind, CommandKind::sleep);
    EXPECT_EQ(c.minutes, 45);
    EXPECT_EQ(parse("sleep in an hour").minutes, 60);
    EXPECT_EQ(parse("stop in thirty minutes").minutes, 30);
    EXPECT_EQ(parse("sleep timer off").minutes, 0);
    EXPECT_EQ(parse("play the afterglow mix").kind, CommandKind::play_songs);
}

TEST(Commands, MyMusicPlaysTheSongsOnTheDrive)
{
    std::vector<hui::audio::Station> list = stations();
    EXPECT_EQ(parse_command("play my music", list, scenes()).kind, CommandKind::none); // no drive
    hui::audio::Station song;
    song.category = hui::audio::kLibraryCategory;
    song.name = "Concrete Halls";
    song.style = "Nova Kessler";
    song.url = "file:///mnt/usb0/a.mp3";
    list.push_back(song);
    for (const char *words : {"play my music", "play my songs", "play music from usb"})
    {
        const auto c = parse_command(words, list, scenes());
        ASSERT_EQ(c.kind, CommandKind::play_genre) << words;
        EXPECT_EQ(c.genre, hui::audio::kLibraryCategory);
        EXPECT_EQ(c.station, static_cast<int>(list.size()) - 1);
    }
    const auto c = parse_command("play nova kessler", list, scenes());
    ASSERT_EQ(c.kind, CommandKind::play_station);
    EXPECT_EQ(c.station, static_cast<int>(list.size()) - 1);
}

TEST(Commands, WhatIsNotUnderstoodIsASearch)
{
    const auto c = parse("zzzz qqqq");
    EXPECT_EQ(c.kind, CommandKind::none);
    EXPECT_EQ(c.words, "zzzz qqqq");
    EXPECT_EQ(parse("").kind, CommandKind::none);
    EXPECT_EQ(parse("?!").kind, CommandKind::none);
    EXPECT_EQ(hui::concepts::describe_command(parse("play bollywood 90s"), stations()),
              "Playing Radio Bollywood 90s");
    EXPECT_EQ(hui::concepts::describe_command(parse("show calm scenes"), stations()),
              "Calm scenes");
}

} // namespace
