// Afterglow - Beat tracker tests: tempo, phase, lock and its absence.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/beat_tracker.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

namespace
{

using hui::audio::BeatTracker;

constexpr float kFrame = 1.0f / 60.0f;

// A tiny deterministic noise source, 0..1.
struct Noise
{
    std::uint32_t state = 12345u;
    float next()
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / 16777216.0f;
    }
};

// Feeds `seconds` of a kick at `bpm` (an onset spike on the frame the kick
// lands, a little noise elsewhere). Returns the time reached.
double feed(BeatTracker &tracker, Noise &noise, float bpm, double start, double seconds,
            float noise_level = 0.15f, double *worst_phase = nullptr, double judge_after = 1e9)
{
    const double period = 60.0 / bpm;
    double t = start;
    const double end = start + seconds;
    while (t < end)
    {
        const double next = t + kFrame;
        // A kick lands during this frame?
        const bool kick = std::floor(next / period) > std::floor(t / period);
        const float onset = (kick ? 1.0f : 0.0f) + noise_level * noise.next();
        tracker.push(onset, kFrame);
        if (kick && worst_phase != nullptr && t >= judge_after)
        {
            double ph = tracker.phase();
            ph -= std::round(ph);
            *worst_phase = std::max(*worst_phase, std::abs(ph));
        }
        t = next;
    }
    return t;
}

TEST(BeatTracker, FindsTheTempoAndLocksToTheBeat)
{
    for (const float bpm : {128.0f, 124.0f, 140.0f, 100.0f})
    {
        BeatTracker tracker;
        Noise noise;
        double worst = 0.0;
        feed(tracker, noise, bpm, 0.0, 14.0, 0.15f, &worst, 10.0);
        EXPECT_NEAR(tracker.bpm(), bpm, 1.5f) << bpm;
        EXPECT_TRUE(tracker.locked()) << bpm;
        EXPECT_GT(tracker.confidence(), 0.3f) << bpm;
        // Each kick of the last seconds lands near the start of a beat.
        EXPECT_LT(worst, 0.15) << bpm;
    }
}

TEST(BeatTracker, HoldsThroughBusyMusic)
{
    // Hats, snares and pads between the kicks: noise nearly half a kick tall.
    BeatTracker tracker;
    Noise noise;
    double worst = 0.0;
    feed(tracker, noise, 126.0f, 0.0, 16.0, 0.45f, &worst, 12.0);
    EXPECT_NEAR(tracker.bpm(), 126.0f, 2.0f);
    EXPECT_TRUE(tracker.locked());
    EXPECT_LT(worst, 0.2);
}

TEST(BeatTracker, ASlowTempoIsNotDoubled)
{
    BeatTracker tracker;
    Noise noise;
    feed(tracker, noise, 90.0f, 0.0, 12.0);
    EXPECT_NEAR(tracker.bpm(), 90.0f, 1.5f);
}

TEST(BeatTracker, DrumAndBassIsTheTempoOrItsHalf)
{
    BeatTracker tracker;
    Noise noise;
    feed(tracker, noise, 174.0f, 0.0, 12.0);
    const float bpm = tracker.bpm();
    EXPECT_TRUE(std::abs(bpm - 174.0f) < 2.5f || std::abs(bpm - 87.0f) < 1.5f) << bpm;
    EXPECT_TRUE(tracker.locked());
}

TEST(BeatTracker, FollowsATempoChange)
{
    BeatTracker tracker;
    Noise noise;
    const double t = feed(tracker, noise, 120.0f, 0.0, 10.0);
    EXPECT_NEAR(tracker.bpm(), 120.0f, 1.5f);
    feed(tracker, noise, 128.0f, t, 10.0);
    EXPECT_NEAR(tracker.bpm(), 128.0f, 1.5f);
}

TEST(BeatTracker, NoiseAndSilenceDoNotLock)
{
    BeatTracker noisy;
    Noise noise;
    for (int i = 0; i < 60 * 12; ++i)
        noisy.push(noise.next(), kFrame);
    EXPECT_FALSE(noisy.locked());

    BeatTracker quiet;
    for (int i = 0; i < 60 * 12; ++i)
        quiet.push(0.0f, kFrame);
    EXPECT_FALSE(quiet.locked());
    // The clock runs on regardless, at a resting tempo.
    EXPECT_GT(quiet.beats(), 20.0);
}

TEST(BeatTracker, TheClockOnlyMovesForwardAndTicksOncePerBeat)
{
    BeatTracker tracker;
    Noise noise;
    feed(tracker, noise, 128.0f, 0.0, 10.0);
    double last = tracker.beats();
    int ticks = 0;
    for (int i = 0; i < 60 * 10; ++i)
    {
        tracker.push(0.1f * noise.next(), kFrame);
        EXPECT_GE(tracker.beats(), last);
        last = tracker.beats();
        ticks += tracker.ticked() ? 1 : 0;
    }
    // Ten seconds at about 128 beats a minute.
    EXPECT_NEAR(ticks, 21, 2);
}

TEST(BeatTracker, IgnoresStallsAndBadValues)
{
    BeatTracker tracker;
    tracker.push(std::nanf(""), kFrame);
    tracker.push(1.0f, 3.0f); // a three-second hitch
    tracker.push(-5.0f, kFrame);
    EXPECT_TRUE(std::isfinite(tracker.beats()));
    EXPECT_FALSE(tracker.locked());
}

} // namespace
