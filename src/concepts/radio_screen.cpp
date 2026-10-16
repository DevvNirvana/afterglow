// Afterglow - The radio browser and the now-playing bar (see radio_screen.hpp).
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later

#include "concepts/radio_screen.hpp"

#include "audio/library.hpp"
#include "net/cover_art.hpp"
#include "ui/glyphs.hpp"
#include "ui/theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace hui::concepts
{

namespace
{

using gfx::Color;
using gfx::Rect;

constexpr float kMargin = 96.0f;
constexpr Rect kBanner{kMargin, 70.0f, gfx::kVirtualWidth - 2.0f * kMargin, 176.0f};
constexpr float kTop = 322.0f;    // where the two lists start
constexpr float kBottom = 966.0f; // where they end (the hints sit below)
constexpr Rect kChannels{kMargin, kTop, 340.0f, kBottom - kTop};
constexpr Rect kStations{466.0f, kTop, 770.0f, kBottom - kTop};
constexpr Rect kCard{1262.0f, 284.0f, 562.0f, kBottom - 284.0f};
constexpr Rect kKeyboard{466.0f, 620.0f, 770.0f, 346.0f};
constexpr float kCover = 270.0f;
constexpr float kRefreshSeconds = 0.25f;
constexpr float kArtFadeSeconds = 0.6f;

const Color kInk = Color::rgb(0xf6f3ff);
const Color kDim = Color::rgb(0xf6f3ff, 0.66f);
const Color kFaint = Color::rgb(0xf6f3ff, 0.40f);
const Color kOk = Color::rgb(0x3ddc84);
const Color kBad = Color::rgb(0xff5a5f);
const Color kLive = Color::rgb(0xff3b5c);

const ui::Theme &browser_theme()
{
    for (const ui::Theme &theme : ui::themes())
        if (std::string(theme.id) == "acrylic")
            return theme;
    return ui::themes().front();
}

std::string lower(std::string text)
{
    for (char &c : text)
        c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return text;
}

const char *state_label(const audio::PlayerStatus &status)
{
    if (status.source == audio::Source::songs)
        return status.songs_paused ? "PAUSED" : "PLAYING";
    switch (status.state)
    {
    case audio::RadioState::connecting:
        return "CONNECTING";
    case audio::RadioState::buffering:
        return "BUFFERING";
    case audio::RadioState::playing:
        return "ON AIR";
    case audio::RadioState::paused:
        return "PAUSED";
    case audio::RadioState::stopped:
        return "STOPPED";
    case audio::RadioState::failed:
        return "OFFLINE";
    case audio::RadioState::off:
        break;
    }
    return "";
}

bool sounding(const audio::PlayerStatus &status)
{
    return status.source == audio::Source::songs ? !status.songs_paused
                                                 : status.state == audio::RadioState::playing;
}

// Cuts text to fit width, with an ellipsis.
std::string fit(const ui::FontRef &font, const std::string &text, float size, float width,
                float tracking = 0.0f)
{
    if (font.measure(text, size, tracking) <= width)
        return text;
    std::string cut = text;
    while (!cut.empty() && font.measure(cut + "...", size, tracking) > width)
    {
        cut.pop_back();
        // Never leave half of a UTF-8 character behind.
        while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80)
            cut.pop_back();
        if (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0x80) != 0)
            cut.pop_back();
    }
    return cut + "...";
}

// Splits text into at most two lines that fit width.
std::vector<std::string> wrap2(const ui::FontRef &font, const std::string &text, float size,
                               float width)
{
    if (font.measure(text, size) <= width)
        return {text};
    std::size_t best = 0;
    for (std::size_t i = 0; i < text.size(); ++i)
        if (text[i] == ' ' && font.measure(text.substr(0, i), size) <= width)
            best = i;
    if (best == 0)
        return {fit(font, text, size, width)};
    return {text.substr(0, best), fit(font, text.substr(best + 1), size, width)};
}

// The station name in its skin's face, shrunk to fit.
void skin_name(gfx::DrawList &list, const ui::Fonts &fonts, const GenreSkin &skin,
               const std::string &name, float x, float baseline, float size, float width,
               Color color)
{
    const ui::FontRef &face = skin_face(fonts, skin.face);
    const std::string text = skin.caps ? skin_title(skin, name) : name;
    float s = size * skin.title_scale / 0.8f;
    const float tracking = skin.tracking * s / 200.0f;
    const float w = face.measure(text, s, tracking);
    if (w > width)
        s = std::max(s * width / w, size * 0.6f);
    ui::text(list, face, fit(face, text, s, width, tracking), x, baseline, s, color,
             gfx::Align::left, tracking);
}

// The transport icons, drawn: previous, play or pause, stop, next.
void icon_triangle(gfx::DrawList &list, float cx, float cy, float size, float dir, Color c)
{
    list.triangle({cx - size * 0.5f, cy - size * 0.5f, size, size}, c, 0.0f, 1.5707963f * dir);
}

void draw_icon(gfx::DrawList &list, int which, float cx, float cy, float k, bool playing, Color c)
{
    switch (which)
    {
    case 0: // previous
        list.rounded_rect({cx - 11.0f * k, cy - 9.0f * k, 3.5f * k, 18.0f * k}, 1.0f, c);
        icon_triangle(list, cx + 2.0f * k, cy, 18.0f * k, -1.0f, c);
        break;
    case 1: // play / pause
        if (playing)
        {
            list.rounded_rect({cx - 8.0f * k, cy - 10.0f * k, 5.5f * k, 20.0f * k}, 1.5f, c);
            list.rounded_rect({cx + 2.5f * k, cy - 10.0f * k, 5.5f * k, 20.0f * k}, 1.5f, c);
        }
        else
        {
            icon_triangle(list, cx + 2.0f * k, cy, 22.0f * k, 1.0f, c);
        }
        break;
    case 2: // stop
        list.rounded_rect({cx - 8.0f * k, cy - 8.0f * k, 16.0f * k, 16.0f * k}, 2.0f, c);
        break;
    default: // next
        icon_triangle(list, cx - 2.0f * k, cy, 18.0f * k, 1.0f, c);
        list.rounded_rect({cx + 7.5f * k, cy - 9.0f * k, 3.5f * k, 18.0f * k}, 1.0f, c);
        break;
    }
}

// A transport button in the skin: chamfered for the hard genres, round else.
void transport_button(gfx::DrawList &list, const GenreSkin &skin, float cx, float cy, float r,
                      bool main, Color colour)
{
    const bool hard = skin.motif == Motif::strobe || skin.motif == Motif::hazard ||
                      skin.motif == Motif::warehouse;
    const Rect box{cx - r, cy - r, r * 2.0f, r * 2.0f};
    const Color fill = main ? colour.with_alpha(0.95f) : Color::rgb(0xffffff, 0.10f);
    if (hard)
        list.chamfer_rect(box, r * 0.35f, fill);
    else
        list.circle(cx, cy, r, fill);
    if (main)
        list.glow(box, hard ? 4.0f : r, 14.0f, colour.with_alpha(0.35f));
}

// A station's logo, when it has one and it has arrived.
net::ArtView logo_view(const audio::RadioControl *radio, int index)
{
    const net::CoverArt *logos = net::station_logos();
    if (radio == nullptr || logos == nullptr || index < 0)
        return {};
    const std::string address = radio->logo(index);
    return address.empty() ? net::ArtView{} : logos->view(address);
}

// A logo on a light tile, like an app icon: logos are made for white pages,
// and many have dark lettering on a transparent background.
void logo_tile(gfx::DrawList &list, const Rect &r, const net::ArtView &logo, float radius,
               float alpha)
{
    list.rounded_rect(r, radius, Color::rgb(0xf7f5f2, alpha));
    // Fitted inside, keeping its shape (wide logos are common).
    const Rect room = r.inset(r.w * 0.1f);
    const float aspect =
        logo.height > 0 ? static_cast<float>(logo.width) / static_cast<float>(logo.height) : 1.0f;
    const float w = aspect >= 1.0f ? room.w : room.h * aspect;
    const float h = aspect >= 1.0f ? room.w / aspect : room.h;
    list.image(logo.texture, {room.x + (room.w - w) * 0.5f, room.y + (room.h - h) * 0.5f, w, h},
               gfx::kFullUv, Color::rgb(0xffffff, alpha), 2.0f);
    list.bordered_rect(r, radius, Color::rgb(0x000000, 0.0f), 1.0f,
                       Color::rgb(0xffffff, 0.25f * alpha));
}

bool disc_is_cd(const GenreSkin &skin)
{
    return skin.motif == Motif::gloss || skin.motif == Motif::laser || skin.motif == Motif::halo ||
           skin.motif == Motif::frost;
}

} // namespace

gfx::Color channel_colour(const std::string &category)
{
    const GenreSkin &skin = skin_for(category);
    if (&skin != &default_skin())
        return Color::rgb(skin.accent);
    // Anything else (an owner's own list): a hue from the name.
    const std::uint32_t hash = name_hash(category);
    const float hue = static_cast<float>(hash % 360u) / 60.0f;
    const float x = 1.0f - std::abs(std::fmod(hue, 2.0f) - 1.0f);
    const int sector = static_cast<int>(hue);
    const float r = sector == 0 || sector == 5 ? 1.0f : sector == 1 ? x : sector == 4 ? x : 0.0f;
    const float g = sector == 1 || sector == 2 ? 1.0f : sector == 0 || sector == 3 ? x : 0.0f;
    const float b = sector == 3 || sector == 4 ? 1.0f : sector == 2 || sector == 5 ? x : 0.0f;
    return {0.25f + 0.75f * r, 0.25f + 0.75f * g, 0.25f + 0.75f * b, 1.0f};
}

std::string clock_text(double seconds)
{
    const auto total = static_cast<long long>(std::max(0.0, seconds));
    char text[32];
    if (total >= 3600)
        std::snprintf(text, sizeof(text), "%lld:%02lld:%02lld", total / 3600, total / 60 % 60,
                      total % 60);
    else
        std::snprintf(text, sizeof(text), "%lld:%02lld", total / 60, total % 60);
    return text;
}

const audio::Station *playing_station(const audio::RadioControl *radio,
                                      const audio::PlayerStatus &status)
{
    if (radio == nullptr || status.source != audio::Source::radio || status.station < 0 ||
        status.station >= static_cast<int>(radio->stations().size()))
        return nullptr;
    return &radio->stations()[static_cast<std::size_t>(status.station)];
}

void draw_cover(gfx::DrawList &list, const ui::Fonts &fonts, const Rect &r, const GenreSkin &skin,
                const audio::PlayerStatus &status, const audio::Station *station,
                const Pulse &pulse, float alpha)
{
    const std::string name = station != nullptr ? station->name : "Afterglow Mix";
    const std::uint32_t seed = name_hash(name);
    list.shadow({r.x, r.y + 10.0f, r.w, r.h}, skin.radius, 30.0f,
                Color::rgb(0x000000, 0.55f * alpha));
    draw_generated_cover(list, fonts, r, skin, name, seed, alpha, pulse);
    const net::CoverArt *art = net::cover_art();
    // The station's own logo sits on its generated cover until (unless) the
    // song's album art arrives over both.
    if (station != nullptr)
    {
        const audio::RadioControl *radio = audio::radio();
        const int index = radio != nullptr ? radio->station_of(station->url) : -1;
        const net::ArtView logo = logo_view(radio, index);
        const bool own_song = station->category == audio::kLibraryCategory;
        if (own_song && logo.state == net::ArtState::ready && logo.texture != 0)
        {
            // A song's own cover fills the square, as album art does.
            const float in = std::min(1.0f, logo.age / kArtFadeSeconds);
            const float radius = std::min(skin.radius, r.w * 0.08f);
            list.image(logo.texture, r, gfx::kFullUv, Color::rgb(0xffffff, alpha * in), radius);
            return;
        }
        if (logo.state == net::ArtState::ready && logo.texture != 0)
        {
            const float in = std::min(1.0f, logo.age / kArtFadeSeconds);
            const float side = r.w * 0.56f;
            const Rect tile{r.x + (r.w - side) * 0.5f, r.y + (r.h - side) * 0.42f, side, side};
            list.shadow(tile, side * 0.12f, side * 0.15f, Color::rgb(0x000000, 0.4f * alpha * in));
            logo_tile(list, tile, logo, side * 0.12f, alpha * in);
        }
    }
    if (art == nullptr || status.title.empty() || station == nullptr)
        return;
    const net::ArtView view = art->view(status.title);
    if (view.state == net::ArtState::searching)
    {
        // A quiet "looking it up" sweep along the bottom edge.
        const float k = std::fmod(pulse.time * 0.8f, 1.0f);
        list.rounded_rect({r.x + r.w * 0.08f, r.y + r.h - 8.0f, r.w * 0.84f, 2.0f}, 1.0f,
                          Color::rgb(0xffffff, 0.15f * alpha));
        list.rounded_rect({r.x + r.w * (0.08f + 0.64f * k), r.y + r.h - 8.0f, r.w * 0.2f, 2.0f},
                          1.0f, Color::rgb(skin.accent2, 0.9f * alpha));
    }
    if (view.state != net::ArtState::ready || view.texture == 0)
        return;
    const float in = std::min(1.0f, view.age / kArtFadeSeconds);
    const float a = alpha * in * in * (3.0f - 2.0f * in);
    const float radius = std::min(skin.radius, r.w * 0.08f);
    list.image(view.texture, r, gfx::kFullUv, Color::rgb(0xffffff, a), radius);
    // A sheen across the top, like light on a jewel case.
    list.gradient_rect({r.x, r.y, r.w, r.h * 0.45f}, radius, Color::rgb(0xffffff, 0.10f * a),
                       Color::rgb(0xffffff, 0.0f));
    list.bordered_rect(r, radius, Color::rgb(0x000000, 0.0f), 1.5f, Color::rgb(0xffffff, 0.2f * a));
}

void draw_now_playing(gfx::DrawList &list, const ui::Fonts &fonts, const Rect &box,
                      const audio::PlayerStatus &status, const audio::Station *station,
                      const Pulse &pulse, std::uint32_t glass)
{
    const bool radio = status.source == audio::Source::radio && station != nullptr;
    const GenreSkin &skin = radio ? skin_for(station->category) : default_skin();
    const Color colour = Color::rgb(skin.accent);
    const std::string name = radio ? station->name : "Afterglow Mix";
    std::string title = status.title;
    if (title.empty())
        title = radio ? (status.state == audio::RadioState::failed
                             ? status.message
                             : station->style + "  /  " + station->country)
                      : "The songs that come with Afterglow";

    // The plate: frosted, tinted with the skin, a hairline and a glowing edge.
    const float radius = std::min(skin.radius, 24.0f);
    if (glass != 0)
        list.glass(glass, box, radius, Color::rgb(skin.bg0, 0.55f));
    else
        list.rounded_rect(box, radius, Color::rgb(skin.bg0, 0.74f));
    list.gradient_rect_h(box, radius, Color::rgb(skin.bg1, 0.55f), Color::rgb(skin.bg0, 0.0f));
    list.bordered_rect(box, radius, Color::rgb(0x000000, 0.0f), 1.5f, Color::rgb(0xffffff, 0.14f));
    list.gradient_rect_h({box.x + radius, box.y + box.h - 3.0f, box.w - radius * 2.0f, 3.0f}, 1.5f,
                         colour.with_alpha(0.0f), colour.with_alpha(0.95f));

    const float pad = 12.0f;
    const Rect tile{box.x + pad, box.y + pad, box.h - pad * 2.0f, box.h - pad * 2.0f};
    draw_cover(list, fonts, tile, skin, status, station, pulse, 1.0f);

    const float text_x = tile.x + tile.w + 24.0f;
    const float transport_w = 320.0f;
    const float text_w = box.x + box.w - transport_w - text_x - 30.0f;
    // The genre, small, in its own face and colour; then the station.
    const std::string genre = radio ? skin_title(skin, skin.title) : "AFTERGLOW";
    const ui::FontRef &face = skin_face(fonts, skin.face);
    ui::text(list, face, genre, text_x, box.y + 30.0f, 17.0f * skin.title_scale / 0.8f, colour,
             gfx::Align::left, 3.0f);
    skin_name(list, fonts, skin, name, text_x, box.y + box.h * 0.58f, 32.0f, text_w, kInk);
    // A long title scrolls, like the marquee of an old desktop player.
    const float title_size = 22.0f;
    const float title_w = fonts.regular.measure(title, title_size);
    list.push_clip({text_x, box.y, text_w, box.h});
    if (title_w <= text_w)
    {
        ui::text(list, fonts.regular, title, text_x, box.y + box.h * 0.84f, title_size, kDim);
    }
    else
    {
        const float gap = 80.0f;
        const float shift = std::fmod(pulse.time * 45.0f, title_w + gap);
        for (int i = 0; i < 2; ++i)
            ui::text(list, fonts.regular, title,
                     text_x - shift + static_cast<float>(i) * (title_w + gap),
                     box.y + box.h * 0.84f, title_size, kDim);
    }
    list.pop_clip();

    // State and time, then the transport with its buttons.
    const float right = box.x + box.w - 24.0f;
    const char *state = state_label(status);
    const bool on_air = sounding(status) && radio;
    const std::string time_text = (radio ? "LIVE " : "") + clock_text(status.elapsed);
    float sx = right - transport_w;
    if (on_air)
    {
        const float beat = 0.55f + 0.45f * std::sin(pulse.time * 4.0f);
        list.circle(sx + 7.0f, box.y + box.h * 0.30f, 6.0f, kLive.with_alpha(beat));
        sx += 20.0f;
    }
    ui::text(list, fonts.semibold, state, sx, box.y + box.h * 0.36f, 18.0f,
             status.state == audio::RadioState::failed && radio ? kBad : kInk, gfx::Align::left,
             2.0f);
    ui::text(list, fonts.mono, time_text, right, box.y + box.h * 0.36f, 18.0f, kDim,
             gfx::Align::right);
    for (int i = 0; i < 4; ++i)
    {
        const float cx = right - transport_w + 40.0f + static_cast<float>(i) * 80.0f;
        const float cy = box.y + box.h * 0.70f;
        const bool main = i == 1;
        if (main)
            transport_button(list, skin, cx, cy, 22.0f, true, colour);
        draw_icon(list, i, cx, cy, main ? 0.95f : 0.8f, sounding(status),
                  main ? Color::rgb(skin.bg0) : kInk);
    }
}

// ---- RadioScreen ----

RadioScreen::RadioScreen(app::Context &context) : context_(context)
{
    for (ui::ListView *view : {&channels_view_, &stations_view_})
    {
        view->style.theme = browser_theme();
        view->style.panel = false;
        view->style.wrap = false;
    }
    channels_view_.style.row_height = 60.0f;
    channels_view_.style.title_size = 24.0f;
    channels_view_.style.value_size = 19.0f;
    channels_view_.style.header_height = 46.0f;
    stations_view_.style.row_height = 80.0f;
    stations_view_.style.title_size = 27.0f;
    stations_view_.style.subtitle_size = 19.0f;
    stations_view_.style.value_size = 18.0f;
    channels_view_.set_bounds(kChannels);
    stations_view_.set_bounds(kStations);
    // The leading slot: a drawn star on a favourite station, a heart on a
    // liked song (the fonts carry no symbols for them), else "01", "02",
    // ... for the skins that number their rows.
    stations_view_.leading = [this](ui::Canvas &canvas, const Rect &row, const ui::ListItem &item,
                                    int index, float focus)
    {
        const GenreSkin &skin = skin_.to();
        const audio::RadioControl *radio = audio::radio();
        // On a filled highlight the marks turn to the skin's darkest colour,
        // like the text; white or the accent would vanish on the plate.
        const bool filled = skin.highlight == ui::HighlightKind::fill;
        const Color lit = filled ? Color::rgb(skin.bg0) : Color::rgb(0xffffff);
        const Color mark = gfx::mix(Color::rgb(skin.accent2), lit, focus);
        const float cx = row.x + row.w * 0.5f;
        const float cy = row.y + row.h * 0.5f;
        if (song_mode())
        {
            if (radio != nullptr && index >= 0 && index < static_cast<int>(songs_.size()) &&
                radio->is_liked(songs_[static_cast<std::size_t>(index)].title))
                draw_heart(canvas.list, cx, cy, 20.0f, mark);
            return;
        }
        if (item.tag < 0)
            return;
        const bool favorite = radio != nullptr && radio->favorite(item.tag);
        const net::ArtView logo = logo_view(radio, item.tag);
        if (logo.state == net::ArtState::ready && logo.texture != 0)
        {
            const float side = std::min(row.w, row.h - 22.0f);
            const float in = std::min(1.0f, logo.age / kArtFadeSeconds);
            logo_tile(canvas.list, {cx - side * 0.5f, cy - side * 0.5f, side, side}, logo, 8.0f,
                      in);
            if (favorite) // a star badge on the logo's corner
            {
                canvas.list.circle(cx + side * 0.45f, cy - side * 0.45f, 10.0f,
                                   Color::rgb(skin.bg0));
                canvas.list.star(cx + side * 0.45f, cy - side * 0.45f, 7.5f, mark);
            }
            return;
        }
        if (favorite)
        {
            canvas.list.star(cx, cy, 11.0f, mark);
            return;
        }
        if (!skin.numbered)
            return;
        char number[8];
        std::snprintf(number, sizeof(number), "%02d", index + 1);
        ui::text(canvas.list, canvas.fonts.mono, number, row.x + row.w, row.y + row.h * 0.60f,
                 20.0f, gfx::mix(kFaint, filled ? lit : Color::rgb(skin.accent), focus),
                 gfx::Align::right);
    };
    style_lists(default_skin());

    keyboard_.style.theme = browser_theme();
    keyboard_.style.bindings = ui::KeyboardBindings::standard();
    keyboard_.style.max_length = 48; // room for "play bollywood nineties please"
    keyboard_.set_bounds(kKeyboard);
    keyboard_.on_text = [this](std::string_view utf8)
    {
        query_.append(utf8);
        build_channels(-1);
        build_stations(false);
    };
    keyboard_.on_backspace = [this]
    {
        if (!query_.empty())
        {
            query_.pop_back();
            while (!query_.empty() && (static_cast<unsigned char>(query_.back()) & 0xC0) == 0x80)
                query_.pop_back();
        }
        build_channels(-1);
        build_stations(false);
    };
}

void RadioScreen::style_lists(const GenreSkin &skin)
{
    ui::Theme theme = browser_theme();
    const Color accent = Color::rgb(skin.accent);
    theme.accent = accent;
    theme.primary = accent;
    theme.on_primary = Color::rgb(skin.bg0);
    theme.focus = gfx::mix(accent, Color::rgb(0xffffff), 0.25f);
    theme.surface_high = Color::rgb(0xffffff, 0.12f);
    theme.radius = std::min(skin.radius * 0.5f, 14.0f);
    theme.corner = skin.motif == Motif::strobe || skin.motif == Motif::hazard ? ui::Corner::chamfer
                                                                              : ui::Corner::round;
    for (ui::ListView *view : {&channels_view_, &stations_view_})
    {
        view->style.theme = theme;
        view->style.highlight.kind = skin.highlight;
        view->style.highlight.thickness = 5.0f;
    }
    // The channel list keeps a quiet highlight; the stations wear the skin.
    channels_view_.style.highlight.kind = ui::HighlightKind::tint;
    stations_view_.style.leading_width = 56.0f; // logos, stars, hearts or row numbers
    keyboard_.style.theme = theme;
}

void RadioScreen::open()
{
    open_ = true;
    age_ = 0.0f;
    keyboard_open_ = false;
    command_mode_ = false;
    for (ui::ListView *view : {&channels_view_, &stations_view_})
        view->style.reduced_motion = context_.settings.reduced_motion;
    // Start where the music is: the channel of the station playing, on it.
    int keep = channels_.empty() ? -1 : channels_view_.focus();
    build_channels(keep);
    const audio::RadioControl *radio = audio::radio();
    if (radio != nullptr && keep < 0)
    {
        const audio::PlayerStatus status = radio->status();
        if (const audio::Station *station = playing_station(radio, status))
        {
            for (std::size_t i = 0; i < channels_.size(); ++i)
            {
                const auto &items = channels_view_.items();
                if (channels_[i].kind == Kind::category &&
                    channels_[i].category == station->category)
                {
                    for (std::size_t row = 0; row < items.size(); ++row)
                        if (items[row].tag == static_cast<int>(i))
                            channels_view_.set_focus(static_cast<int>(row));
                }
            }
        }
    }
    build_stations(false);
    column_ = visible_.empty() ? 0 : 1;
    channels_view_.enter();
    stations_view_.enter();
    last_signature_ = signature();
    choose_skin(true);
}

const RadioScreen::Channel *RadioScreen::focused_channel() const
{
    const auto &rows = channels_view_.items();
    const int row = channels_view_.focus();
    if (rows.empty() || row < 0 || row >= static_cast<int>(rows.size()))
        return nullptr;
    const int tag = rows[static_cast<std::size_t>(row)].tag;
    if (tag < 0 || tag >= static_cast<int>(channels_.size()))
        return nullptr;
    return &channels_[static_cast<std::size_t>(tag)];
}

void RadioScreen::choose_skin(bool instant)
{
    const GenreSkin *skin = &default_skin();
    const Channel *channel = focused_channel();
    if (channel != nullptr && channel->kind == Kind::category)
    {
        skin = &skin_for(channel->category);
    }
    else if (const audio::RadioControl *radio = audio::radio())
    {
        if (const audio::Station *station = playing_station(radio, radio->status()))
            skin = &skin_for(station->category);
    }
    if (skin != &skin_.to() || instant)
    {
        skin_.set(*skin, instant || context_.settings.reduced_motion);
        style_lists(*skin);
    }
}

void RadioScreen::build_channels(int keep)
{
    const audio::RadioControl *radio = audio::radio();
    channels_.clear();
    std::vector<ui::ListItem> items;
    const auto add = [&](Kind kind, const std::string &name, const std::string &category, int count,
                         Color swatch)
    {
        Channel channel;
        channel.kind = kind;
        channel.name = name;
        channel.category = category;
        channels_.push_back(channel);
        ui::ListItem item;
        item.title = name;
        item.value = count >= 0 ? std::to_string(count) : "";
        item.swatch = swatch;
        item.tag = static_cast<int>(channels_.size()) - 1;
        items.push_back(item);
    };
    if (radio == nullptr || radio->stations().empty())
    {
        ui::ListItem none;
        none.title = radio == nullptr ? "Radio plays on the console" : "No stations found";
        none.subtitle = "Add stations.txt to the app's folder";
        none.disabled = true;
        none.tag = -1;
        items.push_back(none);
        channels_view_.set_items(std::move(items));
        return;
    }
    if (!query_.empty())
        add(Kind::search, "Search: " + query_, "", -1, Color::rgb(0xffffff, 0.9f));
    int favourites = 0;
    for (std::size_t i = 0; i < radio->stations().size(); ++i)
        favourites += radio->favorite(static_cast<int>(i)) ? 1 : 0;
    add(Kind::favorites, "Favorites", "", favourites, Color::rgb(0xffd60a));
    add(Kind::recents, "Recently played", "", static_cast<int>(radio->recents().size()),
        Color::rgb(0xffffff, 0.7f));
    add(Kind::liked, "Liked songs", "", static_cast<int>(radio->liked().size()),
        Color::rgb(0xff4f8b));
    add(Kind::history, "Song history", "", static_cast<int>(radio->history().size()),
        Color::rgb(0x7fd8ff));
    const auto songs_on_drives = std::count_if(radio->stations().begin(), radio->stations().end(),
                                               [](const audio::Station &s)
                                               { return s.category == audio::kLibraryCategory; });
    add(Kind::all, "All stations", "",
        static_cast<int>(radio->stations().size()) - static_cast<int>(songs_on_drives),
        Color::rgb(0xffffff, 0.35f));
    // The listener's own songs, always listed: empty, it says how to fill it.
    add(Kind::category, audio::kLibraryCategory, audio::kLibraryCategory,
        static_cast<int>(songs_on_drives), Color::rgb(0x9cf0c8));
    ui::ListItem header;
    header.title = "GENRES";
    header.header = true;
    header.tag = -1;
    items.push_back(header);
    std::vector<std::string> categories;
    for (const audio::Station &station : radio->stations())
        if (station.category != audio::kLibraryCategory &&
            std::find(categories.begin(), categories.end(), station.category) == categories.end())
            categories.push_back(station.category);
    for (const std::string &category : categories)
    {
        const auto count =
            std::count_if(radio->stations().begin(), radio->stations().end(),
                          [&](const audio::Station &s) { return s.category == category; });
        add(Kind::category, category, category, static_cast<int>(count), channel_colour(category));
    }
    channels_view_.set_items(std::move(items));
    if (keep >= 0)
        channels_view_.set_focus(
            std::min(keep, static_cast<int>(channels_view_.items().size()) - 1));
    else
        channels_view_.set_focus(query_.empty() ? 4 : 0); // "All stations", or the search
}

bool RadioScreen::song_mode() const
{
    const Channel *channel = focused_channel();
    return channel != nullptr && (channel->kind == Kind::liked || channel->kind == Kind::history);
}

namespace
{

// "21:14" today, "Oct 14" before.
std::string when_text(std::int64_t when)
{
    if (when <= 0)
        return {};
    const auto then = static_cast<std::time_t>(when);
    const std::time_t now = std::time(nullptr);
    std::tm a{};
    std::tm b{};
    if (const std::tm *t = std::localtime(&then))
        a = *t;
    else
        return {};
    if (const std::tm *t = std::localtime(&now))
        b = *t;
    char text[32];
    const bool today = a.tm_year == b.tm_year && a.tm_yday == b.tm_yday;
    std::strftime(text, sizeof(text), today ? "%H:%M" : "%b %d", &a);
    return text;
}

} // namespace

void RadioScreen::build_stations(bool keep_focus)
{
    const audio::RadioControl *radio = audio::radio();
    visible_.clear();
    songs_.clear();
    const int focus = keep_focus ? stations_view_.focus() : 0;
    std::vector<ui::ListItem> items;
    const Channel *channel = focused_channel();
    if (radio != nullptr && song_mode())
    {
        // Songs: each row tunes in to the station it was heard on.
        songs_ = channel->kind == Kind::liked ? radio->liked() : radio->history();
        for (const audio::SongEntry &song : songs_)
        {
            const int index = radio->station_of(song.url);
            ui::ListItem item;
            item.title = song.title;
            const std::string when = when_text(song.when);
            item.subtitle = (index >= 0 ? song.station : song.station + " (no longer listed)") +
                            (when.empty() ? "" : "  /  " + when);
            item.tag = index;
            items.push_back(item);
            if (index >= 0 && std::find(visible_.begin(), visible_.end(), index) == visible_.end())
                visible_.push_back(index);
        }
        if (items.empty())
        {
            ui::ListItem none;
            none.title = "Nothing here yet";
            none.subtitle = channel->kind == Kind::liked
                                ? "Press Square over the visuals to like the song on air"
                                : "Songs you hear on the radio show up here";
            none.disabled = true;
            none.tag = -1;
            items.push_back(none);
        }
        stations_view_.set_items(std::move(items));
        stations_view_.set_focus(
            std::min(focus, static_cast<int>(stations_view_.items().size()) - 1));
        refresh_rows();
        return;
    }
    if (radio != nullptr && channel != nullptr)
    {
        const auto &stations = radio->stations();
        std::vector<int> order;
        if (channel->kind == Kind::recents)
        {
            order = radio->recents();
        }
        else
        {
            const std::string needle = lower(query_);
            for (std::size_t i = 0; i < stations.size(); ++i)
            {
                const audio::Station &s = stations[i];
                const int index = static_cast<int>(i);
                const bool take =
                    (channel->kind == Kind::all && s.category != audio::kLibraryCategory) ||
                    (channel->kind == Kind::favorites && radio->favorite(index)) ||
                    (channel->kind == Kind::category && s.category == channel->category) ||
                    (channel->kind == Kind::search &&
                     lower(s.name + " " + s.style + " " + s.category + " " + s.country)
                             .find(needle) != std::string::npos);
                if (take)
                    order.push_back(index);
            }
        }
        for (int index : order)
        {
            if (working_only_ && radio->health(index) == audio::Health::failed)
                continue;
            visible_.push_back(index);
        }
    }
    for (int index : visible_)
    {
        if (radio == nullptr)
            break; // visible_ is only filled with a radio; this keeps the analyser sure
        const audio::Station &s = radio->stations()[static_cast<std::size_t>(index)];
        ui::ListItem item;
        item.title = s.name;
        item.subtitle = s.style + "  /  " + s.country;
        item.value = s.format;
        item.tag = index;
        items.push_back(item);
    }
    if (items.empty())
    {
        ui::ListItem none;
        const bool drives = channel != nullptr && channel->category == audio::kLibraryCategory;
        none.title = channels_.empty() ? "" : drives ? "No songs found" : "Nothing here yet";
        none.subtitle = !query_.empty() ? "No station matches \"" + query_ + "\""
                        : drives        ? "Plug in a USB drive with MP3, AAC or Ogg songs"
                        : working_only_ ? "Every station here failed the last check"
                                        : "Square adds the focused station to Favorites";
        none.disabled = true;
        none.tag = -1;
        items.push_back(none);
    }
    stations_view_.set_items(std::move(items));
    stations_view_.set_focus(std::min(focus, static_cast<int>(stations_view_.items().size()) - 1));
    refresh_rows();
}

// Badges, stars and health dots change without rebuilding the rows.
void RadioScreen::refresh_rows()
{
    const audio::RadioControl *radio = audio::radio();
    if (radio == nullptr)
        return;
    const audio::PlayerStatus status = radio->status();
    if (song_mode())
    {
        for (int row = 0; row < static_cast<int>(stations_view_.items().size()) &&
                          row < static_cast<int>(songs_.size());
             ++row)
        {
            ui::ListItem &item = stations_view_.item(row);
            const audio::SongEntry &song = songs_[static_cast<std::size_t>(row)];
            const audio::Station *station = playing_station(radio, status);
            const bool on_air = station != nullptr && !status.title.empty() &&
                                status.title == song.title && station->url == song.url;
            item.badge = on_air ? "ON AIR" : "";
        }
        return;
    }
    for (int row = 0; row < static_cast<int>(stations_view_.items().size()); ++row)
    {
        ui::ListItem &item = stations_view_.item(row);
        if (item.tag < 0)
            continue;
        const audio::Station &s = radio->stations()[static_cast<std::size_t>(item.tag)];
        item.title = s.name;
        const audio::Health health = radio->health(item.tag);
        item.swatch = health == audio::Health::ok       ? kOk
                      : health == audio::Health::failed ? kBad
                                                        : Color::rgb(0xffffff, 0.0f);
        const bool current = status.source == audio::Source::radio && status.station == item.tag;
        item.badge = current ? state_label(status) : "";
    }
}

std::string RadioScreen::signature() const
{
    const audio::RadioControl *radio = audio::radio();
    if (radio == nullptr)
        return {};
    const audio::PlayerStatus status = radio->status();
    const audio::CheckProgress check = radio->check_progress();
    return std::to_string(static_cast<int>(status.source)) + "/" +
           std::to_string(static_cast<int>(status.state)) + "/" + std::to_string(status.station) +
           "/" + std::to_string(check.done) + "/" + std::to_string(check.running ? 1 : 0) + "/" +
           status.title;
}

void RadioScreen::open_command()
{
    open();
    query_.clear();
    command_mode_ = true;
    open_keyboard();
}

void RadioScreen::open_keyboard()
{
    keyboard_open_ = true;
    keyboard_.set_length(static_cast<int>(query_.size()));
    keyboard_.enter();
    if (query_.empty())
    {
        query_.clear();
        build_channels(-1);
        build_stations(false);
    }
}

void RadioScreen::play_focused(app::Feedback &feedback)
{
    audio::RadioControl *radio = audio::radio();
    const auto &items = stations_view_.items();
    const int row = stations_view_.focus();
    if (radio == nullptr || row < 0 || row >= static_cast<int>(items.size()) ||
        items[static_cast<std::size_t>(row)].tag < 0)
    {
        feedback.play(audio::Cue::error);
        return;
    }
    radio->play(items[static_cast<std::size_t>(row)].tag, visible_);
    feedback.play(audio::Cue::launch);
    refresh_rows();
    choose_skin(false);
}

// Square on a song: like it (history) or let it go (either list).
void RadioScreen::toggle_song_like(app::Feedback &feedback)
{
    audio::RadioControl *radio = audio::radio();
    const int row = stations_view_.focus();
    if (radio == nullptr || row < 0 || row >= static_cast<int>(songs_.size()))
    {
        feedback.play(audio::Cue::error);
        return;
    }
    const audio::SongEntry song = songs_[static_cast<std::size_t>(row)];
    const bool on = !radio->is_liked(song.title);
    radio->set_liked(song, on);
    feedback.play(on ? audio::Cue::favorite_on : audio::Cue::favorite_off);
    const int keep = channels_view_.focus();
    build_channels(keep);
    build_stations(true);
}

void RadioScreen::handle(const InputFrame &input, app::Feedback &feedback)
{
    if (!open_)
        return;
    audio::RadioControl *radio = audio::radio();
    if (keyboard_open_)
    {
        const ui::Event event = keyboard_.handle(input, feedback);
        const bool done = event == ui::Event::activated || input.is_pressed(Action::menu);
        if (done && !query_.empty() && on_command && on_command(query_, command_mode_))
        {
            // A command: carried out (the visuals may have taken over).
            query_.clear();
            keyboard_open_ = false;
            command_mode_ = false;
            build_channels(-1);
            build_stations(false);
            return;
        }
        if (done || event == ui::Event::cancelled)
        {
            keyboard_open_ = false;
            command_mode_ = false;
            // A search that found something hands the focus to its results.
            build_channels(-1);
            build_stations(false);
            column_ = visible_.empty() ? 0 : 1;
            feedback.play(audio::Cue::back);
        }
        choose_skin(false);
        return;
    }

    if (input.is_pressed(Action::back))
    {
        close();
        feedback.play(audio::Cue::back);
        return;
    }
    if (input.is_pressed(Action::north))
    {
        open_keyboard();
        choose_skin(false);
        feedback.play(audio::Cue::open);
        return;
    }
    // The transport, the same in every column.
    if (radio != nullptr)
    {
        if (input.is_pressed(Action::page_next))
            radio->next();
        if (input.is_pressed(Action::page_prev))
            radio->previous();
        if (input.is_pressed(Action::jump_next))
            radio->toggle_pause();
        if (input.is_pressed(Action::jump_prev))
            radio->stop();
        if (input.is_pressed(Action::page_next) || input.is_pressed(Action::page_prev) ||
            input.is_pressed(Action::jump_next) || input.is_pressed(Action::jump_prev))
        {
            feedback.play(audio::Cue::toggle);
            refresh_rows();
            choose_skin(false);
        }
        if (input.is_pressed(Action::menu))
        {
            if (radio->check_progress().running)
                radio->cancel_check();
            else
                radio->start_check();
            feedback.play(audio::Cue::toggle);
        }
    }
    if (input.is_pressed(Action::r3))
    {
        working_only_ = !working_only_;
        build_stations(false);
        feedback.play(audio::Cue::toggle);
    }

    const bool dpad = input.nav != Direction::none && !input.nav_repeat;
    if (dpad && input.nav == Direction::left && column_ == 1)
    {
        column_ = 0;
        feedback.play(audio::Cue::focus, 1.0f, -0.4f);
        return;
    }
    if (dpad && input.nav == Direction::right && column_ == 0 &&
        (!visible_.empty() || !songs_.empty()))
    {
        column_ = 1;
        feedback.play(audio::Cue::focus, 1.0f, 0.4f);
        return;
    }

    if (column_ == 0)
    {
        const int before = channels_view_.focus();
        const ui::Event event = channels_view_.handle(input, feedback);
        if (channels_view_.focus() != before)
        {
            build_stations(false); // the stations follow the channel as it moves
            choose_skin(false);    // ...and the screen changes its clothes
        }
        if (event == ui::Event::activated && (!visible_.empty() || !songs_.empty()))
            column_ = 1;
        return;
    }

    if (input.is_pressed(Action::west) && radio != nullptr && song_mode())
    {
        toggle_song_like(feedback);
        return;
    }
    if (input.is_pressed(Action::west) && radio != nullptr)
    {
        const auto &items = stations_view_.items();
        const int row = stations_view_.focus();
        if (row >= 0 && row < static_cast<int>(items.size()) &&
            items[static_cast<std::size_t>(row)].tag >= 0)
        {
            const int index = items[static_cast<std::size_t>(row)].tag;
            const bool on = !radio->favorite(index);
            radio->set_favorite(index, on);
            feedback.play(on ? audio::Cue::favorite_on : audio::Cue::favorite_off);
            // Counts in the channel list; the favourites channel itself.
            const int keep = channels_view_.focus();
            build_channels(keep);
            build_stations(true);
        }
        return;
    }
    const ui::Event event = stations_view_.handle(input, feedback);
    if (event == ui::Event::activated)
        play_focused(feedback);
}

void RadioScreen::request_logos() const
{
    net::CoverArt *logos = net::station_logos();
    const audio::RadioControl *radio = audio::radio();
    if (logos == nullptr || radio == nullptr)
        return;
    // The rows around the focus (a screenful), nearest first, and the
    // station on air.
    const auto &items = stations_view_.items();
    const int focus = stations_view_.focus();
    for (int d = 0; d <= 12; ++d)
        for (const int i : {focus + d, focus - d})
            if (i >= 0 && i < static_cast<int>(items.size()) &&
                items[static_cast<std::size_t>(i)].tag >= 0)
            {
                const std::string address = radio->logo(items[static_cast<std::size_t>(i)].tag);
                if (!address.empty())
                    logos->request(address);
            }
    const std::string playing = radio->logo(radio->status().station);
    if (!playing.empty())
        logos->request(playing);
}

void RadioScreen::update(float dt)
{
    age_ += dt;
    skin_.update(dt);
    channels_view_.set_active(open_ && !keyboard_open_ && column_ == 0);
    stations_view_.set_active(open_ && !keyboard_open_ && column_ == 1);
    keyboard_.set_active(keyboard_open_);
    // The keyboard and its field take the lower half of the middle column.
    if (keyboard_open_ != short_list_)
    {
        short_list_ = keyboard_open_;
        stations_view_.set_bounds(short_list_ ? Rect{kStations.x, kStations.y, kStations.w,
                                                     kKeyboard.y - 90.0f - kStations.y}
                                              : kStations);
    }
    channels_view_.update(dt);
    stations_view_.update(dt);
    keyboard_.update(dt);
    if (!open_)
        return;
    // Songs found on a drive (or a drive taken out) change the lists.
    if (const audio::RadioControl *radio = audio::radio();
        radio != nullptr && radio->stations().size() != station_count_)
    {
        station_count_ = radio->stations().size();
        build_channels(channels_view_.focus());
        build_stations(true);
    }
    refresh_timer_ += dt;
    if (refresh_timer_ >= kRefreshSeconds)
    {
        refresh_timer_ = 0.0f;
        request_logos();
        const std::string now = signature();
        if (now != last_signature_)
        {
            last_signature_ = now;
            refresh_rows();
            choose_skin(false);
        }
    }
}

void RadioScreen::draw_banner_for(gfx::DrawList &list, const GenreSkin &skin, float alpha,
                                  const Pulse &pulse) const
{
    const Channel *channel = focused_channel();
    std::string title;
    std::string tagline;
    const audio::RadioControl *radio = audio::radio();
    if (channel != nullptr && channel->kind != Kind::category)
    {
        switch (channel->kind)
        {
        case Kind::favorites:
            title = "Favorites";
            tagline = "YOUR STARRED STATIONS  /  SQUARE ADDS ONE";
            break;
        case Kind::recents:
            title = "Recent";
            tagline = "WHERE YOU HAVE BEEN LISTENING";
            break;
        case Kind::liked:
            title = "Liked";
            tagline = "SONGS YOU KEPT  /  CROSS TUNES IN  /  ALSO IN LIKED-SONGS.TXT";
            break;
        case Kind::history:
            title = "History";
            tagline = "THE LAST 50 SONGS ON YOUR RADIO";
            break;
        case Kind::search:
            title = "Search";
            tagline = "RESULTS FOR \"" + query_ + "\"";
            break;
        default:
            title = &skin == &default_skin() ? "Afterglow" : "All stations";
            tagline = radio != nullptr ? std::to_string(radio->stations().size()) +
                                             " STATIONS  /  FREE  /  NON-STOP"
                                       : "";
            break;
        }
    }
    draw_banner(list, context_.fonts, kBanner, skin, title, tagline, alpha, pulse);
}

void RadioScreen::draw(app::Frame &frame, const Pulse &pulse) const
{
    if (!open_)
        return;
    const ui::Fonts &fonts = context_.fonts;
    gfx::DrawList &list = frame.overlay;
    frame.glass = true;
    const Rect full{0.0f, 0.0f, gfx::kVirtualWidth, gfx::kVirtualHeight};
    const float in = context_.settings.reduced_motion ? 1.0f : std::min(1.0f, age_ / 0.22f);
    const float rise = (1.0f - in) * (1.0f - in) * 40.0f;
    list.push_opacity(in);

    // ---- the room: the frosted visualiser in the skin's colours ----
    list.glass(frame.glass_texture, full, 0.0f, Color::rgb(0x05060c, 0.55f));
    const float t = skin_.t();
    const float e = t * t * (3.0f - 2.0f * t);
    if (!skin_.settled())
        draw_skin_room(list, full, skin_.from(), 1.0f - e, pulse);
    draw_skin_room(list, full, skin_.to(), skin_.settled() ? 1.0f : e, pulse);
    // Darken under the lists so text always reads.
    list.gradient_rect({0.0f, kTop - 60.0f, gfx::kVirtualWidth, gfx::kVirtualHeight - kTop + 60.0f},
                       0.0f, Color::rgb(0x000000, 0.0f), Color::rgb(0x000000, 0.45f));

    const audio::RadioControl *radio = audio::radio();
    const audio::PlayerStatus status = radio != nullptr ? radio->status() : audio::PlayerStatus{};
    const audio::Station *station = playing_station(radio, status);

    // ---- the banner, morphing ----
    list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, -rise);
    if (!skin_.settled())
    {
        // The old banner lifts away, then the new one drops in: one after
        // the other, so two titles never sit on top of each other.
        const float out = std::max(0.0f, 1.0f - 2.0f * e);
        const float in2 = std::max(0.0f, 2.0f * e - 1.0f);
        if (out > 0.0f)
        {
            list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, -(1.0f - out) * 30.0f);
            draw_banner_for(list, skin_.from(), out, pulse);
            list.pop_transform();
        }
        if (in2 > 0.0f)
        {
            list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, (1.0f - in2) * 30.0f);
            draw_banner_for(list, skin_.to(), in2, pulse);
            list.pop_transform();
        }
    }
    else
    {
        draw_banner_for(list, skin_.to(), 1.0f, pulse);
    }
    list.pop_transform();

    // ---- column titles ----
    const Color accent = skin_.accent();
    ui::text(list, fonts.semibold, "CHANNELS", kChannels.x + 26.0f, kTop - 28.0f, 16.0f,
             column_ == 0 ? kInk : kFaint, gfx::Align::left, 3.0f);
    const bool songs = song_mode();
    std::string title = songs ? "SONGS" : "STATIONS";
    if (const Channel *channel = focused_channel())
        title += "   /   " + ui::upper(channel->name);
    if (songs ? !songs_.empty() : !visible_.empty())
        title += "   (" + std::to_string(songs ? songs_.size() : visible_.size()) + ")";
    ui::text(list, fonts.semibold, fit(fonts.semibold, title, 16.0f, kStations.w - 40.0f, 3.0f),
             kStations.x + 26.0f, kTop - 28.0f, 16.0f, column_ == 1 ? kInk : kFaint,
             gfx::Align::left, 3.0f);
    list.rounded_rect(
        {(column_ == 0 ? kChannels.x : kStations.x) + 26.0f, kTop - 18.0f, 28.0f, 3.0f}, 1.5f,
        accent);

    ui::Canvas canvas{list, fonts, frame.glass_texture, pulse.time};
    channels_view_.draw(canvas);
    stations_view_.draw(canvas);

    // ---- now playing card ----
    const GenreSkin &now = station != nullptr ? skin_for(station->category) : default_skin();
    const Rect card = kCard;
    const float card_radius = std::min(now.radius, 26.0f);
    list.glass(frame.glass_texture, card, card_radius, Color::rgb(now.bg0, 0.62f));
    list.gradient_rect(card, card_radius, Color::rgb(now.bg1, 0.55f), Color::rgb(now.bg0, 0.30f));
    list.bordered_rect(card, card_radius, Color::rgb(0x000000, 0.0f), 1.5f,
                       Color::rgb(0xffffff, 0.14f));
    list.gradient_rect_h({card.x + card_radius, card.y, card.w - card_radius * 2.0f, 3.0f}, 1.5f,
                         Color::rgb(now.accent, 0.0f), Color::rgb(now.accent, 0.95f));
    const float pad = 30.0f;
    {
        float lx = card.x + pad;
        if (sounding(status) && station != nullptr)
        {
            list.circle(lx + 6.0f, card.y + 33.0f, 5.5f,
                        kLive.with_alpha(0.55f + 0.45f * std::sin(pulse.time * 4.0f)));
            lx += 20.0f;
        }
        ui::text(list, fonts.semibold, "NOW PLAYING", lx, card.y + 40.0f, 16.0f, kFaint,
                 gfx::Align::left, 3.0f);
        ui::text(list, skin_face(fonts, now.face), skin_title(now, now.title),
                 card.x + card.w - pad, card.y + 41.0f, 17.0f * now.title_scale / 0.8f,
                 Color::rgb(now.accent), gfx::Align::right, 3.0f);
    }
    const Rect cover{card.x + pad, card.y + 60.0f, kCover, kCover};
    // The record slides out of its sleeve while the music plays.
    const float out = sounding(status) ? 1.0f : 0.35f;
    const float disc_r = kCover * 0.47f;
    const float spin = sounding(status) && !pulse.reduced_motion ? pulse.time * 1.6f : 0.0f;
    draw_disc(list, cover.x + kCover * 0.5f + out * kCover * 0.40f, cover.y + kCover * 0.5f, disc_r,
              now, spin, disc_is_cd(now), 1.0f);
    draw_cover(list, fonts, cover, now, status, station, pulse, 1.0f);

    const float tx = card.x + pad;
    const float tw = card.w - pad * 2.0f;
    float y = cover.y + kCover + 56.0f;
    const std::string name = station != nullptr ? station->name : "Afterglow Mix";
    skin_name(list, fonts, now, name, tx, y, 38.0f, tw, kInk);
    y += 34.0f;
    const std::string what = station != nullptr ? station->style + "  /  " + station->country
                                                : "The songs that come with Afterglow";
    ui::text(list, fonts.regular, fit(fonts.regular, what, 20.0f, tw), tx, y, 20.0f, kDim);
    y += 42.0f;
    std::string song = status.title;
    if (song.empty())
        song = status.source == audio::Source::songs ? "" : "Waiting for the song title...";
    for (const std::string &line : wrap2(fonts.semibold, song, 24.0f, tw))
    {
        ui::text(list, fonts.semibold, line, tx, y, 24.0f, kInk);
        y += 31.0f;
    }
    y += 9.0f;
    const char *state = state_label(status);
    const bool failed = status.state == audio::RadioState::failed && station != nullptr;
    if (state[0] != '\0')
    {
        ui::text(list, fonts.semibold, state, tx, y, 18.0f, failed ? kBad : Color::rgb(now.accent),
                 gfx::Align::left, 2.0f);
        ui::text(list, fonts.mono, (station != nullptr ? "LIVE " : "") + clock_text(status.elapsed),
                 card.x + card.w - pad, y, 18.0f, kDim, gfx::Align::right);
    }
    y += 26.0f;
    std::string detail = failed ? status.message : status.format;
    if (station != nullptr && !failed && !station->format.empty())
        detail += (detail.empty() ? "" : "   /   ") + std::string("listed as ") + station->format;
    ui::text(list, fonts.regular, fit(fonts.regular, detail, 17.0f, tw), tx, y, 17.0f,
             failed ? kBad : kFaint);

    // The transport: previous, play or pause, stop, next, with their buttons.
    const ui::Button buttons[4] = {ui::Button::l1, ui::Button::r2, ui::Button::l2, ui::Button::r1};
    const float row_y = card.y + card.h - 78.0f;
    for (int i = 0; i < 4; ++i)
    {
        const float cx = card.x + 70.0f + static_cast<float>(i) * ((card.w - 140.0f) / 3.0f);
        const bool main = i == 1;
        transport_button(list, now, cx, row_y, main ? 31.0f : 26.0f, main, Color::rgb(now.accent));
        draw_icon(list, i, cx, row_y, main ? 1.15f : 0.95f, sounding(status),
                  main ? Color::rgb(now.bg0) : kInk);
        const float glyph_w = ui::button_width(buttons[i], 22.0f);
        ui::draw_button(list, fonts, ui::GlyphStyle::dark(), buttons[i], cx - glyph_w * 0.5f,
                        row_y + 46.0f, 22.0f);
    }

    if (keyboard_open_)
    {
        list.rounded_rect(
            {kKeyboard.x - 8.0f, kKeyboard.y - 82.0f, kKeyboard.w + 16.0f, kKeyboard.h + 90.0f},
            24.0f, Color::rgb(skin_.to().bg0, 0.94f));
        const Rect field{kKeyboard.x, kKeyboard.y - 74.0f, kKeyboard.w, 56.0f};
        list.rounded_rect(field, 14.0f, Color::rgb(0xffffff, 0.10f));
        list.bordered_rect(field, 14.0f, Color::rgb(0x000000, 0.0f), 2.0f, accent);
        const bool caret = std::fmod(pulse.time, 1.0f) < 0.6f;
        ui::text(list, fonts.semibold,
                 (query_.empty()
                      ? std::string(command_mode_ ? "Try: play bollywood 90s, next, like this song"
                                                  : "Search, or a command: play techno")
                      : query_) +
                     (caret ? "|" : ""),
                 field.x + 22.0f, field.y + 38.0f, 26.0f, query_.empty() ? kFaint : kInk);
        keyboard_.draw(canvas);
    }

    // ---- hints and the station count ----
    ui::HintLayout layout;
    layout.size = 32.0f;
    layout.text_size = 20.0f;
    layout.cy = 1022.0f;
    layout.item_gap = 28.0f;
    if (keyboard_open_)
    {
        static constexpr ui::Hint kHints[] = {
            {ui::Button::cross, "Type"},     {ui::Button::square, "Delete"},
            {ui::Button::triangle, "Space"}, {ui::Button::options, "Go"},
            {ui::Button::circle, "Close"},
        };
        ui::draw_hints(list, fonts, ui::GlyphStyle::dark(), kHints,
                       static_cast<int>(std::size(kHints)), kMargin, false, layout);
    }
    else
    {
        static constexpr ui::Hint kHints[] = {
            {ui::Button::cross, "Play"},
            {ui::Button::square, "Favorite"},
            {ui::Button::triangle, "Search / command"},
            {ui::Button::options, "Check stations"},
            {ui::Button::right_stick, "Working only"},
            {ui::Button::circle, "Back"},
        };
        static constexpr ui::Hint kSongHints[] = {
            {ui::Button::cross, "Tune in"},
            {ui::Button::square, "Like / unlike"},
            {ui::Button::triangle, "Search"},
            {ui::Button::circle, "Back"},
        };
        if (songs)
            ui::draw_hints(list, fonts, ui::GlyphStyle::dark(), kSongHints,
                           static_cast<int>(std::size(kSongHints)), kMargin, false, layout);
        else
            ui::draw_hints(list, fonts, ui::GlyphStyle::dark(), kHints,
                           static_cast<int>(std::size(kHints)), kMargin, false, layout);
    }
    if (radio != nullptr)
    {
        int working = 0;
        for (std::size_t i = 0; i < radio->stations().size(); ++i)
            working += radio->health(static_cast<int>(i)) == audio::Health::ok ? 1 : 0;
        const audio::CheckProgress check = radio->check_progress();
        char line[160];
        if (check.running)
            std::snprintf(line, sizeof(line), "CHECKING  %d / %d   %d WORKING  %d OFFLINE",
                          check.done, check.total, check.ok, check.failed);
        else if (working > 0)
            std::snprintf(line, sizeof(line), "%zu STATIONS  /  %d ANSWERED THE LAST CHECK",
                          radio->stations().size(), working);
        else
            std::snprintf(line, sizeof(line), "%zu STATIONS", radio->stations().size());
        const float right = gfx::kVirtualWidth - kMargin;
        float end = right;
        if (working_only_)
        {
            const float w = fonts.semibold.measure("WORKING ONLY", 15.0f, 2.0f) + 24.0f;
            list.rounded_rect({right - w, 1008.0f, w, 28.0f}, 14.0f, kOk.with_alpha(0.18f));
            ui::text(list, fonts.semibold, "WORKING ONLY", right - 12.0f, 1028.0f, 15.0f, kOk,
                     gfx::Align::right, 2.0f);
            end -= w + 16.0f;
        }
        ui::text(list, fonts.semibold, line, end, 1028.0f, 16.0f,
                 check.running ? Color::rgb(0x7fd8ff) : kFaint, gfx::Align::right, 2.0f);
        if (check.running && check.total > 0)
        {
            const float w = 420.0f;
            const Rect bar{end - w, 1040.0f, w, 3.0f};
            list.rounded_rect(bar, 1.5f, Color::rgb(0xffffff, 0.12f));
            list.rounded_rect({bar.x, bar.y,
                               w * static_cast<float>(check.done) / static_cast<float>(check.total),
                               bar.h},
                              1.5f, Color::rgb(0x7fd8ff));
        }
    }
    list.pop_opacity();
}

} // namespace hui::concepts
