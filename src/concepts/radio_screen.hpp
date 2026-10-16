// Afterglow - The radio browser and the now-playing bar.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The radio browser is a full-screen panel over the (frosted) visualiser,
// laid out like a satellite-radio receiver: channels on the left (favourites,
// recently played, liked songs, the song history, every station, then one
// channel per genre), the stations
// of the chosen channel in the middle, what is playing on the right with its
// cover and transport. Search types into an on-screen keyboard and filters
// every station by name, style, genre and country as you type.
//
// The screen wears the skin of the genre being browsed (genre_skin.hpp): its
// banner, face, colours and list styling morph as the focus moves from
// channel to channel. Favourites, recents and the full list wear the skin of
// what is playing.
//
// The now-playing bar is the strip the visualiser shows at the bottom while
// its overlay is up: the cover, the station and the song in the station's
// skin, the state and the transport.

#pragma once

#include "app/concept.hpp"
#include "audio/radio.hpp"
#include "concepts/genre_skin.hpp"
#include "ui/components/keyboard.hpp"
#include "ui/components/list.hpp"

#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace hui::concepts
{

// A colour per genre channel, the same everywhere it shows (the skin's accent).
gfx::Color channel_colour(const std::string &category);

// "4:05", "1:02:33".
std::string clock_text(double seconds);

// The station playing, if the radio is the source.
const audio::Station *playing_station(const audio::RadioControl *radio,
                                      const audio::PlayerStatus &status);

// The cover for what plays: album art when it has arrived (cross-faded in),
// otherwise the station's generated cover in its skin. A vinyl or CD can peek
// out from behind it (disc_out 0..1).
void draw_cover(gfx::DrawList &list, const ui::Fonts &fonts, const gfx::Rect &r,
                const GenreSkin &skin, const audio::PlayerStatus &status,
                const audio::Station *station, const Pulse &pulse, float alpha);

// The now-playing bar.
void draw_now_playing(gfx::DrawList &list, const ui::Fonts &fonts, const gfx::Rect &box,
                      const audio::PlayerStatus &status, const audio::Station *station,
                      const Pulse &pulse, std::uint32_t glass);

class RadioScreen
{
  public:
    explicit RadioScreen(app::Context &context);

    bool is_open() const
    {
        return open_;
    }
    void open();
    // Opens straight into the box, ready for a command ("play techno").
    void open_command();
    void close()
    {
        open_ = false;
        keyboard_open_ = false;
        command_mode_ = false;
    }
    // Called with what was typed when the box is closed with Done: true when
    // it was a command and has been taken care of (the box then empties);
    // false keeps it a search.
    std::function<bool(const std::string &text, bool command_box)> on_command;

    // While open, the screen takes all input.
    void handle(const InputFrame &input, app::Feedback &feedback);
    void update(float dt);
    void draw(app::Frame &frame, const Pulse &pulse) const;

    // Types text into the search, as the keyboard's keys do.
    void type(std::string_view text)
    {
        if (keyboard_.on_text)
            keyboard_.on_text(text);
    }

    // For tests and the tour.
    const std::vector<int> &visible() const
    {
        return visible_;
    }
    const std::vector<audio::SongEntry> &songs() const
    {
        return songs_;
    }
    const std::string &query() const
    {
        return query_;
    }
    int column() const
    {
        return column_;
    }
    // The skin being shown (or arriving).
    const GenreSkin &skin() const
    {
        return skin_.to();
    }

  private:
    enum class Kind : std::uint8_t
    {
        favorites,
        recents,
        liked,   // songs the listener liked
        history, // songs heard lately
        all,
        category,
        search,
    };
    struct Channel
    {
        Kind kind = Kind::all;
        std::string name;
        std::string category;
    };

    // The middle column lists songs, not stations (liked, history).
    bool song_mode() const;
    void toggle_song_like(app::Feedback &feedback);
    void build_channels(int keep);
    void build_stations(bool keep_focus);
    void refresh_rows();
    void open_keyboard();
    void request_logos() const;
    void play_focused(app::Feedback &feedback);
    std::string signature() const;
    const Channel *focused_channel() const;
    // Picks the skin for the focused channel and restyles the lists.
    void choose_skin(bool instant);
    void style_lists(const GenreSkin &skin);
    void draw_banner_for(gfx::DrawList &list, const GenreSkin &skin, float alpha,
                         const Pulse &pulse) const;

    app::Context &context_;
    bool open_ = false;
    int column_ = 0; // 0: channels, 1: stations
    bool working_only_ = false;
    bool keyboard_open_ = false;
    bool command_mode_ = false; // opened for a command (the placeholder says so)
    std::string query_;
    std::vector<Channel> channels_;
    std::vector<int> visible_;            // station indices in the middle column
    std::vector<audio::SongEntry> songs_; // the rows, when they are songs
    ui::ListView channels_view_;
    ui::ListView stations_view_;
    ui::Keyboard keyboard_;
    SkinMorph skin_;
    float refresh_timer_ = 0.0f;
    std::size_t station_count_ = 0; // to notice songs arriving from a drive
    std::string last_signature_;
    float age_ = 0.0f;
    bool short_list_ = false; // the station list is shortened for the keyboard
};

} // namespace hui::concepts
