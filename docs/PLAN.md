# Afterglow (working name): audio-reactive visuals for PS5 homebrew

Working name only. Rename before the first public release; the catalog had no app called Afterglow when checked on 2026-10-12.

## What it is

A native PS5 app that turns music into real-time 3D and shader visuals on the TV, controlled with the DualSense. The DualSense light bar can follow the music. A hands-off showcase mode suits parties and ambient screens.

## What the research found (2026-10-12)

Verified by reading the repositories and the catalog:

- **Rendering is solved by the community.** `blackbearreloaded/ps5-opengl` is an OpenGL 4.6 Core stack for PS5 homebrew with a static SDK, 1080p/1440p/4K at 60 or 120 Hz, compute shaders, and an SDL2 bridge. Licence: GPL-3.0-or-later.
- **App packaging is solved.** `blackbearreloaded/ps5-native-app-boilerplate` is a C++20 template that builds a deployable title folder. Hardware-verified on firmware 6.02 and 12.70.
- **UI is solved.** `blackbearreloaded/ps5-homebrew-ui` provides themes, components, a 32-voice sound mixer, controller rumble and a PC preview. It also has an `AGENTS.md` for coding agents.
- **The catalog has 62 apps** (`blackbearreloaded/ps5-homebrew-catalog`, `apps/*.json`).

Already taken, so we avoid duplicating them:

| Idea | Existing app |
| --- | --- |
| Controller vibration studio | Prospero Vibrate (live motors, presets, pattern editor) |
| Graphics showcase | PS5 OpenGL Showcase, PS5 FSR4 Showcase |
| Screen test patterns | ScreenTester5 |
| Stick drift test | DriftCheck |
| Music playback | OliSe Player (tracker music), ProsperoRadio, SymphonyStation5 (Navidrome client), VLC, PS5MC |

A search of the catalog for visualizer, audio-reactive, particle and shader terms found only the two showcases and a forwarder manager, so a dedicated audio-reactive visualizer looks like a gap. Caveats: PS5MC (a Kodi port) and VLC may include their own visualizations, and someone may be building one privately.

## Decision

Build the audio-reactive visualizer. Do not write a renderer, packager or UI toolkit; reuse the three projects above.

Our own work is:

1. A set of original shader scenes driven by audio data.
2. Audio file decoding and an FFT analysis stage.
3. A simple controller-first UI and showcase mode.
4. Light bar sync (and optionally gentle rumble) if the pad API allows it.

## Unknowns (nothing below is verified on firmware 13.60)

- The OpenGL SDK's published hardware results are for firmware 6.02. The boilerplate is verified on 6.02 and 12.70. Nobody has documented 13.60 for these.
- Whether native title folders launch under Relapse on 13.60 with your loader setup.
- Whether `scePadSetLightBar` works from our app. The SDL bridge's test stubs reference it, which suggests the backend uses it.
- Audio output and decode performance on the console.

## Milestones

**M0: prove the platform (no coding, about an hour).** On your 13.60 console, install and launch the prebuilt PS5 OpenGL Showcase from its latest GitHub release, and the Homebrew UI Lab from homebrew.page. If both run, the whole stack works on your firmware. If not, stop and report exactly what happened.

**M1: hello world.** Create a repo from the boilerplate template, build locally, run on the console.

**M2: one scene.** A single shader scene driven by a fake audio signal (sine and noise). Proves rendering, frame pacing and input. Record a clip.

**M3: real audio.** Decode a local MP3/FLAC/OGG file, play it, run an FFT, feed bass, mid and treble to the shader. Public-domain single-header decoders (minimp3, dr_flac, stb_vorbis) keep dependencies small.

**M4: controller.** Scene switching, sensitivity controls, light bar sync.

**M5: polish and release.** Three scenes, showcase mode, themed UI. Then a public GitHub repo, a tagged release with a ZIP, a reserved title ID, and a catalog pull request that touches only `apps/<TITLEID>.json` (see the catalog's `docs/submitting.md`).

## Licence and rules

- Code that links the GPL stack must be GPL-3.0-or-later. The catalog requires a public source repository anyway.
- No Sony SDK files, firmware modules or keys. Only the public payload SDK.
- Only use music the user supplies. Do not bundle copyrighted tracks.
- Do not claim firmware compatibility we have not tested.
