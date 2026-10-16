# Afterglow

A native PS5 homebrew music visualiser (title ID PPSA47770), built on a fork
of BlackBearReloaded's ps5-homebrew-ui (OpenGL 4.6 kit, GPL-3.0-or-later).
Target: a PS5 on firmware 13.60 running the Relapse jailbreak. Goal: a
listing on homebrew.page.

## What is ours (start here)

- `src/concepts/afterglow.cpp`: the visualiser screen. 56 scenes in six
  packs (AFTERGLOW, TRIP, CLUB, GALLERY, CALM, RETRO), each with its signature palette,
  finish, pixel height and (trip presets) feedback trail; palettes
  (Signature, Album art, Genre, five fixed); looks (L2); trip level (D-pad
  up/down); Auto-VJ (R3) with drop detection and BPM; station idents;
  the settings sheet (Options, a `ui::Form`); Party and Ambient displays;
  sleep timer; the intro and the one-time flashing-lights notice. Its
  preferences live in the radio store (`RadioControl::setting`).
- `src/concepts/genre_skin.*`: the 17 genre skins plus the default (face,
  colours, motif, list styling, tagline, the scenes Auto-VJ uses), the drawn
  motifs (`draw_banner`, `draw_skin_room`, `draw_generated_cover`,
  `draw_disc`, `sliced_sun`) and `SkinMorph`. Fonts: `kGenreFontFiles`,
  loaded into `ui::Fonts::extra` (font slots 7 to 12).
- `src/concepts/radio_screen.*`: the radio browser (skinned banner, channels,
  stations, now-playing card with cover and spinning disc, search keyboard,
  favourites, check, working-only) and `draw_now_playing()` / `draw_cover()`.
- `src/net/cover_art.*`: album art (MusicBrainz search, Cover Art Archive,
  stb_image decode, disk cache in `<data>/art`, LRU of 12 textures, 1 request
  a second). `src/net/json.*`: a small JSON reader.
- `src/gfx/backdrop.cpp`: scenes 11 to 13 and 30 to 37 in the first program;
  `backdrop_ported.hpp` the React Bits ports (40 to 42);
  `backdrop_trip.hpp` the trip pack (50 to 61) in a second program.
  `backdrop_spec.hpp`: audio uniforms, the 64-point `wave`, `PostFx`,
  `FeedbackFx`, cross-fade fields.
- `src/gfx/renderer.cpp`: `play_finished()` (bloom, fringe, grain,
  scanlines, CRT, low pixel heights, cross-fades) and the feedback pass
  (two RGBA16F trails, RGBA8 fallback). `gl_batch.cpp`: 12 font slots;
  empty clips draw nothing.
- `src/audio/radio.*`: stations, the player, favourites, recents, check,
  key/value settings, all in one store file. `stream_decoder.*`, `icy.*`.
- `src/net/http.*` and `src/platform/ps5/net_ps5.cpp` (libcurl + OpenSSL,
  `OpenResult::status`), `console_curl.c`, `aac_faad.cpp`.
- `src/main.cpp`: fonts, radio (resumes the last station), cover art.
- `host/preview_radio.hpp`: a stand-in radio and canned album art for the
  PC preview. `host/snapshot_main.cpp`: `HUI_WARM=<frames>` grows feedback
  trails before each picture; `HUI_POSTER=<png>[,center]` renders the
  home-screen backgrounds.
- `tools/png-to-dds.py` (BC7 DDS on Linux), `tools/check-stations.py`.
- Tests: `afterglow_test.cpp` (scenes, trails, trip level, settings, Auto-VJ,
  skins, radio browser, idents), `cover_art_test.cpp` (JSON, art lookup and
  cache), `radio_test.cpp`.

## Status

- Version 01.000.006. Builds for the PS5 with no warnings; 514 unit tests
  pass with ASan/UBSan; lint and clang-tidy pass.
- 1.0.6 added: fixes (subtitle contrast under a filled highlight in
  `ListView`; the controls fade under the radio and are back after it;
  Triangle shows controls that faded; the song card shows while the controls
  are hidden; `text_fit.hpp` shrinks, wraps and cuts every card's text);
  `audio/beat_tracker.*` (tempo by autocorrelation, phase by comb, a beat
  clock in `BackdropSpec::aux[3]`, pref `beatsync`); the Calm (70-79) and
  Gallery (80-89) packs in `gfx/backdrop_gallery.hpp`, two more programs;
  scenes reordered best first, Paint Swirl the default, the scene kept by
  name (pref `scenename`); Auto-VJ "Calm"; four genres (Latin, K-Pop,
  Afrobeats, Arabic: `Motif::papel/lightstick/kente/arabesque`) and 67 more
  stations (222); station logos (a 7th field in stations.txt,
  `parse_logos`, `RadioControl::logo`, `net::station_logos()`, a CoverArt in
  `ArtSource::images` mode, ICO/GIF/BMP decoding); crossfade (two radio
  decks on mixer slots 1 and 2, `Mixer::kStreams` 3, setting `xfade`);
  `concepts/commands.*` (the command parser) with the command box (hold
  Triangle, or Done in the radio search); `audio/library.*` (My Music: USB
  scan, ID3/Vorbis tags, embedded covers, `file://` stations, auto-advance);
  Ogg Vorbis (stb_vorbis push API) and Ogg Opus (vendored libopus 1.5.2) in
  `OggStream`, with chains and in-stream titles.
- PC snapshots of every scene, every genre skin, the radio lists with logos,
  the song card with a long title and the command box were reviewed.
- NOT verified on a console: anything from 1.0.6 (frame rate of the calm and
  gallery programs at 4K, USB mount paths and scanning speed, Ogg/Opus
  stations, logos over libcurl, the crossfade by ear, beat sync on real
  music), plus from earlier: loudness by ear, album art lookups, the 12 font
  slots, the RGBA16F trails, `std::localtime`.
- The radio plays on the console (owner confirmed for 1.0.3).

## Next milestones

1. Console check of 1.0.6 (the list above); then release.
2. VJ Deck and Room Lights (1.1); Devanagari, Hangul and Arabic titles.
3. Our own snd0.at9 (needs an ATRAC9 encoder; the kit's tool runs on Windows).
4. Release: tagged GitHub release, catalog pull request adding only
   `apps/PPSA47770.json`. ProsperoRadio (PPSA99001) is a general radio app
   and CD Player (PPSA98427) plays CDs: Afterglow's angle is curated
   electronic channels, genre skins and full-screen visuals.

## Rules for this project

- No features for downloading, installing or mounting games or packages.
- No Sony SDK files, firmware modules or keys. Public payload SDK only.
- Only music the owner supplies, the kit's own licensed tracks, or radio
  stations' own public streams played live (never copied or re-hosted).
  Never list a station whose terms forbid third-party players (SomaFM).
- Keep every licence header; new files are GPL-3.0-or-later.
- Never launch on, upload to, or touch the console without the owner asking.

## Building on Windows (owner's setup: Docker Desktop, no WSL distro)

```powershell
Get-Content Dockerfile.build | docker build -t afterglow-build -
docker run --rm -v "${PWD}:/work" -w /work afterglow-build make
docker run --rm -v "${PWD}:/work" -w /work afterglow-build tools/host-snapshots.sh build/snapshots afterglow
```

The PC preview inside Docker needs the Mesa packages in Dockerfile.build.

## The kit's own guide

Everything below comes from the kit and still applies (preview on the PC and
look at the pictures before calling a visual change done).

@AGENTS.md
