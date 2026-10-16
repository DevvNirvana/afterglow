// Afterglow - 16 spectrum bands from a block of audio samples.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A Hann-windowed 2048-point FFT at 48 kHz, grouped into 16 bands spaced
// evenly in pitch from 40 Hz to 16 kHz (the way we hear, not the way the
// FFT spaces its bins). Music has much more energy in the bass than the
// treble, so each band is tilted up with frequency; an automatic gain then
// follows both the loudest band and each band's own recent peak, with slow
// releases, so quiet songs and loud songs both fill the screen.

#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <vector>

namespace hui::audio
{

class SpectrumAnalyzer
{
  public:
    static constexpr std::size_t kWindow = 2048;
    static constexpr int kBands = 16;
    static constexpr float kSampleRate = 48000.0f;

    SpectrumAnalyzer();

    // samples: kWindow mono samples, oldest first. Returns levels 0..1.
    std::array<float, kBands> analyze(const float *samples, float dt);

    // Hz of the edge below band i (i == kBands gives the top edge).
    static float band_edge(int i);

  private:
    std::vector<float> window_;
    std::vector<std::complex<float>> twiddles_;
    std::vector<std::complex<float>> buffer_;
    std::array<std::size_t, kBands + 1> bins_{};
    std::array<float, kBands> tilt_{};
    float peak_ = 1e-3f;
    std::array<float, kBands> band_peak_{};
};

} // namespace hui::audio
