// Afterglow - Band smoothing and beat detection (see analysis.hpp).
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/analysis.hpp"

#include <algorithm>
#include <cmath>

namespace hui::audio
{

namespace
{

// Rates per second. Fast attack keeps kicks punchy; the slow release is what
// makes bars fall gracefully instead of blinking.
constexpr float kAttack = 40.0f;
constexpr float kRelease = 5.0f;
constexpr float kAverageRate = 1.2f; // the bass baseline beats are measured against
constexpr float kBeatDecay = 6.0f;
// A beat needs the bass this far above its average, and some space after
// the previous one, so a sustained bass note is not a drum roll.
constexpr float kOnsetMargin = 0.16f;
constexpr float kOnsetFloor = 0.22f;
constexpr float kMinBeatGap = 0.18f;
// ...and a beat is a jump: a held bass note is loud but does not rise.
constexpr float kOnsetJump = 0.06f;

float approach(float current, float target, float rate, float dt)
{
    const float k = 1.0f - std::exp(-rate * dt);
    return current + (target - current) * k;
}

float mean(const std::array<float, BandAnalyzer::kBands> &bands, int first, int last)
{
    float sum = 0.0f;
    for (int i = first; i <= last; ++i)
        sum += bands[static_cast<std::size_t>(i)];
    return sum / static_cast<float>(last - first + 1);
}

} // namespace

void BandAnalyzer::reset()
{
    levels_ = {};
    bass_average_ = 0.0f;
    previous_bass_ = 0.0f;
    since_onset_ = 1.0f;
}

void BandAnalyzer::update(const std::array<float, kBands> &raw, float dt, float gain)
{
    dt = std::clamp(dt, 0.0f, 0.1f);
    for (std::size_t i = 0; i < raw.size(); ++i)
    {
        const float target = std::clamp(raw[i] * gain, 0.0f, 1.0f);
        float &band = levels_.bands[i];
        band = approach(band, target, target > band ? kAttack : kRelease, dt);
    }
    levels_.bass = mean(levels_.bands, 0, 2);
    levels_.mid = mean(levels_.bands, 4, 9);
    levels_.treble = mean(levels_.bands, 11, 15);

    // Onsets are judged on the unsmoothed bass so the attack smoothing does
    // not delay them by a frame or two.
    const float raw_bass = std::clamp((raw[0] + raw[1] + raw[2]) * gain / 3.0f, 0.0f, 1.0f);
    since_onset_ += dt;
    levels_.onset = raw_bass > kOnsetFloor && raw_bass > bass_average_ + kOnsetMargin &&
                    raw_bass - previous_bass_ > kOnsetJump && since_onset_ >= kMinBeatGap;
    previous_bass_ = raw_bass;
    if (levels_.onset)
    {
        since_onset_ = 0.0f;
        levels_.beat = 1.0f;
    }
    else
    {
        levels_.beat *= std::exp(-kBeatDecay * dt);
    }
    bass_average_ = approach(bass_average_, raw_bass, kAverageRate, dt);
}

} // namespace hui::audio
