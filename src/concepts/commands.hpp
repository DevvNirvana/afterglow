// Afterglow - Short commands in plain words: "play bollywood 90s", "next".
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What a listener types into the command box (or, one day, says) is turned
// into one action. The parser is forgiving: case, punctuation and filler
// ("please", "can you", "some") do not matter, numbers can be words
// ("nineties"), and a station is found by the words of its name, style and
// genre, so "play bollywood nineties" finds Radio Bollywood 90s. Genres have
// everyday names ("hindi", "k pop", "reggaeton", "dnb"). Anything not
// understood comes back as CommandKind::none with the cleaned words, and the
// caller treats it as a search.

#pragma once

#include "audio/radio.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace hui::concepts
{

enum class CommandKind : std::uint8_t
{
    none,         // not understood
    play_station, // station: the index
    play_genre,   // genre: the category; queue: its stations
    play_songs,   // the songs that come with the app
    resume,
    pause,
    stop,
    next,
    previous,
    like,
    unlike,
    scene,      // scene: a scene's name
    scene_pack, // pack: "TRIP", "CALM", ...
    auto_vj,    // on: start or stop Auto-VJ
    display,    // display: 0 normal, 1 party, 2 ambient
    hide,       // hide the controls
    show,       // show them
    louder,
    quieter,
    sleep, // minutes (0: off)
    open_radio,
};

struct SceneRef
{
    std::string name; // "Paper Cut"
    std::string pack; // "GALLERY"
};

struct Command
{
    CommandKind kind = CommandKind::none;
    int station = -1;
    std::string genre;
    std::vector<int> queue;
    std::string scene;
    std::string pack;
    bool on = true;
    int display = 0;
    int minutes = 0;
    std::string words; // the request, cleaned (what a search would use)
    // Asked for in so many words: a verb ("play techno", "next"). A bare
    // name ("techno") is also a search, and a search box treats it as one.
    bool asked = false;
};

// Lower case, letters and digits only, filler words out, numbers as digits
// ("nineties" -> "90s", "two thousands" -> "2000s").
std::string normalize_command(std::string_view text);

Command parse_command(std::string_view text, const std::vector<audio::Station> &stations,
                      const std::vector<SceneRef> &scenes);

// How the box reads a command back, for the note on screen ("Playing ...").
std::string describe_command(const Command &command, const std::vector<audio::Station> &stations);

} // namespace hui::concepts
