// Afterglow - A synthetic "song" for the scenes before real music is wired.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later
//
// No sound is produced: this only returns what a spectrum analyser would
// show for a simple electronic track at 118 BPM (kick on every beat, snare
// on two and four, hi-hats on eighths, a bassline and slow pads), with
// 16-beat sections that build and drop. It is deterministic in time, so
// the PC snapshots are repeatable.

#pragma once

#include <array>

namespace hui::audio
{

constexpr float kDemoBpm = 118.0f;

// Raw band levels (0..1, low to high) at time t seconds.
std::array<float, 16> demo_spectrum(double t);

} // namespace hui::audio
