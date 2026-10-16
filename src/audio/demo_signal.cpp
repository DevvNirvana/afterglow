// Afterglow - The synthetic spectrum (see demo_signal.hpp).
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/demo_signal.hpp"

#include <algorithm>
#include <cmath>

namespace hui::audio
{

namespace
{

constexpr double kTau = 6.283185307179586;

// Where each instrument sits in the 16 bands.
float low_weight(int band)
{
    return std::exp(-static_cast<float>(band) * 0.55f);
}
float mid_weight(int band)
{
    const float d = (static_cast<float>(band) - 6.5f) / 2.6f;
    return std::exp(-d * d);
}
float high_weight(int band)
{
    return std::clamp((static_cast<float>(band) - 9.0f) / 5.0f, 0.0f, 1.0f);
}

// A cheap repeatable flutter, so the hats are not identical every time.
float flutter(double x)
{
    const double s = std::sin(x * 12.9898) * 43758.5453;
    return static_cast<float>(s - std::floor(s));
}

} // namespace

std::array<float, 16> demo_spectrum(double t)
{
    const double beat_length = 60.0 / static_cast<double>(kDemoBpm);
    const double beats = t / beat_length;
    const auto beat_index = static_cast<long long>(std::floor(beats));
    const auto phase = static_cast<float>(beats - std::floor(beats));
    const auto half_phase = static_cast<float>(beats * 2.0 - std::floor(beats * 2.0));

    // Sections of 16 beats: intro, build, drop, break, then round again.
    const int section = static_cast<int>((beat_index / 16) % 4);
    constexpr float kEnergy[4] = {0.55f, 0.8f, 1.0f, 0.65f};
    const float energy = kEnergy[section];
    const bool drums = section != 3 || (beat_index % 16) >= 12; // the break drops the kit
    // The build rises over its last four beats.
    const float riser =
        section == 1
            ? std::clamp((static_cast<float>(beat_index % 16) + phase - 12.0f) / 4.0f, 0.0f, 1.0f)
            : 0.0f;

    const float kick = drums ? std::exp(-phase * 7.0f) : 0.0f;
    const float snare = drums && (beat_index % 2) == 1 ? std::exp(-phase * 9.0f) : 0.0f;
    const float hat = drums ? std::exp(-half_phase * 18.0f) *
                                  (0.6f + 0.4f * flutter(static_cast<double>(beat_index) +
                                                         (half_phase < 0.5f ? 0.0 : 0.5)))
                            : 0.0f;
    const auto bassline =
        static_cast<float>(0.5 + 0.5 * std::sin(kTau * beats / 8.0)) * (1.0f - kick * 0.6f);

    std::array<float, 16> bands{};
    for (int i = 0; i < 16; ++i)
    {
        const auto pad =
            static_cast<float>(0.5 + 0.5 * std::sin(static_cast<double>(i) * 0.9 + t * 0.7));
        float level = kick * low_weight(i) * 1.0f + bassline * low_weight(i) * 0.35f +
                      snare * mid_weight(i) * 0.8f + hat * high_weight(i) * 0.7f + pad * 0.14f +
                      riser * high_weight(i) * 0.5f;
        bands[static_cast<std::size_t>(i)] = std::clamp(level * energy, 0.0f, 1.0f);
    }
    return bands;
}

} // namespace hui::audio
