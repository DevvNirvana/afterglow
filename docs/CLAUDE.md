# Afterglow: instructions for Claude Code

Read `PLAN.md` first. It holds the research and the milestone order.

## Goal

A native PS5 homebrew app (working name Afterglow) that renders audio-reactive scenes with OpenGL 4.6 and is controlled with a DualSense. Target: a PS5 on firmware 13.60 running Relapse. Final target: a listing on homebrew.page.

## Ground rules

- Build on the community projects; do not rewrite them:
  - `blackbearreloaded/ps5-native-app-boilerplate` for the project skeleton and packaging.
  - `blackbearreloaded/ps5-opengl` for rendering (OpenGL 4.6 Core, `#version 460 core`).
  - `blackbearreloaded/ps5-homebrew-ui` for UI, sound mixer and PC preview. Read its `AGENTS.md`.
  - `ps5-payload-dev/sdk` is the public payload SDK underneath all three.
- Work milestone by milestone (see `PLAN.md`). Do not start M3 before M2 runs on the console.
- Develop on the PC first where possible (the UI kit has a PC preview), then test on the console.
- Never claim something works on firmware 13.60 until the user has run it on their console and confirmed.
- Before adding a feature, check the catalog (`blackbearreloaded/ps5-homebrew-catalog`, `apps/*.json`) for an app that already does it.

## Do not

- Add features for downloading, installing or mounting games or packages, or anything aimed at piracy.
- Copy Sony SDK files, firmware modules or keys into the repo.
- Bundle music the user does not own; load music from user-supplied files.
- Change any licence header. Project code is GPL-3.0-or-later.
- Commit build output, downloaded dependencies or `.deps/`.

## Technical notes

- Scenes are fragment shaders driven by uniforms: time, bass, mid, treble, beat, and the controller stick. Keep each scene in its own file.
- Audio: decode with public-domain single-header libraries (minimp3, dr_flac, stb_vorbis), then FFT into a few smoothed bands. Analyze on a separate thread from rendering.
- Light bar and rumble go through the pad API that the SDL bridge uses; confirm on hardware before depending on it.
- Keep the frame loop allocation-free. Frame-time stability matters more than scene complexity.
- Aim for 1080p60 first; 4K and 120 Hz are options, not requirements.

## When you finish a step

State what you ran, what passed, and what has not been tested on the console. Keep a short running `STATUS.md`.
