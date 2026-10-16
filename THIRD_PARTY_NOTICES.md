# Afterglow

Afterglow is a fork of ps5-homebrew-ui by BlackBearReloaded. Files whose
header names DevvNirvana are Afterglow's own (GPL-3.0-or-later); every other
file keeps its original author and licence as stated below. Afterglow
changed `sce_sys/icon0.png` to its own icon (rendered with
`HUI_ICON=... tools/host-snapshots.sh`) and `pic0.dds` / `pic1.dds` (and
their source PNGs) to its own backgrounds, rendered by the app's own renderer
(`HUI_POSTER=...`) and converted with `tools/png-to-dds.py`. `snd0.at9` is
still the kit owner's.

Afterglow also includes:

- **minimp3** by lieff (https://github.com/lieff/minimp3), the MP3 decoder
  behind the radio, in `src/third_party/minimp3/`. Licence: CC0 1.0 Universal
  (public domain dedication); see `src/third_party/minimp3/LICENSE`.
- **Scenes ported from React Bits** by David Haz
  (https://github.com/DavidHDev/react-bits, `src/content/Backgrounds`:
  Balatro, Prism, Orb), in `src/gfx/backdrop_ported.hpp`, modified to react
  to music and the controller. React Bits is licensed "MIT + Commons Clause",
  which does not by itself allow ported versions to be redistributed; these
  ports are included with the author's permission to distribute them in
  Afterglow under GPL-3.0. Copyright (c) 2026 David Haz.
  <!-- TODO before release: link the author's written permission here. -->
- **stb_image** by Sean Barrett (https://github.com/nothings/stb), which
  decodes album art (JPEG and PNG) in `src/third_party/stb/stb_image.inc`,
  compiled once through `src/third_party/stb/image.c`. Public domain or MIT;
  see `src/third_party/stb/LICENSE`. Linked into the PS5 application.
- **libopus 1.5.2** by the Xiph.Org Foundation and contributors
  (https://github.com/xiph/opus), the Opus decoder behind Ogg Opus radio
  streams and songs, in `src/third_party/opus/` (flattened into one folder,
  decoder use; see `src/third_party/opus/UPSTREAM`). Licence: BSD 3-clause;
  see `src/third_party/opus/LICENSE`. Linked into the PS5 application.
- **Station list and logos** from Radio Browser (https://www.radio-browser.info),
  public domain data, as built into github.com/AlonDrilich/radio-playlists
  (CC0 1.0). The logos are the stations' own pictures: the app fetches them
  from their addresses and caches them on the console; none is shipped.
- **Genre typefaces**, baked by `tools/bake-fonts.sh` like the kit's (see
  *Fonts and font baking* below for the table).
- **Album information and covers** are fetched at run time from
  [MusicBrainz](https://musicbrainz.org) (its data is CC0) and the
  [Cover Art Archive](https://coverartarchive.org); nothing from either is
  bundled. Cover images belong to their rights holders; the app shows the
  cover of the song playing, as music players do, and caches it only on the
  console that showed it. The invented covers in `tests/unit/data/cover-*.jpg`
  were drawn for the tests.
- **The station list** in `assets/radio/stations.txt` names public internet
  radio stations and their own stream addresses, chosen from the
  [Radio Browser](https://www.radio-browser.info) directory (public domain
  data) as built into
  [AlonDrilich/radio-playlists](https://github.com/AlonDrilich/radio-playlists)
  (CC0). Afterglow plays them directly from each station, like any radio
  player; it does not copy or re-host their audio. The names and streams
  belong to their stations. Stations whose terms forbid third-party players
  are not listed.
- **console_curl.c / console_curl.h** in `src/platform/ps5/`, from
  [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
  (`examples/update-check`), Copyright (C) 2026 BlackBearReloaded,
  GPL-3.0-or-later. Five libc stand-ins were removed because the kit's
  `src/runtime/runtime_shims.c` already defines them.
- **Linked statically into the console build** (from PacBrew v0.40.2, the
  PS5 payload SDK's ports; licence texts in `third_party/licenses/`):

  | Component | Version | Licence |
  | --- | --- | --- |
  | libcurl | 8.18.0 | curl licence (MIT/X derivative), `curl-COPYING.txt` |
  | OpenSSL | 3.5.2 | Apache License 2.0, `openssl-LICENSE.txt` |
  | zlib | 1.3.2 | zlib licence, `zlib-LICENSE.txt` |
  | zstd | 1.5.6 | BSD-3-Clause, `zstd-LICENSE.txt` |
  | libpsl | 0.21.5 | MIT, `libpsl-COPYING.txt`; its Public Suffix List data is MPL-2.0, `publicsuffix-LICENSE.txt` |
  | FAAD2 (AAC decoder) | 2.11.1 | GPL-2.0-or-later, `faad2-COPYING.txt` |

# Third-party notices

## Credits and acknowledgements

ps5-homebrew-ui exists thanks to the maintainers and contributors of:

- [ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl), with
  [Mesa](https://mesa3d.org/) and OpenGNM PSBC, for OpenGL 4.6 on PS5;
- [PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
  and the [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) for the
  reproducible native foundation;
- [Inter](https://github.com/rsms/inter),
  [Montserrat](https://github.com/JulietaUla/Montserrat),
  [DejaVu](https://dejavu-fonts.github.io/),
  [Press Start 2P](https://fonts.google.com/specimen/Press+Start+2P) and
  [Patrick Hand](https://fonts.google.com/specimen/Patrick+Hand) for the
  interface typefaces, and
  [stb](https://github.com/nothings/stb) for music decoding, font baking and
  PNG writing;
- LLVM/Clang, Python, zlib and GoogleTest for build, packaging and validation
  tooling;
- Inigo Quilez, whose published signed-distance functions the shape shader
  follows (rounded box, triangle, star and arc).

The rendering kit, the sound mixer and the sound effects come from
[ProsperoPuzzles](https://github.com/blackbearreloaded/ProsperoPuzzles) and
ProsperoEden, both by BlackBearReloaded.

ps5-homebrew-ui is Copyright (C) 2026 BlackBearReloaded and licensed under
GPL-3.0-or-later. It is built on `ps5-native-app-boilerplate`; the notices
below cover the boilerplate's dependencies and every third-party component the
project adds.

## Fonts and font baking

| Typeface | Files | Copyright | Licence |
| --- | --- | --- | --- |
| Inter Regular, SemiBold | `third_party/fonts/Inter-*.ttf` | (c) 2016 The Inter Project Authors | SIL Open Font License 1.1 (`Inter-LICENSE.txt`) |
| Montserrat Medium | `third_party/fonts/Montserrat-Medium.ttf` | (c) 2011 The Montserrat Project Authors | SIL Open Font License 1.1 (`Montserrat-LICENSE.txt`) |
| DejaVu Sans Mono | `third_party/fonts/DejaVuSansMono.ttf` | (c) 2003 Bitstream, Inc.; DejaVu changes are public domain | Bitstream Vera licence (`DejaVu-LICENSE.txt`) |
| Press Start 2P | `third_party/fonts/PressStart2P-Regular.ttf` | (c) 2012 The Press Start 2P Project Authors | SIL Open Font License 1.1 (`PressStart2P-LICENSE.txt`) |
| Patrick Hand | `third_party/fonts/PatrickHand-Regular.ttf` | (c) 2010-2012 Patrick Wagesreiter | SIL Open Font License 1.1 (`PatrickHand-LICENSE.txt`) |
| Bebas Neue (Afterglow) | `third_party/fonts/BebasNeue-Regular.ttf` | (c) 2010 Dharma Type | SIL Open Font License 1.1 (`BebasNeue-LICENSE.txt`) |
| Michroma (Afterglow) | `third_party/fonts/Michroma-Regular.ttf` | (c) 2011 The Michroma Project Authors | SIL Open Font License 1.1 (`Michroma-LICENSE.txt`) |
| Monoton (Afterglow) | `third_party/fonts/Monoton-Regular.ttf` | (c) 2011 Vernon Adams, Reserved Font Name "Monoton" | SIL Open Font License 1.1 (`Monoton-LICENSE.txt`) |
| Bungee (Afterglow) | `third_party/fonts/Bungee-Regular.ttf` | (c) 2023 The Bungee Project Authors | SIL Open Font License 1.1 (`Bungee-LICENSE.txt`) |
| Permanent Marker (Afterglow) | `third_party/fonts/PermanentMarker-Regular.ttf` | (c) Font Diner, Inc. | Apache License 2.0 (`PermanentMarker-LICENSE.txt`) |
| Righteous (Afterglow) | `third_party/fonts/Righteous-Regular.ttf` | (c) 2011 Brian J. Bonislawsky (Astigmatic) | SIL Open Font License 1.1 (`Righteous-LICENSE.txt`) |

`assets/fonts/*.huifont` are distance-field renderings of these typefaces
produced by `tools/bake-fonts.sh`; the licence texts ship beside them.

The baker uses [stb_truetype](https://github.com/nothings/stb)
(`third_party/stb/stb_truetype.h`, public domain or MIT) and the host snapshot
tool uses stb_image_write (`third_party/stb/stb_image_write.h`, same terms).
Both are host-only and are not linked into the PS5 application.

## Sound effects and music

`assets/audio/sfx/paper/*.wav` are the sound effects of ProsperoPuzzles and
`assets/audio/sfx/glass/*.wav` those of ProsperoEden. They were generated for
those projects with ElevenLabs Sound Effects v2 and prepared with
`tools/process-sfx.py` (trimmed, faded and levelled). No sound effect was
generated for this repository. `assets/audio/music/*.ogg` ("First Light",
"Open Strings" and "Quiet Hours") are the project's own background music,
supplied by its owner and encoded to 48 kHz OGG Vorbis at -18 LUFS. All are
Copyright (C) 2026 BlackBearReloaded and distributed under the project licence.

## Music decoding

Music is decoded by [stb_vorbis](https://github.com/nothings/stb) (public
domain or MIT; see `src/third_party/stb/LICENSE`), vendored at the commit in
`src/third_party/stb/UPSTREAM` by `tools/update-stb.sh`. It is linked into the
PS5 application. Afterglow also uses its push API for Ogg Vorbis radio
streams, and stb_image for station logos (GIF and BMP as well as JPEG and
PNG).

## Shader distance functions

The fragment shader in `src/gfx/gl_batch.cpp` adapts the two-dimensional
signed-distance functions published by Inigo Quilez
(https://iquilezles.org/articles/distfunctions2d/), which he releases under
the MIT licence.

## OpenGL runtime

The PS5 build statically links the
[ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl) SDK
(GPL-3.0-or-later), which contains Mesa (MIT) and OpenGNM PSBC components under
their own licenses. The SDK is fetched or selected at build time and is not
stored in this repository; its license texts ship inside the SDK archive.

## Native build dependencies

The application build uses LLVM/Clang/lld, zlib 1.3.2, and the public
[PS5 payload SDK](https://github.com/ps5-payload-dev/sdk). The bootstrapper
downloads SDK v0.42 after verifying SHA-256
`8cfbc7cd5811e719eb4f0c47eea668d3dc7b40bc8ab11c4a5031d40c23ec02da`.
It downloads zlib 1.3.2 from the upstream source archive after verifying
SHA-256 `bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16`
and compiles its static archive locally. Both dependencies remain under ignored
`.deps/native/`, retain their upstream licenses, and are not distributed by
this repository. No Sony SDK file is included.

Target C++ compilation uses the LLVM libc++ headers distributed by the public
SDK. Those headers retain the Apache-2.0 WITH LLVM-exception license recorded
upstream. The application does not redistribute or dynamically load the
complete libc++ or libc++abi archives.

The PS5 ELF converter and FSELF writer in `tooling/native/` are derived from
[SharpProspero](https://github.com/SvenGDK/SharpProspero), Copyright (C) 2026
SvenGDK, GPL-3.0, and were translated to C++ and modified by BlackBearReloaded.

## Host test dependency

The host unit-test target downloads
[GoogleTest](https://github.com/google/googletest) 1.17.0 after verifying
SHA-256 `65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c`.
It remains under ignored `.deps/test/`, retains its BSD-3-Clause license, and
is not linked into any PS5 application, runtime, or package artifact.

## Optional PacBrew dependencies

When selected through `PACBREW_*` build variables, the build downloads the prebuilt ports image
from [ps5-payload-dev/pacbrew-repo](https://github.com/ps5-payload-dev/pacbrew-repo)
release `v0.40.2`, verifies its published SHA-256, and extracts only the
`target/user/homebrew` prefix under ignored `.deps/pacbrew/`. It does not
replace the pinned SDK or install files globally. PacBrew recipes and every
linked third-party library retain their upstream licenses; applications must
review those terms before redistribution.

## Independently authored runtime shim

`tooling/native/libc_builder.cpp` and the manifests under
`tooling/native/runtime/` are independently authored for this project and
licensed under GPL-3.0-or-later. The generated `runtime/libc.prx` contains
project-authored compatibility stubs, startup code, and semantic loader
metadata. It contains no Sony runtime implementation.

Original ps5-native-app-boilerplate code is Copyright (C) 2026
BlackBearReloaded and licensed under GPL-3.0-or-later. Source and script files
carry matching SPDX identifiers.

## Presentation assets

`sce_sys/icon0.png`, `sce_sys/pic0.dds` (shown while the title is selected) and
`sce_sys/pic1.dds` (shown while it starts) are Afterglow's own artwork,
rendered by the app (`background-source.png` and
`launch-background-source.png` are the renders the two backgrounds were made
from). `sce_sys/snd0.at9`, the music the console plays while the title is
selected, is the owner's own track, encoded with
[ps5-at9-converter](https://github.com/blackbearreloaded/ps5-at9-converter)
(57.6 s, 192 kb/s, -28 LUFS, whole-track loop).
`HUI_ICON=<file> tools/host-snapshots.sh` still renders a plain icon
with the project's own renderer, for forks that want one of their own. The
cover art inside the app is rendered at start-up from invented titles; no real
product's name, artwork or trademark is used.

No proprietary runtime module, encryption key, or game file is included.
