# Afterglow

Radio and visuals for PS5. A native homebrew app with 222 free internet radio
stations in 17 genres (electronic music first, plus Bollywood & Desi, Latin,
K-Pop, Afrobeats and Arabic), your own songs from a USB drive, and 56
audio-reactive scenes drawn with OpenGL 4.6 that move in time with the music.
The player changes its look with the genre, shows the album art of the song on
air, announces every new song, keeps the songs you like, takes short commands
("play bollywood 90s", "next", "show calm scenes"), and the visuals can run
themselves like a VJ.

> Status: in development. Earlier builds ran on a PS5 (firmware 13.60,
> Relapse) and the radio plays there; this version is built and tested on a
> PC and not yet confirmed on a console. Title ID PPSA47770.

## What is in it

- **Genre skins.** Every channel has its own type, colours, banner and list
  styling: a Teletech-style warehouse look for Techno (red ticker tape,
  corner brackets, glitching title), a spinning record for House, rings of
  light for Trance, hazard tape for Drum & Bass, LFO waves for Bass, strobing
  chevrons for Hard Dance, sweeping lasers for EDM, a sliced sun over a grid
  for Synthwave, drifting bokeh for Chill, bubbles for Pop, halftone and
  paint drips for Hip-Hop, silk for R&B, marigold garlands and a turning
  mandala for Bollywood, cut-paper flags and a blooming flower for Latin, a
  sea of lightsticks for K-Pop, woven kente and a talking drum for
  Afrobeats, an eight-point star lattice and a swinging lantern for Arabic.
  Moving through the channels, the whole screen changes its clothes.
- **Station logos** in the station list and on the now-playing cover, from
  Radio Browser, fetched as they come on screen and cached.
- **Album art.** The song title a station sends is looked up in MusicBrainz
  and the cover comes from the Cover Art Archive (both free and open). It is
  cached on the console. A record (or a CD) slides out of the cover while the
  music plays. Without a cover, the station gets a generated one in its
  genre's style.
- **56 scenes in six packs:** the originals, a Trip pack of psychedelic
  scenes and Milkdrop-style feedback presets, a Club pack, a Gallery pack of
  scenes like art objects (gold-mended pottery, stained glass, paper cuts,
  ferrofluid, a snowflake), a Calm pack for winding down (tide pool, aurora,
  lanterns, rain on glass, moonlit sea, jellyfish) and a Retro pack. Paint
  Swirl opens the app; the best scenes come first in the list.
- **Beat sync:** a beat tracker measures the tempo and where the beats fall;
  the scenes move at the music's pace, pulses land on the beat even where a
  kick is buried, loops last a beat, a bar or a phrase, and Auto-VJ cuts at
  the end of a phrase.
- **Your own music:** songs on a USB drive (MP3, AAC, Ogg Vorbis, Opus) show
  up in *My Music* with their titles, artists, albums and embedded covers,
  and play one after another with the same visuals, song cards and likes.
- **Commands:** hold Triangle over the visuals (or type in the radio's
  search) and write what you want: "play bollywood 90s", "play some k-pop",
  "next station", "like this song", "show trippy scenes", "party mode",
  "sleep in 30 minutes", "turn it up". Everyday names work ("hindi",
  "reggaeton", "dnb", "nineties").
- **Crossfade:** a new station (or the next song from the drive) blends in
  over the one playing; Off, 2, 3 or 5 seconds.
- **Song card:** when a new song starts, a card shows its cover, title and
  artist in the genre's style (in the corner, across the middle, or off),
  also while the controls are hidden. Long titles shrink and wrap to fit.
- **Liked songs and history:** Square over the visuals likes the song on air.
  The radio has a *Liked songs* channel and a *Song history* of the last 50
  songs; Cross on a song tunes in to its station. The liked songs are also
  written to `liked-songs.txt` in the app's storage, to read on a PC.
- **Scene list:** Cross opens all 56 scenes by pack; the one under the focus
  plays behind the list as you move. Square stars a scene, and Auto-VJ can
  use only your starred scenes.
- **Auto-VJ** picks scenes that suit the genre on air and cuts on the drop.
  The tempo (BPM) shows in the corner.
- **Sound:** a Loudness setting (+6 or +12 dB, held by a peak limiter so it
  does not distort), EQ presets (Flat, Bass boost, Club, Vocal) and an output
  meter in the settings. The music volume starts at 8 of 10 (the kit's
  default was far too quiet for radio).
- **Station idents:** a new station is announced in its genre's style.
- **Settings** (Options): mode, trip level, palette (including one taken from
  the album art), look, flashing on or off, display (Normal, Party with big
  type and a clock, Ambient: slow and dim), song card, Auto-VJ scenes (Genre,
  Favorites or Calm), beat sync, a sleep timer, volume, loudness, EQ,
  crossfade, sensitivity, beat rumble and resuming the last station at start.

## Scenes

| Pack | Scenes |
| --- | --- |
| Afterglow | Paint Swirl, Orb, Nebula, Ember Peak, Pulse Tunnel, Cathedral Rays, Hall of Candles, Prism |
| Trip | Wormhole, Mirror Maze, Kaleidoscope, Fractal Bloom, Vortex, Hypnotic, Dimension, Kaleido Melt, Acid Bloom, Echo Orb, Smoke Signals, Silk, Liquid Dream, Starfall, Neon Smear, Spiral Rings, Melting Bars |
| Club | Liquid Chrome, Rave Tunnel, Bubble Pop, Strobe Lasers, Scope, Classic Bars |
| Gallery | Stained Glass, Kintsugi, Paper Cut, Crystal Growth, Coral Labyrinth, Halftone Pop, Lissajous Loom, Origami, Ferrofluid, Ridgelines |
| Calm | Tide Pool, Aurora Veil, Lantern Drift, Moon Tide, Cloud Sea, Deep Sea Glow, Slow Ink, Rain on Glass, Sand Ripples, Fireflies |
| Retro | Skyline, Green Rain, Mode Seven, Polygon 95, Demo Plasma |

Every scene follows 16 spectrum bands, the waveform, the beat and the beat
clock. The
feedback presets draw each frame over the last one, zoomed, turned, warped,
mirrored and faded, so everything leaves trails; the trip level (D-pad up and
down) sets how far they melt. Paint Swirl, Orb and Prism are ported from
React Bits by David Haz (see `THIRD_PARTY_NOTICES.md`).

## Controls

Over the visualiser:

| Button | Action |
| --- | --- |
| Circle | Open the radio |
| L1 / R1 | Previous / next station (or song) |
| L3 | Play / pause |
| Cross | The scene list (Square stars a scene, Circle goes back) |
| D-pad left/right | Next or previous scene |
| D-pad up/down | Trip level: Calm, Normal, Melt |
| Square | Like the song on air (again: unlike) |
| L2 | Next palette |
| R3 | Auto-VJ on or off |
| Left stick | Steer the camera |
| R2 | Drive: push the light up |
| Triangle | Hide or show the overlay (for clean recordings); hold it for the command box |
| Options | Settings |

In the radio:

| Button | Action |
| --- | --- |
| D-pad | Move; left and right switch between channels and stations |
| Cross | Play the station (next/previous then stay in that channel); on a song, tune in to its station |
| Square | Add to or remove from Favorites; on a song, like or unlike it |
| Triangle | Search by name, style, genre or country, or type a command ("play techno") |
| L1 / R1 | Previous / next station |
| R2 / L2 | Play-pause / stop |
| Options | Check every station (in the background) |
| R3 | Show only stations that passed the check |
| Circle | Back to the visualiser |

## Radio

The radio is laid out like a satellite-radio receiver: **channels** on the
left (Favorites, Recently played, Liked songs, Song history, All stations,
My Music, then seventeen genres: Techno, House, Trance, Drum & Bass, Bass &
Dubstep, Hard Dance, EDM & Dance, Synthwave, Chill & Electronica, Pop & Hits,
Hip-Hop & Rap, R&B & Soul, Bollywood & Desi, Latin, K-Pop, Afrobeats,
Arabic), the
**stations** of the channel in the middle, and **now playing** on the right:
the cover, the station, the song, the format, the time on air and the
transport.

The stations are in `assets/radio/stations.txt`, one per line:
`Category | Name | Style | Country | Format | Stream address`, optionally
followed by `| Logo address`. To use your
own list without rebuilding, put a `stations.txt` in the install folder
(`/data/homebrew/PPSA47770/stations.txt`); it is read instead.

- **Formats:** MP3; AAC and AAC+ on the console (FAAD2); Ogg Vorbis
  (stb_vorbis) and Ogg Opus (libopus), chained streams included, with their
  song titles. FLAC and HLS (`.m3u8`) do not play yet.
- **My Music:** the app looks for songs on the USB drives the console
  mounts (`/mnt/usb0` to `/mnt/usb7`), any folder up to six deep, at most
  4,000 songs, and again whenever a drive is plugged in or taken out. MP3,
  AAC (`.aac`), Ogg and Opus play; `.m4a`, FLAC and WAV are not listed yet.
- **Network:** libcurl with OpenSSL on the console, set up the way
  BlackBearReloaded's console-tested apps do (`src/platform/ps5/console_curl.c`).
- **Checking stations:** Options in the radio tries every station and marks
  it green or red; R3 then hides the red ones. From a PC,
  `python tools/check-stations.py --write-good good.txt` does the same.
- **Saved on the console:** favourites, recently played, the last check, the
  song history, the liked songs, the visualiser's settings and the album art
  cache, in the app's own storage.
- **Bollywood & Desi:** 23 stations (Bollywood hits and classics, Punjabi,
  Tamil, filmi) from Radio Browser's checked list. Song titles sent in
  Devanagari or other Indian scripts are not drawn yet (the fonts are Latin);
  the song card is skipped for them.
- **Latin, K-Pop, Afrobeats, Arabic:** 16, 13, 14 and 12 stations from the
  same checked list (reggaeton, salsa and bachata; idol pop and city pop;
  Afrobeats, Amapiano and Afro House from Lagos, Accra and Johannesburg;
  Arabic pop, tarab, raï and chaabi). Song titles in Korean or Arabic script
  are not drawn yet either.

Stations whose terms forbid third-party players (SomaFM, for one) are left
out. If a station asks to be removed, it is removed. Album art is looked up
politely: at most one MusicBrainz request a second, with a User-Agent that
names the app, and every answer is cached.

## How it listens

The app plays its music, and the radio, through the kit's mixer. Afterglow
taps the music inside the mixer (so the picture stays in time with what you
hear), runs a 2048-point FFT, groups it into 16 bands spaced by pitch,
normalises them automatically, smooths them like a VU meter, detects beats
from jumps in the bass and drops from the bass returning after a breakdown.
See `src/audio/` and `src/concepts/afterglow.cpp`.

## Build

Linux, WSL or Docker (see `CLAUDE.md` for the Windows Docker commands):

```bash
make                                              # PS5 app folder and ZIP in dist/
make test-unit                                    # unit tests with sanitizers
tools/host-snapshots.sh build/snapshots afterglow # PC preview pictures
HUI_WARM=30 tools/host-snapshots.sh build/snapshots afterglow  # with feedback trails grown
```

The home-screen backgrounds are rendered by the app itself
(`HUI_POSTER=pic0.png tools/host-snapshots.sh out afterglow 3840 2160`) and
converted with `python3 tools/png-to-dds.py` (BC7, as the console wants).

Copy `dist/PPSA47770/` to `/data/homebrew/` on the console and launch it
like any other native homebrew title.

## Credits and licence

Afterglow is built on [ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui)
by BlackBearReloaded (its renderer, UI kit, mixer and background music),
the [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) by John
Törnblom, [minimp3](https://github.com/lieff/minimp3) by lieff,
[stb_image](https://github.com/nothings/stb) by Sean Barrett, libcurl,
OpenSSL and FAAD2 from PacBrew, three scenes ported from
[React Bits](https://github.com/DavidHDev/react-bits) by David Haz, and the
free fonts Bebas Neue, Michroma, Monoton, Bungee, Permanent Marker and
Righteous. Album information comes from [MusicBrainz](https://musicbrainz.org)
and covers from the [Cover Art Archive](https://coverartarchive.org).
GPL-3.0-or-later; see `LICENSE` and `THIRD_PARTY_NOTICES.md`. The kit's
original README is in `docs/UI-KIT-README.md`.
