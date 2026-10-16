// Afterglow - The tempo and the beat of the music, as a clock scenes can follow.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The input is an onset strength once per frame (how much louder the music
// just got, mostly in the bass). It is resampled to 100 values a second and
// kept for six seconds. Twice a second the tracker looks for the beat
// period: the lag at which that envelope best matches itself, also at twice
// the lag (a comb), with a mild preference for dance tempos. Then it finds
// where the beats fall by summing the envelope along that period at every
// offset. A free-running clock counts beats at the found tempo and is
// nudged towards the found phase a little at a time, so it never jumps:
// scenes read beats() and loops land on the beat even when a kick is missed.
// Without a clear pulse (ambient, talk) locked() is false and the clock runs
// on at its last tempo.

#pragma once

#include <array>
#include <cstdint>

namespace hui::audio
{

class BeatTracker
{
  public:
    static constexpr float kRate = 100.0f; // envelope values a second
    static constexpr int kHistory = 600;   // six seconds of envelope
    static constexpr float kMinBpm = 70.0f;
    static constexpr float kMaxBpm = 180.0f;

    // onset: this frame's onset strength (>= 0, any scale); dt in seconds.
    void push(float onset, float dt);
    void reset();

    // The tempo in beats a minute; 0 until one has been found.
    float bpm() const
    {
        return bpm_;
    }
    // How clear the pulse is, 0..1.
    float confidence() const
    {
        return confidence_;
    }
    // A clear, steady pulse: scenes may follow it.
    bool locked() const
    {
        return locked_;
    }
    // Beats counted since the start, continuous (its fraction is the phase).
    double beats() const
    {
        return beats_;
    }
    // Where in the current beat we are: 0 on the beat, rising to 1.
    float phase() const;
    // A beat boundary was crossed during the last push.
    bool ticked() const
    {
        return ticked_;
    }

  private:
    void append(float value);
    float at(int ago) const; // the envelope `ago` samples back (0 = newest)
    void analyse();

    std::array<float, kHistory> env_{};
    int head_ = 0;   // where the next value goes
    int filled_ = 0; // how many values are valid
    float carry_ = 0.0f;
    float mean_ = 0.0f;
    int since_analysis_ = 0;

    float bpm_ = 0.0f;
    float candidate_ = 0.0f;
    int candidate_votes_ = 0;
    float confidence_ = 0.0f;
    bool locked_ = false;
    double beats_ = 0.0;
    float correction_ = 0.0f; // phase still to apply, in beats
    bool ticked_ = false;
};

} // namespace hui::audio
