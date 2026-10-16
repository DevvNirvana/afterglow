// Afterglow - A tap on the music the mixer is playing right now.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The audio thread writes the mono music mix into a ring as it renders it;
// the main thread copies out the newest samples once per frame for the
// spectrum. Tapping the mixer (not the decoder, which runs ~0.7 s ahead)
// keeps the picture in time with what is heard. The music is tapped before
// the volume controls, so turning the music down does not flatten the
// picture. Samples are relaxed atomics: a frame may mix a few samples from
// two grains, which a visualiser cannot see, and there is no data race.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace hui::audio
{

class Monitor
{
  public:
    static constexpr std::size_t kSize = 8192; // power of two, ~170 ms at 48 kHz

    // Audio thread.
    void push(const float *mono, std::size_t count);

    // Main thread: the newest count samples, oldest first. False until that
    // many have been written.
    bool latest(float *out, std::size_t count) const;
    // Total samples ever written; it stops moving when no audio is rendered.
    std::uint64_t written() const
    {
        return written_.load(std::memory_order_acquire);
    }

  private:
    std::array<std::atomic<float>, kSize> ring_{};
    std::atomic<std::uint64_t> written_{0};
};

// The one music tap of the process (the mixer writes it).
Monitor &music_monitor();

} // namespace hui::audio
