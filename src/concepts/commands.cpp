// Afterglow - Short commands in plain words (see commands.hpp).
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later

#include "concepts/commands.hpp"

#include <algorithm>
#include <cstdlib>
#include <set>

namespace hui::concepts
{

namespace
{

using Words = std::vector<std::string>;

// Words that carry no meaning in a request.
const std::set<std::string> &filler()
{
    static const std::set<std::string> kWords = {
        "please",  "pls",   "plz",       "can",  "could",  "would", "will",   "you",     "u",
        "hey",     "hi",    "ok",        "okay", "siri",   "i",     "want",   "wanna",   "to",
        "listen",  "hear",  "some",      "the",  "a",      "an",    "me",     "for",     "of",
        "music",   "now",   "just",      "go",   "let",    "lets",  "us",     "station", "stations",
        "channel", "radio", "something", "kind", "little", "bit",   "thanks", "thank",   "from"};
    return kWords;
}

// Words an everyday request uses for a number.
int number_word(const std::string &word)
{
    static const std::pair<const char *, int> kNumbers[] = {
        {"one", 1},      {"two", 2},     {"three", 3},   {"four", 4},   {"five", 5},
        {"six", 6},      {"seven", 7},   {"eight", 8},   {"nine", 9},   {"ten", 10},
        {"fifteen", 15}, {"twenty", 20}, {"thirty", 30}, {"forty", 40}, {"fortyfive", 45},
        {"fifty", 50},   {"sixty", 60},  {"ninety", 90}, {"half", 30},  {"an", 1}};
    for (const auto &[name, value] : kNumbers)
        if (word == name)
            return value;
    if (!word.empty() &&
        std::all_of(word.begin(), word.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return std::atoi(word.c_str());
    return -1;
}

Words split(const std::string &text)
{
    Words words;
    std::string word;
    for (const char c : text)
    {
        if (c == ' ')
        {
            if (!word.empty())
                words.push_back(word);
            word.clear();
        }
        else
        {
            word += c;
        }
    }
    if (!word.empty())
        words.push_back(word);
    return words;
}

std::string join(const Words &words, std::size_t from = 0)
{
    std::string text;
    for (std::size_t i = from; i < words.size(); ++i)
        text += (text.empty() ? "" : " ") + words[i];
    return text;
}

// Lower case; letters, digits and other scripts kept; '&' read as "and";
// an apostrophe joins ("90's" is "90s"); everything else separates.
std::string clean(std::string_view text)
{
    std::string out;
    for (const char raw : text)
    {
        const auto c = static_cast<unsigned char>(raw);
        if (c >= 'A' && c <= 'Z')
            out += static_cast<char>(c - 'A' + 'a');
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c >= 0x80)
            out += raw;
        else if (c == '&')
            out += " and ";
        else if (c == '\'')
            continue;
        else
            out += ' ';
    }
    // Decades and spellings people use.
    Words words = split(out);
    Words fixed;
    for (std::size_t i = 0; i < words.size(); ++i)
    {
        const std::string &w = words[i];
        const std::string next = i + 1 < words.size() ? words[i + 1] : "";
        if (w == "nineties")
            fixed.emplace_back("90s");
        else if (w == "eighties")
            fixed.emplace_back("80s");
        else if (w == "seventies")
            fixed.emplace_back("70s");
        else if (w == "sixties")
            fixed.emplace_back("60s");
        else if (w == "noughties" || w == "twothousands")
            fixed.emplace_back("2000s");
        else if (w == "two" && (next == "thousands" || next == "thousand"))
        {
            fixed.emplace_back("2000s");
            ++i;
        }
        else if (w == "forty" && next == "five")
        {
            fixed.emplace_back("45");
            ++i;
        }
        else
            fixed.push_back(w);
    }
    return join(fixed);
}

bool has(const Words &words, const char *word)
{
    return std::find(words.begin(), words.end(), word) != words.end();
}

// The words in order contain this phrase ("volume up").
bool phrase(const Words &words, std::string_view text)
{
    const Words wanted = split(std::string(text));
    if (wanted.empty() || wanted.size() > words.size())
        return false;
    for (std::size_t i = 0; i + wanted.size() <= words.size(); ++i)
        if (std::equal(wanted.begin(), wanted.end(),
                       words.begin() + static_cast<std::ptrdiff_t>(i)))
            return true;
    return false;
}

Words without_filler(const Words &words)
{
    Words kept;
    for (std::size_t i = 0; i < words.size(); ++i)
    {
        // The app's own name is a wake word only at the start.
        if (i == 0 && words[i] == "afterglow")
            continue;
        if (filler().count(words[i]) == 0)
            kept.push_back(words[i]);
    }
    return kept;
}

// Everyday names for the genres, in the cleaned form.
struct GenreName
{
    const char *category;
    const char *names[9];
};
// (The library category is kLibraryCategory; spelled out here to keep the
// table a constant.)

constexpr GenreName kGenreNames[] = {
    {"Techno", {"techno", "minimal techno", "hard techno"}},
    {"House", {"house", "house music"}},
    {"Trance", {"trance", "uplifting trance", "psytrance", "psy trance"}},
    {"Drum & Bass", {"dnb", "drum and bass", "drum n bass", "d and b", "jungle", "drumandbass"}},
    {"Bass & Dubstep", {"dubstep", "bass", "bass music", "riddim", "dub step"}},
    {"Hard Dance", {"hardstyle", "hard dance", "hardcore", "gabber", "hard style"}},
    {"EDM & Dance", {"edm", "dance", "dance music", "festival", "big room"}},
    {"Synthwave", {"synthwave", "synth wave", "outrun", "retrowave", "retro wave"}},
    {"Chill & Electronica",
     {"chill", "chillout", "chill out", "ambient", "lofi", "lo fi", "downtempo", "relaxing",
      "relax"}},
    {"Pop & Hits", {"pop", "hits", "top 40", "top forty", "charts", "pop hits"}},
    {"Hip-Hop & Rap", {"hip hop", "hiphop", "rap", "trap"}},
    {"R&B & Soul", {"rnb", "r and b", "r b", "soul", "r and b and soul"}},
    {"Bollywood & Desi",
     {"bollywood", "hindi", "desi", "indian", "filmi", "bollywood and desi", "hindi songs"}},
    {"Latin", {"latin", "reggaeton", "salsa", "bachata", "spanish", "latino", "latin pop"}},
    {"K-Pop", {"kpop", "k pop", "korean", "korean pop"}},
    {"Afrobeats", {"afrobeats", "afrobeat", "afro", "amapiano", "naija", "african", "afro house"}},
    {"Arabic", {"arabic", "arab", "khaleeji", "arabian", "arabic pop"}},
    // The listener's songs on a drive ("my music": "music" is a filler word).
    {"My Music", {"my", "my own", "own", "usb", "my usb", "drive", "my drive", "library"}},
};

// The category the whole of `query` names, or "".
std::string genre_of(const std::string &query, const std::vector<audio::Station> &stations)
{
    for (const GenreName &genre : kGenreNames)
    {
        for (const char *name : genre.names)
        {
            if (name == nullptr || query != name)
                continue;
            const bool listed =
                std::any_of(stations.begin(), stations.end(),
                            [&](const audio::Station &s) { return s.category == genre.category; });
            if (listed)
                return genre.category;
        }
    }
    // A category spelled out ("hip hop and rap").
    for (const audio::Station &s : stations)
        if (clean(s.category) == query)
            return s.category;
    return {};
}

bool matches(const std::string &query, const std::string &word)
{
    if (query == word)
        return true;
    // A word typed in part ("bolly" for "bollywood"), long enough to mean it.
    return query.size() >= 4 && word.size() > query.size() &&
           word.compare(0, query.size(), query) == 0;
}

// The station whose name, style and genre best cover the query's words.
int best_station(const Words &query, const std::vector<audio::Station> &stations)
{
    int best = -1;
    float best_score = 0.0f;
    std::size_t best_matched = 0;
    for (std::size_t i = 0; i < stations.size(); ++i)
    {
        const audio::Station &s = stations[i];
        const Words name = without_filler(split(clean(s.name)));
        const Words style = split(clean(s.style));
        const Words genre = split(clean(s.category));
        const Words country = split(clean(s.country));
        float score = 0.0f;
        std::size_t matched = 0;
        std::size_t name_matched = 0;
        for (const std::string &q : query)
        {
            const auto in = [&](const Words &field)
            {
                return std::any_of(field.begin(), field.end(),
                                   [&](const std::string &w) { return matches(q, w); });
            };
            float word = 0.0f;
            if (in(name))
            {
                word = 3.0f;
                ++name_matched;
            }
            else if (in(style))
                word = 2.0f;
            else if (in(genre))
                word = 1.5f;
            else if (in(country))
                word = 1.0f;
            if (word > 0.0f)
                ++matched;
            score += word;
        }
        // Every word of a short name asked for: that station, not one that
        // merely mentions the words ("Hindi Retro" over "Hindi Hits").
        if (name_matched == name.size() && !name.empty())
            score += 2.0f;
        score -= 0.05f * static_cast<float>(name.size()); // the more exact name wins a tie
        if (matched > best_matched || (matched == best_matched && score > best_score))
        {
            best = static_cast<int>(i);
            best_score = score;
            best_matched = matched;
        }
    }
    // At least most of what was asked must be there.
    if (best < 0 || best_matched == 0 || best_matched * 3 < query.size() * 2)
        return -1;
    return best;
}

int minutes_in(const Words &words)
{
    int minutes = -1;
    for (std::size_t i = 0; i < words.size(); ++i)
    {
        const int n = number_word(words[i]);
        if (n < 0)
            continue;
        const std::string unit = i + 1 < words.size() ? words[i + 1] : "";
        minutes = unit == "hour" || unit == "hours" ? n * 60 : n;
        if (words[i] == "half" && (unit == "hour" || unit == "an"))
            minutes = 30;
        break;
    }
    if (minutes < 0 && (has(words, "hour") || has(words, "hours")))
        minutes = 60;
    return minutes;
}

std::string scene_named(const Words &words, const std::vector<SceneRef> &scenes)
{
    // The longest scene name whose words appear, in order, in the request.
    std::string found;
    std::size_t longest = 0;
    for (const SceneRef &scene : scenes)
    {
        const std::string name = clean(scene.name);
        if (!name.empty() && phrase(words, name) && name.size() > longest)
        {
            found = scene.name;
            longest = name.size();
        }
    }
    return found;
}

std::string pack_named(const Words &words)
{
    static const std::pair<const char *, const char *> kPacks[] = {
        {"trippy", "TRIP"},     {"trip", "TRIP"},         {"psychedelic", "TRIP"},
        {"acid", "TRIP"},       {"melting", "TRIP"},      {"calm", "CALM"},
        {"chill", "CALM"},      {"relaxing", "CALM"},     {"soft", "CALM"},
        {"slow", "CALM"},       {"peaceful", "CALM"},     {"sleepy", "CALM"},
        {"club", "CLUB"},       {"lasers", "CLUB"},       {"rave", "CLUB"},
        {"art", "GALLERY"},     {"artsy", "GALLERY"},     {"arty", "GALLERY"},
        {"gallery", "GALLERY"}, {"classy", "GALLERY"},    {"unique", "GALLERY"},
        {"retro", "RETRO"},     {"vintage", "RETRO"},     {"arcade", "RETRO"},
        {"80s", "RETRO"},       {"classic", "AFTERGLOW"}, {"original", "AFTERGLOW"}};
    for (const auto &[word, pack] : kPacks)
        if (has(words, word))
            return pack;
    return {};
}

bool starts(const Words &words, std::initializer_list<const char *> first)
{
    if (words.empty())
        return false;
    for (const char *word : first)
        if (words.front() == word)
            return true;
    return false;
}

} // namespace

std::string normalize_command(std::string_view text)
{
    return join(without_filler(split(clean(text))));
}

Command parse_command(std::string_view text, const std::vector<audio::Station> &stations,
                      const std::vector<SceneRef> &scenes)
{
    Command command;
    const Words all = split(clean(text));
    const Words words = without_filler(all);
    command.words = join(words);
    command.asked = true; // every intent below has its verb, but a bare name
    if (all.empty())
        return command;

    // The display, the controls and the sound first: their words are few.
    if (has(all, "sleep") || ((has(all, "stop") || has(all, "off")) &&
                              (has(all, "in") || has(all, "after")) && minutes_in(all) > 0))
    {
        command.kind = CommandKind::sleep;
        command.minutes = has(all, "cancel") || (has(all, "off") && minutes_in(all) < 0)
                              ? 0
                              : std::max(0, minutes_in(all) < 0 ? 30 : minutes_in(all));
        return command;
    }
    if (has(all, "louder") || phrase(all, "volume up") || phrase(all, "turn it up") ||
        phrase(all, "turn up") || phrase(all, "more volume"))
    {
        command.kind = CommandKind::louder;
        return command;
    }
    if (has(all, "quieter") || has(all, "softer") || phrase(all, "volume down") ||
        phrase(all, "turn it down") || phrase(all, "turn down") || phrase(all, "less volume"))
    {
        command.kind = CommandKind::quieter;
        return command;
    }
    if (has(all, "mode") || has(all, "display"))
    {
        if (has(all, "party"))
            command.display = 1;
        else if (has(all, "ambient") || has(all, "dim"))
            command.display = 2;
        else if (has(all, "normal") || has(all, "default"))
            command.display = 0;
        else
            command.display = -1;
        if (command.display >= 0)
        {
            command.kind = CommandKind::display;
            return command;
        }
    }
    if (has(all, "vj") || has(all, "autovj") || has(all, "autopilot"))
    {
        command.kind = CommandKind::auto_vj;
        command.on = !(has(all, "off") || has(all, "stop") || has(all, "disable"));
        return command;
    }
    if (starts(words, {"hide"}))
    {
        command.kind = CommandKind::hide;
        return command;
    }
    if (starts(words, {"show"}) &&
        (has(words, "controls") || has(words, "menu") || has(words, "overlay") ||
         has(words, "hud") || has(words, "buttons")))
    {
        command.kind = CommandKind::show;
        return command;
    }

    // The song on air.
    if (starts(words, {"unlike", "dislike", "unheart"}) || phrase(words, "remove like") ||
        phrase(words, "remove from liked"))
    {
        command.kind = CommandKind::unlike;
        return command;
    }
    if (starts(words, {"like", "love", "heart", "save"}) || phrase(words, "add to liked"))
    {
        command.kind = CommandKind::like;
        return command;
    }

    // The transport.
    if (starts(words, {"next", "skip"}) || phrase(all, "change station") ||
        phrase(all, "change the station") || phrase(all, "change channel") ||
        phrase(all, "switch station") || phrase(all, "another station") ||
        phrase(all, "another one") || phrase(all, "something else") ||
        (words.size() == 1 && starts(words, {"change", "switch", "another"})))
    {
        command.kind = CommandKind::next;
        return command;
    }
    if (starts(words, {"previous", "prev", "back"}) || phrase(all, "go back") ||
        phrase(all, "last station") || phrase(all, "last one"))
    {
        command.kind = CommandKind::previous;
        return command;
    }
    if (words.size() <= 2 && starts(words, {"pause", "mute", "hold"}))
    {
        command.kind = CommandKind::pause;
        return command;
    }
    if (words.size() <= 2 && starts(words, {"stop"}))
    {
        command.kind = CommandKind::stop;
        return command;
    }
    if (words.size() <= 2 && (starts(words, {"resume", "continue", "unpause", "unmute"}) ||
                              phrase(words, "keep playing")))
    {
        command.kind = CommandKind::resume;
        return command;
    }
    if (words.size() == 1 && starts(words, {"play"}))
    {
        command.kind = CommandKind::resume;
        return command;
    }

    // The visuals.
    const bool visual = has(all, "scene") || has(all, "scenes") || has(all, "visual") ||
                        has(all, "visuals") || has(all, "visualizer") || has(all, "visualiser") ||
                        has(all, "vibe") || starts(words, {"show"});
    if (visual)
    {
        const std::string scene = scene_named(words, scenes);
        if (!scene.empty())
        {
            command.kind = CommandKind::scene;
            command.scene = scene;
            return command;
        }
        const std::string pack = pack_named(words);
        if (!pack.empty())
        {
            command.kind = CommandKind::scene_pack;
            command.pack = pack;
            return command;
        }
        return command; // a scene we do not have
    }
    if (words.size() <= 2 && (starts(words, {"open", "browse"}) || phrase(all, "show stations")))
    {
        command.kind = CommandKind::open_radio;
        return command;
    }
    if (phrase(all, "built in songs") || phrase(all, "offline songs") ||
        phrase(all, "afterglow mix") || phrase(all, "afterglow songs"))
    {
        command.kind = CommandKind::play_songs;
        return command;
    }

    // Playing something: "play", "put on", "tune in to", or just the name.
    Words query = words;
    command.asked =
        starts(query, {"play", "put", "start", "tune", "switch", "change", "turn", "give"});
    if (command.asked)
        query.erase(query.begin());
    while (!query.empty() &&
           (query.front() == "on" || query.front() == "in" || query.front() == "into"))
        query.erase(query.begin());
    // Words that only describe what is wanted.
    query.erase(std::remove_if(query.begin(), query.end(),
                               [](const std::string &w)
                               {
                                   return w == "songs" || w == "song" || w == "mix" ||
                                          w == "vibes" || w == "tunes" || w == "track" ||
                                          w == "tracks" || w == "playlist";
                               }),
                query.end());
    if (query.empty())
        return command;
    const std::string genre = genre_of(join(query), stations);
    if (!genre.empty())
    {
        command.kind = CommandKind::play_genre;
        command.genre = genre;
        for (std::size_t i = 0; i < stations.size(); ++i)
            if (stations[i].category == genre)
                command.queue.push_back(static_cast<int>(i));
        command.station = command.queue.empty() ? -1 : command.queue.front();
        return command;
    }
    const int station = best_station(query, stations);
    if (station >= 0)
    {
        command.kind = CommandKind::play_station;
        command.station = station;
    }
    return command;
}

std::string describe_command(const Command &command, const std::vector<audio::Station> &stations)
{
    const auto station_name = [&](int index)
    {
        return index >= 0 && index < static_cast<int>(stations.size())
                   ? stations[static_cast<std::size_t>(index)].name
                   : std::string("a station");
    };
    switch (command.kind)
    {
    case CommandKind::play_station:
        return "Playing " + station_name(command.station);
    case CommandKind::play_genre:
        return "Playing " + command.genre + ": " + station_name(command.station);
    case CommandKind::play_songs:
        return "Playing the Afterglow mix";
    case CommandKind::resume:
        return "Playing";
    case CommandKind::pause:
        return "Paused";
    case CommandKind::stop:
        return "Stopped";
    case CommandKind::next:
        return "Next station";
    case CommandKind::previous:
        return "Previous station";
    case CommandKind::like:
        return "Liked";
    case CommandKind::unlike:
        return "Removed from Liked songs";
    case CommandKind::scene:
        return "Scene: " + command.scene;
    case CommandKind::scene_pack:
    {
        std::string pack = command.pack;
        for (std::size_t i = 1; i < pack.size(); ++i)
            pack[i] =
                static_cast<char>(pack[i] >= 'A' && pack[i] <= 'Z' ? pack[i] - 'A' + 'a' : pack[i]);
        return pack + " scenes";
    }
    case CommandKind::auto_vj:
        return command.on ? "Auto-VJ on" : "Auto-VJ off";
    case CommandKind::display:
        return command.display == 1   ? "Party display"
               : command.display == 2 ? "Ambient display"
                                      : "Normal display";
    case CommandKind::hide:
        return "Controls hidden";
    case CommandKind::show:
        return "Controls shown";
    case CommandKind::louder:
        return "Louder";
    case CommandKind::quieter:
        return "Quieter";
    case CommandKind::sleep:
        return command.minutes > 0 ? "Sleep in " + std::to_string(command.minutes) + " min"
                                   : "Sleep timer off";
    case CommandKind::open_radio:
        return "Radio";
    case CommandKind::none:
        break;
    }
    return "Not understood";
}

} // namespace hui::concepts
