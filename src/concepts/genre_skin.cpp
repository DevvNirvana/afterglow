// Afterglow - Genre skins (see genre_skin.hpp).
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every motif is drawn from the draw list's shapes, so the banners cost no
// textures and can breathe with the bass. Angles follow the draw list:
// radians, clockwise from 12 o'clock.

#include "concepts/genre_skin.hpp"

#include "concepts/text_fit.hpp"

#include <algorithm>
#include <cmath>

namespace hui::concepts
{

namespace
{

using gfx::Color;
using gfx::Rect;

constexpr float kPi = 3.14159265f;
constexpr float kMorphSeconds = 0.6f;

// clang-format off
constexpr GenreSkin kSkins[] = {
    {"Techno", "TECHNO", "WAREHOUSE  /  130 BPM  /  LIGHTS OFF", Face::bebas, 2.0f, true, 1.0f,
     0x050505, 0x1a0406, 0xff2a2a, 0xf2f2f2, Motif::warehouse, 4.0f, ui::HighlightKind::bar, true,
     {"Rave Tunnel", "Wormhole", "Strobe Lasers", "Green Rain"}},
    {"House", "HOUSE", "DEEP  /  SOULFUL  /  ALL NIGHT LONG", Face::righteous, 1.0f, true, 0.82f,
     0x140904, 0x3a1a08, 0xffa31a, 0xffe3b0, Motif::vinyl, 24.0f, ui::HighlightKind::fill, false,
     {"Paint Swirl", "Liquid Chrome", "Echo Orb", "Hall of Candles"}},
    {"Trance", "TRANCE", "UPLIFTING  /  138 BPM  /  HANDS IN THE AIR", Face::michroma, 6.0f, true,
     0.56f, 0x06041a, 0x1d1052, 0x9b7bff, 0x6fe6ff, Motif::halo, 18.0f, ui::HighlightKind::glow,
     false, {"Kaleido Melt", "Prism", "Dimension", "Cathedral Rays"}},
    {"Drum & Bass", "DRUM & BASS", "174 BPM  /  ROLLERS  /  JUNGLE PRESSURE", Face::bebas, 2.0f,
     true, 1.0f, 0x020a07, 0x06281c, 0x14e0a4, 0xffb000, Motif::hazard, 6.0f,
     ui::HighlightKind::bar, true, {"Vortex", "Fractal Bloom", "Pulse Tunnel", "Classic Bars"}},
    {"Bass & Dubstep", "BASS", "LOW END  /  140 BPM  /  SUB PRESSURE", Face::bungee, 1.0f, true,
     0.72f, 0x050a02, 0x1c2c05, 0x9be22e, 0xe040ff, Motif::wobble, 10.0f, ui::HighlightKind::fill,
     false, {"Melting Bars", "Ember Peak", "Liquid Chrome", "Scope"}},
    {"Hard Dance", "HARD DANCE", "HARDSTYLE  /  150+ BPM  /  NO MERCY", Face::bungee, 1.0f, true,
     0.72f, 0x0a0000, 0x2e0600, 0xff4b2b, 0xffe600, Motif::strobe, 2.0f, ui::HighlightKind::fill,
     true, {"Strobe Lasers", "Neon Smear", "Rave Tunnel", "Acid Bloom"}},
    {"EDM & Dance", "EDM", "MAIN STAGE  /  128 BPM  /  DROP INCOMING", Face::michroma, 6.0f, true,
     0.56f, 0x020818, 0x07224e, 0x2ec5ff, 0xff3cac, Motif::laser, 16.0f, ui::HighlightKind::glow,
     false, {"Neon Smear", "Prism", "Kaleidoscope", "Skyline"}},
    {"Synthwave", "SYNTHWAVE", "OUTRUN  /  1986  /  NIGHT DRIVE", Face::monoton, 2.0f, true, 0.66f,
     0x0b0221, 0x3a0a4a, 0xff2bd6, 0xffb347, Motif::sunset, 12.0f, ui::HighlightKind::glow, false,
     {"Skyline", "Mode Seven", "Polygon 95", "Liquid Chrome"}},
    {"Chill & Electronica", "chill", "DOWNTEMPO  /  AMBIENT  /  SLOW DOWN", Face::display, 8.0f,
     false, 0.86f, 0x06121a, 0x103440, 0x64d2ff, 0xb8f5e0, Motif::frost, 28.0f,
     ui::HighlightKind::tint, false, {"Smoke Signals", "Silk", "Nebula", "Liquid Dream"}},
    {"Pop & Hits", "POP", "TOP 40  /  SING ALONG  /  ON REPEAT", Face::righteous, 1.0f, true, 0.82f,
     0x1a0614, 0x4c0f3c, 0xff7ab6, 0xffe066, Motif::gloss, 26.0f, ui::HighlightKind::fill, false,
     {"Bubble Pop", "Orb", "Kaleidoscope", "Starfall"}},
    {"Hip-Hop & Rap", "HIP-HOP", "BARS  /  BEATS  /  BOOM BAP TO TRAP", Face::marker, 0.0f, true,
     0.80f, 0x0d0b05, 0x2c2405, 0xffd60a, 0xffffff, Motif::sticker, 8.0f, ui::HighlightKind::bar,
     true, {"Classic Bars", "Skyline", "Scope", "Ember Peak"}},
    {"R&B & Soul", "r&b + soul", "SMOOTH  /  LATE NIGHT  /  SLOW JAMS", Face::display, 6.0f, false,
     0.86f, 0x12051a, 0x36104c, 0xc38bff, 0xffc6a8, Motif::velvet, 28.0f, ui::HighlightKind::tint,
     false, {"Silk", "Hall of Candles", "Liquid Dream", "Echo Orb"}},
    {"Bollywood & Desi", "BOLLYWOOD", "FILMI  /  BHANGRA  /  DESI BEATS", Face::righteous, 1.5f,
     true, 0.82f, 0x1a0508, 0x4a0d1c, 0xff8a1f, 0xff3d8b, Motif::marigold, 22.0f,
     ui::HighlightKind::fill, false, {"Kaleidoscope", "Hall of Candles", "Bubble Pop", "Kaleido Melt"}},
    {"Latin", "LATIN", "REGGAETON  /  SALSA  /  BACHATA", Face::bungee, 1.0f, true, 0.72f,
     0x16030f, 0x4a0a2e, 0xff3d6e, 0x2de0c8, Motif::papel, 18.0f, ui::HighlightKind::glow, false,
     {"Paint Swirl", "Origami", "Bubble Pop", "Kaleidoscope"}},
    {"K-Pop", "K-POP", "IDOLS  /  COMEBACKS  /  LIGHTSTICKS UP", Face::michroma, 4.0f, true, 0.56f,
     0x0a0618, 0x26104a, 0xff5ec4, 0x7ee8ff, Motif::lightstick, 24.0f, ui::HighlightKind::glow,
     false, {"Prism", "Halftone Pop", "Bubble Pop", "Strobe Lasers"}},
    {"Afrobeats", "AFROBEATS", "AFROBEATS  /  AMAPIANO  /  AFRO HOUSE", Face::bebas, 2.0f, true,
     1.0f, 0x120a02, 0x3a2206, 0xffb81c, 0x22c77a, Motif::kente, 8.0f, ui::HighlightKind::bar,
     true, {"Paint Swirl", "Coral Labyrinth", "Ember Peak", "Paper Cut"}},
    {"Arabic", "ARABIC", "KHALEEJI  /  RAI  /  ARABIC POP", Face::display, 10.0f, true, 0.80f,
     0x03100f, 0x0b302b, 0xe8b04a, 0x3fd6c6, Motif::arabesque, 20.0f, ui::HighlightKind::tint,
     false, {"Stained Glass", "Kintsugi", "Moon Tide", "Hall of Candles"}},
};

constexpr GenreSkin kDefault = {
    "", "AFTERGLOW", "FREE  /  NON-STOP  /  FROM AROUND THE WORLD", Face::display, 8.0f, true, 0.80f,
    0x0b0612, 0x2a0a1e, 0xff5a36, 0xffc857, Motif::afterglow, 20.0f, ui::HighlightKind::tint, false,
    {"Paint Swirl", "Orb", "Nebula", "Pulse Tunnel"}};
// clang-format on

Color rgb(std::uint32_t c, float a = 1.0f)
{
    return Color::rgb(c, a);
}

float hash01(std::uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return static_cast<float>(x & 0xffffffu) / 16777215.0f;
}

float fract(float x)
{
    return x - std::floor(x);
}

float band(const Pulse &pulse, int i)
{
    return pulse.bands != nullptr ? (*pulse.bands)[static_cast<std::size_t>(std::clamp(i, 0, 15))]
                                  : 0.0f;
}

void sine_ribbon(gfx::DrawList &list, float x0, float x1, float cy, float amplitude, float freq,
                 float phase, float thickness, Color color)
{
    constexpr int kSegments = 48;
    float px = x0;
    float py = cy + amplitude * std::sin(phase);
    for (int i = 1; i <= kSegments; ++i)
    {
        const float k = static_cast<float>(i) / static_cast<float>(kSegments);
        const float x = x0 + (x1 - x0) * k;
        // Taper the ends so ribbons fade into the banner.
        const float envelope = std::sin(k * kPi);
        const float y = cy + amplitude * envelope * std::sin(phase + k * freq);
        list.line(px, py, x, y, thickness, color.with_alpha(color.a * (0.25f + 0.75f * envelope)));
        px = x;
        py = y;
    }
}

void corner_brackets(gfx::DrawList &list, const Rect &r, float length, float thickness, Color color)
{
    const float x0 = r.x;
    const float y0 = r.y;
    const float x1 = r.x + r.w;
    const float y1 = r.y + r.h;
    list.rounded_rect({x0, y0, length, thickness}, 0.0f, color);
    list.rounded_rect({x0, y0, thickness, length}, 0.0f, color);
    list.rounded_rect({x1 - length, y0, length, thickness}, 0.0f, color);
    list.rounded_rect({x1 - thickness, y0, thickness, length}, 0.0f, color);
    list.rounded_rect({x0, y1 - thickness, length, thickness}, 0.0f, color);
    list.rounded_rect({x0, y1 - length, thickness, length}, 0.0f, color);
    list.rounded_rect({x1 - length, y1 - thickness, length, thickness}, 0.0f, color);
    list.rounded_rect({x1 - thickness, y1 - length, thickness, length}, 0.0f, color);
}

void beam(gfx::DrawList &list, float x, float y, float angle, float length, float width,
          Color color)
{
    const float ex = x + std::sin(angle) * length;
    const float ey = y - std::cos(angle) * length;
    list.line(x, y, ex, ey, width * 5.0f, color.with_alpha(color.a * 0.10f));
    list.line(x, y, ex, ey, width * 2.0f, color.with_alpha(color.a * 0.30f));
    list.line(x, y, ex, ey, width, color);
}

// Diagonal stripes across r, two colours, scrolling.
void stripes(gfx::DrawList &list, const Rect &r, Color a, Color b, float scroll)
{
    list.push_clip(r);
    list.rounded_rect(r, 0.0f, b);
    const float pitch = r.h * 2.2f;
    const float start = r.x - r.h * 2.0f - fract(scroll) * pitch;
    for (int i_x = 0; (start) + static_cast<float>(i_x) * (pitch) < r.x + r.w + r.h; ++i_x)
    {
        const float x = (start) + static_cast<float>(i_x) * (pitch);
        const float xy[8] = {
            x, r.y + r.h, x + pitch * 0.5f, r.y + r.h, x + pitch * 0.5f + r.h, r.y, x + r.h, r.y};
        list.polygon(xy, 4, a);
    }
    list.pop_clip();
}

void chevron(gfx::DrawList &list, float x, float cy, float h, float thickness, Color color)
{
    list.line(x, cy - h * 0.5f, x + h * 0.5f, cy, thickness, color);
    list.line(x + h * 0.5f, cy, x, cy + h * 0.5f, thickness, color);
}

void bokeh(gfx::DrawList &list, const Rect &r, int count, Color a, Color b, float time, float size,
           std::uint32_t seed)
{
    for (int i = 0; i < count; ++i)
    {
        const auto k = static_cast<std::uint32_t>(i) * 7919u + seed;
        const float speed = 0.01f + 0.02f * hash01(k + 3u);
        const float x = r.x + r.w * fract(hash01(k) + time * speed);
        const float y = r.y + r.h * fract(hash01(k + 1u) - time * speed * 0.6f);
        const float radius = size * (0.4f + hash01(k + 2u));
        const Color c = gfx::mix(a, b, hash01(k + 5u));
        const float twinkle =
            0.6f + 0.4f * std::sin(time * (0.5f + hash01(k + 6u)) + hash01(k) * 9.0f);
        list.circle(x, y, radius, c.with_alpha(c.a * twinkle));
        list.ring(x, y, radius, 1.5f, c.with_alpha(c.a * twinkle * 1.6f));
    }
}

void halftone(gfx::DrawList &list, const Rect &r, Color color, float pitch, float time)
{
    for (int i_y = 0; (r.y + pitch * 0.5f) + static_cast<float>(i_y) * (pitch) < r.y + r.h; ++i_y)
    {
        const float y = (r.y + pitch * 0.5f) + static_cast<float>(i_y) * (pitch);
        for (int i_x = 0; (r.x + pitch * 0.5f) + static_cast<float>(i_x) * (pitch) < r.x + r.w;
             ++i_x)
        {
            const float x = (r.x + pitch * 0.5f) + static_cast<float>(i_x) * (pitch);
            // Bigger dots towards the top-right, rippling slowly.
            const float k = ((x - r.x) / r.w + (r.y + r.h - y) / r.h) * 0.5f;
            const float wave = 0.5f + 0.5f * std::sin(time * 0.8f + (x + y) * 0.01f);
            const float radius =
                pitch * 0.42f * std::clamp(k * k * (0.7f + 0.3f * wave), 0.0f, 1.0f);
            if (radius > 0.6f)
                list.circle(x, y, radius, color);
        }
    }
}

void perspective_grid(gfx::DrawList &list, const Rect &r, Color color, float travel)
{
    // Horizontal lines rushing towards the viewer, vertical ones converging.
    const float horizon = r.y;
    const float cx = r.x + r.w * 0.5f;
    for (int i = 0; i < 14; ++i)
    {
        const float z = (static_cast<float>(i) + 1.0f - fract(travel)) / 14.0f;
        const float y = horizon + r.h * z * z;
        list.rounded_rect({r.x, y, r.w, 1.0f + 2.0f * z}, 0.0f, color.with_alpha(color.a * z));
    }
    for (int i = -12; i <= 12; ++i)
    {
        const float spread = static_cast<float>(i) / 12.0f;
        list.line(cx + spread * r.w * 0.08f, horizon, cx + spread * r.w * 1.2f, r.y + r.h, 1.5f,
                  color.with_alpha(color.a * 0.7f));
    }
}

// A mandala: rings of petals around a bright centre, turning; the outer
// rings breathe with the music.
void mandala(gfx::DrawList &list, float cx, float cy, float radius, Color a, Color b, float turn,
             const Pulse &pulse)
{
    constexpr int kRings = 4;
    for (int ring = kRings - 1; ring >= 0; --ring)
    {
        const float k = static_cast<float>(ring + 1) / static_cast<float>(kRings);
        const int petals = 8 + ring * 4;
        const float lift = 1.0f + 0.12f * band(pulse, ring * 4) * k;
        const float r = radius * k * lift;
        const float petal = radius * (0.30f - 0.03f * static_cast<float>(ring));
        const float spin =
            (ring % 2 == 0 ? turn : -turn) * (1.0f - 0.15f * static_cast<float>(ring));
        const Color c = ring % 2 == 0 ? a : b;
        for (int i = 0; i < petals; ++i)
        {
            const float angle =
                spin + static_cast<float>(i) * 2.0f * kPi / static_cast<float>(petals);
            const float px = cx + std::cos(angle) * (r - petal * 0.5f);
            const float py = cy + std::sin(angle) * (r - petal * 0.5f);
            list.rotated_rect({px - petal * 0.5f, py - petal * 0.18f, petal, petal * 0.36f},
                              petal * 0.18f, angle, c.with_alpha(c.a * (0.55f + 0.45f * k)));
        }
        list.ring(cx, cy, r * 0.62f, 1.5f, c.with_alpha(c.a * 0.5f));
    }
    list.circle(cx, cy, radius * 0.16f, b);
    list.circle(cx, cy, radius * 0.09f, rgb(0xfff2c0, a.a));
}

// A garland of marigolds sagging between x0 and x1 from height y.
void garland(gfx::DrawList &list, float x0, float x1, float y, float sag, float bead, Color a,
             Color b, float sway)
{
    const int count = std::max(2, static_cast<int>((x1 - x0) / (bead * 1.7f)));
    for (int i = 0; i <= count; ++i)
    {
        const float k = static_cast<float>(i) / static_cast<float>(count);
        const float x = x0 + (x1 - x0) * k + std::sin(sway + k * 3.0f) * bead * 0.3f;
        const float yy = y + sag * 4.0f * k * (1.0f - k);
        list.circle(x, yy, bead, i % 3 == 0 ? b : a);
        list.circle(x - bead * 0.3f, yy - bead * 0.3f, bead * 0.35f, rgb(0xffffff, 0.25f * a.a));
    }
}

// A string of papel picado: cut-paper flags with holes and a zigzag hem,
// hanging from a line that sags between x0 and x1.
void papel_picado(gfx::DrawList &list, float x0, float x1, float y, float sag, float flag,
                  const Color *colours, int colour_count, Color hole, float sway)
{
    const int count = std::max(2, static_cast<int>((x1 - x0) / (flag * 1.25f)));
    // The string sags like the flags hanging from it.
    for (int i = 0; i < 24; ++i)
    {
        const float a = static_cast<float>(i) / 24.0f;
        const float b = static_cast<float>(i + 1) / 24.0f;
        list.line(x0 + (x1 - x0) * a, y + sag * 4.0f * a * (1.0f - a), x0 + (x1 - x0) * b,
                  y + sag * 4.0f * b * (1.0f - b), 1.5f, rgb(0xffffff, 0.35f * colours[0].a));
    }
    for (int i = 0; i < count; ++i)
    {
        const float k = (static_cast<float>(i) + 0.5f) / static_cast<float>(count);
        const float x = x0 + (x1 - x0) * k;
        const float top = y + sag * 4.0f * k * (1.0f - k);
        // The flags sway on the string (shifted, the draw list does not turn).
        const float lean = std::sin(sway + k * 5.0f + static_cast<float>(i)) * flag * 0.08f;
        const Color c = colours[i % colour_count];
        const float w = flag;
        const float h = flag * 1.15f;
        list.push_transform(1.0f, 0.0f, 0.0f, x + lean, top);
        list.rounded_rect({-w * 0.5f, 0.0f, w, h}, 1.0f, c);
        // The hem: a row of small triangles below the flag.
        for (int z = 0; z < 5; ++z)
        {
            const float zx = -w * 0.5f + w * (static_cast<float>(z) + 0.5f) / 5.0f;
            const float xy[6] = {zx - w * 0.1f, h, zx + w * 0.1f, h, zx, h + w * 0.12f};
            list.polygon(xy, 3, c);
        }
        // The cut-outs: a flower of holes and a diamond.
        list.circle(0.0f, h * 0.42f, w * 0.12f, hole);
        for (int petal = 0; petal < 6; ++petal)
        {
            const float a = static_cast<float>(petal) * kPi / 3.0f;
            list.circle(std::sin(a) * w * 0.24f, h * 0.42f - std::cos(a) * w * 0.24f, w * 0.06f,
                        hole);
        }
        list.rotated_rect({-w * 0.06f, h * 0.78f, w * 0.12f, w * 0.12f}, 0.0f, 0.785398f, hole);
        list.pop_transform();
    }
}

// A flower of round petals, opening a little on the beat.
void bloom(gfx::DrawList &list, float cx, float cy, float radius, Color petal, Color heart,
           float turn, float open)
{
    for (int i = 0; i < 5; ++i)
    {
        const float a = turn + static_cast<float>(i) * 2.0f * kPi / 5.0f;
        const float d = radius * (0.48f + 0.08f * open);
        list.circle(cx + std::sin(a) * d, cy - std::cos(a) * d, radius * 0.46f, petal);
    }
    list.circle(cx, cy, radius * 0.3f, heart);
    list.circle(cx - radius * 0.08f, cy - radius * 0.08f, radius * 0.1f,
                rgb(0xffffff, 0.5f * heart.a));
}

// A lightstick: a handle and a glowing head (a heart or a ball).
void lightstick(gfx::DrawList &list, float x, float y, float size, float lean, Color glow,
                bool heart)
{
    const float hx = x + std::sin(lean) * size * 1.6f;
    const float hy = y - std::cos(lean) * size * 1.6f;
    list.line(x, y, hx, hy, size * 0.22f, rgb(0x1a1a24, glow.a));
    list.line(x, y, hx, hy, size * 0.08f, rgb(0xffffff, 0.25f * glow.a));
    list.glow({hx - size, hy - size, size * 2.0f, size * 2.0f}, size, size * 0.9f,
              glow.with_alpha(glow.a * 0.35f));
    if (heart)
        draw_heart(list, hx, hy, size * 1.25f, glow);
    else
        list.circle(hx, hy, size * 0.62f, glow);
    list.circle(hx - size * 0.18f, hy - size * 0.2f, size * 0.18f, rgb(0xffffff, 0.75f * glow.a));
}

// A kente band: blocks of woven stripes, alternating direction, in the
// cloth's gold, green, red and black.
void kente(gfx::DrawList &list, const Rect &r, Color gold, Color green, Color dark, float scroll,
           float alpha)
{
    const Color red = rgb(0xd6312b, alpha);
    const Color colours[4] = {gold, green, red, dark};
    const float block = r.h;
    list.push_clip(r);
    const float start = r.x - fract(scroll) * block * 2.0f;
    for (int i = 0; start + static_cast<float>(i) * block < r.x + r.w; ++i)
    {
        const float x = start + static_cast<float>(i) * block;
        // Blocks keep their colours as the band scrolls (either way).
        const int phase = ((i + static_cast<int>(std::floor(scroll)) * 2) % 4 + 4) % 4;
        const Color base = colours[phase];
        list.rounded_rect({x, r.y, block, r.h}, 0.0f, base);
        const Color stripe = colours[(phase + 1) % 4];
        if (i % 2 == 0)
        {
            for (int s = 0; s < 4; ++s)
                list.rounded_rect(
                    {x, r.y + r.h * (0.12f + 0.22f * static_cast<float>(s)), block, r.h * 0.08f},
                    0.0f, stripe);
        }
        else
        {
            for (int s = 0; s < 3; ++s)
                list.rounded_rect(
                    {x + block * (0.2f + 0.25f * static_cast<float>(s)), r.y, block * 0.1f, r.h},
                    0.0f, stripe);
            list.rounded_rect({x + block * 0.35f, r.y + r.h * 0.35f, block * 0.3f, r.h * 0.3f},
                              0.0f, colours[(phase + 2) % 4]);
        }
    }
    list.pop_clip();
}

// The eight-point star of Islamic geometry (two squares, one turned 45
// degrees), drawn as an outline in `line` over a fill.
void khatam(gfx::DrawList &list, float cx, float cy, float radius, float turn, Color fill,
            Color line, float thickness)
{
    float xy[32];
    for (int i = 0; i < 16; ++i)
    {
        const float a = turn + static_cast<float>(i) * kPi / 8.0f;
        const float r = i % 2 == 0 ? radius : radius * 0.7654f; // where the squares cross
        xy[i * 2] = cx + std::sin(a) * r;
        xy[i * 2 + 1] = cy - std::cos(a) * r;
    }
    if (fill.a > 0.0f)
        list.polygon(xy, 16, fill);
    if (thickness > 0.0f)
        for (int i = 0; i < 16; ++i)
        {
            const int j = (i + 1) % 16;
            list.line(xy[i * 2], xy[i * 2 + 1], xy[j * 2], xy[j * 2 + 1], thickness, line);
        }
}

// A pierced brass lantern on a chain, swinging.
void lantern(gfx::DrawList &list, float x, float top, float size, float swing, Color brass,
             Color light)
{
    const float lx = x + std::sin(swing) * size * 0.6f;
    const float ly = top + size * 1.6f;
    list.line(x, top, lx, ly - size * 0.6f, 2.0f, brass);
    list.glow({lx - size * 1.2f, ly - size * 1.0f, size * 2.4f, size * 2.4f}, size * 1.2f,
              size * 1.2f, light.with_alpha(light.a * 0.30f));
    const float body[12] = {lx,
                            ly - size * 0.6f,
                            lx + size * 0.45f,
                            ly - size * 0.2f,
                            lx + size * 0.45f,
                            ly + size * 0.5f,
                            lx,
                            ly + size * 0.9f,
                            lx - size * 0.45f,
                            ly + size * 0.5f,
                            lx - size * 0.45f,
                            ly - size * 0.2f};
    list.polygon(body, 6, brass);
    list.rounded_rect({lx - size * 0.3f, ly - size * 0.1f, size * 0.6f, size * 0.5f}, size * 0.1f,
                      light);
    for (int i = 0; i < 3; ++i)
        list.rounded_rect({lx - size * 0.24f + static_cast<float>(i) * size * 0.2f,
                           ly - size * 0.1f, size * 0.04f, size * 0.5f},
                          0.0f, brass);
    list.circle(lx, ly - size * 0.66f, size * 0.08f, brass);
}

// A crescent: a disc with a darker disc over its side.
void crescent(gfx::DrawList &list, float cx, float cy, float radius, Color moon, Color sky)
{
    list.circle(cx, cy, radius, moon);
    list.circle(cx + radius * 0.38f, cy - radius * 0.18f, radius * 0.86f, sky);
}

std::string upper(std::string_view text)
{
    std::string out(text);
    for (char &c : out)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    return out;
}

std::string initials(std::string_view name)
{
    std::string out;
    bool start = true;
    for (char c : name)
    {
        const bool letter =
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (letter && start && out.size() < 2)
            out += static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
        start = !letter;
    }
    return out.empty() ? "FM" : out;
}

} // namespace

const GenreSkin &skin_for(std::string_view category)
{
    for (const GenreSkin &skin : kSkins)
        if (category == skin.id)
            return skin;
    return kDefault;
}

const GenreSkin &default_skin()
{
    return kDefault;
}

std::span<const GenreSkin> genre_skins()
{
    return kSkins;
}

const ui::FontRef &skin_face(const ui::Fonts &fonts, Face face)
{
    const auto extra = [&](int i) -> const ui::FontRef &
    { return fonts.extra[i].font != nullptr ? fonts.extra[i] : fonts.display; };
    switch (face)
    {
    case Face::bebas:
        return extra(0);
    case Face::michroma:
        return extra(1);
    case Face::monoton:
        return extra(2);
    case Face::bungee:
        return extra(3);
    case Face::marker:
        return extra(4);
    case Face::righteous:
        return extra(5);
    case Face::mono:
        return fonts.mono;
    case Face::display:
        break;
    }
    return fonts.display;
}

void draw_heart(gfx::DrawList &list, float cx, float cy, float size, Color color)
{
    list.circle(cx - size * 0.25f, cy - size * 0.08f, size * 0.27f, color);
    list.circle(cx + size * 0.25f, cy - size * 0.08f, size * 0.27f, color);
    list.rotated_rect({cx - size * 0.26f, cy - size * 0.14f, size * 0.52f, size * 0.52f}, 1.0f,
                      0.7853982f, color);
}

std::string skin_title(const GenreSkin &skin, std::string_view text)
{
    return skin.caps ? upper(text) : std::string(text);
}

std::uint32_t name_hash(std::string_view text)
{
    std::uint32_t hash = 2166136261u;
    for (char c : text)
        hash = (hash ^ static_cast<unsigned char>(c)) * 16777619u;
    return hash;
}

void sliced_sun(gfx::DrawList &list, float cx, float cy, float radius, Color top, Color bottom,
                float gaps_from, float scroll)
{
    // The disc is one smooth circle, drawn once per strip through a clip,
    // so its edge stays anti-aliased while the gaps cut straight across.
    constexpr int kBands = 6;
    const Rect disc{cx - radius, cy - radius, radius * 2.0f, radius * 2.0f};
    const auto strip = [&](float k0, float k1)
    {
        if (k1 <= k0)
            return;
        list.push_clip({disc.x - 2.0f, disc.y + disc.h * k0, disc.w + 4.0f, disc.h * (k1 - k0)});
        list.gradient_rect(disc, radius, top, bottom);
        list.pop_clip();
    };
    strip(0.0f, gaps_from);
    const float band = (1.0f - gaps_from) / static_cast<float>(kBands);
    const float shift = (scroll - std::floor(scroll)) * band;
    for (int i = -1; i < kBands; ++i)
    {
        const float b0 = gaps_from + static_cast<float>(i) * band + shift;
        const float depth = std::clamp((b0 - gaps_from) / (1.0f - gaps_from), 0.0f, 1.0f);
        const float gap = band * (0.18f + 0.5f * depth);
        strip(std::max(b0, gaps_from), std::min(b0 + band - gap, 1.0f));
    }
}

// ---- SkinMorph ----

void SkinMorph::set(const GenreSkin &skin, bool instant)
{
    if (&skin == to_)
    {
        if (instant)
            t_ = 1.0f;
        return;
    }
    // A change mid-morph starts from whichever skin is showing more.
    from_ = t_ >= 0.5f ? to_ : from_;
    to_ = &skin;
    t_ = instant ? 1.0f : 0.0f;
    if (instant)
        from_ = to_;
}

void SkinMorph::update(float dt)
{
    t_ = std::min(1.0f, t_ + dt / kMorphSeconds);
}

namespace
{
float ease(float t)
{
    return t * t * (3.0f - 2.0f * t);
}
} // namespace

Color SkinMorph::bg0() const
{
    return gfx::mix(rgb(from_->bg0), rgb(to_->bg0), ease(t_));
}
Color SkinMorph::bg1() const
{
    return gfx::mix(rgb(from_->bg1), rgb(to_->bg1), ease(t_));
}
Color SkinMorph::accent() const
{
    return gfx::mix(rgb(from_->accent), rgb(to_->accent), ease(t_));
}
Color SkinMorph::accent2() const
{
    return gfx::mix(rgb(from_->accent2), rgb(to_->accent2), ease(t_));
}

// ---- the room ----

void draw_skin_room(gfx::DrawList &list, const Rect &r, const GenreSkin &skin, float alpha,
                    const Pulse &pulse)
{
    if (alpha <= 0.001f)
        return;
    const float t = pulse.reduced_motion ? 0.0f : pulse.time;
    const Color accent = rgb(skin.accent);
    const Color accent2 = rgb(skin.accent2);
    list.gradient_rect(r, 0.0f, rgb(skin.bg0, 0.80f * alpha), rgb(skin.bg1, 0.80f * alpha));
    // The room's light, from the top-right.
    list.glow({r.x + r.w * 0.62f, r.y - r.h * 0.2f, r.w * 0.36f, r.h * 0.30f}, r.h * 0.15f,
              r.h * 0.45f, accent.with_alpha(0.10f * alpha * (1.0f + pulse.bass * 0.6f)));
    switch (skin.motif)
    {
    case Motif::warehouse:
        for (int i_x = 0; (r.x) + static_cast<float>(i_x) * (80.0f) < r.x + r.w; ++i_x)
        {
            const float x = (r.x) + static_cast<float>(i_x) * (80.0f);
            list.rounded_rect({x, r.y, 1.0f, r.h}, 0.0f, accent2.with_alpha(0.035f * alpha));
        }
        for (int i_y = 0; (r.y) + static_cast<float>(i_y) * (80.0f) < r.y + r.h; ++i_y)
        {
            const float y = (r.y) + static_cast<float>(i_y) * (80.0f);
            list.rounded_rect({r.x, y, r.w, 1.0f}, 0.0f, accent2.with_alpha(0.035f * alpha));
        }
        break;
    case Motif::sunset:
        perspective_grid(list, {r.x, r.y + r.h * 0.74f, r.w, r.h * 0.26f},
                         accent.with_alpha(0.22f * alpha), t * 0.6f);
        break;
    case Motif::frost:
        bokeh(list, r, 26, accent.with_alpha(0.05f * alpha), accent2.with_alpha(0.05f * alpha), t,
              60.0f, 11u);
        break;
    case Motif::gloss:
        bokeh(list, r, 18, accent.with_alpha(0.05f * alpha), accent2.with_alpha(0.06f * alpha), t,
              90.0f, 23u);
        break;
    case Motif::sticker:
        halftone(list, r, accent.with_alpha(0.05f * alpha), 24.0f, t);
        break;
    case Motif::velvet:
        for (int i = 0; i < 4; ++i)
            sine_ribbon(list, r.x - 100.0f, r.x + r.w + 100.0f,
                        r.y + r.h * (0.55f + 0.1f * static_cast<float>(i)), r.h * 0.12f, 5.0f,
                        t * 0.25f + static_cast<float>(i) * 1.3f, 60.0f,
                        (i % 2 == 0 ? accent : accent2).with_alpha(0.025f * alpha));
        break;
    case Motif::marigold:
        bokeh(list, r, 16, accent.with_alpha(0.05f * alpha), accent2.with_alpha(0.05f * alpha), t,
              70.0f, 31u);
        mandala(list, r.x + r.w * 0.86f, r.y + r.h * 0.82f, r.h * 0.42f,
                accent.with_alpha(0.035f * alpha), accent2.with_alpha(0.03f * alpha), t * 0.03f,
                pulse);
        break;
    case Motif::papel:
    {
        const Color flags[3] = {accent.with_alpha(0.06f * alpha), accent2.with_alpha(0.06f * alpha),
                                rgb(0xffd23f, 0.06f * alpha)};
        papel_picado(list, r.x - 20.0f, r.x + r.w + 20.0f, r.y + 6.0f, r.h * 0.06f, 46.0f, flags, 3,
                     rgb(skin.bg0, 0.0f), t * 0.6f);
        break;
    }
    case Motif::lightstick:
        bokeh(list, r, 22, accent.with_alpha(0.05f * alpha), accent2.with_alpha(0.05f * alpha), t,
              40.0f, 41u);
        break;
    case Motif::kente:
        kente(list, {r.x, r.y + r.h - 26.0f, r.w, 26.0f}, accent.with_alpha(0.10f * alpha),
              accent2.with_alpha(0.10f * alpha), rgb(skin.bg0, 0.10f * alpha), t * 0.05f,
              0.10f * alpha);
        break;
    case Motif::arabesque:
        for (int i_y = 0; i_y < 6; ++i_y)
            for (int i_x = 0; i_x < 14; ++i_x)
                khatam(list,
                       r.x + static_cast<float>(i_x) * 150.0f + static_cast<float>(i_y % 2) * 75.0f,
                       r.y + static_cast<float>(i_y) * 130.0f, 52.0f, 0.0f, rgb(0, 0.0f),
                       accent.with_alpha(0.045f * alpha), 1.5f);
        break;
    case Motif::hazard:
        for (int i_y = 0; (r.y + 40.0f) + static_cast<float>(i_y) * (140.0f) < r.y + r.h; ++i_y)
        {
            const float y = (r.y + 40.0f) + static_cast<float>(i_y) * (140.0f);
            list.rounded_rect({r.x, y, r.w, 1.0f}, 0.0f, accent.with_alpha(0.05f * alpha));
        }
        break;
    default:
        break;
    }
}

// ---- the banner ----

void draw_banner(gfx::DrawList &list, const ui::Fonts &fonts, const Rect &r, const GenreSkin &skin,
                 std::string_view title_text, std::string_view tagline, float alpha,
                 const Pulse &pulse)
{
    if (alpha <= 0.001f)
        return;
    const float t = pulse.reduced_motion ? 0.0f : pulse.time;
    const float beat = pulse.reduced_motion ? pulse.beat * 0.3f : pulse.beat;
    const float bass = pulse.bass;
    const Color accent = rgb(skin.accent, alpha);
    const Color accent2 = rgb(skin.accent2, alpha);
    const Color ink = rgb(0xf6f3ff, alpha);
    const Color dark = rgb(skin.bg0, alpha);
    list.push_opacity(1.0f);

    // ---- art, on the right ----
    const float art_x = r.x + r.w * 0.66f;
    const float art_w = r.w - (art_x - r.x);
    const float cy = r.y + r.h * 0.5f;
    switch (skin.motif)
    {
    case Motif::afterglow:
    {
        const float radius = r.h * 0.62f;
        list.glow({art_x + art_w * 0.5f - radius, cy - radius, radius * 2.0f, radius * 2.0f},
                  radius, radius * 0.7f, accent.with_alpha(0.18f * alpha * (1.0f + bass)));
        sliced_sun(list, art_x + art_w * 0.5f, cy + r.h * 0.1f, radius, accent2, accent, 0.45f,
                   t * 0.3f);
        break;
    }
    case Motif::warehouse:
    {
        // A ticker tape across the top, the brand repeated, scrolling.
        const Rect tape{r.x, r.y - 6.0f, r.w, 30.0f};
        list.rounded_rect(tape, 0.0f, accent);
        list.push_clip(tape);
        const ui::FontRef &mono = fonts.mono;
        const std::string unit = upper(skin.title) + "  ///  AFTERGLOW  ///  ";
        const float unit_w = mono.measure(unit, 17.0f, 3.0f);
        float x = tape.x - fract(t * 0.08f) * unit_w;
        while (x < tape.x + tape.w)
        {
            ui::text(list, mono, unit, x, tape.y + 21.0f, 17.0f, dark, gfx::Align::left, 3.0f);
            x += unit_w;
        }
        list.pop_clip();
        corner_brackets(list, {art_x + 20.0f, r.y + 38.0f, art_w - 20.0f, r.h - 46.0f}, 26.0f, 3.0f,
                        accent2.with_alpha(0.75f * alpha));
        // A level meter of hard blocks inside the brackets.
        const float inner_x = art_x + 54.0f;
        const float inner_w = art_w - 88.0f;
        for (int i = 0; i < 16; ++i)
        {
            const float level = band(pulse, i);
            const int blocks = 1 + static_cast<int>(level * 8.0f);
            const float bw = inner_w / 16.0f;
            for (int b = 0; b < blocks; ++b)
                list.rounded_rect({inner_x + static_cast<float>(i) * bw,
                                   r.y + r.h - 22.0f - static_cast<float>(b) * 13.0f, bw - 5.0f,
                                   9.0f},
                                  0.0f, (b >= 6 ? accent : accent2.with_alpha(0.8f * alpha)));
        }
        ui::text(list, mono, "[ LIVE ]", art_x + art_w - 18.0f, r.y + 68.0f, 16.0f, accent,
                 gfx::Align::right, 3.0f);
        break;
    }
    case Motif::vinyl:
    {
        const float radius = r.h * 0.78f;
        draw_disc(list, art_x + art_w * 0.55f, cy + r.h * 0.06f, radius, skin, t * 1.4f, false,
                  alpha);
        break;
    }
    case Motif::halo:
    {
        const float hx = art_x + art_w * 0.55f;
        for (int i = 0; i < 9; ++i)
        {
            const float a = (static_cast<float>(i) - 4.0f) * 0.16f + std::sin(t * 0.4f) * 0.12f;
            beam(list, hx, cy + r.h * 0.9f, a, r.h * 2.0f, 2.0f,
                 (i % 2 == 0 ? accent2 : accent).with_alpha(0.22f * alpha));
        }
        for (int i = 0; i < 6; ++i)
        {
            const float phase = fract(t * 0.22f + static_cast<float>(i) / 6.0f);
            const float radius = 18.0f + phase * r.h * 1.2f;
            list.ring(hx, cy, radius, 2.0f + bass * 4.0f,
                      gfx::mix(accent2, accent, phase)
                          .with_alpha((1.0f - phase) * (1.0f - phase) * 0.6f * alpha));
        }
        list.glow({hx - 30.0f, cy - 30.0f, 60.0f, 60.0f}, 30.0f, 60.0f + bass * 40.0f,
                  accent2.with_alpha(0.35f * alpha));
        list.circle(hx, cy, 10.0f + beat * 6.0f, rgb(0xffffff, 0.9f * alpha));
        break;
    }
    case Motif::hazard:
    {
        ui::text(list, skin_face(fonts, Face::bebas), "174", art_x + art_w, r.y + r.h * 0.98f,
                 r.h * 1.25f, accent.with_alpha(0.10f * alpha), gfx::Align::right);
        stripes(list, {art_x, r.y + r.h - 34.0f, art_w, 26.0f}, accent2, dark, t * 0.8f);
        // A rolling waveform over the tape.
        for (int i = 0; i < 40; ++i)
        {
            const float level = band(pulse, i * 16 / 40);
            const float h =
                6.0f + 70.0f * level * (0.6f + 0.4f * std::sin(t * 9.0f + static_cast<float>(i)));
            list.rounded_rect({art_x + static_cast<float>(i) * (art_w / 40.0f),
                               r.y + r.h - 46.0f - h, art_w / 40.0f - 4.0f, h},
                              2.0f, accent.with_alpha(0.85f * alpha));
        }
        break;
    }
    case Motif::wobble:
    {
        for (int i = 0; i < 3; ++i)
        {
            const float fi = static_cast<float>(i);
            sine_ribbon(list, art_x - 40.0f, r.x + r.w, cy + (fi - 1.0f) * 18.0f,
                        r.h * (0.18f + 0.22f * bass) * (1.0f - fi * 0.25f), 9.0f + fi * 3.0f,
                        t * (3.0f + fi) + fi, 7.0f - fi * 2.0f,
                        (i == 1 ? accent2 : accent).with_alpha((0.95f - fi * 0.2f) * alpha));
        }
        break;
    }
    case Motif::strobe:
    {
        list.rounded_rect(r, 0.0f, accent2.with_alpha(beat * beat * 0.10f * alpha));
        for (int i = 0; i < 9; ++i)
        {
            const float x = art_x + fract(t * 0.6f + static_cast<float>(i) / 9.0f) * art_w;
            const float k = (x - art_x) / art_w;
            chevron(list, x, cy, r.h * 0.7f, 10.0f,
                    (i % 2 == 0 ? accent : accent2).with_alpha(std::sin(k * kPi) * 0.9f * alpha));
        }
        break;
    }
    case Motif::laser:
    {
        const float ox = art_x + art_w * 0.5f;
        const float oy = r.y + r.h * 1.02f;
        for (int i = 0; i < 13; ++i)
        {
            const float a = (static_cast<float>(i) - 6.0f) * 0.13f +
                            std::sin(t * 0.9f + static_cast<float>(i) * 0.2f) * 0.35f;
            beam(list, ox, oy, a, r.h * 2.4f, 2.0f + beat * 1.5f,
                 (i % 2 == 0 ? accent : accent2).with_alpha(0.75f * alpha));
        }
        list.glow({ox - 20.0f, oy - 20.0f, 40.0f, 40.0f}, 20.0f, 50.0f,
                  accent.with_alpha(0.5f * alpha));
        list.circle(ox, oy, 9.0f, rgb(0xffffff, alpha));
        break;
    }
    case Motif::sunset:
    {
        const float radius = r.h * 0.66f;
        const float sx = art_x + art_w * 0.5f;
        const float horizon = r.y + r.h - 10.0f;
        list.glow({sx - radius, horizon - radius * 1.6f, radius * 2.0f, radius * 1.6f}, radius,
                  radius * 0.6f, accent.with_alpha(0.22f * alpha * (1.0f + bass)));
        list.push_clip({r.x, r.y - 200.0f, r.w, horizon - r.y + 200.0f});
        sliced_sun(list, sx, horizon - radius * 0.35f, radius, accent2, accent, 0.40f, t * 0.4f);
        list.pop_clip();
        list.rounded_rect({art_x - 40.0f, horizon, art_w + 40.0f, 2.0f}, 0.0f,
                          accent.with_alpha(0.9f * alpha));
        list.glow({art_x - 40.0f, horizon, art_w + 40.0f, 2.0f}, 1.0f, 18.0f,
                  accent.with_alpha(0.5f * alpha));
        break;
    }
    case Motif::frost:
        bokeh(list, {art_x, r.y, art_w, r.h}, 12, accent.with_alpha(0.16f * alpha),
              accent2.with_alpha(0.16f * alpha), t * 2.0f, 34.0f, 5u);
        break;
    case Motif::gloss:
    {
        for (int i = 0; i < 10; ++i)
        {
            const auto k = static_cast<std::uint32_t>(i) * 131u + 7u;
            const float x = art_x + art_w * hash01(k);
            const float y =
                r.y + r.h * fract(hash01(k + 1u) - t * (0.05f + 0.05f * hash01(k + 2u)));
            const float radius = 12.0f + 34.0f * hash01(k + 3u) * (1.0f + bass * 0.3f);
            list.circle(x, y, radius, (i % 2 == 0 ? accent : accent2).with_alpha(0.14f * alpha));
            list.ring(x, y, radius, 2.0f, rgb(0xffffff, 0.45f * alpha));
            list.circle(x - radius * 0.35f, y - radius * 0.35f, radius * 0.18f,
                        rgb(0xffffff, 0.7f * alpha));
        }
        for (int i = 0; i < 6; ++i)
        {
            const auto k = static_cast<std::uint32_t>(i) * 977u + 3u;
            const float twinkle = std::max(0.0f, std::sin(t * 2.0f + hash01(k) * 20.0f));
            list.star(art_x + art_w * hash01(k + 1u), r.y + r.h * hash01(k + 2u),
                      6.0f + 10.0f * twinkle, rgb(skin.accent2, twinkle * alpha));
        }
        break;
    }
    case Motif::sticker:
    {
        halftone(list, {art_x, r.y, art_w, r.h}, accent.with_alpha(0.35f * alpha), 16.0f, t);
        list.rotated_rect({art_x + art_w * 0.18f, cy - 26.0f, art_w * 0.7f, 52.0f}, 2.0f, -0.10f,
                          accent.with_alpha(0.92f * alpha));
        ui::text(list, skin_face(fonts, Face::marker), "PARENTAL ADVISORY: LOUD",
                 art_x + art_w * 0.53f, cy + 12.0f, 26.0f, dark, gfx::Align::center);
        break;
    }
    case Motif::marigold:
    {
        // Two garlands swing across the top; the mandala turns with the beat.
        garland(list, art_x - 60.0f, r.x + r.w, r.y - 4.0f, r.h * 0.22f, 9.0f,
                accent.with_alpha(0.95f * alpha), rgb(0xffd23f, alpha), t * 0.9f);
        garland(list, art_x + art_w * 0.25f, r.x + r.w + 40.0f, r.y + 8.0f, r.h * 0.30f, 7.0f,
                accent2.with_alpha(0.9f * alpha), accent.with_alpha(alpha), t * 0.7f + 1.0f);
        const float mx = art_x + art_w * 0.58f;
        list.glow({mx - r.h * 0.5f, cy - r.h * 0.5f, r.h, r.h}, r.h * 0.5f, r.h * 0.4f,
                  accent.with_alpha(0.16f * alpha * (1.0f + bass)));
        mandala(list, mx, cy + r.h * 0.06f, r.h * 0.62f * (1.0f + 0.05f * beat),
                accent.with_alpha(0.9f * alpha), accent2.with_alpha(0.85f * alpha), t * 0.25f,
                pulse);
        break;
    }
    case Motif::papel:
    {
        // Two strings of flags across the top, a flower opening on the beat.
        const Color flags[4] = {accent.with_alpha(0.92f * alpha), accent2.with_alpha(0.9f * alpha),
                                rgb(0xffd23f, 0.92f * alpha), rgb(0xb06bff, 0.9f * alpha)};
        papel_picado(list, art_x - 80.0f, r.x + r.w + 30.0f, r.y - 8.0f, r.h * 0.16f, 34.0f, flags,
                     4, rgb(skin.bg1, alpha), t * 0.9f);
        const float fx = art_x + art_w * 0.6f;
        list.glow({fx - r.h * 0.4f, cy - r.h * 0.2f, r.h * 0.8f, r.h * 0.8f}, r.h * 0.4f,
                  r.h * 0.3f, accent.with_alpha(0.16f * alpha * (1.0f + bass)));
        bloom(list, fx, cy + r.h * 0.22f, r.h * 0.3f * (1.0f + 0.06f * beat),
              accent.with_alpha(0.95f * alpha), rgb(0xffd23f, alpha), t * 0.2f, beat);
        bloom(list, fx + r.h * 0.48f, cy + r.h * 0.34f, r.h * 0.17f,
              accent2.with_alpha(0.9f * alpha), rgb(0xffd23f, alpha), -t * 0.3f, bass);
        break;
    }
    case Motif::lightstick:
    {
        // Rows of lightsticks waving together, a wave of light rolling along.
        for (int row = 0; row < 3; ++row)
        {
            const float fr = static_cast<float>(row);
            const int count = 9 + row * 2;
            for (int i = 0; i < count; ++i)
            {
                const float k = (static_cast<float>(i) + 0.5f) / static_cast<float>(count);
                const float x = art_x - 40.0f + (art_w + 60.0f) * k;
                const float y = r.y + r.h * (1.05f - fr * 0.16f);
                const float lean = std::sin(t * 2.2f + fr * 0.6f) * 0.35f;
                const float wave = 0.45f + 0.55f * std::max(0.0f, std::sin(t * 3.0f - k * 6.0f));
                const Color c = (i + row) % 2 == 0 ? accent : accent2;
                lightstick(list, x, y, (13.0f - fr * 2.5f) * (1.0f + 0.1f * beat), lean,
                           c.with_alpha(c.a * (0.4f + 0.6f * wave) * (1.0f - fr * 0.25f)),
                           (i + row) % 3 == 0);
            }
        }
        for (int i = 0; i < 7; ++i)
        {
            const auto k = static_cast<std::uint32_t>(i) * 613u + 5u;
            const float twinkle = std::max(0.0f, std::sin(t * 2.6f + hash01(k) * 20.0f));
            list.star(art_x + art_w * hash01(k + 1u), r.y + r.h * 0.45f * hash01(k + 2u),
                      5.0f + 9.0f * twinkle, rgb(0xffffff, twinkle * alpha));
        }
        break;
    }
    case Motif::kente:
    {
        // A woven band, and a talking drum's rings answering the bass.
        kente(list, {art_x - 30.0f, r.y + r.h * 0.12f, art_w + 60.0f, 44.0f},
              accent.with_alpha(alpha), accent2.with_alpha(alpha), dark, t * 0.08f, alpha);
        kente(list, {art_x - 30.0f, r.y + r.h * 0.76f, art_w + 60.0f, 30.0f},
              accent2.with_alpha(alpha), accent.with_alpha(alpha), dark, -t * 0.06f + 0.5f, alpha);
        const float dx = art_x + art_w * 0.55f;
        const float dy = r.y + r.h * 0.48f;
        for (int i = 0; i < 4; ++i)
        {
            const float k = static_cast<float>(i);
            list.ring(dx, dy, r.h * (0.1f + 0.07f * k) * (1.0f + bass * 0.25f), 3.0f,
                      (i % 2 == 0 ? accent : accent2).with_alpha((0.8f - k * 0.17f) * alpha));
        }
        list.circle(dx, dy, r.h * 0.08f * (1.0f + beat * 0.2f), accent.with_alpha(0.95f * alpha));
        break;
    }
    case Motif::arabesque:
    {
        // A lattice of eight-point stars, a crescent and a swinging lantern.
        list.push_clip({art_x - 40.0f, r.y - 10.0f, art_w + 60.0f, r.h + 20.0f});
        for (int i_y = 0; i_y < 3; ++i_y)
            for (int i_x = 0; i_x < 7; ++i_x)
            {
                const float sx = art_x - 20.0f + static_cast<float>(i_x) * 64.0f +
                                 static_cast<float>(i_y % 2) * 32.0f;
                const float sy = r.y + 20.0f + static_cast<float>(i_y) * 60.0f;
                const float shine = 0.3f + 0.7f * std::max(0.0f, std::sin(t * 0.8f - sx * 0.01f));
                khatam(list, sx, sy, 24.0f, 0.0f, accent2.with_alpha(0.06f * alpha * shine),
                       accent.with_alpha((0.25f + 0.35f * shine) * alpha), 1.5f);
            }
        list.pop_clip();
        crescent(list, art_x + art_w * 0.78f, cy - r.h * 0.12f, r.h * 0.2f, rgb(0xfff1c4, alpha),
                 rgb(skin.bg1, alpha));
        lantern(list, art_x + art_w * 0.4f, r.y - 10.0f, r.h * 0.2f, std::sin(t * 1.1f) * 0.25f,
                accent.with_alpha(alpha), rgb(0xffe2a0, (0.75f + 0.25f * bass) * alpha));
        break;
    }
    case Motif::velvet:
        for (int i = 0; i < 5; ++i)
        {
            const float fi = static_cast<float>(i);
            sine_ribbon(list, art_x - 120.0f, r.x + r.w + 40.0f, cy + (fi - 2.0f) * 14.0f,
                        r.h * 0.28f, 4.0f + fi * 0.5f, t * 0.6f + fi * 0.7f, 22.0f - fi * 3.0f,
                        gfx::mix(accent, accent2, fi / 4.0f).with_alpha(0.22f * alpha));
        }
        break;
    }

    // ---- the title ----
    const ui::FontRef &face = skin_face(fonts, skin.face);
    const std::string title =
        skin_title(skin, title_text.empty() ? std::string_view(skin.title) : title_text);
    float size = r.h * 0.66f * skin.title_scale;
    const float max_w = r.w * 0.64f;
    float tracking = skin.tracking * size / 100.0f;
    const float measured = face.measure(title, size, tracking);
    if (measured > max_w)
    {
        size *= max_w / measured;
        tracking = skin.tracking * size / 100.0f;
    }
    const float base = r.y + r.h * 0.70f;
    float x = r.x;
    float shake = 0.0f;
    if (skin.motif == Motif::strobe && !pulse.reduced_motion)
        shake = std::sin(t * 70.0f) * beat * 7.0f;
    x += shake;

    if (skin.motif == Motif::warehouse)
    {
        // A glitch: colour-split copies on the beat, and now and then a slice
        // of the title slips sideways.
        const float flick = hash01(static_cast<std::uint32_t>(t * 9.0f)) > 0.88f ? 0.7f : 0.0f;
        const float g = pulse.reduced_motion ? 0.0f : std::max(beat > 0.5f ? beat : 0.0f, flick);
        if (g > 0.0f)
        {
            ui::text(list, face, title, x + g * 9.0f, base, size, accent.with_alpha(0.8f * alpha),
                     gfx::Align::left, tracking);
            ui::text(list, face, title, x - g * 7.0f, base, size, rgb(0x2bf5ff, 0.55f * alpha),
                     gfx::Align::left, tracking);
        }
        ui::text(list, face, title, x, base, size, ink, gfx::Align::left, tracking);
        if (g > 0.0f)
        {
            const float band_y =
                base - size * (0.15f + 0.5f * hash01(static_cast<std::uint32_t>(t * 13.0f)));
            list.push_clip({r.x - 40.0f, band_y, max_w + 80.0f, size * 0.10f});
            list.rounded_rect({r.x - 40.0f, band_y, max_w + 80.0f, size * 0.10f}, 0.0f, dark);
            ui::text(list, face, title, x + g * 26.0f, base, size, accent, gfx::Align::left,
                     tracking);
            list.pop_clip();
        }
    }
    else if (skin.motif == Motif::sunset || skin.motif == Motif::laser || skin.motif == Motif::halo)
    {
        // Neon: the title glows in the accent.
        const float w = face.measure(title, size, tracking);
        list.glow({x, base - size * 0.62f, w, size * 0.55f}, size * 0.2f, size * 0.35f,
                  accent.with_alpha((0.20f + 0.25f * bass) * alpha));
        ui::text(list, face, title, x, base, size,
                 skin.motif == Motif::sunset ? gfx::mix(accent2, rgb(0xffffff, alpha), 0.35f) : ink,
                 gfx::Align::left, tracking);
    }
    else if (skin.motif == Motif::marigold)
    {
        // A film-poster title: gold, with a deep shadow and a warm glow.
        const float w = face.measure(title, size, tracking);
        list.glow({x, base - size * 0.62f, w, size * 0.55f}, size * 0.2f, size * 0.3f,
                  accent.with_alpha((0.18f + 0.2f * bass) * alpha));
        ui::text(list, face, title, x + 4.0f, base + 4.0f, size, accent2.with_alpha(0.75f * alpha),
                 gfx::Align::left, tracking);
        ui::text(list, face, title, x, base, size, rgb(0xffd23f, alpha), gfx::Align::left,
                 tracking);
    }
    else if (skin.motif == Motif::sticker)
    {
        // A drop shadow like a sticker, and paint drips under it.
        const float w = face.measure(title, size, tracking);
        ui::text(list, face, title, x + 5.0f, base + 5.0f, size, dark, gfx::Align::left, tracking);
        ui::text(list, face, title, x, base, size, accent, gfx::Align::left, tracking);
        for (int i = 0; i < 7; ++i)
        {
            const auto k = static_cast<std::uint32_t>(i) * 53u + 9u;
            const float dx = x + w * (0.08f + 0.84f * hash01(k));
            const float grow =
                pulse.reduced_motion ? 1.0f : 0.5f + 0.5f * fract(t * 0.05f + hash01(k + 1u));
            const float len = (5.0f + 13.0f * hash01(k + 2u)) * grow;
            list.line(dx, base - 4.0f, dx, base + len, 5.0f, accent);
            list.circle(dx, base + len, 4.5f, accent);
        }
    }
    else
    {
        ui::text(list, face, title, x, base, size, ink, gfx::Align::left, tracking);
    }

    // ---- tagline ----
    const std::string tag = upper(tagline.empty() ? std::string_view(skin.tagline) : tagline);
    list.rounded_rect({r.x, r.y + r.h * 0.84f, 34.0f, 3.0f}, 0.0f, accent);
    // A station's name, style and country can be long: keep it left of the art.
    const TextBlock line = fit_line(fonts.semibold, tag, 18.0f, r.w * 0.66f - 72.0f, 0.8f, 3.0f);
    ui::text(list, fonts.semibold, line.lines.front(), r.x + 48.0f, r.y + r.h * 0.84f + 8.0f,
             line.size, rgb(0xf6f3ff, 0.72f * alpha), gfx::Align::left, 3.0f);
    list.pop_opacity();
}

// ---- covers ----

void draw_disc(gfx::DrawList &list, float cx, float cy, float radius, const GenreSkin &skin,
               float angle, bool cd, float alpha)
{
    const Color accent = rgb(skin.accent, alpha);
    const Color accent2 = rgb(skin.accent2, alpha);
    list.shadow({cx - radius, cy - radius + 8.0f, radius * 2.0f, radius * 2.0f}, radius, 24.0f,
                rgb(0x000000, 0.5f * alpha));
    if (cd)
    {
        list.gradient_rect({cx - radius, cy - radius, radius * 2.0f, radius * 2.0f}, radius,
                           rgb(0xe8ecf4, alpha), rgb(0x9aa3b5, alpha));
        // Rainbow sheen, turning with the disc.
        const Color sheen[3] = {accent.with_alpha(0.45f * alpha), rgb(0x2bf5ff, 0.40f * alpha),
                                accent2.with_alpha(0.45f * alpha)};
        for (int i = 0; i < 3; ++i)
        {
            const float a = angle + static_cast<float>(i) * 0.5f;
            list.arc(cx, cy, radius * 0.98f, radius * 0.6f, a, 0.45f, sheen[i], false);
            list.arc(cx, cy, radius * 0.98f, radius * 0.6f, a + kPi, 0.45f, sheen[i], false);
        }
        list.circle(cx, cy, radius * 0.33f, rgb(0xd0d6e2, alpha));
        list.ring(cx, cy, radius * 0.33f, 2.0f, rgb(0xffffff, 0.6f * alpha));
        list.circle(cx, cy, radius * 0.12f, rgb(skin.bg0, alpha));
        return;
    }
    list.circle(cx, cy, radius, rgb(0x0b0b0d, alpha));
    for (int i = 0; i < 16; ++i)
    {
        const float k = 0.40f + static_cast<float>(i) * 0.036f;
        list.ring(cx, cy, radius * k, 1.0f, rgb(0xffffff, (i % 3 == 0 ? 0.10f : 0.05f) * alpha));
    }
    // Light catching the grooves: two opposite arcs that stay put while the
    // record turns under them (that is what real light does).
    list.arc(cx, cy, radius * 0.97f, radius * 0.55f, -0.9f, 0.55f, rgb(0xffffff, 0.08f * alpha),
             false);
    list.arc(cx, cy, radius * 0.97f, radius * 0.55f, -0.9f + kPi, 0.55f,
             rgb(0xffffff, 0.06f * alpha), false);
    // The label turns.
    const float label = radius * 0.36f;
    list.gradient_rect({cx - label, cy - label, label * 2.0f, label * 2.0f}, label, accent,
                       gfx::mix(accent, rgb(skin.bg0, alpha), 0.35f));
    list.arc(cx, cy, label * 0.86f, label * 0.10f, angle, 1.2f, accent2.with_alpha(0.9f * alpha));
    list.arc(cx, cy, label * 0.62f, label * 0.06f, angle + kPi, 0.8f,
             accent2.with_alpha(0.6f * alpha));
    list.circle(cx + std::sin(angle) * label * 0.45f, cy - std::cos(angle) * label * 0.45f,
                label * 0.07f, rgb(0xffffff, 0.8f * alpha));
    list.circle(cx, cy, radius * 0.025f + 2.0f, rgb(0xd8d8d8, alpha));
}

void draw_generated_cover(gfx::DrawList &list, const ui::Fonts &fonts, const Rect &r,
                          const GenreSkin &skin, std::string_view name, std::uint32_t seed,
                          float alpha, const Pulse &pulse)
{
    const float t = pulse.reduced_motion ? 0.0f : pulse.time;
    const Color accent = rgb(skin.accent, alpha);
    const Color accent2 = rgb(skin.accent2, alpha);
    const Color dark = rgb(skin.bg0, alpha);
    const float radius = std::min(skin.radius, r.w * 0.08f);
    // Two layouts per skin, picked by the station, so neighbours differ.
    const bool flip = (seed & 1u) != 0;
    list.gradient_rect(r, radius, flip ? gfx::mix(accent, dark, 0.35f) : rgb(skin.bg1, alpha),
                       dark);
    list.push_clip(r.inset(1.0f));
    const float cx = r.x + r.w * (flip ? 0.66f : 0.5f);
    const float cy = r.y + r.h * 0.42f;
    const float big = r.w * 0.36f;
    switch (skin.motif)
    {
    case Motif::afterglow:
    case Motif::sunset:
        sliced_sun(list, cx, cy, big, accent2, accent, 0.45f, t * 0.3f);
        if (skin.motif == Motif::sunset)
            perspective_grid(list, {r.x, cy + big * 0.6f, r.w, r.h - (cy + big * 0.6f - r.y)},
                             accent.with_alpha(0.8f * alpha), t * 0.6f);
        break;
    case Motif::vinyl:
        draw_disc(list, cx, cy, big * 1.15f, skin, t * 1.4f, false, alpha);
        break;
    case Motif::halo:
    case Motif::frost:
        for (int i = 0; i < 5; ++i)
            list.ring(cx, cy, big * (0.3f + 0.18f * static_cast<float>(i)),
                      3.0f + pulse.bass * 3.0f,
                      gfx::mix(accent2, accent, static_cast<float>(i) / 4.0f)
                          .with_alpha((1.0f - static_cast<float>(i) * 0.17f) * alpha));
        list.glow({cx - 10.0f, cy - 10.0f, 20.0f, 20.0f}, 10.0f, big * 0.5f,
                  accent2.with_alpha(0.5f * alpha));
        break;
    case Motif::hazard:
    case Motif::strobe:
        for (int i = 0; i < 5; ++i)
            stripes(list, {r.x, r.y + r.h * (0.12f + 0.12f * static_cast<float>(i)), r.w, 18.0f},
                    i % 2 == 0 ? accent : accent2, dark.with_alpha(0.0f),
                    t * 0.5f + static_cast<float>(i) * 0.3f);
        break;
    case Motif::wobble:
    case Motif::velvet:
        for (int i = 0; i < 4; ++i)
            sine_ribbon(list, r.x - 20.0f, r.x + r.w + 20.0f,
                        cy + (static_cast<float>(i) - 1.5f) * 18.0f, big * 0.5f, 7.0f,
                        t * 1.5f + static_cast<float>(i) + static_cast<float>(seed % 7u), 6.0f,
                        (i % 2 == 0 ? accent : accent2).with_alpha(0.9f * alpha));
        break;
    case Motif::laser:
        for (int i = 0; i < 9; ++i)
            beam(list, cx, r.y + r.h * 0.9f, (static_cast<float>(i) - 4.0f) * 0.16f, r.h * 1.1f,
                 2.0f, (i % 2 == 0 ? accent : accent2).with_alpha(0.8f * alpha));
        break;
    case Motif::gloss:
        for (int i = 0; i < 6; ++i)
        {
            const auto k = static_cast<std::uint32_t>(i) * 97u + seed;
            const float x = r.x + r.w * hash01(k);
            const float y = r.y + r.h * 0.7f * hash01(k + 1u);
            const float radius2 = r.w * (0.05f + 0.12f * hash01(k + 2u));
            list.circle(x, y, radius2, (i % 2 == 0 ? accent : accent2).with_alpha(0.5f * alpha));
            list.circle(x - radius2 * 0.35f, y - radius2 * 0.35f, radius2 * 0.2f,
                        rgb(0xffffff, 0.8f * alpha));
        }
        break;
    case Motif::sticker:
        halftone(list, r, accent.with_alpha(0.55f * alpha), r.w / 16.0f, t);
        break;
    case Motif::marigold:
        mandala(list, cx, cy, big * 1.05f, accent, accent2,
                t * 0.2f + static_cast<float>(seed % 9u), pulse);
        garland(list, r.x - 10.0f, r.x + r.w + 10.0f, r.y + r.h * 0.04f, r.h * 0.12f, r.w * 0.028f,
                accent, rgb(0xffd23f, alpha), t * 0.8f);
        break;
    case Motif::papel:
    {
        const Color flags[3] = {accent, accent2, rgb(0xffd23f, alpha)};
        papel_picado(list, r.x - 10.0f, r.x + r.w + 10.0f, r.y + r.h * 0.04f, r.h * 0.1f,
                     r.w * 0.16f, flags, 3, dark, t * 0.8f);
        bloom(list, cx, cy + big * 0.2f, big * 0.8f, accent, rgb(0xffd23f, alpha), t * 0.2f, 0.0f);
        break;
    }
    case Motif::lightstick:
        for (int i = 0; i < 4; ++i)
            list.ring(cx, cy, big * (0.5f + 0.2f * static_cast<float>(i)), 2.0f,
                      (i % 2 == 0 ? accent : accent2)
                          .with_alpha((0.7f - 0.15f * static_cast<float>(i)) * alpha));
        lightstick(list, cx, cy + big * 1.1f, big * 0.55f, 0.15f * std::sin(t * 2.0f), accent,
                   true);
        break;
    case Motif::kente:
        for (int i = 0; i < 4; ++i)
            kente(list,
                  {r.x, r.y + r.h * (0.18f + 0.18f * static_cast<float>(i)), r.w, r.h * 0.15f},
                  i % 2 == 0 ? accent : accent2, i % 2 == 0 ? accent2 : accent, dark,
                  t * 0.05f + static_cast<float>(i) * 0.37f + static_cast<float>(seed % 5u), alpha);
        break;
    case Motif::arabesque:
        khatam(list, cx, cy, big * 1.05f, t * 0.05f, accent2.with_alpha(0.25f * alpha), accent,
               3.0f);
        khatam(list, cx, cy, big * 0.6f, -t * 0.08f + 0.39f, accent.with_alpha(0.35f * alpha),
               accent2, 2.0f);
        crescent(list, cx, cy, big * 0.22f, rgb(0xfff1c4, alpha), accent.with_alpha(0.35f * alpha));
        break;
    case Motif::warehouse:
        for (int i_x = 0; (r.x) + static_cast<float>(i_x) * (r.w / 8.0f) < r.x + r.w; ++i_x)
        {
            const float x = (r.x) + static_cast<float>(i_x) * (r.w / 8.0f);
            list.rounded_rect({x, r.y, 1.0f, r.h}, 0.0f, accent2.with_alpha(0.12f * alpha));
        }
        corner_brackets(list, r.inset(r.w * 0.08f), r.w * 0.1f, 3.0f,
                        accent2.with_alpha(0.8f * alpha));
        list.rounded_rect({r.x + r.w * 0.08f, cy - r.h * 0.05f, r.w * 0.84f, r.h * 0.1f}, 0.0f,
                          accent);
        break;
    }
    // Initials in the skin's face, bottom left; the brand at the top.
    const ui::FontRef &face = skin_face(fonts, skin.face);
    const std::string letters = initials(name);
    float size = r.h * 0.36f * skin.title_scale / 0.8f;
    const float w = face.measure(letters, size);
    if (w > r.w * 0.8f)
        size *= r.w * 0.8f / w;
    ui::text(list, face, letters, r.x + r.w * 0.08f + 3.0f, r.y + r.h * 0.90f + 3.0f, size,
             rgb(0x000000, 0.5f * alpha));
    ui::text(list, face, letters, r.x + r.w * 0.08f, r.y + r.h * 0.90f, size,
             rgb(0xffffff, 0.95f * alpha));
    ui::text(list, fonts.semibold, "AFTERGLOW RADIO", r.x + r.w * 0.08f, r.y + r.h * 0.11f,
             std::max(10.0f, r.w * 0.045f), rgb(0xffffff, 0.75f * alpha), gfx::Align::left,
             r.w * 0.012f);
    list.pop_clip();
    list.bordered_rect(r, radius, rgb(0x000000, 0.0f), 1.5f, rgb(0xffffff, 0.18f * alpha));
}

} // namespace hui::concepts
