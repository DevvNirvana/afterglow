// ps5-homebrew-ui - Real-time stereo mixer (48 kHz, S16 output).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "audio/stream_ring.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace hui::audio
{

constexpr int kSampleRate = 48000;

enum class Bus : std::uint8_t
{
    ui,
    sfx,
    music,
    count,
};

// Immutable PCM owned by the caller; must outlive every voice playing it.
struct Clip
{
    const float *samples = nullptr; // interleaved stereo, -1..1
    std::size_t frames = 0;
};

enum class Wave : std::uint8_t
{
    sine,
    triangle,
    soft_square,
};

// Synthesized placeholder sound: a pitch sweep with an attack/release envelope.
struct Tone
{
    Wave wave = Wave::sine;
    float start_hz = 440.0f;
    float end_hz = 440.0f;
    float seconds = 0.08f;
    float attack = 0.004f;
    float release = 0.03f;
};

// Afterglow: tone shaping for the music streams (the radio and the songs).
enum class Eq : std::uint8_t
{
    flat,
    bass,  // a low shelf: more weight under the kick
    club,  // bass, presence and air, like a club rig
    vocal, // less boom, a lift where voices sit
    count,
};
const char *eq_name(Eq eq);

struct PlayParams
{
    float gain = 1.0f;
    float pitch = 1.0f; // playback-rate multiplier for clips, frequency multiplier for tones
    float pan = 0.0f;   // -1 left .. 1 right
    Bus bus = Bus::sfx;
};

// The game thread posts commands; the audio thread renders. Posting never
// blocks or allocates; if the queue is full the command is dropped.
class Mixer
{
  public:
    static constexpr std::size_t kVoices = 32;
    static constexpr std::size_t kQueue = 256;
    // Music decks: the songs, and two for the radio so one station can fade
    // into the next.
    static constexpr std::size_t kStreams = 3;
    static constexpr std::size_t kMaxGrain = 1024; // frames per render call

    Mixer();

    // ---- game thread ----
    bool play_clip(const Clip *clip, const PlayParams &params);
    bool play_tone(const Tone &tone, const PlayParams &params);
    bool set_bus_gain(Bus bus, float gain);
    bool set_master_gain(float gain);
    bool stop_all();
    // Attach before the audio thread starts; streams play on the music bus.
    void attach_stream(std::size_t slot, StreamRing *ring)
    {
        if (slot < kStreams)
            streams_[slot] = ring;
    }
    // Switches a slot to another ring on the audio thread; the caller keeps
    // the old ring alive for a few grains.
    bool swap_stream(std::size_t slot, StreamRing *ring);
    // Ramps a stream's gain to gain over seconds (0 = at once).
    bool set_stream_gain(std::size_t slot, float gain, float seconds);
    // Afterglow: extra gain on the music streams before the limiter (1 to 4,
    // that is up to +12 dB) and their EQ. Quiet stations and the controller's
    // headphone jack need the first; the limiter keeps it from clipping.
    bool set_music_boost(float gain);
    bool set_music_eq(Eq eq);

    // ---- audio thread ----
    // Renders frames of interleaved stereo S16 at kSampleRate.
    void render(std::int16_t *out, int frames);
    int active_voices() const
    {
        return active_.load(std::memory_order_relaxed);
    }
    // Grains where an audible stream ran dry (the decoder fell behind).
    std::uint64_t stream_underruns() const
    {
        return underruns_.load(std::memory_order_relaxed);
    }
    std::uint64_t dropped_commands() const
    {
        return dropped_.load(std::memory_order_relaxed);
    }

    // The loudest output sample of the last grain (0..1, after the limiter)
    // and the limiter's gain then (1: not limiting). Any thread may read.
    float output_peak() const
    {
        return peak_.load(std::memory_order_relaxed);
    }
    float limiter_gain() const
    {
        return reduction_.load(std::memory_order_relaxed);
    }

    // Soft limiter applied to the final mix: linear below the knee, never
    // louder than kCeiling (-1 dBFS).
    static constexpr float kKnee = 0.7f;
    static constexpr float kCeiling = 0.891f;
    static float limit(float sample);
    // Before the soft limiter, a peak limiter (instant attack, slow release)
    // holds the mix under this, so a boosted song is turned down rather than
    // flattened into distortion.
    static constexpr float kLimitLevel = 0.84f;
    static constexpr float kMaxBoost = 4.0f;

  private:
    enum class CommandType : std::uint8_t
    {
        clip,
        tone,
        bus_gain,
        master_gain,
        stop_all,
        stream_gain,
        stream_swap,
        music_boost,
        music_eq,
    };
    // One second-order section (RBJ cookbook), stereo state, transposed
    // direct form II.
    struct Biquad
    {
        float b0 = 1.0f;
        float b1 = 0.0f;
        float b2 = 0.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;
        float z1[2] = {0.0f, 0.0f};
        float z2[2] = {0.0f, 0.0f};
        float run(float x, int channel)
        {
            const float y = b0 * x + z1[channel];
            z1[channel] = b1 * x - a1 * y + z2[channel];
            z2[channel] = b2 * x - a2 * y;
            return y;
        }
    };
    static constexpr int kEqStages = 3;
    struct Command
    {
        CommandType type = CommandType::stop_all;
        const Clip *clip = nullptr;
        Tone tone{};
        PlayParams params{};
        float value = 0.0f;
        float seconds = 0.0f;
        std::size_t slot = 0;
        StreamRing *ring = nullptr;
    };
    struct Voice
    {
        bool active = false;
        bool is_tone = false;
        const Clip *clip = nullptr;
        double position = 0.0; // frames (clips) or seconds (tones)
        double phase = 0.0;
        Tone tone{};
        PlayParams params{};
        float left = 1.0f;
        float right = 1.0f;
        std::uint32_t age = 0;
    };

    bool post(const Command &command);
    void render_chunk(std::int16_t *out, int frames);
    void apply(const Command &command);
    Voice &allocate_voice();
    static float tone_sample(Voice &voice);

    std::array<Command, kQueue> queue_{};
    std::atomic<std::size_t> head_{0}; // written by the game thread
    std::atomic<std::size_t> tail_{0}; // written by the audio thread
    std::array<Voice, kVoices> voices_{};
    std::array<float, static_cast<std::size_t>(Bus::count)> bus_gain_{};
    std::array<float, static_cast<std::size_t>(Bus::count)> bus_target_{};
    float master_gain_ = 1.0f;
    float master_target_ = 1.0f;
    std::uint32_t voice_clock_ = 0;
    std::atomic<int> active_{0};
    std::atomic<std::uint64_t> dropped_{0};
    std::array<StreamRing *, kStreams> streams_{};
    std::array<float, kStreams> stream_gain_{};
    std::array<float, kStreams> stream_target_{};
    std::array<float, kStreams> stream_step_{};
    std::array<std::array<float, kMaxGrain * 2>, kStreams> stream_buffer_{};
    std::atomic<std::uint64_t> underruns_{0};

    void set_eq(Eq eq);
    float boost_ = 1.0f;
    float boost_target_ = 1.0f;
    std::array<Biquad, kEqStages> eq_{};
    int eq_stages_ = 0;
    float limiter_ = 1.0f;
    std::atomic<float> peak_{0.0f};
    std::atomic<float> reduction_{1.0f};
};

// Afterglow: the app's mixer, for screens that shape the sound (null in tests).
Mixer *main_mixer();
void set_main_mixer(Mixer *mixer);

} // namespace hui::audio
