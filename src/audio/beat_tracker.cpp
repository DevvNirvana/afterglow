// Afterglow - The tempo and the beat of the music (see beat_tracker.hpp).
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/beat_tracker.hpp"

#include <algorithm>
#include <cmath>

namespace hui::audio
{

namespace
{

constexpr int kMinLag = static_cast<int>(60.0f * BeatTracker::kRate / BeatTracker::kMaxBpm);
constexpr int kMaxLag = static_cast<int>(60.0f * BeatTracker::kRate / BeatTracker::kMinBpm) + 1;
constexpr int kAnalyseEvery = 50; // envelope values: twice a second
constexpr int kMinHistory = 300;  // three seconds before the first guess
constexpr float kLockOn = 0.30f;
constexpr float kLockOff = 0.18f;

// Dance music sits around 120 to 130: of two equally good periods (a tempo
// and its half), prefer the one nearer that.
float prior(float bpm)
{
    const float octaves = std::log2(bpm / 128.0f) / 0.9f;
    return std::exp(-0.5f * octaves * octaves);
}

} // namespace

void BeatTracker::reset()
{
    *this = BeatTracker{};
}

float BeatTracker::phase() const
{
    return static_cast<float>(beats_ - std::floor(beats_));
}

void BeatTracker::append(float value)
{
    env_[static_cast<std::size_t>(head_)] = value;
    head_ = (head_ + 1) % kHistory;
    filled_ = std::min(filled_ + 1, kHistory);
}

float BeatTracker::at(int ago) const
{
    const int index = ((head_ - 1 - ago) % kHistory + kHistory) % kHistory;
    return env_[static_cast<std::size_t>(index)];
}

void BeatTracker::push(float onset, float dt)
{
    ticked_ = false;
    if (!(dt > 0.0f) || dt > 0.5f)
        return; // a stall (loading, a breakpoint) is not a tempo
    onset = std::isfinite(onset) ? std::max(onset, 0.0f) : 0.0f;
    // What rises above its own recent level is an onset.
    mean_ += (onset - mean_) * std::min(1.0f, dt / 1.5f);
    const float value = std::max(0.0f, onset - mean_);
    carry_ += dt * kRate;
    while (carry_ >= 1.0f)
    {
        carry_ -= 1.0f;
        append(value);
        if (++since_analysis_ >= kAnalyseEvery && filled_ >= kMinHistory)
        {
            since_analysis_ = 0;
            analyse();
        }
    }

    // The clock: on at the tempo, eased towards the found phase.
    const float rate = (bpm_ > 0.0f ? bpm_ : 120.0f) / 60.0f;
    const float step = rate * dt;
    float nudge = correction_ * std::min(1.0f, dt * 3.0f);
    nudge = std::max(nudge, -0.6f * step); // never run backwards
    correction_ -= nudge;
    const double before = std::floor(beats_);
    beats_ += static_cast<double>(step + nudge);
    ticked_ = std::floor(beats_) > before;
}

void BeatTracker::analyse()
{
    // The newest envelope, newest first, lightly smoothed (a kick that lands
    // between two frames is spread over two values) and without its mean.
    const int n = filled_;
    std::array<float, kHistory> y{};
    float mean = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float prev = i > 0 ? at(i - 1) : at(i);
        const float next = i + 1 < n ? at(i + 1) : at(i);
        y[static_cast<std::size_t>(i)] = 0.25f * prev + 0.5f * at(i) + 0.25f * next;
        mean += y[static_cast<std::size_t>(i)];
    }
    mean /= static_cast<float>(n);
    float energy = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        y[static_cast<std::size_t>(i)] -= mean;
        energy += y[static_cast<std::size_t>(i)] * y[static_cast<std::size_t>(i)];
    }
    energy /= static_cast<float>(n);
    if (energy < 1e-9f)
    {
        confidence_ *= 0.5f;
        locked_ = locked_ && confidence_ > kLockOff;
        return;
    }
    const auto ac = [&](int lag)
    {
        float sum = 0.0f;
        for (int i = 0; i + lag < n; ++i)
            sum += y[static_cast<std::size_t>(i)] * y[static_cast<std::size_t>(i + lag)];
        return sum / static_cast<float>(n - lag);
    };

    std::array<float, kMaxLag + 2> score{};
    int best = kMinLag;
    for (int lag = kMinLag; lag <= kMaxLag; ++lag)
    {
        const float bpm = 60.0f * kRate / static_cast<float>(lag);
        const float s = (ac(lag) + 0.5f * ac(lag * 2)) * prior(bpm);
        score[static_cast<std::size_t>(lag)] = s;
        if (s > score[static_cast<std::size_t>(best)])
            best = lag;
    }
    // Between two lags: the top of a parabola through three scores.
    float lag = static_cast<float>(best);
    if (best > kMinLag && best < kMaxLag)
    {
        const float a = score[static_cast<std::size_t>(best - 1)];
        const float b = score[static_cast<std::size_t>(best)];
        const float c = score[static_cast<std::size_t>(best + 1)];
        const float d = a - 2.0f * b + c;
        if (d < 0.0f)
            lag += std::clamp(0.5f * (a - c) / d, -0.5f, 0.5f);
    }
    const float found = 60.0f * kRate / lag;
    confidence_ = std::clamp(ac(best) / energy, 0.0f, 1.0f);
    locked_ = confidence_ > (locked_ ? kLockOff : kLockOn);
    if (confidence_ < kLockOff)
        return; // no pulse to follow: keep the last tempo and phase

    // A small drift is followed; a new tempo must hold for three looks.
    if (bpm_ <= 0.0f)
    {
        bpm_ = found;
    }
    else if (std::abs(found - bpm_) / bpm_ < 0.05f)
    {
        bpm_ += (found - bpm_) * 0.3f;
        candidate_votes_ = 0;
    }
    else
    {
        if (candidate_ > 0.0f && std::abs(found - candidate_) / candidate_ < 0.05f)
            ++candidate_votes_;
        else
            candidate_votes_ = 1;
        candidate_ = found;
        if (candidate_votes_ >= 3)
        {
            bpm_ = found;
            candidate_votes_ = 0;
        }
    }

    // Where the beats fall: the offset (samples since the last beat) whose
    // comb along the period collects the most onset.
    const float period = 60.0f * kRate / bpm_;
    const int offsets = static_cast<int>(period);
    float best_sum = -1e30f;
    int best_offset = 0;
    for (int o = 0; o < offsets; ++o)
    {
        float sum = 0.0f;
        for (int k = 0;; ++k)
        {
            const int i = static_cast<int>(
                std::lround(static_cast<float>(o) + static_cast<float>(k) * period));
            if (i >= n)
                break;
            sum += y[static_cast<std::size_t>(i)];
        }
        if (sum > best_sum)
        {
            best_sum = sum;
            best_offset = o;
        }
    }
    // The envelope at this moment is `best_offset` values after a beat.
    const float target = static_cast<float>(best_offset) / period;
    float error = target - phase();
    error -= std::round(error); // the nearest way round, -0.5..0.5
    correction_ = error;
}

} // namespace hui::audio
