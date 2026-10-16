// ps5-homebrew-ui - What a backdrop or post overlay should paint (no GL here).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/draw_list.hpp"

namespace hui::gfx
{

// What the fragment shader paints behind (or over) the draw list. Each mode
// reads the four colours and the four parameters in its own way; see
// docs/BACKDROPS.md for the table.
enum class BackdropMode : int
{
    none = 0,     // nothing is drawn
    gradient = 1, // c0 top, c1 bottom, c2 highlight at (p0, p1) of strength p2
    aurora = 2,   // slow domain-warped colour clouds: c0/c1 base, c2/c3 clouds
    grid = 3,     // synthwave floor and sun: c0 sky top, c1 horizon, c2 lines, c3 sun
    stars = 4,    // star field with a nebula: c0/c1 space, c2/c3 nebula; p0,p1 camera
    waves = 5,    // layered sine ribbons: c0/c1 base, c2/c3 ribbons
    bokeh = 6,    // drifting soft discs: c0/c1 base, c2/c3 discs
    phosphor = 7, // CRT glass: c0 dark, c1 centre glow
    paper = 8,    // warm paper with grain: c0/c1 paper, c2 light from the top-left
    dots = 9,     // dot matrix with a travelling pulse: c0/c1 base, c2 dots
    vista = 10,   // parallax ridges under a sky, a stand-in game world: c0 sky,
                  // c1 horizon haze, c2 far ridge, c3 near ridge; p0 scroll speed
    // Audio-reactive scenes (Afterglow). They read BackdropSpec::audio,
    // ::aux and ::spectrum besides the palette.
    tunnel = 11,  // rings rushing at the camera: c0/c1 walls, c2/c3 rings
    skyline = 12, // mirrored spectrum bars on a reflective floor under a sun
    nebula = 13,  // domain-warped clouds that breathe with the bass
    // Post overlays, blended over the finished frame:
    scanlines = 20, // CRT lines and vignette: p0 line strength, p1 vignette
    vignette = 21,  // darkened corners: c0 tint, p0 strength
    // More audio-reactive scenes (Afterglow), same inputs as 11 to 13.
    // p0 distance travelled, p1 slow spin, p2 heading (steered).
    rain = 30,    // falling columns of glyphs: c2 trails, c3 heads
    polygon = 31, // a flat-shaded low-poly object over a checker floor (render low)
    mode7 = 32,   // a rotating tiled floor in perspective under a banded sky (render low)
    bars = 33,    // segmented LED spectrum bars with peak caps and a scope line
    candles = 34, // floating candles under a night ceiling: c2/c3 flames
    rays = 35,    // light shafts from above, one per band, with dust
    plasma = 36,  // demo-scene plasma through a four-colour gradient map
    ember = 37,   // a volcanic peak with glowing cracks and rising embers
    // Ported from React Bits backgrounds (gfx/backdrop_ported.hpp).
    swirl = 40, // swirling paint: c2/c3 paint, c0 shadow
    prism = 41, // a glowing glass pyramid; p3 > 0.5 keeps its own rainbow
    orb = 42,   // a living ring of light: c2/c3 ring, c1 inner glow
    // The trip pack (gfx/backdrop_trip.hpp), drawn by a second program.
    // Same inputs as the audio scenes, plus BackdropSpec::wave.
    kaleido = 50, // a turning kaleidoscope of flowing noise and rings
    chrome = 51,  // liquid metal reflecting a banded sky
    fractal = 52, // a breathing Julia set (render at 720 lines)
    lasers = 53,  // sweeping laser beams through haze, strobes on the beat
    rave = 54,    // a twisting checkerboard tunnel in cycling colours
    silk = 55,    // slow flowing ribbons of light
    bubbles = 56, // glossy metaballs that swell with the bass
    scope = 57,   // an oscilloscope of the music, line and ring
    ring = 58,    // a ring of spectrum bars (a seed for feedback)
    sparks = 59,  // sparks bursting from the centre on beats (a seed for feedback)
    smoke = 60,   // glowing wisps of coloured smoke
    spirals = 61, // counter-rotating polygon rings, a moire of colour
    // The calm pack (gfx/backdrop_gallery.hpp), a third program. Same
    // inputs as the audio scenes; aux.w is the beat clock (beats counted).
    tidepool = 70,  // caustic light on a sandy floor, a ripple each bar
    veil = 71,      // aurora curtains over a dark ridge
    ink = 72,       // drops of colour blooming in still water
    lanterns = 73,  // paper lanterns rising over a lake
    rainglass = 74, // drops sliding down a window over city lights
    moontide = 75,  // a full moon and its path on the sea
    fireflies = 76, // a meadow at dusk, flies blinking on the beat
    deepsea = 77,   // jellyfish pulsing in the dark
    dunes = 78,     // sand dunes and their ripples under a low sun
    cloudsea = 79,  // above the clouds at sunrise
    // The gallery pack, a fourth program: scenes like art objects.
    kintsugi = 80,  // glazed pottery mended with gold
    ridges = 81,    // rows of mountain lines drawn by the spectrum
    origami = 82,   // a folded paper sheet turning its facets
    halftone = 83,  // three screens of printed dots
    labyrinth = 84, // a slowly rearranging Turing pattern
    glass = 85,     // a stained-glass window lit from behind
    loom = 86,      // woven Lissajous threads
    crystal = 87,   // a snowflake breathing with the bars
    ferro = 88,     // ferrofluid spikes rising on the bass
    papercut = 89,  // layered paper hills
};

constexpr bool is_trip_scene(BackdropMode mode)
{
    const int m = static_cast<int>(mode);
    return m >= 50 && m <= 69;
}

constexpr bool is_calm_scene(BackdropMode mode)
{
    const int m = static_cast<int>(mode);
    return m >= 70 && m <= 79;
}

constexpr bool is_gallery_scene(BackdropMode mode)
{
    const int m = static_cast<int>(mode);
    return m >= 80 && m <= 89;
}

constexpr bool is_audio_scene(BackdropMode mode)
{
    const int m = static_cast<int>(mode);
    return (m >= 11 && m <= 13) || (m >= 30 && m <= 89);
}

constexpr bool is_post_overlay(BackdropMode mode)
{
    const int m = static_cast<int>(mode);
    return m >= 20 && m <= 29;
}

// Finishing for a backdrop drawn through the renderer's off-screen path: the
// scene is drawn into its own target (optionally at a low "pixel" height,
// upscaled without filtering), then composited with these effects. All 0..1.
// Milkdrop-style feedback: each frame the previous finished picture is
// zoomed, turned, warped, folded and faded, and the fresh scene is laid over
// it, so everything leaves trails that melt and spiral. Off when !enabled.
struct FeedbackFx
{
    bool enabled = false;
    float zoom = 1.0f;   // > 1 trails rush outwards, < 1 they fall inwards
    float rotate = 0.0f; // radians per frame
    float warp = 0.0f;   // wobble of the trail, 0..2
    float decay = 0.9f;  // how much of the trail survives a frame
    float hue = 0.0f;    // colour turn per frame (radians)
    int mirror = 0;      // kaleidoscope segments folded into the trail (0: none)
    float shift_x = 0.0f;
    float shift_y = 0.0f; // drift per frame, in screen fractions
    int blend = 0;        // 0: lighten, 1: add, 2: fresh over trail by brightness
    float gain = 1.0f;    // how bright the fresh scene goes in
};

struct PostFx
{
    bool enabled = false;
    float bloom = 0.0f;     // soft glow around bright parts
    float fringe = 0.0f;    // colour fringing towards the edges
    float grain = 0.0f;     // film grain
    float scanlines = 0.0f; // CRT lines
    float vignette = 0.0f;  // darker corners
    float curve = 0.0f;     // CRT glass bulge
    int pixel_height = 0;   // 0: full resolution (at most 1080 lines)
    bool smooth = false;    // upscale a reduced height with filtering (a cost saver, not a look)
    FeedbackFx feedback;
};

struct BackdropSpec
{
    BackdropMode mode = BackdropMode::none;
    Color colors[4] = {};
    float params[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float time = 0.0f; // seconds; freeze it to stop the motion
    // Audio-reactive modes only. audio: bass, mid, treble, beat pulse (0..1).
    // aux: steer x, steer y (-1..1), drive (0..1), the beat clock (beats
    // counted, wrapping at 64; its fraction is the place in the beat).
    float audio[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float aux[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    static constexpr int kBands = 16;
    float spectrum[kBands] = {}; // low to high, 0..1
    float peaks[kBands] = {};    // slowly falling maxima of the bands
    static constexpr int kWave = 64;
    float wave[kWave] = {}; // the waveform, -1..1, starting at a rising zero crossing
    // Only with fx.enabled: a live cross-fade from another scene. blend is
    // how much of `mode` shows (1: only it).
    PostFx fx;
    BackdropMode from_mode = BackdropMode::none;
    int from_pixel_height = 0;
    bool from_smooth = false;
    FeedbackFx from_feedback;
    float blend = 1.0f;
};

} // namespace hui::gfx
