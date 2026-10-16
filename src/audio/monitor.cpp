// Afterglow - The music tap (see monitor.hpp).
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/monitor.hpp"

namespace hui::audio
{

void Monitor::push(const float *mono, std::size_t count)
{
    std::uint64_t index = written_.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < count; ++i, ++index)
        ring_[static_cast<std::size_t>(index) & (kSize - 1)].store(mono[i],
                                                                   std::memory_order_relaxed);
    written_.store(index, std::memory_order_release);
}

bool Monitor::latest(float *out, std::size_t count) const
{
    const std::uint64_t end = written_.load(std::memory_order_acquire);
    if (count > kSize || end < count)
        return false;
    const std::uint64_t start = end - count;
    for (std::size_t i = 0; i < count; ++i)
        out[i] = ring_[static_cast<std::size_t>(start + i) & (kSize - 1)].load(
            std::memory_order_relaxed);
    return true;
}

Monitor &music_monitor()
{
    static Monitor monitor;
    return monitor;
}

} // namespace hui::audio
