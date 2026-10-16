// Afterglow - FFT spectrum bands (see spectrum.hpp).
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/spectrum.hpp"

#include <algorithm>
#include <cmath>

namespace hui::audio
{

namespace
{

constexpr float kPi = 3.14159265358979f;
constexpr float kLowest = 40.0f;
constexpr float kHighest = 16000.0f;
constexpr float kPeakRelease = 0.35f; // per second: how fast the gain recovers after a loud part
constexpr float kBandRelease = 0.6f;  // the same for each band's own peak
constexpr float kBandFloorShare = 0.15f;
constexpr float kPeakFloor = 2e-4f; // silence stays dark instead of amplifying noise

// In-place iterative radix-2 FFT; n is a power of two.
void fft(std::vector<std::complex<float>> &a, const std::vector<std::complex<float>> &twiddles)
{
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (std::size_t length = 2; length <= n; length <<= 1)
    {
        const std::size_t step = n / length;
        const std::size_t half = length / 2;
        for (std::size_t start = 0; start < n; start += length)
        {
            for (std::size_t k = 0; k < half; ++k)
            {
                const std::complex<float> u = a[start + k];
                const std::complex<float> v = a[start + k + half] * twiddles[k * step];
                a[start + k] = u + v;
                a[start + k + half] = u - v;
            }
        }
    }
}

} // namespace

float SpectrumAnalyzer::band_edge(int i)
{
    const float t = static_cast<float>(i) / static_cast<float>(kBands);
    return kLowest * std::pow(kHighest / kLowest, t);
}

SpectrumAnalyzer::SpectrumAnalyzer() : window_(kWindow), twiddles_(kWindow / 2), buffer_(kWindow)
{
    for (std::size_t i = 0; i < kWindow; ++i)
        window_[i] = 0.5f - 0.5f * std::cos(2.0f * kPi * static_cast<float>(i) /
                                            static_cast<float>(kWindow - 1));
    for (std::size_t k = 0; k < kWindow / 2; ++k)
    {
        const float angle = -2.0f * kPi * static_cast<float>(k) / static_cast<float>(kWindow);
        twiddles_[k] = {std::cos(angle), std::sin(angle)};
    }
    const float bin_hz = kSampleRate / static_cast<float>(kWindow);
    for (int i = 0; i <= kBands; ++i)
        bins_[static_cast<std::size_t>(i)] =
            static_cast<std::size_t>(std::lround(band_edge(i) / bin_hz));
    // The lowest bands are narrower than one bin: give each at least one.
    for (int i = 1; i <= kBands; ++i)
    {
        auto &edge = bins_[static_cast<std::size_t>(i)];
        edge = std::max(edge, bins_[static_cast<std::size_t>(i - 1)] + 1);
    }
    for (int i = 0; i < kBands; ++i)
    {
        const float centre = std::sqrt(band_edge(i) * band_edge(i + 1));
        tilt_[static_cast<std::size_t>(i)] = std::sqrt(centre / 100.0f);
    }
}

std::array<float, SpectrumAnalyzer::kBands> SpectrumAnalyzer::analyze(const float *samples,
                                                                      float dt)
{
    for (std::size_t i = 0; i < kWindow; ++i)
        buffer_[i] = {samples[i] * window_[i], 0.0f};
    fft(buffer_, twiddles_);

    std::array<float, kBands> raw{};
    float loudest = 0.0f;
    for (int band = 0; band < kBands; ++band)
    {
        const std::size_t first = bins_[static_cast<std::size_t>(band)];
        const std::size_t last = bins_[static_cast<std::size_t>(band + 1)];
        float power = 0.0f;
        for (std::size_t bin = first; bin < last && bin < kWindow / 2; ++bin)
            power += std::norm(buffer_[bin]);
        const float count = static_cast<float>(std::max<std::size_t>(last - first, 1));
        const float level = std::sqrt(power / count) * tilt_[static_cast<std::size_t>(band)] /
                            static_cast<float>(kWindow);
        raw[static_cast<std::size_t>(band)] = level;
        loudest = std::max(loudest, level);
    }

    dt = std::clamp(dt, 0.0f, 0.1f);
    peak_ = std::max({loudest, peak_ * std::exp(-kPeakRelease * dt), kPeakFloor});
    std::array<float, kBands> out{};
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        // Each band also follows its own recent peak, so a song with little
        // deep bass or air still moves those bars. The floor (a share of the
        // loudest band) stops an empty band from inflating its own noise.
        float &band_peak = band_peak_[i];
        band_peak = std::max({raw[i], band_peak * std::exp(-kBandRelease * dt),
                              peak_ * kBandFloorShare, kPeakFloor});
        const float own = raw[i] / band_peak;
        const float shared = raw[i] / peak_;
        const float v = std::clamp(0.6f * own + 0.4f * shared, 0.0f, 1.0f);
        out[i] = std::pow(v, 0.8f); // lifts the quiet detail a little
    }
    return out;
}

} // namespace hui::audio
