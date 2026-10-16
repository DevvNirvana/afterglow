// Afterglow - The visualiser: audio-reactive scenes steered with the DualSense.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Each scene is one full-screen fragment shader (gfx/backdrop.cpp, the React
// Bits ports in gfx/backdrop_ported.hpp, the trip pack in
// gfx/backdrop_trip.hpp, the calm and gallery packs in
// gfx/backdrop_gallery.hpp) fed 16 spectrum bands, a 64-point waveform, four
// levels (bass, mid, treble and a beat pulse) and a beat clock: a tracker
// (audio/beat_tracker.hpp) measures the tempo and where the beats fall, so
// motion, pulses, loops and Auto-VJ's cuts can land on the music. Scenes are drawn off-screen and
// finished with their own look (bloom, fringing, grain, scanlines, a CRT
// bulge, chunky low-resolution pixels) and, for the trip presets, a
// Milkdrop-style feedback trail; a scene change cross-fades two live scenes.
// On the console the levels come from the music the mixer is playing
// (audio/monitor.hpp); where no audio is being rendered (PC snapshots, unit
// tests) a synthetic track stands in, and the screen says so.
//
// The screen also runs the show: Auto-VJ picks scenes that suit the genre
// of the station playing and cuts on drops; a station change shows an ident
// in the station's skin (concepts/genre_skin.hpp); the palette can follow
// the album art. Options opens the settings sheet; its choices are kept in
// the radio's store file.
//
// A new song is announced with a card (its cover and title in the skin);
// Square likes it, and the radio keeps the liked songs and the history.
// Loudness and EQ in the settings shape the music in the mixer, and a meter
// under the settings shows the level that leaves the console.
//
// Controls: Cross opens the scene list (the scene under the focus plays
// behind it; Square stars it for Auto-VJ), D-pad left/right change the
// scene, Square likes the song, L2 the palette, D-pad up/down the trip
// level, R3 Auto-VJ, Triangle hides the overlay, Options the settings,
// Circle the radio, L1/R1 the station, L3 pause. The look is in the
// settings. The touchpad belongs to the shell.

#include "concepts/concepts.hpp"

#include "audio/analysis.hpp"
#include "audio/beat_tracker.hpp"
#include "audio/demo_signal.hpp"
#include "audio/mixer.hpp"
#include "audio/monitor.hpp"
#include "audio/radio.hpp"
#include "audio/spectrum.hpp"
#include "concepts/commands.hpp"
#include "concepts/genre_skin.hpp"
#include "concepts/radio_screen.hpp"
#include "concepts/text_fit.hpp"
#include "gfx/font.hpp"
#include "net/cover_art.hpp"
#include "ui/components/form.hpp"
#include "ui/glyphs.hpp"
#include "ui/theme.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

namespace hui::concepts
{

namespace
{

using gfx::Color;
using gfx::Rect;

// What a scene looks like after it is drawn: see gfx::PostFx.
struct Look
{
    float bloom;
    float fringe;
    float grain;
    float scanlines;
    float vignette;
    float curve;
};

struct Scene
{
    gfx::BackdropMode mode;
    const char *name;
    const char *pack;           // "AFTERGLOW", "CLUB", "TRIP", "RETRO", "CALM", "GALLERY"
    float speed;                // how fast it travels at rest; the bass adds to it
    std::uint32_t signature[4]; // its own colours: two dark, two bright
    Look look;                  // its own finish
    int pixel_height = 0;       // drawn this many lines tall (0: full)
    bool smooth = false;        // ...and upscaled smoothly (a cost saver)
    bool rainbow = false;       // its signature look keeps the shader's own colours
    gfx::FeedbackFx feedback{}; // trails (the trip presets)
};

constexpr gfx::FeedbackFx trail(float zoom, float rotate, float warp, float decay, float hue,
                                int mirror, int blend, float shift_y = 0.0f)
{
    gfx::FeedbackFx fx;
    fx.enabled = true;
    fx.zoom = zoom;
    fx.rotate = rotate;
    fx.warp = warp;
    fx.decay = decay;
    fx.hue = hue;
    fx.mirror = mirror;
    fx.blend = blend;
    fx.shift_y = shift_y;
    return fx;
}

using M = gfx::BackdropMode;
constexpr Look kSoft = {0.55f, 0.3f, 0.2f, 0.0f, 0.65f, 0.0f};
constexpr Look kGlow = {0.8f, 0.3f, 0.2f, 0.0f, 0.6f, 0.0f};
constexpr Look kCalm = {0.45f, 0.1f, 0.15f, 0.0f, 0.55f, 0.0f};   // gentle: little fringe
constexpr Look kGallery = {0.35f, 0.1f, 0.18f, 0.0f, 0.5f, 0.0f}; // a print, not a light show
constexpr Look kPrint = {0.1f, 0.0f, 0.12f, 0.0f, 0.3f, 0.0f};    // paper and ink

// clang-format off
constexpr Scene kScenes[] = {
    // ---- AFTERGLOW: the originals; Paint Swirl, the first, is the default ----
    {M::swirl, "Paint Swirl", "AFTERGLOW", 1.0f, {0x162325, 0x0b1416, 0xde443b, 0x006bb4}, {0.25f, 0.3f, 0.2f, 0.0f, 0.5f, 0.0f}},
    {M::orb, "Orb", "AFTERGLOW", 1.0f, {0x000000, 0x101499, 0x9c43fe, 0x4cc2e9}, {0.7f, 0.25f, 0.25f, 0.0f, 0.6f, 0.0f}},
    {M::nebula, "Nebula", "AFTERGLOW", 1.0f, {0x02060f, 0x061a33, 0x18c8ff, 0x9b5cff}, {0.5f, 0.2f, 0.25f, 0.0f, 0.7f, 0.0f}},
    {M::ember, "Ember Peak", "AFTERGLOW", 1.0f, {0x060202, 0x2a0805, 0xff4a1a, 0xffc04a}, {0.8f, 0.3f, 0.35f, 0.0f, 0.8f, 0.0f}},
    {M::tunnel, "Pulse Tunnel", "AFTERGLOW", 0.9f, {0x0b0612, 0x2a0a1e, 0xff5a36, 0xffc857}, {0.55f, 0.4f, 0.25f, 0.0f, 0.7f, 0.0f}},
    {M::rays, "Cathedral Rays", "AFTERGLOW", 1.0f, {0x060a14, 0x1a2440, 0x8fd0ff, 0xfff4d6}, {0.7f, 0.2f, 0.3f, 0.0f, 0.7f, 0.0f}},
    {M::candles, "Hall of Candles", "AFTERGLOW", 1.0f, {0x05040c, 0x1a1530, 0xffb347, 0xfff2c0}, {0.8f, 0.15f, 0.35f, 0.0f, 0.9f, 0.0f}},
    {M::prism, "Prism", "AFTERGLOW", 1.0f, {0x020205, 0x0a0a14, 0x9b5cff, 0x2bf5ff}, {0.35f, 0.35f, 0.25f, 0.0f, 0.6f, 0.0f}, 540, true, true},
    // ---- TRIP: psychedelic, most with feedback trails ----
    {M::scope, "Wormhole", "TRIP", 1.0f, {0x000000, 0x080018, 0x2bf5ff, 0xff2bd6}, kGlow, 0, false, false, trail(1.035f, 0.012f, 0.3f, 0.95f, 0.012f, 0, 0)},
    {M::spirals, "Mirror Maze", "TRIP", 1.0f, {0x000000, 0x0a0018, 0xff2bd6, 0x40e0ff}, kGlow, 0, false, false, trail(1.02f, 0.0f, 0.2f, 0.92f, 0.008f, 4, 0)},
    {M::kaleido, "Kaleidoscope", "TRIP", 1.0f, {0x05010c, 0x2a0a4a, 0xff3cac, 0x2bf5ff}, {0.5f, 0.35f, 0.2f, 0.0f, 0.6f, 0.0f}},
    {M::fractal, "Fractal Bloom", "TRIP", 1.0f, {0x020108, 0x1a0a3a, 0xff7a18, 0x7af0ff}, {0.6f, 0.25f, 0.2f, 0.0f, 0.6f, 0.0f}, 720, true},
    {M::scope, "Vortex", "TRIP", 1.0f, {0x000000, 0x001018, 0x40ffb0, 0xffe060}, kGlow, 0, false, false, trail(1.015f, 0.05f, 0.2f, 0.94f, 0.01f, 0, 0)},
    {M::spirals, "Hypnotic", "TRIP", 1.0f, {0x000000, 0x101020, 0xffd040, 0x40e0ff}, kGlow, 0, false, true, trail(1.01f, -0.03f, 0.3f, 0.90f, 0.0f, 0, 2)},
    {M::ring, "Dimension", "TRIP", 1.0f, {0x000000, 0x100020, 0x9b5cff, 0x2bf5ff}, kGlow, 0, false, true, trail(0.98f, 0.01f, 0.4f, 0.94f, 0.015f, 8, 0)},
    {M::ring, "Kaleido Melt", "TRIP", 1.0f, {0x000000, 0x200030, 0xff3cac, 0x2bf5ff}, kGlow, 0, false, true, trail(1.02f, 0.0f, 0.8f, 0.95f, 0.02f, 6, 0)},
    {M::sparks, "Acid Bloom", "TRIP", 1.0f, {0x000000, 0x003010, 0xb6ff2b, 0xff2bd6}, kGlow, 0, false, true, trail(1.03f, -0.02f, 1.2f, 0.94f, 0.03f, 0, 0)},
    {M::orb, "Echo Orb", "TRIP", 1.0f, {0x000000, 0x101499, 0x9c43fe, 0x4cc2e9}, kGlow, 0, false, false, trail(0.96f, 0.0f, 0.1f, 0.90f, 0.0f, 0, 0)},
    {M::smoke, "Smoke Signals", "TRIP", 1.0f, {0x030208, 0x1a0a2a, 0x7a5cff, 0xff9ad5}, {0.6f, 0.2f, 0.25f, 0.0f, 0.7f, 0.0f}, 720, true},
    {M::silk, "Silk", "TRIP", 1.0f, {0x07040f, 0x22103a, 0xc38bff, 0xffc6a8}, kSoft},
    {M::silk, "Liquid Dream", "TRIP", 1.0f, {0x07040f, 0x22103a, 0xc38bff, 0xffc6a8}, kSoft, 0, false, false, trail(1.005f, 0.0f, 1.5f, 0.85f, 0.0f, 0, 2)},
    {M::sparks, "Starfall", "TRIP", 1.0f, {0x000000, 0x0a0a20, 0xfff0c0, 0x80c0ff}, kGlow, 0, false, false, trail(0.97f, 0.01f, 0.2f, 0.93f, 0.005f, 0, 0)},
    {M::lasers, "Neon Smear", "TRIP", 1.0f, {0x020208, 0x0a0a1e, 0x18f0ff, 0xff2bd6}, kGlow, 0, false, false, trail(1.012f, 0.0f, 0.4f, 0.88f, 0.0f, 0, 0, -0.002f)},
    {M::spirals, "Spiral Rings", "TRIP", 1.0f, {0x000000, 0x101020, 0xffd040, 0x40e0ff}, kGlow, 0, false, true},
    {M::bars, "Melting Bars", "TRIP", 1.0f, {0x000000, 0x0a1a30, 0x22e040, 0xffe040}, kSoft, 0, false, false, trail(1.01f, 0.0f, 1.0f, 0.90f, 0.0f, 0, 2, -0.003f)},
    // ---- CLUB: for the dance floor ----
    {M::chrome, "Liquid Chrome", "CLUB", 1.0f, {0x04060c, 0x1a2a4a, 0xff8a3d, 0xc8e6ff}, {0.45f, 0.2f, 0.2f, 0.0f, 0.6f, 0.0f}},
    {M::rave, "Rave Tunnel", "CLUB", 1.0f, {0x000000, 0x1a0040, 0xff2bd6, 0x2bf5ff}, {0.5f, 0.5f, 0.2f, 0.0f, 0.6f, 0.0f}, 0, false, true},
    {M::bubbles, "Bubble Pop", "CLUB", 1.0f, {0x12041e, 0x2a0a3c, 0xff6ec7, 0x6ee7ff}, {0.5f, 0.2f, 0.2f, 0.0f, 0.6f, 0.0f}},
    {M::lasers, "Strobe Lasers", "CLUB", 1.0f, {0x020208, 0x0a0a1e, 0x18f0ff, 0xff2bd6}, {0.85f, 0.3f, 0.25f, 0.0f, 0.6f, 0.0f}},
    {M::scope, "Scope", "CLUB", 1.0f, {0x010806, 0x022014, 0x30ff9c, 0x9cfff0}, {0.7f, 0.3f, 0.25f, 0.35f, 0.8f, 0.06f}, 0, false, false, trail(1.0f, 0.0f, 0.0f, 0.80f, 0.0f, 0, 0)},
    {M::bars, "Classic Bars", "CLUB", 1.0f, {0x000000, 0x0a1a30, 0x22e040, 0xffe040}, {0.5f, 0.4f, 0.2f, 0.35f, 0.6f, 0.0f}},
    // ---- GALLERY: pieces that look like art objects ----
    {M::glass, "Stained Glass", "GALLERY", 1.0f, {0x050308, 0x2a0a40, 0x1fa0ff, 0xffb020}, kGallery},
    {M::kintsugi, "Kintsugi", "GALLERY", 1.0f, {0x07090c, 0x1e2a34, 0x4a6a80, 0xffd27a}, kGallery},
    {M::papercut, "Paper Cut", "GALLERY", 0.8f, {0x1c1a2e, 0x5a4a7a, 0xff8f70, 0xffe2b0}, kPrint},
    {M::crystal, "Crystal Growth", "GALLERY", 1.0f, {0x02060e, 0x0c2a48, 0x9ad8ff, 0xffffff}, kGallery},
    {M::labyrinth, "Coral Labyrinth", "GALLERY", 1.0f, {0x08030a, 0x2a0e2a, 0xff6a5c, 0xffd0a0}, kGallery},
    {M::halftone, "Halftone Pop", "GALLERY", 1.0f, {0x101010, 0x2a2a2a, 0x00b4e0, 0xff2a7a}, kPrint},
    {M::loom, "Lissajous Loom", "GALLERY", 1.0f, {0x02030a, 0x0a1430, 0x5cf0ff, 0xff5cd6}, kGlow, 720, true},
    {M::origami, "Origami", "GALLERY", 1.0f, {0x10141c, 0x3a4458, 0xff8a6a, 0xf3e6c8}, kGallery},
    {M::ferro, "Ferrofluid", "GALLERY", 1.0f, {0x0a0a10, 0x2a2a3a, 0xff3c7a, 0x3cc8ff}, kGallery, 720, true},
    {M::ridges, "Ridgelines", "GALLERY", 1.0f, {0x07060c, 0x2a1838, 0xff7a5c, 0xffd2a8}, kGallery},
    // ---- CALM: slow and soft, for late nights and winding down ----
    {M::tidepool, "Tide Pool", "CALM", 0.6f, {0x02141a, 0x0b4a4f, 0x7ff0e0, 0xfff1c4}, kCalm},
    {M::veil, "Aurora Veil", "CALM", 0.6f, {0x01040c, 0x0b2038, 0x2bffb0, 0xb48cff}, kCalm},
    {M::lanterns, "Lantern Drift", "CALM", 0.6f, {0x04030f, 0x1a1238, 0xff9f43, 0xffd38a}, kCalm},
    {M::moontide, "Moon Tide", "CALM", 0.6f, {0x01030a, 0x0c1e3c, 0x6fa8ff, 0xe8f0ff}, kCalm},
    {M::cloudsea, "Cloud Sea", "CALM", 0.6f, {0x1a1030, 0x5a4a8a, 0xffb38a, 0xffd6a0}, kCalm},
    {M::deepsea, "Deep Sea Glow", "CALM", 0.6f, {0x000308, 0x02223a, 0x3cf2ff, 0xff7ae0}, kCalm},
    {M::ink, "Slow Ink", "CALM", 0.6f, {0x05060c, 0x1c2433, 0xff6f91, 0x5ec8ff}, kCalm},
    {M::rainglass, "Rain on Glass", "CALM", 0.6f, {0x04060c, 0x141c30, 0xff5a8a, 0x4fd3ff}, kCalm},
    {M::dunes, "Sand Ripples", "CALM", 0.6f, {0x2a120a, 0x7a3a20, 0xf2a65e, 0xffd9a0}, kCalm},
    {M::fireflies, "Fireflies", "CALM", 0.6f, {0x020608, 0x10283a, 0xd8ff6a, 0xffe08a}, kCalm},
    // ---- RETRO: other eras ----
    {M::skyline, "Skyline", "RETRO", 1.2f, {0x07010f, 0x1d0638, 0xff2bd6, 0x2bf5ff}, {0.6f, 0.5f, 0.3f, 0.25f, 0.6f, 0.0f}},
    {M::rain, "Green Rain", "RETRO", 1.0f, {0x010804, 0x03140a, 0x22ff66, 0xd8ffe4}, {0.7f, 0.6f, 0.35f, 0.5f, 0.8f, 0.08f}},
    {M::mode7, "Mode Seven", "RETRO", 1.0f, {0x2a62ff, 0x2f9e3a, 0xf8d040, 0xffffff}, {0.2f, 0.0f, 0.0f, 0.0f, 0.3f, 0.0f}, 224},
    {M::polygon, "Polygon 95", "RETRO", 1.0f, {0x0a0420, 0x3a1a6e, 0x20c0ff, 0xffd040}, {0.3f, 0.0f, 0.1f, 0.0f, 0.4f, 0.0f}, 240},
    {M::plasma, "Demo Plasma", "RETRO", 1.0f, {0x10002a, 0xff3cac, 0x2b86c5, 0xffe066}, {0.4f, 0.5f, 0.0f, 0.5f, 0.6f, 0.1f}, 270},
};
// clang-format on
constexpr int kSceneCount = static_cast<int>(std::size(kScenes));

// Palettes 0 to 2 follow something (the scene, the cover, the genre); the
// others recolour any scene.
struct Palette
{
    const char *name;
    std::uint32_t colours[4];
};

constexpr Palette kPalettes[] = {
    {"Signature", {0, 0, 0, 0}},
    {"Album art", {0, 0, 0, 0}},
    {"Genre", {0, 0, 0, 0}},
    {"Ember", {0x0b0612, 0x2a0a1e, 0xff5a36, 0xffc857}},
    {"Abyss", {0x02060f, 0x061a33, 0x18c8ff, 0x9b5cff}},
    {"Neon", {0x07010f, 0x1d0638, 0xff2bd6, 0x2bf5ff}},
    {"Aurora", {0x03110f, 0x0a2b2a, 0x2bffb0, 0xb6ff5c}},
    {"Mono", {0x050505, 0x161616, 0xbfbfbf, 0xffffff}},
};
constexpr int kPaletteCount = static_cast<int>(std::size(kPalettes));
constexpr int kSignature = 0;
constexpr int kAlbum = 1;
constexpr int kGenre = 2;

// L2 cycles the finish: the scene's own, a clean picture, or an old CRT.
constexpr const char *kLookNames[] = {"Scene look", "Clean", "CRT"};
constexpr int kLookCount = static_cast<int>(std::size(kLookNames));
constexpr Look kClean = {0.3f, 0.0f, 0.0f, 0.0f, 0.3f, 0.0f};
constexpr Look kCrt = {0.6f, 0.8f, 0.4f, 0.75f, 0.9f, 0.14f};

constexpr const char *kModeNames[] = {"Manual", "Showcase", "Auto-VJ"};
constexpr const char *kTripNames[] = {"Calm", "Normal", "Melt"};
constexpr float kTripLevels[] = {0.55f, 1.0f, 1.6f};
constexpr const char *kDisplayNames[] = {"Normal", "Party", "Ambient"};
constexpr const char *kSleepNames[] = {"Off", "15 min", "30 min", "60 min", "90 min"};
constexpr int kSleepMinutes[] = {0, 15, 30, 60, 90};
constexpr const char *kRevealNames[] = {"Off", "Corner", "Center"};
constexpr const char *kVjNames[] = {"Genre", "Favorites", "Calm"};
constexpr int kVjGenre = 0;
constexpr int kVjFavorites = 1;
constexpr int kVjCalm = 2;
// Loudness: extra gain on the music before the mixer's limiter.
constexpr const char *kLoudNames[] = {"Normal", "+6 dB", "+12 dB"};
constexpr float kLoudGains[] = {1.0f, 2.0f, 4.0f};
constexpr int kEqCount = static_cast<int>(audio::Eq::count);
// Station changes cross-fade for this long (the radio's "xfade" setting).
constexpr const char *kFadeNames[] = {"Off", "2 s", "3 s", "5 s"};
constexpr const char *kFadeValues[] = {"0", "2", "3", "5"};
constexpr int kFadeCount = static_cast<int>(std::size(kFadeNames));

enum Mode
{
    kManual = 0,
    kShowcase = 1,
    kAutoVj = 2,
};
enum DisplayMode
{
    kNormal = 0,
    kParty = 1,
    kAmbient = 2,
};
enum RevealMode
{
    kRevealOff = 0,
    kRevealCorner = 1,
    kRevealCenter = 2,
};

// The settings sheet's rows.
enum Row
{
    kRowMode = 1,
    kRowTrip,
    kRowPalette,
    kRowLook,
    kRowFlashing,
    kRowDisplay,
    kRowReveal,
    kRowVjScenes,
    kRowBeatSync,
    kRowSleep,
    kRowVolume,
    kRowLoudness,
    kRowEq,
    kRowCrossfade,
    kRowSensitivity,
    kRowRumble,
    kRowResume,
};

constexpr float kHudIdleSeconds = 5.0f;
constexpr float kHoldSeconds = 0.6f; // Triangle held this long opens the command box
constexpr float kShowcaseSeconds = 30.0f;
constexpr float kVjMinSeconds = 20.0f; // Auto-VJ holds a scene at least this long
constexpr float kVjMaxSeconds = 48.0f; // ...and moves on after this long
constexpr float kEnterFadeSeconds = 0.45f;
constexpr float kCrossFadeSeconds = 0.9f;
constexpr float kIdentSeconds = 3.6f;
constexpr float kIntroSeconds = 2.9f;
constexpr float kRevealSeconds = 6.5f; // a new song's card, in and out
constexpr float kToastSeconds = 2.6f;
// The music volume Afterglow starts from once: the kit's default (4 of 10,
// about -16 dB on its curve) is far too quiet for the radio.
constexpr int kStartVolume = 8;
// Peak caps fall this fast (levels per second), as on old players.
constexpr float kPeakFall = 0.45f;
// The pad's light bar is a slow device; fifteen updates a second look smooth.
constexpr float kLightBarInterval = 1.0f / 15.0f;
// Audio counts as live while the mixer wrote samples this recently (its
// grains are longer than a frame, so some frames see no new samples).
constexpr float kLiveHoldSeconds = 0.3f;
constexpr float kMinGain = 0.5f;
constexpr float kMaxGain = 2.0f;
constexpr float kMargin = 96.0f;
constexpr float kRight = gfx::kVirtualWidth - kMargin;
constexpr Rect kSheet{1160.0f, 84.0f, 664.0f, 900.0f};
constexpr Rect kPicker{80.0f, 70.0f, 620.0f, 940.0f};

const Color kInk = Color::rgb(0xf6f3ff);
const Color kDim = Color::rgb(0xf6f3ff, 0.64f);
const Color kShade = Color::rgb(0x000000, 0.55f);
const Color kClear = Color::rgb(0x000000, 0.0f);

constexpr const char *kTechniques[] = {
    "One full-screen fragment shader per scene, fed 16 spectrum bands, a waveform and four levels",
    "Separate shader programs for the trip, calm and gallery packs, so heavy loops never slow the "
    "other scenes",
    "A beat clock: the tempo from the onset envelope's autocorrelation, the phase from a comb, "
    "eased in",
    "Milkdrop-style feedback: the last frame zoomed, turned, warped, folded and faded each frame",
    "Scenes drawn off-screen, finished with bloom, fringing, grain, scanlines or chunky pixels",
    "Scene changes cross-fade two live scenes; trails carry across the change",
    "Auto-VJ: scenes chosen for the genre, cuts on drops found in the bass envelope",
    "The palette can follow the album art: the cover's common and most striking colours",
    "2048-point FFT grouped into 16 pitch-spaced bands with a tilt and automatic gain",
};

float approach(float current, float target, float rate, float dt)
{
    return current + (target - current) * (1.0f - std::exp(-rate * dt));
}

Color scale(Color c, float k)
{
    return {std::min(c.r * k, 1.0f), std::min(c.g * k, 1.0f), std::min(c.b * k, 1.0f), 1.0f};
}

// A colour pushed towards full saturation and a minimum brightness, so a
// muddy cover still makes a lively palette.
Color vivify(Color c, float saturation, float floor)
{
    const float hi = std::max(c.r, std::max(c.g, c.b));
    if (hi <= 0.001f)
        return {floor, floor, floor, 1.0f};
    const auto push = [&](float v) { return std::max(0.0f, hi - (hi - v) * saturation); };
    const Color out{push(c.r), push(c.g), push(c.b), 1.0f};
    return scale(out, std::max(1.0f, floor / hi));
}

int scene_index(const char *name)
{
    for (int i = 0; i < kSceneCount; ++i)
        if (std::string(kScenes[i].name) == name)
            return i;
    return -1;
}

class Afterglow final : public app::Concept
{
  public:
    explicit Afterglow(app::Context &context)
        : context_(context), samples_(audio::SpectrumAnalyzer::kWindow)
    {
        for (int i = 0; i < 4; ++i)
            colours_[static_cast<std::size_t>(i)] = target_colour(i);
        light_ = colours_[2];
        build_sheet();
        // The radio's search box also takes commands; they are carried out
        // here, in update, where the feedback is.
        radio_.on_command = [this](const std::string &text, bool command_box)
        {
            const audio::RadioControl *radio = audio::radio();
            if (radio == nullptr)
                return false;
            Command command = parse_command(text, radio->stations(), scene_refs());
            // A bare name in the search box is a search; in the command box
            // (Triangle held over the visuals) it plays.
            if (command.kind == CommandKind::none || (!command.asked && !command_box))
                return false;
            pending_command_ = std::move(command);
            has_command_ = true;
            return true;
        };
    }

    // What the command box does with text (afterglow_command, for tests).
    bool type_command(const std::string &text)
    {
        return radio_.on_command && radio_.on_command(text, true);
    }

    const app::ConceptInfo &info() const override
    {
        static const app::ConceptInfo kInfo{
            "afterglow",
            "Afterglow",
            "Music you can see: radio and audio-reactive scenes steered with the controller",
            "src/concepts/afterglow.cpp",
            audio::SoundSet::glass,
            Color::rgb(0xff5a36),
            kTechniques,
        };
        return kInfo;
    }

    void enter() override
    {
        hud_timer_ = kHudIdleSeconds;
        fade_ = 1.0f;
        if (!loaded_)
        {
            loaded_ = true;
            load_preferences();
        }
    }

    gfx::Color live_accent() const override
    {
        return light_;
    }

    void update(const InputFrame &input, float dt, app::Feedback &feedback) override
    {
        clock_ += static_cast<double>(dt);
        const float speed = display_ == kAmbient ? 0.45f : 1.0f;
        // With beat sync the scenes move at the music's pace: faster for
        // drum and bass, slower for downtempo (about 120 is "as designed").
        const float pace = synced() ? std::clamp(beat_.bpm() / 120.0f, 0.7f, 1.45f) : 1.0f;
        tempo_pace_ = approach(tempo_pace_, pace, 0.5f, dt);
        scene_clock_ += static_cast<double>(dt * speed * tempo_pace_);

        if (intro_ < kIntroSeconds)
        {
            intro_ += dt;
            if (input.pressed != 0)
                intro_ = kIntroSeconds; // any button skips it
        }
        else if (notice_)
        {
            if (input.is_pressed(Action::confirm) || input.is_pressed(Action::back))
            {
                notice_ = false;
                set_pref("warned", "yes");
                feedback.play(audio::Cue::select);
            }
        }
        else if (sheet_open_)
        {
            handle_sheet(input, feedback);
        }
        else if (picker_open_)
        {
            handle_picker(input, feedback);
        }
        else if (radio_.is_open())
        {
            radio_.handle(input, feedback);
        }
        else
        {
            handle_input(input, dt, feedback);
        }
        if (has_command_)
        {
            has_command_ = false;
            run_command(pending_command_, feedback);
        }
        radio_.update(dt);
        sheet_.update(dt);
        sheet_age_ += dt;
        picker_.update(dt);
        picker_age_ += dt;
        listen(dt);
        follow_radio();
        run_reveal(dt);
        toast_ = std::max(0.0f, toast_ - dt);
        sleep_timer(dt, feedback);
        // The level meter: a fast rise, a slow fall, like a VU.
        if (const audio::Mixer *mixer = audio::main_mixer())
        {
            const float peak = mixer->output_peak();
            meter_ = peak > meter_ ? peak : approach(meter_, peak, 3.0f, dt);
            meter_limit_ = approach(meter_limit_, 1.0f - mixer->limiter_gain(), 8.0f, dt);
        }

        const auto &levels = analyzer_.levels();
        const Scene &scene = kScenes[scene_];
        travel_ += dt * speed * tempo_pace_ * scene.speed * (0.6f + 1.6f * levels.bass);
        spin_ += dt * speed * 0.02f * (1.0f + levels.mid);
        heading_ += dt * speed * (0.08f + steer_x_ * 1.1f);
        for (std::size_t i = 0; i < peaks_.size(); ++i)
            peaks_[i] = std::max(levels.bands[i], peaks_[i] - kPeakFall * dt);
        track_tempo(dt);
        if (rumble_ && levels.onset && !context_.settings.reduced_motion)
            feedback.rumble(0.18f + 0.25f * levels.bass, 0.06f);

        run_show(dt, feedback);

        for (int i = 0; i < 4; ++i)
        {
            Color &c = colours_[static_cast<std::size_t>(i)];
            c = gfx::mix(c, target_colour(i), 1.0f - std::exp(-3.0f * dt));
        }

        light_timer_ += dt;
        if (light_timer_ >= kLightBarInterval)
        {
            light_timer_ = 0.0f;
            const float glow = std::min(1.0f, levels.bass + levels.beat * 0.5f);
            light_ = scale(gfx::mix(colours_[2], colours_[3], levels.bass), 0.25f + 0.75f * glow);
        }

        fade_ = std::max(0.0f, fade_ - dt / kEnterFadeSeconds);
        blend_ = std::min(1.0f, blend_ + dt / kCrossFadeSeconds);
        flash_ = std::max(0.0f, flash_ - dt * 2.5f);
        ident_ = std::max(0.0f, ident_ - dt);
        // While the radio, the settings or the scene list cover the screen
        // the idle clock waits, so the controls are there on the way back.
        if (radio_.is_open() || sheet_open_ || picker_open_)
            hud_timer_ = kHudIdleSeconds;
        hud_timer_ = std::max(0.0f, hud_timer_ - dt);
        // Under the radio the controls fade away (its frosted panels would
        // show them through), and fade back in on the way out.
        const bool show_hud =
            !hud_hidden_ && hud_timer_ > 0.0f && display_ != kAmbient && !radio_.is_open();
        hud_alpha_ = approach(hud_alpha_, show_hud ? 1.0f : 0.0f, 6.0f, dt);
    }

    void draw(app::Frame &frame) const override
    {
        const auto &levels = analyzer_.levels();
        // The detected beat, or the clock's when it is surer: pulses land on
        // the beat even where a kick is buried in the mix.
        float beat = std::max(levels.beat, 0.85f * clock_pulse());
        if (context_.settings.reduced_motion || !flashing_)
            beat *= 0.3f; // softer flashes

        gfx::BackdropSpec &spec = frame.backdrop;
        const Scene &scene = kScenes[scene_];
        spec.mode = scene.mode;
        for (int i = 0; i < 4; ++i)
            spec.colors[i] = colours_[static_cast<std::size_t>(i)];
        spec.params[0] = travel_;
        spec.params[1] = spin_;
        spec.params[2] = heading_;
        spec.params[3] = palette_ == kSignature && scene.rainbow ? 1.0f : 0.0f;
        spec.time = static_cast<float>(std::fmod(scene_clock_, 3600.0));
        spec.audio[0] = levels.bass;
        spec.audio[1] = levels.mid;
        spec.audio[2] = levels.treble;
        spec.audio[3] = beat;
        spec.aux[0] = steer_x_;
        spec.aux[1] = steer_y_;
        spec.aux[2] = drive_ + flash_ * 0.8f;
        // The beat clock: beats counted (the fraction is where in the beat),
        // wrapped at 64 (16 bars) so a float keeps its precision. Without a
        // pulse it ticks at 120 a minute.
        spec.aux[3] = static_cast<float>(
            std::fmod(synced() ? beat_.beats() : std::fmod(clock_, 3600.0) * 2.0, 64.0));
        for (int i = 0; i < gfx::BackdropSpec::kBands; ++i)
        {
            spec.spectrum[i] = levels.bands[static_cast<std::size_t>(i)];
            spec.peaks[i] = peaks_[static_cast<std::size_t>(i)];
        }
        for (int i = 0; i < gfx::BackdropSpec::kWave; ++i)
            spec.wave[i] = wave_[static_cast<std::size_t>(i)];
        finish(spec, scene);

        if (display_ == kAmbient)
        {
            frame.post.mode = gfx::BackdropMode::vignette;
            frame.post.colors[0] = Color::rgb(0x000000);
            frame.post.params[0] = 0.85f;
            frame.scene.rounded_rect({0.0f, 0.0f, gfx::kVirtualWidth, gfx::kVirtualHeight}, 0.0f,
                                     Color::rgb(0x000000, 0.30f));
        }
        const bool covered = radio_.is_open() || sheet_open_ || picker_open_;
        if (display_ == kParty && !covered)
            draw_party(frame.scene);
        if (ident_ > 0.0f && !covered)
            draw_ident(frame.scene);
        if (reveal_ > 0.0f && !covered)
            draw_reveal(frame.scene);
        if (hud_alpha_ > 0.01f && !picker_open_)
            draw_hud(frame.scene);
        radio_.draw(frame, pulse());
        if (sheet_open_)
            draw_sheet(frame);
        if (picker_open_)
            draw_picker(frame);
        if (toast_ > 0.0f)
            draw_toast(frame.overlay);
        if (sleep_end_ > 0.0f)
            draw_sleep_end(frame.overlay);
        if (fade_ > 0.0f)
            frame.overlay.rounded_rect({0.0f, 0.0f, gfx::kVirtualWidth, gfx::kVirtualHeight}, 0.0f,
                                       Color::rgb(0x000000, fade_ * fade_));
        if (intro_ < kIntroSeconds)
            draw_intro(frame.overlay);
        else if (notice_)
            draw_notice(frame.overlay);
    }

    std::span<const app::TourStep> tour() const override
    {
        static const app::TourStep kTour[] = {
            {.wait = 1.6f, .press = action_bit(Action::confirm), .capture = "notice"},
            {.wait = 2.2f, .nav = Direction::right, .capture = "swirl"},
            {.wait = 0.45f, .capture = "crossfade"},
            {.wait = 1.6f, .nav = Direction::right, .capture = "orb"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "nebula"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "ember"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "tunnel"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "rays"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "candles"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "prism"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "wormhole"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "mirror-maze"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "kaleido"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "fractal"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "vortex"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "hypnotic"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "dimension"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "kaleido-melt"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "acid-bloom"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "echo-orb"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "smoke"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "silk"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "liquid-dream"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "starfall"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "neon-smear"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "spirals"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "melting-bars"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "chrome"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "rave"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "bubbles"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "lasers"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "scope"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "bars"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "glass"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "kintsugi"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "papercut"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "crystal"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "labyrinth"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "halftone"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "loom"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "origami"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "ferro"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "ridges"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "tidepool"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "veil"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "lanterns"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "moontide"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "cloudsea"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "deepsea"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "ink"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "rainglass"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "dunes"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "fireflies"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "skyline"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "rain"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "mode7"},
            {.wait = 2.0f, .nav = Direction::right, .capture = "polygon"},
            // The palette (L2), a liked song (Square), the overlay hidden.
            {.wait = 2.0f, .press = action_bit(Action::jump_prev), .capture = "plasma"},
            {.wait = 1.5f, .press = action_bit(Action::west), .capture = "plasma-album"},
            {.wait = 0.5f,
             .press = action_bit(Action::north),
             .capture = "liked",
             .stick_x = 0.8f,
             .stick_y = -0.5f,
             .trigger_r = 1.0f},
            {.wait = 1.0f, .press = action_bit(Action::north), .capture = "plasma-hidden"},
            // The settings, down to the sound and its meter.
            {.wait = 1.0f, .press = action_bit(Action::menu), .capture = "plasma-hud"},
            {.wait = 1.0f, .nav = Direction::down, .capture = "settings"},
            {.wait = 0.15f, .nav = Direction::down},
            {.wait = 0.15f, .nav = Direction::down},
            {.wait = 0.15f, .nav = Direction::down},
            {.wait = 0.15f, .nav = Direction::down},
            {.wait = 0.15f, .nav = Direction::down},
            {.wait = 0.15f, .nav = Direction::down},
            {.wait = 0.15f, .nav = Direction::down},
            {.wait = 0.15f, .nav = Direction::down},
            {.wait = 0.15f, .nav = Direction::down},
            {.wait = 0.15f, .nav = Direction::down},
            {.wait = 0.8f, .press = action_bit(Action::back), .capture = "settings-sound"},
            // A new station: its ident, then the song's card.
            {.wait = 0.6f, .press = action_bit(Action::page_next), .capture = "settings-closed"},
            {.wait = 1.2f, .capture = "ident"},
            {.wait = 3.4f, .capture = "song-card"},
            {.wait = 0.4f, .press = action_bit(Action::back), .capture = "song-card-full"},
            // The radio, channel by channel.
            {.wait = 1.0f, .nav = Direction::left, .capture = "radio"},
            {.wait = 0.4f, .nav = Direction::down},
            {.wait = 0.25f, .capture = "radio-morph"},
            {.wait = 0.7f, .nav = Direction::right, .capture = "radio-house"},
            {.wait = 0.9f, .nav = Direction::left, .capture = "radio-house-stations"},
            {.wait = 0.4f, .nav = Direction::down},
            {.wait = 0.9f, .nav = Direction::down, .capture = "radio-trance"},
            {.wait = 0.9f, .nav = Direction::down, .capture = "radio-dnb"},
            {.wait = 0.9f, .nav = Direction::down, .capture = "radio-bass"},
            {.wait = 0.9f, .nav = Direction::down, .capture = "radio-hard"},
            {.wait = 0.9f, .nav = Direction::down, .capture = "radio-edm"},
            {.wait = 0.9f, .nav = Direction::down, .capture = "radio-synthwave"},
            {.wait = 0.9f, .nav = Direction::down, .capture = "radio-chill"},
            {.wait = 0.9f, .nav = Direction::right, .capture = "radio-pop"},
            {.wait = 0.9f, .nav = Direction::left, .capture = "radio-pop-stations"},
            {.wait = 0.4f, .nav = Direction::down},
            {.wait = 0.9f, .nav = Direction::down, .capture = "radio-hiphop"},
            {.wait = 0.9f, .nav = Direction::down, .capture = "radio-rnb"},
            {.wait = 0.9f, .nav = Direction::down, .capture = "radio-bollywood-channel"},
            {.wait = 0.9f, .nav = Direction::right, .capture = "radio-latin"},
            {.wait = 0.9f, .nav = Direction::left, .capture = "radio-latin-stations"},
            {.wait = 0.4f, .nav = Direction::down},
            {.wait = 0.9f, .nav = Direction::down, .capture = "radio-kpop"},
            {.wait = 0.9f, .nav = Direction::down, .capture = "radio-afrobeats"},
            {.wait = 0.9f, .nav = Direction::up, .capture = "radio-arabic"},
            {.wait = 0.3f, .nav = Direction::up},
            {.wait = 0.3f, .nav = Direction::up},
            {.wait = 0.3f, .nav = Direction::up},
            {.wait = 0.9f, .press = action_bit(Action::confirm), .capture = "radio-bollywood"},
            {.wait = 0.5f, .press = action_bit(Action::confirm)},
            {.wait = 1.2f,
             .press = action_bit(Action::north),
             .capture = "radio-bollywood-playing"},
            {.wait = 1.0f, .press = action_bit(Action::back), .capture = "radio-search"},
            {.wait = 0.5f, .nav = Direction::left},
            {.wait = 0.4f, .nav = Direction::up},
            {.wait = 0.8f, .nav = Direction::up, .capture = "radio-history"},
            {.wait = 0.8f, .press = action_bit(Action::back), .capture = "radio-liked"},
            // Auto-VJ, the scene list, a starred scene.
            {.wait = 0.8f, .press = action_bit(Action::r3), .capture = "radio-closed"},
            {.wait = 1.5f, .press = action_bit(Action::confirm), .capture = "auto-vj"},
            {.wait = 0.8f, .nav = Direction::down, .capture = "scene-list"},
            {.wait = 0.6f, .press = action_bit(Action::west), .capture = "scene-list-preview"},
            {.wait = 0.5f, .press = action_bit(Action::back), .capture = "scene-list-star"},
            // The song card in the middle of the screen.
            {.wait = 0.5f, .press = action_bit(Action::menu)},
            {.wait = 0.12f, .nav = Direction::down},
            {.wait = 0.12f, .nav = Direction::down},
            {.wait = 0.12f, .nav = Direction::down},
            {.wait = 0.12f, .nav = Direction::down},
            {.wait = 0.12f, .nav = Direction::down},
            {.wait = 0.12f, .nav = Direction::down},
            {.wait = 0.3f, .nav = Direction::right},
            {.wait = 0.4f, .press = action_bit(Action::menu)},
            {.wait = 0.4f, .press = action_bit(Action::page_next)},
            {.wait = 4.6f, .capture = "song-card-center"},
            // Triangle held: the command box.
            {.wait = 0.9f, .hold = action_bit(Action::north)},
            {.wait = 0.5f, .capture = "command-box"},
        };
        return kTour;
    }

  private:
    // ---- preferences ----

    void set_pref(const char *key, const std::string &value)
    {
        if (audio::RadioControl *radio = audio::radio())
            radio->set_setting(key, value);
    }

    int pref_int(const char *key, int fallback, int count) const
    {
        const audio::RadioControl *radio = audio::radio();
        if (radio == nullptr)
            return fallback;
        const std::string text = radio->setting(key);
        if (text.empty())
            return fallback;
        const int value = std::atoi(text.c_str());
        return value >= 0 && value < count ? value : fallback;
    }

    void load_preferences()
    {
        // The scene is kept by name ("scenename"), so reordering the list
        // never swaps someone's scene; the old index pref is left alone.
        scene_ = 0;
        if (const audio::RadioControl *store = audio::radio())
            scene_ = std::max(0, scene_index(store->setting("scenename").c_str()));
        from_scene_ = scene_;
        palette_ = pref_int("palette", kSignature, kPaletteCount);
        look_ = pref_int("look", 0, kLookCount);
        mode_ = pref_int("mode", kManual, 3);
        trip_ = pref_int("trip", 1, 3);
        display_ = pref_int("display", kNormal, 3);
        flashing_ = pref_int("flashing", 1, 2) == 1;
        rumble_ = pref_int("rumble", 0, 2) == 1;
        gain_ = kMinGain + 0.25f * static_cast<float>(pref_int("sens", 2, 7));
        reveal_mode_ = pref_int("reveal", kRevealCorner, 3);
        vj_set_ = pref_int("vjfav", kVjGenre, 3);
        beat_sync_ = pref_int("beatsync", 1, 2) == 1;
        loudness_ = pref_int("boost", 0, 3);
        eq_ = pref_int("eq", 0, kEqCount);
        favorites_.fill(false);
        const audio::RadioControl *radio = audio::radio();
        if (radio != nullptr)
        {
            // "Pulse Tunnel,Orb": favourite scenes by name, so a new scene
            // list keeps them.
            const std::string names = radio->setting("scenefav");
            std::size_t at = 0;
            while (at <= names.size())
            {
                std::size_t comma = names.find(',', at);
                if (comma == std::string::npos)
                    comma = names.size();
                const int index = scene_index(names.substr(at, comma - at).c_str());
                if (index >= 0)
                    favorites_[static_cast<std::size_t>(index)] = true;
                at = comma + 1;
            }
            // Once: lift the kit's quiet default volume for the radio.
            if (radio->setting("vol5") != "1")
            {
                if (context_.settings.music_volume < kStartVolume)
                {
                    context_.settings.music_volume = kStartVolume;
                    context_.settings_changed = true;
                }
                set_pref("vol5", "1");
            }
        }
        notice_ = radio != nullptr && radio->setting("warned") != "yes";
        for (int i = 0; i < 4; ++i)
            colours_[static_cast<std::size_t>(i)] = target_colour(i);
        apply_sound();
        sync_sheet();
    }

    // The loudness and EQ live in the mixer.
    void apply_sound() const
    {
        if (audio::Mixer *mixer = audio::main_mixer())
        {
            mixer->set_music_boost(kLoudGains[loudness_]);
            mixer->set_music_eq(static_cast<audio::Eq>(eq_));
        }
    }

    void save_favorites()
    {
        std::string names;
        for (int i = 0; i < kSceneCount; ++i)
            if (favorites_[static_cast<std::size_t>(i)])
                names += (names.empty() ? "" : ",") + std::string(kScenes[i].name);
        set_pref("scenefav", names);
    }

    int favorite_count() const
    {
        return static_cast<int>(std::count(favorites_.begin(), favorites_.end(), true));
    }

    // The crossfade row's choice for the radio's setting (3 s when unset).
    int crossfade_index() const
    {
        const audio::RadioControl *radio = audio::radio();
        const std::string value = radio != nullptr ? radio->setting("xfade") : std::string();
        for (int i = 0; i < kFadeCount; ++i)
            if (value == kFadeValues[i])
                return i;
        return 2;
    }

    void save_preferences()
    {
        set_pref("scenename", kScenes[scene_].name);
        set_pref("palette", std::to_string(palette_));
        set_pref("look", std::to_string(look_));
        set_pref("mode", std::to_string(mode_));
        set_pref("trip", std::to_string(trip_));
        set_pref("display", std::to_string(display_));
        set_pref("flashing", flashing_ ? "1" : "0");
        set_pref("rumble", rumble_ ? "1" : "0");
        set_pref("sens", std::to_string(static_cast<int>(std::lround((gain_ - kMinGain) / 0.25f))));
        set_pref("reveal", std::to_string(reveal_mode_));
        set_pref("vjfav", std::to_string(vj_set_));
        set_pref("beatsync", beat_sync_ ? "1" : "0");
        set_pref("boost", std::to_string(loudness_));
        set_pref("eq", std::to_string(eq_));
    }

    // ---- the settings sheet ----

    void build_sheet()
    {
        sheet_.clear();
        sheet_.style.row_height = 62.0f;
        sheet_.style.label_size = 23.0f;
        sheet_.style.value_size = 21.0f;
        sheet_.style.description_size = 18.0f;
        sheet_.style.header_size = 16.0f;
        sheet_.style.control_width = 250.0f;
        sheet_.style.label_ratio = 0.46f;
        sheet_.style.on_page = false;
        sheet_.add_header("VISUALS");
        sheet_.add_choice(kRowMode, "Mode", {kModeNames[0], kModeNames[1], kModeNames[2]}, mode_)
            .description = "Auto-VJ picks scenes for the genre and cuts on the drop";
        sheet_
            .add_choice(kRowTrip, "Trip level", {kTripNames[0], kTripNames[1], kTripNames[2]},
                        trip_)
            .description = "How far the trails melt, swirl and change colour";
        std::vector<std::string> palettes;
        for (const Palette &palette : kPalettes)
            palettes.emplace_back(palette.name);
        sheet_.add_choice(kRowPalette, "Palette", palettes, palette_).description =
            "Album art takes its colours from the cover playing";
        sheet_.add_choice(kRowLook, "Look", {kLookNames[0], kLookNames[1], kLookNames[2]}, look_);
        sheet_.add_toggle(kRowFlashing, "Flashing", flashing_).description =
            "Strobes and hard flashes on beats. Off if you are sensitive to them";
        sheet_
            .add_choice(kRowDisplay, "Display",
                        {kDisplayNames[0], kDisplayNames[1], kDisplayNames[2]}, display_)
            .description = "Party: big type for the room. Ambient: slow and dim";
        sheet_
            .add_choice(kRowReveal, "Song card",
                        {kRevealNames[0], kRevealNames[1], kRevealNames[2]}, reveal_mode_)
            .description = "A card with the cover and title when a new song starts";
        sheet_
            .add_choice(kRowVjScenes, "Auto-VJ scenes", {kVjNames[0], kVjNames[1], kVjNames[2]},
                        vj_set_)
            .description = "Favorites: the scenes you starred. Calm: the slow, soft pack";
        sheet_.add_toggle(kRowBeatSync, "Beat sync", beat_sync_).description =
            "Motion, pulses and Auto-VJ cuts follow the measured tempo";
        sheet_.add_header("SOUND");
        sheet_
            .add_choice(
                kRowSleep, "Sleep timer",
                {kSleepNames[0], kSleepNames[1], kSleepNames[2], kSleepNames[3], kSleepNames[4]}, 0)
            .description = "Stops the music after a while";
        sheet_.add_slider(kRowVolume, "Music volume",
                          static_cast<float>(context_.settings.music_volume), 0.0f, 10.0f, 1.0f);
        sheet_
            .add_choice(kRowLoudness, "Loudness", {kLoudNames[0], kLoudNames[1], kLoudNames[2]},
                        loudness_)
            .description = "More level for quiet stations and headphones, without clipping";
        std::vector<std::string> eqs;
        for (int i = 0; i < kEqCount; ++i)
            eqs.emplace_back(audio::eq_name(static_cast<audio::Eq>(i)));
        sheet_.add_choice(kRowEq, "EQ", eqs, eq_).description = "The tone of the music";
        sheet_
            .add_choice(kRowCrossfade, "Crossfade",
                        {kFadeNames[0], kFadeNames[1], kFadeNames[2], kFadeNames[3]}, 2)
            .description = "A new station blends in over the one playing";
        sheet_.add_slider(kRowSensitivity, "Sensitivity", gain_, kMinGain, kMaxGain, 0.25f)
            .decimals = 2;
        sheet_.add_toggle(kRowRumble, "Beat rumble", rumble_);
        sheet_.add_toggle(kRowResume, "Resume radio at start", true).description =
            "Plays the last station when Afterglow opens";
        // The level meter sits under the rows.
        sheet_.set_bounds(
            {kSheet.x + 20.0f, kSheet.y + 112.0f, kSheet.w - 40.0f, kSheet.h - 206.0f});
    }

    void sync_sheet()
    {
        sheet_.set_choice(kRowMode, mode_);
        sheet_.set_choice(kRowTrip, trip_);
        sheet_.set_choice(kRowPalette, palette_);
        sheet_.set_choice(kRowLook, look_);
        sheet_.set_toggle(kRowFlashing, flashing_);
        sheet_.set_choice(kRowDisplay, display_);
        sheet_.set_choice(kRowReveal, reveal_mode_);
        sheet_.set_choice(kRowVjScenes, vj_set_);
        sheet_.set_toggle(kRowBeatSync, beat_sync_);
        sheet_.set_choice(kRowLoudness, loudness_);
        sheet_.set_choice(kRowEq, eq_);
        sheet_.set_choice(kRowCrossfade, crossfade_index());
        sheet_.set_slider(kRowVolume, static_cast<float>(context_.settings.music_volume));
        sheet_.set_slider(kRowSensitivity, gain_);
        sheet_.set_toggle(kRowRumble, rumble_);
        const audio::RadioControl *radio = audio::radio();
        sheet_.set_toggle(kRowResume, radio == nullptr || radio->setting("resume") != "off");
    }

    void open_sheet(app::Feedback &feedback)
    {
        sheet_open_ = true;
        sheet_age_ = 0.0f;
        sync_sheet();
        sheet_.style.reduced_motion = context_.settings.reduced_motion;
        sheet_.style.theme = sheet_theme();
        sheet_.focus_row(kRowMode, true);
        sheet_.enter();
        sheet_.set_active(true);
        feedback.play(audio::Cue::open);
    }

    void close_sheet()
    {
        sheet_open_ = false;
        sheet_.set_active(false);
        save_preferences();
    }

    ui::Theme sheet_theme() const
    {
        ui::Theme theme = ui::themes().front();
        for (const ui::Theme &t : ui::themes())
            if (std::string(t.id) == "acrylic")
                theme = t;
        const GenreSkin &skin = current_skin();
        theme.accent = Color::rgb(skin.accent);
        theme.primary = Color::rgb(skin.accent);
        theme.on_primary = Color::rgb(skin.bg0);
        theme.focus = gfx::mix(Color::rgb(skin.accent), Color::rgb(0xffffff), 0.25f);
        return theme;
    }

    void handle_sheet(const InputFrame &input, app::Feedback &feedback)
    {
        hud_timer_ = kHudIdleSeconds;
        if (input.is_pressed(Action::menu))
        {
            close_sheet();
            feedback.play(audio::Cue::back);
            return;
        }
        const ui::Event event = sheet_.handle(input, feedback);
        if (event == ui::Event::cancelled)
        {
            close_sheet();
            return;
        }
        if (event != ui::Event::changed)
            return;
        switch (sheet_.changed_id())
        {
        case kRowMode:
            set_mode(sheet_.choice_index(kRowMode));
            break;
        case kRowTrip:
            trip_ = sheet_.choice_index(kRowTrip);
            break;
        case kRowPalette:
            palette_ = sheet_.choice_index(kRowPalette);
            break;
        case kRowLook:
            look_ = sheet_.choice_index(kRowLook);
            break;
        case kRowFlashing:
            flashing_ = sheet_.toggle_value(kRowFlashing);
            break;
        case kRowDisplay:
            display_ = sheet_.choice_index(kRowDisplay);
            break;
        case kRowReveal:
            reveal_mode_ = std::clamp(sheet_.choice_index(kRowReveal), 0, 2);
            break;
        case kRowVjScenes:
            vj_set_ = std::clamp(sheet_.choice_index(kRowVjScenes), 0, 2);
            break;
        case kRowBeatSync:
            beat_sync_ = sheet_.toggle_value(kRowBeatSync);
            break;
        case kRowLoudness:
            loudness_ = std::clamp(sheet_.choice_index(kRowLoudness), 0, 2);
            apply_sound();
            break;
        case kRowEq:
            eq_ = std::clamp(sheet_.choice_index(kRowEq), 0, kEqCount - 1);
            apply_sound();
            break;
        case kRowCrossfade:
            set_pref(
                "xfade",
                kFadeValues[std::clamp(sheet_.choice_index(kRowCrossfade), 0, kFadeCount - 1)]);
            break;
        case kRowSleep:
        {
            const int minutes = kSleepMinutes[std::clamp(sheet_.choice_index(kRowSleep), 0, 4)];
            sleep_left_ = static_cast<float>(minutes) * 60.0f;
            sleep_end_ = 0.0f;
            break;
        }
        case kRowVolume:
            context_.settings.music_volume =
                static_cast<int>(std::lround(sheet_.slider_value(kRowVolume)));
            context_.settings_changed = true;
            break;
        case kRowSensitivity:
            gain_ = sheet_.slider_value(kRowSensitivity);
            break;
        case kRowRumble:
            rumble_ = sheet_.toggle_value(kRowRumble);
            if (rumble_)
                feedback.rumble(0.3f, 0.08f);
            break;
        case kRowResume:
            set_pref("resume", sheet_.toggle_value(kRowResume) ? "on" : "off");
            break;
        default:
            break;
        }
    }

    // ---- the scene list (Cross) ----

    void open_picker(app::Feedback &feedback)
    {
        picker_open_ = true;
        picker_age_ = 0.0f;
        picker_from_ = scene_;
        const ui::Theme theme = sheet_theme();
        picker_.style.theme = theme;
        picker_.style.panel = false;
        picker_.style.row_height = 54.0f;
        picker_.style.header_height = 50.0f;
        picker_.style.title_size = 24.0f;
        picker_.style.value_size = 22.0f;
        picker_.style.header_size = 15.0f;
        picker_.style.highlight.kind = current_skin().highlight;
        picker_.style.reduced_motion = context_.settings.reduced_motion;
        picker_.style.leading_width = 30.0f;
        // Starred scenes wear a drawn star (the fonts have no symbol for one).
        picker_.leading =
            [this](ui::Canvas &canvas, const Rect &row, const ui::ListItem &item, int, float focus)
        {
            if (item.tag < 0 || !favorites_[static_cast<std::size_t>(item.tag)])
                return;
            const Color c =
                gfx::mix(Color::rgb(current_skin().accent2), Color::rgb(0xffffff), focus);
            canvas.list.star(row.x + row.w * 0.5f, row.y + row.h * 0.5f, 10.0f, c);
        };
        picker_.set_bounds(
            {kPicker.x + 14.0f, kPicker.y + 166.0f, kPicker.w - 28.0f, kPicker.h - 166.0f - 86.0f});
        std::vector<ui::ListItem> items;
        const char *pack = "";
        int focus = 0;
        for (int i = 0; i < kSceneCount; ++i)
        {
            const Scene &scene = kScenes[i];
            if (std::string(pack) != scene.pack)
            {
                pack = scene.pack;
                ui::ListItem header;
                header.header = true;
                header.tag = -1;
                header.title = std::string(pack) + "  /  " + pack_line(pack);
                items.push_back(header);
            }
            ui::ListItem item;
            item.title = scene.name;
            item.swatch = Color::rgb(scene.signature[2]);
            item.tag = i;
            if (i == scene_)
                focus = static_cast<int>(items.size());
            items.push_back(item);
        }
        picker_.set_items(std::move(items));
        picker_.set_focus(focus);
        picker_.enter();
        picker_.set_active(true);
        feedback.play(audio::Cue::open);
    }

    static const char *pack_line(std::string_view pack)
    {
        if (pack == "AFTERGLOW")
            return "THE ORIGINALS";
        if (pack == "CLUB")
            return "FOR THE DANCE FLOOR";
        if (pack == "TRIP")
            return "PSYCHEDELIC, MOSTLY WITH TRAILS";
        return "OTHER ERAS";
    }

    int picker_scene() const
    {
        const auto &items = picker_.items();
        const int row = picker_.focus();
        if (row < 0 || row >= static_cast<int>(items.size()))
            return -1;
        return items[static_cast<std::size_t>(row)].tag;
    }

    void close_picker(bool keep)
    {
        picker_open_ = false;
        picker_.set_active(false);
        if (!keep && scene_ != picker_from_)
            set_scene(picker_from_);
        set_pref("scenename", kScenes[scene_].name);
    }

    void handle_picker(const InputFrame &input, app::Feedback &feedback)
    {
        hud_timer_ = kHudIdleSeconds;
        if (input.is_pressed(Action::back))
        {
            close_picker(false); // back where it was
            feedback.play(audio::Cue::back);
            return;
        }
        if (input.is_pressed(Action::menu))
        {
            close_picker(true);
            feedback.play(audio::Cue::back);
            return;
        }
        if (input.is_pressed(Action::west))
        {
            const int index = picker_scene();
            if (index >= 0)
            {
                bool &on = favorites_[static_cast<std::size_t>(index)];
                on = !on;
                save_favorites();
                feedback.play(on ? audio::Cue::favorite_on : audio::Cue::favorite_off);
            }
            return;
        }
        const int before = picker_.focus();
        const ui::Event event = picker_.handle(input, feedback);
        if (picker_.focus() != before)
        {
            // The scene under the focus plays behind the list as you move.
            const int index = picker_scene();
            if (index >= 0 && index != scene_)
                set_scene(index);
        }
        if (event == ui::Event::activated)
            close_picker(true);
    }

    // ---- the show ----

    void set_scene(int index)
    {
        // A change during a cross-fade starts from the scene that was arriving.
        from_scene_ = scene_;
        scene_ = (index % kSceneCount + kSceneCount) % kSceneCount;
        blend_ = 0.0f;
        vj_timer_ = 0.0f;
    }

    void set_mode(int mode)
    {
        mode_ = std::clamp(mode, 0, 2);
        showcase_timer_ = 0.0f;
        vj_timer_ = 0.0f;
        if (mode_ == kAutoVj)
            vj_pick(false);
    }

    const GenreSkin &current_skin() const
    {
        const audio::RadioControl *radio = audio::radio();
        if (radio == nullptr)
            return default_skin();
        const audio::Station *station = playing_station(radio, radio->status());
        return station != nullptr ? skin_for(station->category) : default_skin();
    }

    // The next scene for the genre, round its list; a drop jumps further.
    void vj_pick(bool drop)
    {
        const GenreSkin &skin = current_skin();
        std::array<int, kSceneCount> choices{};
        int count = 0;
        if (vj_set_ == kVjFavorites && favorite_count() >= 2)
        {
            // The listener's own set, whatever the genre.
            for (int i = 0; i < kSceneCount; ++i)
                if (favorites_[static_cast<std::size_t>(i)])
                    choices[static_cast<std::size_t>(count++)] = i;
        }
        else if (vj_set_ == kVjCalm)
        {
            // Winding down: the calm pack only, whatever is playing.
            for (int i = 0; i < kSceneCount; ++i)
                if (std::string(kScenes[i].pack) == "CALM")
                    choices[static_cast<std::size_t>(count++)] = i;
        }
        else
        {
            for (const char *name : skin.scenes)
            {
                const int index = name != nullptr ? scene_index(name) : -1;
                if (index >= 0)
                    choices[static_cast<std::size_t>(count++)] = index;
            }
        }
        if (count == 0)
            return;
        vj_step_ = (vj_step_ + (drop ? 2 : 1) + count) % count;
        const int next = choices[static_cast<std::size_t>(vj_step_)];
        if (next != scene_)
            set_scene(next);
        else if (count > 1)
            set_scene(choices[static_cast<std::size_t>((vj_step_ + 1) % count)]);
    }

    void run_show(float dt, app::Feedback &feedback)
    {
        if (mode_ == kShowcase)
        {
            showcase_timer_ += dt;
            if (showcase_timer_ >= kShowcaseSeconds)
            {
                showcase_timer_ = 0.0f;
                set_scene(scene_ + 1);
            }
        }
        // The drop: the bass comes back hard after a breakdown.
        const auto &levels = analyzer_.levels();
        bass_fast_ = approach(bass_fast_, levels.bass, 1.0f / 0.25f, dt);
        bass_slow_ = approach(bass_slow_, levels.bass, 1.0f / 6.0f, dt);
        if (bass_fast_ < bass_slow_ * 0.55f)
            breakdown_ += dt;
        else if (bass_fast_ > bass_slow_ * 0.9f)
            breakdown_ = std::max(0.0f, breakdown_ - dt * 0.5f);
        const bool drop = breakdown_ > 2.5f && bass_fast_ > bass_slow_ * 1.3f + 0.05f;
        if (drop)
        {
            breakdown_ = 0.0f;
            if (flashing_ && !context_.settings.reduced_motion)
                flash_ = 1.0f;
            if (rumble_)
                feedback.rumble(0.6f, 0.25f);
        }
        if (mode_ != kAutoVj)
            return;
        vj_timer_ += dt;
        if ((drop && vj_timer_ > 6.0f) || vj_timer_ > kVjMaxSeconds ||
            (vj_timer_ > kVjMinSeconds && (synced() ? phrase_ : levels.onset && beats_ % 32 == 0)))
            vj_pick(drop);
    }

    // Watches the radio: a new station brings its ident (and, under
    // Auto-VJ, a scene for its genre); a new song asks for its cover.
    void follow_radio()
    {
        const audio::RadioControl *radio = audio::radio();
        if (radio == nullptr)
            return;
        const audio::PlayerStatus status = radio->status();
        const int station = status.source == audio::Source::radio ? status.station : -1;
        if (station != last_station_)
        {
            const bool first = last_station_ == -2;
            last_station_ = station;
            net::CoverArt *logos = net::station_logos();
            if (logos != nullptr && station >= 0 && !radio->logo(station).empty())
                logos->request(radio->logo(station));
            if (station >= 0 && !first)
            {
                ident_ = kIdentSeconds;
                ident_station_ = station;
                if (mode_ == kAutoVj)
                {
                    vj_step_ = -1;
                    vj_pick(false);
                }
            }
        }
        if (status.source == audio::Source::radio && status.title != last_song_)
        {
            last_song_ = status.title;
            if (net::CoverArt *art = net::cover_art())
                art->request(status.title);
            // A new song gets its card, once the station's ident has gone.
            if (!status.title.empty() && station >= 0)
            {
                reveal_pending_ = true;
                reveal_ = 0.0f;
            }
        }
    }

    // Whether the face can draw every character of text (a title in a
    // script the fonts do not carry would show as gaps).
    static bool drawable(const ui::FontRef &face, std::string_view text)
    {
        if (face.font == nullptr)
            return false;
        std::size_t at = 0;
        while (at < text.size())
        {
            const std::uint32_t c = gfx::next_codepoint(text, &at);
            if (c > 0x20 && !face.font->has_glyph(c))
                return false;
        }
        return true;
    }

    // Starts a waiting song card when nothing else holds the screen.
    void run_reveal(float dt)
    {
        reveal_ = std::max(0.0f, reveal_ - dt);
        if (!reveal_pending_ || ident_ > 0.0f || radio_.is_open() || sheet_open_ || picker_open_ ||
            intro_ < kIntroSeconds || notice_)
            return;
        reveal_pending_ = false;
        const audio::RadioControl *radio = audio::radio();
        // The card has its own setting: hiding the controls does not hide it.
        if (reveal_mode_ == kRevealOff || display_ == kParty || radio == nullptr)
            return;
        const audio::PlayerStatus status = radio->status();
        if (status.source != audio::Source::radio || status.title.empty() ||
            !drawable(context_.fonts.semibold, status.title))
            return;
        reveal_ = kRevealSeconds;
        reveal_title_ = status.title;
        reveal_station_ = status.station;
    }

    static std::vector<SceneRef> scene_refs()
    {
        std::vector<SceneRef> refs;
        for (const Scene &scene : kScenes)
        {
            SceneRef ref;
            ref.name = scene.name;
            ref.pack = scene.pack;
            refs.push_back(std::move(ref));
        }
        return refs;
    }

    // Carries out a command from the box, then says what it did.
    void run_command(const Command &command, app::Feedback &feedback)
    {
        audio::RadioControl *radio = audio::radio();
        if (radio == nullptr)
            return;
        const audio::PlayerStatus status = radio->status();
        const bool sounding = status.source == audio::Source::radio
                                  ? status.state == audio::RadioState::playing ||
                                        status.state == audio::RadioState::buffering ||
                                        status.state == audio::RadioState::connecting
                                  : !status.songs_paused;
        bool shown = true; // the visuals take over after most commands
        switch (command.kind)
        {
        case CommandKind::play_station:
        {
            // Next and previous stay in the station's genre.
            const auto &stations = radio->stations();
            const std::string &genre = stations[static_cast<std::size_t>(command.station)].category;
            std::vector<int> queue;
            for (std::size_t i = 0; i < stations.size(); ++i)
                if (stations[i].category == genre)
                    queue.push_back(static_cast<int>(i));
            radio->play(command.station, queue);
            break;
        }
        case CommandKind::play_genre:
        {
            // A station that answered the last check, if any did.
            int pick = command.station;
            for (int index : command.queue)
                if (radio->health(index) == audio::Health::ok)
                {
                    pick = index;
                    break;
                }
            radio->play(pick, command.queue);
            break;
        }
        case CommandKind::play_songs:
            radio->play_songs();
            break;
        case CommandKind::resume:
            if (!sounding)
                radio->toggle_pause();
            break;
        case CommandKind::pause:
            if (sounding)
                radio->toggle_pause();
            break;
        case CommandKind::stop:
            radio->stop();
            break;
        case CommandKind::next:
            radio->next();
            break;
        case CommandKind::previous:
            radio->previous();
            break;
        case CommandKind::like:
        case CommandKind::unlike:
            if (status.title.empty() ||
                radio->is_liked(status.title) != (command.kind == CommandKind::like))
                like_song(feedback); // it says what happened itself
            else
                feedback.play(audio::Cue::select);
            radio_.close();
            return;
        case CommandKind::scene:
            set_scene(std::max(0, scene_index(command.scene.c_str())));
            set_pref("scenename", kScenes[scene_].name);
            break;
        case CommandKind::scene_pack:
        {
            // The next scene of the pack after the one showing.
            for (int step = 1; step <= kSceneCount; ++step)
            {
                const int i = (scene_ + step) % kSceneCount;
                if (command.pack == kScenes[i].pack)
                {
                    set_scene(i);
                    set_pref("scenename", kScenes[i].name);
                    break;
                }
            }
            if (command.pack == "CALM" && mode_ == kAutoVj)
                vj_set_ = kVjCalm;
            break;
        }
        case CommandKind::auto_vj:
            set_mode(command.on ? kAutoVj : kManual);
            set_pref("mode", std::to_string(mode_));
            break;
        case CommandKind::display:
            display_ = std::clamp(command.display, 0, 2);
            set_pref("display", std::to_string(display_));
            break;
        case CommandKind::hide:
            hud_hidden_ = true;
            break;
        case CommandKind::show:
            hud_hidden_ = false;
            hud_timer_ = kHudIdleSeconds;
            break;
        case CommandKind::louder:
        case CommandKind::quieter:
            context_.settings.music_volume = std::clamp(
                context_.settings.music_volume + (command.kind == CommandKind::louder ? 1 : -1), 0,
                10);
            context_.settings_changed = true;
            break;
        case CommandKind::sleep:
            sleep_left_ = static_cast<float>(command.minutes) * 60.0f;
            sleep_end_ = 0.0f;
            break;
        case CommandKind::open_radio:
            shown = false;
            break;
        case CommandKind::none:
            return;
        }
        if (shown)
            radio_.close();
        sync_sheet();
        toast_text_ = describe_command(command, radio->stations());
        if (command.kind == CommandKind::louder || command.kind == CommandKind::quieter)
            toast_text_ += ": volume " + std::to_string(context_.settings.music_volume);
        toast_liked_ = false;
        toast_ = kToastSeconds;
        feedback.play(audio::Cue::select);
    }

    // Square: keep the song on air (or let it go again).
    void like_song(app::Feedback &feedback)
    {
        audio::RadioControl *radio = audio::radio();
        const audio::PlayerStatus status =
            radio != nullptr ? radio->status() : audio::PlayerStatus{};
        const audio::Station *station = playing_station(radio, status);
        toast_ = kToastSeconds;
        if (radio == nullptr || station == nullptr || status.title.empty())
        {
            toast_text_ = "No song title to like on this station";
            toast_liked_ = false;
            feedback.play(audio::Cue::error);
            return;
        }
        audio::SongEntry song;
        song.title = status.title;
        song.station = station->name;
        song.url = station->url;
        song.when = static_cast<std::int64_t>(std::time(nullptr));
        const bool on = !radio->is_liked(status.title);
        radio->set_liked(song, on);
        toast_liked_ = on;
        toast_text_ = on ? status.title : "Removed from Liked songs";
        feedback.play(on ? audio::Cue::favorite_on : audio::Cue::favorite_off);
        if (on && rumble_)
            feedback.rumble(0.35f, 0.08f);
    }

    void sleep_timer(float dt, app::Feedback &feedback)
    {
        if (sleep_end_ > 0.0f)
            sleep_end_ = std::max(0.0f, sleep_end_ - dt);
        if (sleep_left_ <= 0.0f)
            return;
        sleep_left_ -= dt;
        if (sleep_left_ > 0.0f)
            return;
        sleep_left_ = 0.0f;
        sleep_end_ = 6.0f;
        sheet_.set_choice(kRowSleep, 0);
        if (audio::RadioControl *radio = audio::radio())
        {
            const audio::PlayerStatus status = radio->status();
            if (status.source == audio::Source::radio)
                radio->stop();
            else if (!status.songs_paused)
                radio->toggle_pause();
        }
        display_ = kAmbient;
        feedback.play(audio::Cue::back);
    }

    // The tempo shown and used: the beat clock's, while it has a pulse.
    void track_tempo(float dt)
    {
        (void)dt;
        if (analyzer_.levels().onset)
            ++beats_;
        bpm_ = beat_.locked() ? beat_.bpm() : 0.0f;
        // A phrase is 32 beats: Auto-VJ may cut where one ends.
        if (synced() && beat_.ticked())
        {
            const auto beat = static_cast<long long>(std::floor(beat_.beats()));
            phrase_ = beat % 32 == 0;
        }
        else
        {
            phrase_ = false;
        }
    }

    // The scenes follow the beat clock: on, and the music has a pulse.
    bool synced() const
    {
        return beat_sync_ && beat_.locked();
    }

    // A pulse on every beat the clock predicts: 1 on the beat, fading fast.
    float clock_pulse() const
    {
        if (!synced())
            return 0.0f;
        return std::exp(-beat_.phase() * 7.0f) * std::min(1.0f, beat_.confidence() * 2.0f);
    }

    Color target_colour(int i) const
    {
        const auto k = static_cast<std::size_t>(i);
        if (palette_ > kGenre)
            return Color::rgb(kPalettes[palette_].colours[k]);
        const GenreSkin &skin = current_skin();
        const bool radio_on = &skin != &default_skin();
        if (palette_ == kAlbum && radio_on)
        {
            const audio::RadioControl *radio = audio::radio();
            const net::CoverArt *art = net::cover_art();
            if (radio != nullptr && art != nullptr)
            {
                const net::ArtView view = art->view(radio->status().title);
                if (view.state == net::ArtState::ready)
                {
                    const Color vivid = vivify(view.vivid, 1.25f, 0.75f);
                    const Color dominant = vivify(view.dominant, 1.1f, 0.4f);
                    const Color colours[4] = {scale(dominant, 0.10f), scale(dominant, 0.35f), vivid,
                                              gfx::mix(vivid, Color::rgb(0xffffff), 0.45f)};
                    return colours[k];
                }
            }
        }
        if ((palette_ == kAlbum || palette_ == kGenre) && radio_on)
        {
            const std::uint32_t colours[4] = {skin.bg0, skin.bg1, skin.accent, skin.accent2};
            return Color::rgb(colours[k]);
        }
        return Color::rgb(kScenes[scene_].signature[k]);
    }

    void finish(gfx::BackdropSpec &spec, const Scene &scene) const
    {
        const Look &look = look_ == 0 ? scene.look : look_ == 1 ? kClean : kCrt;
        gfx::PostFx &fx = spec.fx;
        fx.enabled = true;
        fx.bloom = look.bloom;
        fx.fringe = look.fringe;
        fx.grain = look.grain;
        fx.scanlines = look.scanlines;
        fx.vignette = look.vignette;
        fx.curve = look.curve;
        fx.pixel_height = scene.pixel_height;
        fx.smooth = scene.smooth;
        fx.feedback = tripped(scene.feedback);
        if (context_.settings.reduced_motion)
            fx.grain *= 0.3f; // a still picture should look still
        if (blend_ < 1.0f)
        {
            const Scene &from = kScenes[from_scene_];
            spec.from_mode = from.mode;
            spec.from_pixel_height = from.pixel_height;
            spec.from_smooth = from.smooth;
            spec.from_feedback = tripped(from.feedback);
            spec.blend = blend_ * blend_ * (3.0f - 2.0f * blend_);
        }
    }

    // The trip level scales how far the trails travel and turn.
    gfx::FeedbackFx tripped(gfx::FeedbackFx fb) const
    {
        if (!fb.enabled)
            return fb;
        float k = kTripLevels[trip_];
        if (context_.settings.reduced_motion)
            k = std::min(k, 0.55f);
        fb.zoom = 1.0f + (fb.zoom - 1.0f) * k;
        fb.rotate *= k;
        fb.warp *= k;
        fb.hue *= flashing_ ? k : std::min(k, 0.5f);
        fb.shift_y *= k;
        fb.decay = std::clamp(1.0f - (1.0f - fb.decay) / k, 0.5f, 0.975f);
        return fb;
    }

    void handle_input(const InputFrame &input, float dt, app::Feedback &feedback)
    {
        const bool dpad = input.nav != Direction::none && !input.nav_from_stick;
        if (input.pressed != 0 || dpad ||
            std::abs(input.stick_x) + std::abs(input.stick_y) > 0.3f || input.trigger_r > 0.2f)
            hud_timer_ = kHudIdleSeconds;

        if (input.is_pressed(Action::confirm))
        {
            open_picker(feedback);
            return;
        }
        if (dpad && !input.nav_repeat && input.nav == Direction::right)
        {
            set_scene(scene_ + 1);
            feedback.play(audio::Cue::tab, 1.0f, 0.3f);
            set_pref("scenename", kScenes[scene_].name);
        }
        else if (dpad && !input.nav_repeat && input.nav == Direction::left)
        {
            set_scene(scene_ - 1);
            feedback.play(audio::Cue::tab, 1.0f, -0.3f);
            set_pref("scenename", kScenes[scene_].name);
        }
        if (dpad && !input.nav_repeat &&
            (input.nav == Direction::up || input.nav == Direction::down))
        {
            const int next = std::clamp(trip_ + (input.nav == Direction::up ? 1 : -1), 0, 2);
            if (next == trip_)
            {
                feedback.play(audio::Cue::error);
            }
            else
            {
                trip_ = next;
                feedback.play(audio::Cue::slider, 0.8f + 0.2f * static_cast<float>(trip_));
                set_pref("trip", std::to_string(trip_));
            }
        }
        if (input.is_pressed(Action::west))
            like_song(feedback);
        if (input.is_pressed(Action::jump_prev))
        {
            palette_ = (palette_ + 1) % kPaletteCount;
            feedback.play(audio::Cue::toggle);
            set_pref("palette", std::to_string(palette_));
        }
        if (input.is_pressed(Action::back))
        {
            radio_.open();
            feedback.play(audio::Cue::open);
            return;
        }
        // The transport: L1 / R1 change the station (or the song), L3 plays
        // and pauses.
        if (audio::RadioControl *radio = audio::radio())
        {
            if (input.is_pressed(Action::page_next))
                radio->next();
            if (input.is_pressed(Action::page_prev))
                radio->previous();
            if (input.is_pressed(Action::l3))
                radio->toggle_pause();
            if (input.is_pressed(Action::page_next) || input.is_pressed(Action::page_prev) ||
                input.is_pressed(Action::l3))
                feedback.play(audio::Cue::toggle);
        }
        // Triangle held: the command box ("play techno", "next", "like").
        if (input.is_held(Action::north))
        {
            north_held_ += dt;
            if (north_held_ >= kHoldSeconds && !north_long_)
            {
                north_long_ = true;
                hud_hidden_ = hidden_before_hold_; // the tap's hide is undone
                radio_.open_command();
                feedback.play(audio::Cue::open);
                return;
            }
        }
        else
        {
            north_held_ = 0.0f;
            north_long_ = false;
        }
        if (input.is_pressed(Action::north))
        {
            hidden_before_hold_ = hud_hidden_;
            // Hidden stays hidden through other input, for clean recordings.
            // An overlay that has only faded away after a while (not hidden)
            // comes back: Triangle must never seem to do nothing.
            if (!hud_hidden_ && hud_alpha_ < 0.5f)
                hud_timer_ = kHudIdleSeconds;
            else
                hud_hidden_ = !hud_hidden_;
            hud_timer_ = hud_hidden_ ? 0.0f : kHudIdleSeconds;
            feedback.play(audio::Cue::toggle);
        }
        if (input.is_pressed(Action::menu))
        {
            open_sheet(feedback);
            return;
        }
        if (input.is_pressed(Action::r3))
        {
            set_mode(mode_ == kAutoVj ? kManual : kAutoVj);
            feedback.play(mode_ == kAutoVj ? audio::Cue::open : audio::Cue::back);
            set_pref("mode", std::to_string(mode_));
        }

        steer_x_ = approach(steer_x_, input.stick_x, 4.0f, dt);
        steer_y_ = approach(steer_y_, input.stick_y, 4.0f, dt);
        drive_ = approach(drive_, input.trigger_r, 8.0f, dt);
    }

    // Reads the music tap when the mixer is running, else the demo track.
    void listen(float dt)
    {
        const audio::Monitor &monitor = audio::music_monitor();
        const std::uint64_t written = monitor.written();
        if (written != last_written_)
            live_hold_ = kLiveHoldSeconds;
        else
            live_hold_ = std::max(0.0f, live_hold_ - dt);
        last_written_ = written;
        live_ = live_hold_ > 0.0f && monitor.latest(samples_.data(), samples_.size());

        const std::array<float, 16> raw =
            live_ ? spectrum_.analyze(samples_.data(), dt) : audio::demo_spectrum(clock_);
        analyzer_.update(raw, dt, gain_);
        // The onset strength for the beat clock: how much the spectrum rose
        // since the last frame, the kick drum's bands counted most.
        float flux = 0.0f;
        for (std::size_t i = 0; i < raw.size(); ++i)
        {
            const float weight = i < 4 ? 1.0f : i < 10 ? 0.45f : 0.2f;
            flux += weight * std::max(0.0f, raw[i] - last_raw_[i]);
        }
        last_raw_ = raw;
        beat_.push(flux, dt);
        make_wave();
    }

    // 64 points of the waveform, from a rising zero crossing so the scope
    // stands still on a steady tone, scaled to its own peak.
    void make_wave()
    {
        if (!live_)
        {
            // The demo: a sum of sines shaped by the bands.
            const auto &bands = analyzer_.levels().bands;
            const float t = static_cast<float>(clock_);
            for (int i = 0; i < 64; ++i)
            {
                const float x = static_cast<float>(i) / 64.0f * 6.2831853f;
                float v = 0.0f;
                for (int b = 0; b < 6; ++b)
                {
                    const auto fb = static_cast<float>(b);
                    v += bands[static_cast<std::size_t>(b * 2)] *
                         std::sin(x * (fb + 1.0f) + t * (0.7f + 0.3f * fb));
                }
                wave_[static_cast<std::size_t>(i)] = std::clamp(v * 0.6f, -1.0f, 1.0f);
            }
            return;
        }
        constexpr std::size_t kSpan = 512;
        const std::size_t n = samples_.size();
        std::size_t start = n - kSpan * 2;
        for (std::size_t i = start + 1; i < n - kSpan; ++i)
        {
            if (samples_[i - 1] < 0.0f && samples_[i] >= 0.0f)
            {
                start = i;
                break;
            }
        }
        float peak = 0.05f;
        for (std::size_t i = start; i < start + kSpan; ++i)
            peak = std::max(peak, std::abs(samples_[i]));
        for (std::size_t p = 0; p < wave_.size(); ++p)
        {
            float sum = 0.0f;
            for (std::size_t k = 0; k < 8; ++k)
                sum += samples_[start + p * 8 + k];
            const float target = std::clamp(sum / 8.0f / peak, -1.0f, 1.0f);
            wave_[p] = wave_[p] * 0.35f + target * 0.65f;
        }
    }

    Pulse pulse() const
    {
        const auto &levels = analyzer_.levels();
        Pulse p;
        p.time = static_cast<float>(std::fmod(clock_, 3600.0));
        p.bass = levels.bass;
        p.beat = flashing_ ? levels.beat : levels.beat * 0.3f;
        p.bands = &levels.bands;
        p.reduced_motion = context_.settings.reduced_motion;
        return p;
    }

    // ---- drawing ----

    void draw_hud(gfx::DrawList &list) const
    {
        const ui::Fonts &fonts = context_.fonts;
        list.push_opacity(hud_alpha_);
        // Shade the top and bottom edges so the text reads over bright scenes.
        list.gradient_rect({0.0f, 0.0f, gfx::kVirtualWidth, 250.0f}, 0.0f, kShade, kClear);
        list.gradient_rect({0.0f, gfx::kVirtualHeight - 290.0f, gfx::kVirtualWidth, 290.0f}, 0.0f,
                           kClear, kShade);

        const Scene &scene = kScenes[scene_];
        ui::text(list, fonts.display, "AFTERGLOW", kMargin, 108.0f, 26.0f, kDim, gfx::Align::left,
                 9.0f);
        // The scene's name large, its pack and number small.
        ui::text(list, fonts.semibold, scene.name, kMargin, 162.0f, 40.0f, kInk);
        char counter[48];
        std::snprintf(counter, sizeof(counter), "%s  %02d / %02d", scene.pack, scene_ + 1,
                      kSceneCount);
        const float name_w = fonts.semibold.measure(scene.name, 40.0f);
        ui::text(list, fonts.semibold, counter, kMargin + name_w + 24.0f, 160.0f, 16.0f, kDim,
                 gfx::Align::left, 3.0f);
        const std::string details = std::string(kPalettes[palette_].name) + "   /   " +
                                    kLookNames[look_] + "   /   Trip: " + kTripNames[trip_];
        ui::text(list, fonts.regular, details, kMargin, 200.0f, 21.0f, kDim);

        // Right: the mode, the tempo, where the sound comes from.
        float y = 104.0f;
        if (mode_ != kManual)
        {
            const char *mode = mode_ == kAutoVj ? "AUTO-VJ" : "SHOWCASE";
            const Color accent = Color::rgb(current_skin().accent);
            const float w = fonts.semibold.measure(mode, 16.0f, 3.0f) + 30.0f;
            list.rounded_rect({kRight - w, y - 22.0f, w, 32.0f}, 16.0f, accent.with_alpha(0.22f));
            list.bordered_rect({kRight - w, y - 22.0f, w, 32.0f}, 16.0f, kClear, 1.5f, accent);
            ui::text(list, fonts.semibold, mode, kRight - 15.0f, y, 16.0f, kInk, gfx::Align::right,
                     3.0f);
            y += 42.0f;
        }
        char status[96];
        if (bpm_ > 0.0f)
            std::snprintf(status, sizeof(status), "%.0f BPM   %s", static_cast<double>(bpm_),
                          live_ ? "LIVE AUDIO" : "DEMO SIGNAL");
        else
            std::snprintf(status, sizeof(status), "%s", live_ ? "LIVE AUDIO" : "DEMO SIGNAL");
        ui::text(list, fonts.mono, status, kRight, y, 19.0f, kDim, gfx::Align::right);
        y += 28.0f;
        if (context_.settings.show_fps || !live_)
        {
            char fps[32];
            std::snprintf(fps, sizeof(fps), "%.0f FPS",
                          static_cast<double>(context_.telemetry.fps));
            ui::text(list, fonts.mono, fps, kRight, y, 19.0f, kDim, gfx::Align::right);
            y += 28.0f;
        }
        if (sleep_left_ > 0.0f)
            ui::text(list, fonts.mono, "SLEEP " + clock_text(sleep_left_), kRight, y, 19.0f, kDim,
                     gfx::Align::right);

        // A small live spectrum: proof that the picture is listening.
        const auto &levels = analyzer_.levels();
        constexpr float kBarWidth = 10.0f;
        constexpr float kBarGap = 5.0f;
        constexpr float kBarHeight = 34.0f;
        const float left = kRight - 16.0f * (kBarWidth + kBarGap) + kBarGap;
        for (int i = 0; i < 16; ++i)
        {
            const float h = 3.0f + kBarHeight * levels.bands[static_cast<std::size_t>(i)];
            const Color c = gfx::mix(colours_[2], colours_[3], static_cast<float>(i) / 15.0f);
            list.rounded_rect(
                {left + static_cast<float>(i) * (kBarWidth + kBarGap), 236.0f - h, kBarWidth, h},
                2.0f, c);
        }

        // What is playing, Winamp-style.
        {
            const audio::RadioControl *radio = audio::radio();
            const audio::PlayerStatus now =
                radio != nullptr ? radio->status() : audio::PlayerStatus{};
            draw_now_playing(list, fonts,
                             {kMargin, 836.0f, gfx::kVirtualWidth - 2.0f * kMargin, 118.0f}, now,
                             playing_station(radio, now), pulse(), 0);
        }

        static constexpr ui::Hint kHints[] = {
            {ui::Button::circle, "Radio"},
            {ui::Button::l1, "Station", ui::Button::r1},
            {ui::Button::cross, "Scenes"},
            {ui::Button::square, "Like"},
            {ui::Button::l2, "Palette"},
            {ui::Button::dpad, "Scene / Trip"},
            {ui::Button::right_stick, "Auto-VJ"},
            {ui::Button::left_stick, "Pause"},
            {ui::Button::triangle, "Hide (hold: Command)"},
            {ui::Button::options, "Settings"},
        };
        ui::HintLayout layout;
        layout.size = 30.0f;
        layout.text_size = 20.0f;
        layout.cy = 1012.0f;
        layout.item_gap = 24.0f;
        ui::draw_hints(list, fonts, ui::GlyphStyle::dark(), kHints,
                       static_cast<int>(std::size(kHints)), kMargin, false, layout);
        list.pop_opacity();
    }

    // A station change, announced like a TV channel ident.
    void draw_ident(gfx::DrawList &list) const
    {
        const audio::RadioControl *radio = audio::radio();
        if (radio == nullptr || ident_station_ < 0 ||
            ident_station_ >= static_cast<int>(radio->stations().size()))
            return;
        const audio::Station &station = radio->stations()[static_cast<std::size_t>(ident_station_)];
        const GenreSkin &skin = skin_for(station.category);
        const float age = kIdentSeconds - ident_;
        const float in = std::min(1.0f, age / 0.35f);
        const float out = std::min(1.0f, ident_ / 0.6f);
        const float alpha = in * out;
        const float slide =
            context_.settings.reduced_motion ? 0.0f : (1.0f - in) * (1.0f - in) * 80.0f;
        const Rect band{0.0f, 380.0f, gfx::kVirtualWidth, 300.0f};
        list.push_clip(band);
        draw_skin_room(list, band, skin, alpha, pulse());
        list.pop_clip();
        list.gradient_rect({0.0f, band.y - 70.0f, gfx::kVirtualWidth, 70.0f}, 0.0f, kClear,
                           Color::rgb(skin.bg0, 0.7f * alpha));
        list.gradient_rect({0.0f, band.y + band.h, gfx::kVirtualWidth, 70.0f}, 0.0f,
                           Color::rgb(skin.bg0, 0.7f * alpha), kClear);
        list.push_transform(1.0f, 0.0f, 0.0f, -slide, 0.0f);
        const std::string line = station.name + "  /  " + station.style + "  /  " + station.country;
        draw_banner(list, context_.fonts,
                    {kMargin, 440.0f, gfx::kVirtualWidth - 2.0f * kMargin, 190.0f}, skin, "", line,
                    alpha, pulse());
        list.pop_transform();
    }

    // A new song, announced: its cover, title and artist in the station's
    // skin. Corner: a card low on the left. Center: a band across the screen.
    void draw_reveal(gfx::DrawList &list) const
    {
        const audio::RadioControl *radio = audio::radio();
        if (radio == nullptr || reveal_station_ < 0 ||
            reveal_station_ >= static_cast<int>(radio->stations().size()))
            return;
        const audio::Station &station =
            radio->stations()[static_cast<std::size_t>(reveal_station_)];
        const GenreSkin &skin = skin_for(station.category);
        const ui::Fonts &fonts = context_.fonts;
        const ui::FontRef &face = skin_face(fonts, skin.face);
        const bool still = context_.settings.reduced_motion;
        const float age = kRevealSeconds - reveal_;
        const float in = std::min(1.0f, age / 0.5f);
        const float ease_in = 1.0f - (1.0f - in) * (1.0f - in) * (1.0f - in);
        const float alpha = ease_in * std::min(1.0f, reveal_ / 0.6f);
        // The title writes itself in, left to right, just after the card.
        const float wipe = still ? 1.0f : std::clamp((age - 0.25f) / 0.7f, 0.0f, 1.0f);
        std::string artist;
        std::string title;
        if (!net::CoverArt::split_song(reveal_title_, &artist, &title))
        {
            title = reveal_title_;
            artist = station.name;
        }
        audio::PlayerStatus shown;
        shown.source = audio::Source::radio;
        shown.state = audio::RadioState::playing;
        shown.station = reveal_station_;
        shown.title = reveal_title_;
        const Color accent = Color::rgb(skin.accent, alpha);
        const Color ink = kInk.with_alpha(alpha);
        const Color dim = kDim.with_alpha(0.64f * alpha);
        const bool liked = radio->is_liked(reveal_title_);
        char tempo[24] = "";
        if (bpm_ > 0.0f)
            std::snprintf(tempo, sizeof(tempo), "  /  %.0f BPM", static_cast<double>(bpm_));
        const std::string footer = ui::upper(station.name) + tempo;
        const bool cd = skin.motif == Motif::gloss || skin.motif == Motif::laser ||
                        skin.motif == Motif::halo || skin.motif == Motif::frost;
        const float spin = still ? 0.0f : static_cast<float>(std::fmod(clock_, 3600.0)) * 1.6f;

        if (reveal_mode_ == kRevealCenter)
        {
            const Rect band{0.0f, 330.0f, gfx::kVirtualWidth, 420.0f};
            list.push_clip(band);
            draw_skin_room(list, band, skin, alpha, pulse());
            list.pop_clip();
            list.gradient_rect({0.0f, band.y - 80.0f, gfx::kVirtualWidth, 80.0f}, 0.0f, kClear,
                               Color::rgb(skin.bg0, 0.75f * alpha));
            list.gradient_rect({0.0f, band.y + band.h, gfx::kVirtualWidth, 80.0f}, 0.0f,
                               Color::rgb(skin.bg0, 0.75f * alpha), kClear);
            const float slide = still ? 0.0f : (1.0f - ease_in) * 70.0f;
            const Rect cover{260.0f - slide, 390.0f, 300.0f, 300.0f};
            draw_disc(list, cover.x + 150.0f + ease_in * 120.0f, cover.y + 150.0f, 141.0f, skin,
                      spin, cd, alpha);
            draw_cover(list, fonts, cover, skin, shown, &station, pulse(), alpha);
            const float tx = 690.0f + slide;
            const float tw = kRight - tx;
            const float head_size = 22.0f * skin.title_scale / 0.8f;
            const TextBlock head = fit_line(face, skin_title(skin, skin.title) + "  /  NOW PLAYING",
                                            head_size, tw - 60.0f, 0.75f, 4.0f);
            ui::text(list, face, head.lines.front(), tx, 450.0f, head.size, accent,
                     gfx::Align::left, 4.0f);
            const TextBlock block = fit_block(face, skin_title(skin, title),
                                              96.0f * skin.title_scale / 0.8f, tw, 2, 0.5f);
            const float line_h = block.size * 1.05f;
            float base = 476.0f + block.size * 0.96f;
            list.push_clip({tx - 10.0f, 440.0f, (tw + 20.0f) * wipe,
                            40.0f + line_h * static_cast<float>(block.lines.size())});
            for (const std::string &line : block.lines)
            {
                ui::text(list, face, line, tx, base, block.size, ink);
                base += line_h;
            }
            list.pop_clip();
            base += 64.0f - line_h;
            const TextBlock by = fit_line(fonts.semibold, artist, 40.0f, tw);
            ui::text(list, fonts.semibold, by.lines.front(), tx, base, by.size, dim);
            const TextBlock foot = fit_line(fonts.mono, footer, 22.0f, tw, 0.8f, 2.0f);
            ui::text(list, fonts.mono, foot.lines.front(), tx, base + 54.0f, foot.size, dim,
                     gfx::Align::left, 2.0f);
            if (liked)
                draw_heart(list, kRight - 20.0f, 446.0f, 30.0f, accent);
            return;
        }

        // The corner card: above the now-playing bar while the overlay shows.
        // A long title wraps onto a second line and the card grows upwards.
        const float card_w = 860.0f;
        const float text_w = card_w - 16.0f - 164.0f - 26.0f - 28.0f;
        const TextBlock block = fit_block(face, skin_title(skin, title),
                                          52.0f * skin.title_scale / 0.8f, text_w, 2, 0.55f);
        const float line_h = block.size * 1.08f;
        // Baselines measured from the card's top.
        const float first = 56.0f + block.size;
        const float last = first + line_h * static_cast<float>(block.lines.size() - 1);
        const float artist_at = last + 38.0f;
        const float footer_at = artist_at + 32.0f;
        const float card_h = std::max(196.0f, footer_at + 20.0f);
        // It moves out of the way as soon as the overlay starts to show,
        // so the two never sit on top of each other.
        const float lift = std::min(1.0f, hud_alpha_ * 4.0f);
        const float y = 824.0f - 214.0f * lift * (2.0f - lift) + 196.0f - card_h;
        const float slide = still ? 0.0f : (1.0f - ease_in) * -70.0f;
        const Rect card{kMargin + slide, y, card_w, card_h};
        const float radius = std::min(skin.radius, 22.0f);
        list.shadow(card, radius, 30.0f, Color::rgb(0x000000, 0.45f * alpha));
        list.rounded_rect(card, radius, Color::rgb(skin.bg0, 0.86f * alpha));
        list.gradient_rect_h(card, radius, Color::rgb(skin.bg1, 0.7f * alpha),
                             Color::rgb(skin.bg0, 0.0f));
        list.bordered_rect(card, radius, kClear, 1.5f, Color::rgb(0xffffff, 0.14f * alpha));
        list.rounded_rect({card.x, card.y + radius, 5.0f, card.h - radius * 2.0f}, 2.5f, accent);
        // Time left, as a line along the bottom.
        const float left = std::clamp(reveal_ / kRevealSeconds, 0.0f, 1.0f);
        list.rounded_rect(
            {card.x + radius, card.y + card.h - 4.0f, (card.w - radius * 2.0f) * left, 2.0f}, 1.0f,
            accent.with_alpha(0.7f * alpha));
        const Rect cover{card.x + 16.0f, card.y + (card.h - 164.0f) * 0.5f, 164.0f, 164.0f};
        draw_cover(list, fonts, cover, skin, shown, &station, pulse(), alpha);
        const float tx = cover.x + cover.w + 26.0f;
        const float tw = text_w;
        // The header leaves room for the like button at the right.
        const TextBlock head = fit_line(face, skin_title(skin, skin.title) + "  /  NOW PLAYING",
                                        17.0f * skin.title_scale / 0.8f, tw - 120.0f, 0.75f, 3.0f);
        ui::text(list, face, head.lines.front(), tx, card.y + 46.0f, head.size, accent,
                 gfx::Align::left, 3.0f);
        list.push_clip({tx - 6.0f, card.y + 50.0f, (tw + 12.0f) * wipe, last + 24.0f - 50.0f});
        for (std::size_t i = 0; i < block.lines.size(); ++i)
            ui::text(list, face, block.lines[i], tx,
                     card.y + first + line_h * static_cast<float>(i), block.size, ink);
        list.pop_clip();
        const TextBlock by = fit_line(fonts.semibold, artist, 27.0f, tw);
        ui::text(list, fonts.semibold, by.lines.front(), tx, card.y + artist_at, by.size, dim);
        const TextBlock foot = fit_line(fonts.mono, footer, 16.0f, tw, 0.85f, 2.0f);
        ui::text(list, fonts.mono, foot.lines.front(), tx, card.y + footer_at, foot.size, dim,
                 gfx::Align::left, 2.0f);
        // Liked already, or how to like it.
        const float right = card.x + card.w - 26.0f;
        if (liked)
        {
            draw_heart(list, right - 12.0f, card.y + 40.0f, 24.0f, accent);
        }
        else
        {
            ui::text(list, fonts.semibold, "LIKE", right, card.y + 46.0f, 15.0f, dim,
                     gfx::Align::right, 3.0f);
            const float label_w = fonts.semibold.measure("LIKE", 15.0f, 3.0f);
            ui::draw_button(list, fonts, ui::GlyphStyle::dark(), ui::Button::square,
                            right - label_w - 40.0f, card.y + 40.0f, 28.0f);
        }
    }

    // A short note at the top: a song liked, or why it could not be.
    void draw_toast(gfx::DrawList &list) const
    {
        const float a =
            std::min(1.0f, toast_ / 0.4f) * std::min(1.0f, (kToastSeconds - toast_) / 0.15f);
        const ui::Fonts &fonts = context_.fonts;
        const GenreSkin &skin = current_skin();
        const Color accent = Color::rgb(skin.accent, a);
        const std::string label = toast_liked_ ? "LIKED" : "";
        const float label_w = label.empty() ? 0.0f : fonts.semibold.measure(label, 16.0f, 3.0f);
        const std::string text = fit_text(fonts.semibold, toast_text_, 24.0f, 900.0f);
        const float text_w = fonts.semibold.measure(text, 24.0f);
        const float icon = toast_liked_ ? 40.0f : 0.0f;
        const float gap = label.empty() ? 0.0f : 18.0f;
        const float w = 56.0f + icon + label_w + gap + text_w;
        const float drop = context_.settings.reduced_motion ? 0.0f : (1.0f - a) * -16.0f;
        const Rect pill{gfx::kVirtualWidth * 0.5f - w * 0.5f, 52.0f + drop, w, 60.0f};
        list.shadow(pill, 30.0f, 24.0f, Color::rgb(0x000000, 0.45f * a));
        list.rounded_rect(pill, 30.0f, Color::rgb(skin.bg0, 0.92f * a));
        list.bordered_rect(pill, 30.0f, kClear, 1.5f, accent.with_alpha(0.6f * a));
        float x = pill.x + 28.0f;
        if (toast_liked_)
        {
            draw_heart(list, x + 14.0f, pill.y + 30.0f, 26.0f, accent);
            x += icon;
            ui::text(list, fonts.semibold, label, x, pill.y + 38.0f, 16.0f, accent,
                     gfx::Align::left, 3.0f);
            x += label_w + gap;
        }
        ui::text(list, fonts.semibold, text, x, pill.y + 39.0f, 24.0f, kInk.with_alpha(a));
    }

    static std::string fit_text(const ui::FontRef &font, const std::string &text, float size,
                                float width)
    {
        if (font.measure(text, size) <= width)
            return text;
        std::string cut = text;
        while (!cut.empty() && font.measure(cut + "...", size) > width)
        {
            cut.pop_back();
            while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80)
                cut.pop_back();
            if (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0x80) != 0)
                cut.pop_back();
        }
        return cut + "...";
    }

    void draw_picker(app::Frame &frame) const
    {
        gfx::DrawList &list = frame.overlay;
        frame.glass = true;
        const float in =
            context_.settings.reduced_motion ? 1.0f : std::min(1.0f, picker_age_ / 0.2f);
        const float slide = (1.0f - in) * (1.0f - in) * -60.0f;
        const GenreSkin &skin = current_skin();
        const ui::Fonts &fonts = context_.fonts;
        list.push_opacity(in);
        list.push_transform(1.0f, 0.0f, 0.0f, slide, 0.0f);
        const float radius = std::min(skin.radius, 26.0f);
        list.shadow(kPicker, radius, 40.0f, Color::rgb(0x000000, 0.5f));
        list.glass(frame.glass_texture, kPicker, radius, Color::rgb(skin.bg0, 0.70f));
        list.gradient_rect(kPicker, radius, Color::rgb(skin.bg1, 0.40f),
                           Color::rgb(skin.bg0, 0.2f));
        list.bordered_rect(kPicker, radius, kClear, 1.5f, Color::rgb(0xffffff, 0.14f));
        list.gradient_rect_h({kPicker.x + radius, kPicker.y, kPicker.w - radius * 2.0f, 3.0f}, 1.5f,
                             Color::rgb(skin.accent, 0.0f), Color::rgb(skin.accent));
        ui::text(list, skin_face(fonts, skin.face), skin_title(skin, "Scenes"), kPicker.x + 40.0f,
                 kPicker.y + 78.0f, 46.0f * skin.title_scale / 0.8f, kInk, gfx::Align::left,
                 skin.tracking * 0.3f);
        char line[96];
        std::snprintf(line, sizeof(line), "%d SCENES  /  6 PACKS  /  %d FAVORITES", kSceneCount,
                      favorite_count());
        ui::text(list, fonts.semibold, line, kPicker.x + 40.0f, kPicker.y + 120.0f, 15.0f, kDim,
                 gfx::Align::left, 3.0f);
        ui::Canvas canvas{list, fonts, frame.glass_texture, static_cast<float>(clock_)};
        picker_.draw(canvas);
        static constexpr ui::Hint kHints[] = {
            {ui::Button::cross, "Choose"},
            {ui::Button::square, "Favorite"},
            {ui::Button::circle, "Back"},
        };
        ui::HintLayout layout;
        layout.size = 30.0f;
        layout.text_size = 19.0f;
        layout.cy = kPicker.y + kPicker.h - 44.0f;
        layout.item_gap = 26.0f;
        ui::draw_hints(list, fonts, ui::GlyphStyle::dark(), kHints,
                       static_cast<int>(std::size(kHints)), kPicker.x + 40.0f, false, layout);
        list.pop_transform();
        list.pop_opacity();
    }

    // Big type for a room: the station and the song lower left, the clock
    // and the tempo upper right.
    void draw_party(gfx::DrawList &list) const
    {
        const ui::Fonts &fonts = context_.fonts;
        const audio::RadioControl *radio = audio::radio();
        const audio::PlayerStatus status =
            radio != nullptr ? radio->status() : audio::PlayerStatus{};
        const audio::Station *station = playing_station(radio, status);
        const GenreSkin &skin = station != nullptr ? skin_for(station->category) : default_skin();
        list.gradient_rect({0.0f, 600.0f, gfx::kVirtualWidth, 480.0f}, 0.0f, kClear,
                           Color::rgb(0x000000, 0.65f));
        const ui::FontRef &face = skin_face(fonts, skin.face);
        const std::string name = station != nullptr ? station->name : "Afterglow Mix";
        const std::string shown = skin_title(skin, name);
        const Rect cover{kMargin, 700.0f, 270.0f, 270.0f};
        const float tx = cover.x + cover.w + 44.0f;
        const TextBlock big =
            fit_line(face, shown, 112.0f * skin.title_scale / 0.8f, kRight - tx, 0.45f);
        draw_cover(list, fonts, cover, skin, status, station, pulse(), 1.0f);
        ui::text(list, face, skin_title(skin, skin.title), tx, 760.0f,
                 28.0f * skin.title_scale / 0.8f, Color::rgb(skin.accent), gfx::Align::left, 4.0f);
        ui::text(list, face, big.lines.front(), tx, 872.0f, big.size, kInk);
        const std::string song =
            status.title.empty() ? (station != nullptr ? station->style : "") : status.title;
        // Two lines at most, under the station, above the bottom edge.
        const TextBlock lines = fit_block(fonts.semibold, song, 42.0f, kRight - tx, 2, 0.7f);
        float base = 944.0f - (lines.lines.size() > 1 ? lines.size * 0.55f : 0.0f);
        for (const std::string &line : lines.lines)
        {
            ui::text(list, fonts.semibold, line, tx, base, lines.size, kDim);
            base += lines.size * 1.15f;
        }
        // The clock (the console's local time) and the tempo.
        char clock[16] = "";
        const std::time_t now = std::time(nullptr);
        if (const std::tm *local = now > 0 ? std::localtime(&now) : nullptr)
            std::snprintf(clock, sizeof(clock), "%02d:%02d", local->tm_hour, local->tm_min);
        ui::text(list, fonts.display, clock, kRight, 170.0f, 104.0f, kInk, gfx::Align::right, 4.0f);
        if (bpm_ > 0.0f)
        {
            char tempo[24];
            std::snprintf(tempo, sizeof(tempo), "%.0f BPM", static_cast<double>(bpm_));
            ui::text(list, fonts.mono, tempo, kRight, 222.0f, 30.0f,
                     Color::rgb(skin.accent).with_alpha(0.6f + 0.4f * analyzer_.levels().beat),
                     gfx::Align::right);
        }
    }

    void draw_sheet(app::Frame &frame) const
    {
        gfx::DrawList &list = frame.overlay;
        frame.glass = true;
        const float in =
            context_.settings.reduced_motion ? 1.0f : std::min(1.0f, sheet_age_ / 0.2f);
        const float slide = (1.0f - in) * (1.0f - in) * 60.0f;
        const GenreSkin &skin = current_skin();
        list.push_opacity(in);
        list.push_transform(1.0f, 0.0f, 0.0f, slide, 0.0f);
        const float radius = std::min(skin.radius, 26.0f);
        list.shadow(kSheet, radius, 40.0f, Color::rgb(0x000000, 0.5f));
        list.glass(frame.glass_texture, kSheet, radius, Color::rgb(skin.bg0, 0.74f));
        list.gradient_rect(kSheet, radius, Color::rgb(skin.bg1, 0.45f), Color::rgb(skin.bg0, 0.2f));
        list.bordered_rect(kSheet, radius, kClear, 1.5f, Color::rgb(0xffffff, 0.14f));
        list.gradient_rect_h({kSheet.x + radius, kSheet.y, kSheet.w - radius * 2.0f, 3.0f}, 1.5f,
                             Color::rgb(skin.accent, 0.0f), Color::rgb(skin.accent));
        const ui::Fonts &fonts = context_.fonts;
        ui::text(list, skin_face(fonts, skin.face), skin_title(skin, "Settings"), kSheet.x + 40.0f,
                 kSheet.y + 78.0f, 46.0f * skin.title_scale / 0.8f, kInk, gfx::Align::left,
                 skin.tracking * 0.3f);
        ui::draw_button(list, fonts, ui::GlyphStyle::dark(), ui::Button::options,
                        kSheet.x + kSheet.w - 150.0f, kSheet.y + 52.0f, 28.0f);
        ui::text(list, fonts.semibold, "CLOSE", kSheet.x + kSheet.w - 40.0f, kSheet.y + 72.0f,
                 15.0f, kDim, gfx::Align::right, 3.0f);
        ui::Canvas canvas{list, fonts, frame.glass_texture, static_cast<float>(clock_)};
        sheet_.draw(canvas);
        draw_meter(list, {kSheet.x + 40.0f, kSheet.y + kSheet.h - 74.0f, kSheet.w - 80.0f, 50.0f},
                   skin);
        list.pop_transform();
        list.pop_opacity();
    }

    // What leaves the console: the output level in segments, and how hard
    // the limiter is holding it, so Loudness can be set by eye.
    void draw_meter(gfx::DrawList &list, const Rect &r, const GenreSkin &skin) const
    {
        const ui::Fonts &fonts = context_.fonts;
        list.rounded_rect({r.x, r.y - 14.0f, r.w, 1.0f}, 0.0f, Color::rgb(0xffffff, 0.10f));
        ui::text(list, fonts.semibold, "OUTPUT", r.x, r.y + 20.0f, 15.0f, kDim, gfx::Align::left,
                 3.0f);
        const bool live = audio::main_mixer() != nullptr;
        constexpr int kSegments = 28;
        const float x0 = r.x + 110.0f;
        const float w = r.w - 110.0f - 150.0f;
        const float seg = w / static_cast<float>(kSegments);
        // The meter reads in dB: -36 dB at the left, full scale at the right.
        const float db = 20.0f * std::log10(std::max(meter_, 1e-4f));
        const float lit = std::clamp((db + 36.0f) / 36.0f, 0.0f, 1.0f) * kSegments;
        for (int i = 0; i < kSegments; ++i)
        {
            const float k = static_cast<float>(i) / static_cast<float>(kSegments - 1);
            const Color on = k > 0.92f   ? Color::rgb(0xff5a5f)
                             : k > 0.75f ? Color::rgb(0xffd60a)
                                         : Color::rgb(skin.accent);
            const bool is_lit = static_cast<float>(i) < lit;
            list.rounded_rect({x0 + static_cast<float>(i) * seg, r.y + 4.0f, seg - 4.0f, 18.0f},
                              2.0f, is_lit ? on : Color::rgb(0xffffff, 0.08f));
        }
        char status[48];
        if (!live)
            std::snprintf(status, sizeof(status), "NO AUDIO HERE");
        else if (meter_limit_ > 0.02f)
            std::snprintf(
                status, sizeof(status), "LIMITING %.0f dB",
                static_cast<double>(-20.0f * std::log10(std::max(0.01f, 1.0f - meter_limit_))));
        else
            std::snprintf(status, sizeof(status), "%s", meter_ > 0.01f ? "CLEAN" : "QUIET");
        ui::text(list, fonts.semibold, status, r.x + r.w, r.y + 20.0f, 15.0f,
                 meter_limit_ > 0.25f ? Color::rgb(0xffd60a) : kDim, gfx::Align::right, 2.0f);
    }

    void draw_sleep_end(gfx::DrawList &list) const
    {
        const float a = std::min(1.0f, sleep_end_);
        const ui::Fonts &fonts = context_.fonts;
        ui::text(list, fonts.display, "GOOD NIGHT", gfx::kVirtualWidth * 0.5f, 540.0f, 64.0f,
                 kInk.with_alpha(a), gfx::Align::center, 12.0f);
        ui::text(list, fonts.regular, "The sleep timer stopped the music",
                 gfx::kVirtualWidth * 0.5f, 600.0f, 26.0f, kDim.with_alpha(a * 0.64f),
                 gfx::Align::center);
    }

    // The opening: a sliced sun rises behind the name, then the scene fades in.
    void draw_intro(gfx::DrawList &list) const
    {
        const float t = intro_;
        const float fade_out = std::clamp((kIntroSeconds - t) / 0.6f, 0.0f, 1.0f);
        list.rounded_rect({0.0f, 0.0f, gfx::kVirtualWidth, gfx::kVirtualHeight}, 0.0f,
                          Color::rgb(0x07030c, fade_out));
        list.push_opacity(fade_out);
        const ui::Fonts &fonts = context_.fonts;
        const GenreSkin &skin = default_skin();
        const float cx = gfx::kVirtualWidth * 0.5f;
        const float rise = std::clamp(t / 1.4f, 0.0f, 1.0f);
        const float e = 1.0f - (1.0f - rise) * (1.0f - rise) * (1.0f - rise);
        const float horizon = 560.0f;
        const float radius = 170.0f;
        list.push_clip({0.0f, 0.0f, gfx::kVirtualWidth, horizon});
        list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, (1.0f - e) * 260.0f);
        list.glow({cx - radius, 450.0f - radius, radius * 2.0f, radius * 2.0f}, radius, 120.0f,
                  Color::rgb(skin.accent, 0.30f));
        sliced_sun(list, cx, 450.0f, radius, Color::rgb(skin.accent2), Color::rgb(skin.accent));
        list.pop_transform();
        list.pop_clip();
        list.rounded_rect({cx - 420.0f * e, horizon - 1.0f, 840.0f * e, 2.0f}, 0.0f,
                          Color::rgb(skin.accent, 0.8f));
        const float word = std::clamp((t - 0.5f) / 1.0f, 0.0f, 1.0f);
        const float we = word * word * (3.0f - 2.0f * word);
        ui::text(list, fonts.display, "AFTERGLOW", cx, 680.0f, 84.0f, kInk.with_alpha(we),
                 gfx::Align::center, 10.0f + (1.0f - we) * 40.0f);
        const float tag = std::clamp((t - 1.2f) / 0.8f, 0.0f, 1.0f);
        ui::text(list, fonts.semibold, "RADIO  +  VISUALS  FOR  PS5", cx, 740.0f, 22.0f,
                 kDim.with_alpha(tag), gfx::Align::center, 8.0f);
        list.pop_opacity();
    }

    // Shown once, before the first scene: a fair warning about flashing.
    void draw_notice(gfx::DrawList &list) const
    {
        const ui::Fonts &fonts = context_.fonts;
        list.rounded_rect({0.0f, 0.0f, gfx::kVirtualWidth, gfx::kVirtualHeight}, 0.0f,
                          Color::rgb(0x05030a, 0.82f));
        const Rect card{460.0f, 300.0f, 1000.0f, 470.0f};
        list.rounded_rect(card, 24.0f, Color::rgb(0x120a1c, 0.96f));
        list.bordered_rect(card, 24.0f, kClear, 1.5f, Color::rgb(0xffffff, 0.16f));
        list.gradient_rect_h({card.x + 24.0f, card.y, card.w - 48.0f, 3.0f}, 1.5f,
                             Color::rgb(0xff5a36, 0.0f), Color::rgb(0xff5a36));
        ui::text(list, fonts.semibold, "BEFORE YOU START", card.x + 60.0f, card.y + 80.0f, 18.0f,
                 Color::rgb(0xff5a36), gfx::Align::left, 4.0f);
        ui::text(list, fonts.display, "Flashing lights", card.x + 60.0f, card.y + 150.0f, 50.0f,
                 kInk);
        ui::paragraph(list, fonts.regular,
                      "Some scenes flash, strobe and move quickly with the music. If you or anyone "
                      "watching is sensitive to flashing lights, open Settings with Options and "
                      "turn Flashing off, or set the Trip level to Calm.",
                      card.x + 60.0f, card.y + 210.0f, 26.0f, card.w - 120.0f, 38.0f, kDim, 5);
        ui::draw_button(list, fonts, ui::GlyphStyle::dark(), ui::Button::cross, card.x + 60.0f,
                        card.y + card.h - 70.0f, 34.0f);
        ui::text(list, fonts.semibold, "Got it", card.x + 108.0f, card.y + card.h - 44.0f, 24.0f,
                 kInk);
    }

    app::Context &context_;
    audio::BandAnalyzer analyzer_;
    audio::SpectrumAnalyzer spectrum_;
    std::vector<float> samples_;
    std::array<float, 64> wave_{};
    std::uint64_t last_written_ = 0;
    float live_hold_ = 0.0f;
    bool live_ = false;

    double clock_ = 0.0;
    double scene_clock_ = 0.0;
    int scene_ = 0;
    int palette_ = kSignature;
    std::array<Color, 4> colours_{};
    Color light_;
    float light_timer_ = 0.0f;
    float travel_ = 0.0f;
    float spin_ = 0.0f;
    float heading_ = 0.0f;
    std::array<float, 16> peaks_{};
    int from_scene_ = 0;
    float blend_ = 1.0f;
    int look_ = 0;
    int trip_ = 1;
    bool flashing_ = true;
    float flash_ = 0.0f;
    float steer_x_ = 0.0f;
    float steer_y_ = 0.0f;
    float drive_ = 0.0f;
    float gain_ = 1.0f;
    float fade_ = 1.0f;

    bool hud_hidden_ = false;
    float north_held_ = 0.0f;
    bool north_long_ = false;
    bool hidden_before_hold_ = false;
    Command pending_command_;
    bool has_command_ = false;
    float hud_timer_ = kHudIdleSeconds;
    float hud_alpha_ = 1.0f;
    int mode_ = kManual;
    float showcase_timer_ = 0.0f;
    float vj_timer_ = 0.0f;
    int vj_step_ = -1;
    float bass_fast_ = 0.0f;
    float bass_slow_ = 0.0f;
    float breakdown_ = 0.0f;
    int beats_ = 0;
    float bpm_ = 0.0f;
    audio::BeatTracker beat_;
    std::array<float, 16> last_raw_{};
    bool beat_sync_ = true;
    bool phrase_ = false;
    float tempo_pace_ = 1.0f;
    bool rumble_ = false;
    int display_ = kNormal;
    float sleep_left_ = 0.0f;
    float sleep_end_ = 0.0f;
    float intro_ = 0.0f;
    bool notice_ = false;
    bool loaded_ = false;

    float ident_ = 0.0f;
    int ident_station_ = -1;
    int last_station_ = -2;
    std::string last_song_;

    int reveal_mode_ = kRevealCorner;
    bool reveal_pending_ = false;
    float reveal_ = 0.0f;
    std::string reveal_title_;
    int reveal_station_ = -1;
    float toast_ = 0.0f;
    std::string toast_text_;
    bool toast_liked_ = false;
    int loudness_ = 0;
    int eq_ = 0;
    float meter_ = 0.0f;
    float meter_limit_ = 0.0f;
    int vj_set_ = kVjGenre; // Genre, Favorites or Calm
    std::array<bool, kSceneCount> favorites_{};

    RadioScreen radio_{context_};
    ui::Form sheet_;
    bool sheet_open_ = false;
    float sheet_age_ = 0.0f;
    ui::ListView picker_;
    bool picker_open_ = false;
    float picker_age_ = 0.0f;
    int picker_from_ = 0;
};

} // namespace

std::unique_ptr<app::Concept> make_afterglow(app::Context &context)
{
    return std::make_unique<Afterglow>(context);
}

bool afterglow_has_scene(const char *name)
{
    return scene_index(name) >= 0;
}

bool afterglow_command(app::Concept &design, const std::string &text)
{
    // Only ever given what make_afterglow made (there is no RTTI to check).
    auto &afterglow = static_cast<Afterglow &>(design);
    return afterglow.type_command(text);
}

} // namespace hui::concepts
