// Afterglow - AAC and AAC+ stream decoding with FAAD2 (console).
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Radio AAC comes as ADTS: a 7-byte header before every frame. FAAD2 finds
// the first header itself (NeAACDecInit), then decodes frame by frame; SBR
// (AAC+) doubles the output rate and parametric stereo turns one channel into
// two, so rate and channels are read from every frame, not from the header.
// After damage the decoder is moved to the next header it can find.

#include "audio/stream_decoder.hpp"

#include <neaacdec.h>

#include <algorithm>

namespace hui::audio
{

namespace
{

// FAAD2 needs a whole frame in hand; a frame is at most 768 bytes a channel.
constexpr std::size_t kLookAhead = 6 * 1024;

class AacStream final : public StreamDecoder
{
  public:
    AacStream() : handle_(NeAACDecOpen())
    {
        if (handle_ != nullptr)
        {
            NeAACDecConfigurationPtr config = NeAACDecGetCurrentConfiguration(handle_);
            config->outputFormat = FAAD_FMT_FLOAT;
            config->downMatrix = 1; // 5.1 down to stereo
            config->dontUpSampleImplicitSBR = 0;
            NeAACDecSetConfiguration(handle_, config);
        }
    }
    ~AacStream() override
    {
        if (handle_ != nullptr)
            NeAACDecClose(handle_);
    }
    AacStream(const AacStream &) = delete;
    AacStream &operator=(const AacStream &) = delete;

    void feed(const std::uint8_t *data, std::size_t size) override
    {
        if (start_ > 0 && start_ >= bytes_.size() / 2)
        {
            bytes_.erase(bytes_.begin(), bytes_.begin() + static_cast<std::ptrdiff_t>(start_));
            start_ = 0;
        }
        bytes_.insert(bytes_.end(), data, data + size);
    }

    int decode(std::vector<float> &out, bool final) override
    {
        if (handle_ == nullptr)
            return 0;
        const std::size_t before = out.size();
        if (!started_)
        {
            if (bytes_.size() - start_ < kLookAhead && !final)
                return 0;
            unsigned long rate = 0;
            unsigned char channels = 0;
            const long skip =
                NeAACDecInit(handle_, bytes_.data() + start_,
                             static_cast<unsigned long>(bytes_.size() - start_), &rate, &channels);
            if (skip < 0)
            {
                // No header in sight: drop most of it and wait for more.
                const std::size_t drop = (bytes_.size() - start_) / 2;
                start_ += drop;
                skipped_ += drop;
                return 0;
            }
            start_ += static_cast<std::size_t>(skip);
            skipped_ += static_cast<std::size_t>(skip);
            started_ = true;
        }
        while (bytes_.size() - start_ > (final ? 0 : kLookAhead))
        {
            NeAACDecFrameInfo info{};
            void *pcm = NeAACDecDecode(handle_, &info, bytes_.data() + start_,
                                       static_cast<unsigned long>(bytes_.size() - start_));
            if (info.error != 0)
            {
                // Damaged: step past this byte and look for the next header.
                ++start_;
                ++skipped_;
                while (bytes_.size() - start_ >= 2 &&
                       !(bytes_[start_] == 0xFF && (bytes_[start_ + 1] & 0xF6) == 0xF0))
                {
                    ++start_;
                    ++skipped_;
                }
                continue;
            }
            if (info.bytesconsumed == 0)
                break;
            start_ += info.bytesconsumed;
            if (pcm == nullptr || info.samples == 0 || info.channels == 0)
                continue;
            rate_ = static_cast<int>(info.samplerate);
            channels_ = info.channels;
            const int frames = static_cast<int>(info.samples / info.channels);
            const auto *samples = static_cast<const float *>(pcm);
            stereo_.resize(static_cast<std::size_t>(frames) * 2);
            for (int i = 0; i < frames; ++i)
            {
                const float left = samples[static_cast<std::size_t>(i) * info.channels];
                const float right = info.channels > 1
                                        ? samples[static_cast<std::size_t>(i) * info.channels + 1]
                                        : left;
                stereo_[static_cast<std::size_t>(i) * 2] = left;
                stereo_[static_cast<std::size_t>(i) * 2 + 1] = right;
            }
            resampler_.set_rate(rate_);
            resampler_.process(stereo_.data(), frames, out);
        }
        return static_cast<int>((out.size() - before) / 2);
    }

    int rate() const override
    {
        return rate_;
    }
    int channels() const override
    {
        return channels_;
    }
    std::size_t skipped() const override
    {
        return skipped_;
    }
    const char *name() const override
    {
        return "AAC";
    }

  private:
    NeAACDecHandle handle_;
    bool started_ = false;
    std::vector<std::uint8_t> bytes_;
    std::size_t start_ = 0;
    std::vector<float> stereo_;
    Resampler resampler_;
    int rate_ = 0;
    int channels_ = 0;
    std::size_t skipped_ = 0;
};

} // namespace

std::unique_ptr<StreamDecoder> make_aac_decoder()
{
    auto decoder = std::make_unique<AacStream>();
    return decoder;
}

} // namespace hui::audio
