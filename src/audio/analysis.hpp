// Afterglow - Turns raw per-frame band levels into what the scenes react to.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The input is 16 band levels (0..1, low to high) once per frame, from any
// source: the built-in demo signal today, an FFT of real music later. The
// output is smoothed so the picture breathes instead of flickering: bands
// rise fast and fall slowly, the way a VU meter does, and a beat is
// detected as the bass jumping clearly above its own recent average.

#pragma once

#include <array>

namespace hui::audio
{

class BandAnalyzer
{
  public:
    static constexpr int kBands = 16;

    struct Levels
    {
        std::array<float, kBands> bands{}; // smoothed, 0..1
        float bass = 0.0f;                 // mean of the lowest three bands
        float mid = 0.0f;                  // bands 4..9
        float treble = 0.0f;               // bands 11..15
        float beat = 0.0f;                 // 1 on a detected beat, then decays
        bool onset = false;                // a beat started this frame
    };

    // gain scales the input before smoothing (the player's sensitivity).
    void update(const std::array<float, kBands> &raw, float dt, float gain = 1.0f);
    const Levels &levels() const
    {
        return levels_;
    }
    void reset();

  private:
    Levels levels_;
    float bass_average_ = 0.0f;
    float previous_bass_ = 0.0f;
    float since_onset_ = 1.0f;
};

} // namespace hui::audio
