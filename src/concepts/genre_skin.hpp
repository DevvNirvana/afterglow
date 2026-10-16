// Afterglow - Genre skins: every channel has its own type, colours and banner.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A skin is data (the face, four colours, a motif, list styling, a tagline,
// the scenes that suit it) plus drawing code for its motif: a banner that is
// redrawn every frame from shapes, so it can move with the music. The radio
// screen wears the skin of the channel being browsed; the visualiser wears
// the skin of the station that plays. SkinMorph cross-fades from one skin to
// the next so a genre change reads as the whole screen changing its clothes.
//
// The faces are free fonts (OFL, Apache 2.0) baked into the app's extra font
// slots; kGenreFontFiles is the order they are loaded in. A face that did not
// load falls back to the kit's display face.

#pragma once

#include "gfx/draw_list.hpp"
#include "ui/components/component.hpp"
#include "ui/fonts.hpp"
#include "ui/theme.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace hui::concepts
{

// Loaded into ui::Fonts::extra in this order (main.cpp, snapshot_main.cpp).
inline constexpr const char *kGenreFontFiles[ui::Fonts::kExtra] = {
    "bebas-neue.huifont", "michroma.huifont",         "monoton.huifont",
    "bungee.huifont",     "permanent-marker.huifont", "righteous.huifont",
};

enum class Face : std::uint8_t
{
    display,   // Montserrat (the kit's)
    bebas,     // Bebas Neue: tall condensed capitals
    michroma,  // Michroma: wide, technical
    monoton,   // Monoton: neon tubes
    bungee,    // Bungee: heavy signage
    marker,    // Permanent Marker: a fat felt pen
    righteous, // Righteous: rounded, glossy
    mono,      // DejaVu Sans Mono (the kit's)
};

enum class Motif : std::uint8_t
{
    afterglow,  // a sliced sun
    warehouse,  // techno: grid, corner brackets, a glitching title, a ticker
    vinyl,      // house: a spinning record
    halo,       // trance: rings of light and beams
    hazard,     // drum & bass: hazard tape and a big tempo
    wobble,     // bass: LFO waves
    strobe,     // hard dance: chevrons, flashes, a shaking title
    laser,      // EDM: a fan of sweeping lasers
    sunset,     // synthwave: a striped sun over a moving grid
    frost,      // chill: drifting bokeh
    gloss,      // pop: bubbles and sparkles
    sticker,    // hip-hop: halftone, tape and drips
    velvet,     // R&B: flowing silk
    marigold,   // Bollywood: a turning mandala and marigold garlands
    papel,      // Latin: strings of cut-paper flags and a blooming flower
    lightstick, // K-Pop: a sea of lightsticks waving in time
    kente,      // Afrobeats: woven kente bands and a talking drum
    arabesque,  // Arabic: an eight-point star lattice and a swinging lantern
};

struct GenreSkin
{
    const char *id;      // the station category it dresses ("" for the default)
    const char *title;   // the banner word ("TECHNO")
    const char *tagline; // under the banner
    Face face;
    float tracking;    // extra space in the banner title, per 100 px of size
    bool caps;         // the title is shown in capitals
    float title_scale; // the face's size relative to the others (tall vs wide faces)
    std::uint32_t bg0; // darkest
    std::uint32_t bg1; // the room's colour
    std::uint32_t accent;
    std::uint32_t accent2;
    Motif motif;
    float radius;                // panels and covers
    ui::HighlightKind highlight; // the station list's focus
    bool numbered;               // rows carry "01", "02", ...
    const char *scenes[4];       // the visualiser scenes that suit it, best first
};

// The skin for a station category; unknown categories get a default skin in
// a colour of their own (see channel_colour()).
const GenreSkin &skin_for(std::string_view category);
const GenreSkin &default_skin();
// All the built-in genre skins (tests).
std::span<const GenreSkin> genre_skins();

const ui::FontRef &skin_face(const ui::Fonts &fonts, Face face);

// What the motifs move with.
struct Pulse
{
    float time = 0.0f;
    float bass = 0.0f;
    float beat = 0.0f;
    const std::array<float, 16> *bands = nullptr;
    bool reduced_motion = false;
};

// A skin on its way in: draw `from` at 1 - t and `to` at t.
class SkinMorph
{
  public:
    void set(const GenreSkin &skin, bool instant = false);
    void update(float dt);
    const GenreSkin &from() const
    {
        return *from_;
    }
    const GenreSkin &to() const
    {
        return *to_;
    }
    float t() const
    {
        return t_;
    }
    bool settled() const
    {
        return t_ >= 1.0f;
    }
    // A skin colour, mixed between the two.
    gfx::Color bg0() const;
    gfx::Color bg1() const;
    gfx::Color accent() const;
    gfx::Color accent2() const;

  private:
    const GenreSkin *from_ = &default_skin();
    const GenreSkin *to_ = &default_skin();
    float t_ = 1.0f;
};

// The room behind a screen: the skin's gradient and its motif's background
// pattern over the whole rectangle.
void draw_skin_room(gfx::DrawList &list, const gfx::Rect &r, const GenreSkin &skin, float alpha,
                    const Pulse &pulse);

// The banner: the motif's art and the title in the skin's face, with the
// tagline under it. title overrides the skin's own word when not empty.
void draw_banner(gfx::DrawList &list, const ui::Fonts &fonts, const gfx::Rect &r,
                 const GenreSkin &skin, std::string_view title, std::string_view tagline,
                 float alpha, const Pulse &pulse);

// A cover for a station without album art: the skin's colours and motif in a
// square, the station's initials in the skin's face. seed varies the art.
void draw_generated_cover(gfx::DrawList &list, const ui::Fonts &fonts, const gfx::Rect &r,
                          const GenreSkin &skin, std::string_view name, std::uint32_t seed,
                          float alpha, const Pulse &pulse);

// A vinyl record (or, with cd, a compact disc) centred at (cx, cy), turned
// by angle (radians). label is drawn in the skin's colours.
void draw_disc(gfx::DrawList &list, float cx, float cy, float radius, const GenreSkin &skin,
               float angle, bool cd, float alpha);

// Afterglow's emblem: a sun cut by bands that widen towards the bottom.
// gaps_from: where the bands start (0 top, 1 bottom); scroll moves them down.
void sliced_sun(gfx::DrawList &list, float cx, float cy, float radius, gfx::Color top,
                gfx::Color bottom, float gaps_from = 0.5f, float scroll = 0.0f);

// A heart (liked songs): the fonts carry no symbol for one.
void draw_heart(gfx::DrawList &list, float cx, float cy, float size, gfx::Color color);

// The title in capitals if the skin asks for them.
std::string skin_title(const GenreSkin &skin, std::string_view text);

// Number of characters to show of the ticker text and other helpers live in
// the .cpp; this hash is shared so covers are stable per station.
std::uint32_t name_hash(std::string_view text);

} // namespace hui::concepts
