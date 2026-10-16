// Afterglow - Stream decoders for the radio: MP3 (minimp3), AAC (platform),
// Ogg Vorbis (stb_vorbis) and Ogg Opus (libopus), resampling.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/stream_decoder.hpp"

#include "audio/mixer.hpp"
#include "third_party/minimp3/mp3.h"
#include "third_party/opus/opus.h"
#include "third_party/stb/vorbis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace hui::audio
{

namespace
{

std::string lower(std::string text)
{
    for (char &c : text)
        c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return text;
}

// An ADTS header: 12 sync bits, then MPEG version, layer 0, and a frame
// length that leads to the next header.
bool adts_at(const std::uint8_t *p, std::size_t size)
{
    if (size < 7 || p[0] != 0xFF || (p[1] & 0xF6) != 0xF0)
        return false;
    const std::size_t length = (static_cast<std::size_t>(p[3] & 0x03) << 11) |
                               (static_cast<std::size_t>(p[4]) << 3) |
                               static_cast<std::size_t>(p[5] >> 5);
    if (length < 7)
        return false;
    return length + 2 > size || (p[length] == 0xFF && (p[length + 1] & 0xF6) == 0xF0);
}

// An MPEG audio frame header (layer I to III).
bool mpeg_at(const std::uint8_t *p, std::size_t size)
{
    return size >= 4 && p[0] == 0xFF && (p[1] & 0xE0) == 0xE0 && (p[1] & 0x06) != 0 &&
           (p[2] & 0xF0) != 0xF0 && (p[2] & 0x0C) != 0x0C;
}

} // namespace

const char *codec_name(Codec codec)
{
    switch (codec)
    {
    case Codec::mp3:
        return "MP3";
    case Codec::aac:
        return "AAC";
    case Codec::ogg:
        return "Ogg";
    case Codec::unknown:
        break;
    }
    return "?";
}

Codec detect_codec(const std::string &content_type, const std::uint8_t *data, std::size_t size)
{
    const std::string type = lower(content_type);
    if (type.find("ogg") != std::string::npos || type.find("opus") != std::string::npos ||
        type.find("vorbis") != std::string::npos)
        return Codec::ogg;
    if (size >= 4 && std::memcmp(data, "OggS", 4) == 0)
        return Codec::ogg;
    if (type.find("aac") != std::string::npos || type.find("mp4a") != std::string::npos)
        return Codec::aac;
    if (type.find("mpeg") != std::string::npos || type.find("mp3") != std::string::npos)
        return Codec::mp3;
    // No useful type: look at the bytes. An ID3 tag is put before MP3.
    if (size >= 3 && std::memcmp(data, "ID3", 3) == 0)
        return Codec::mp3;
    for (std::size_t i = 0; i + 8 < size; ++i)
    {
        if (adts_at(data + i, size - i))
            return Codec::aac;
        if (mpeg_at(data + i, size - i))
            return Codec::mp3;
    }
    return Codec::unknown;
}

bool is_unsupported_type(const std::string &content_type)
{
    const std::string type = lower(content_type);
    return type.find("flac") != std::string::npos || type.find("mpegurl") != std::string::npos ||
           type.find("text/html") != std::string::npos;
}

std::unique_ptr<StreamDecoder> make_decoder(Codec codec)
{
    if (codec == Codec::mp3)
        return std::make_unique<Mp3Stream>();
    if (codec == Codec::aac)
        return make_aac_decoder();
    if (codec == Codec::ogg)
        return std::make_unique<OggStream>();
    return nullptr;
}

// ---- Resampler ----

void Resampler::set_rate(int rate)
{
    if (rate != rate_)
    {
        rate_ = rate;
        position_ = 0.0;
    }
}

void Resampler::process(const float *stereo, int frames, std::vector<float> &out)
{
    if (rate_ <= 0 || frames <= 0)
        return;
    if (rate_ == kSampleRate)
    {
        out.insert(out.end(), stereo, stereo + static_cast<std::ptrdiff_t>(frames) * 2);
        last_[0] = stereo[(frames - 1) * 2];
        last_[1] = stereo[(frames - 1) * 2 + 1];
        return;
    }
    // position_ runs from -1 (between the last frame of the previous call and
    // the first of this one) up to frames - 1.
    const double step = static_cast<double>(rate_) / static_cast<double>(kSampleRate);
    while (position_ < static_cast<double>(frames - 1))
    {
        const double floor_position = std::floor(position_);
        const int index = static_cast<int>(floor_position);
        const auto t = static_cast<float>(position_ - floor_position);
        for (int c = 0; c < 2; ++c)
        {
            const float a = index < 0 ? last_[c] : stereo[index * 2 + c];
            const float b = stereo[(index + 1) * 2 + c];
            out.push_back(a + (b - a) * t);
        }
        position_ += step;
    }
    position_ -= static_cast<double>(frames);
    last_[0] = stereo[(frames - 1) * 2];
    last_[1] = stereo[(frames - 1) * 2 + 1];
}

// ---- Mp3Stream ----

struct Mp3Stream::Decoder
{
    mp3dec_t state;
};

Mp3Stream::Mp3Stream()
    : decoder_(std::make_unique<Decoder>()), pcm_(MINIMP3_MAX_SAMPLES_PER_FRAME),
      stereo_(MINIMP3_MAX_SAMPLES_PER_FRAME * 2)
{
    mp3dec_init(&decoder_->state);
}

Mp3Stream::~Mp3Stream() = default;

void Mp3Stream::feed(const std::uint8_t *data, std::size_t size)
{
    // Keep the buffer small: drop what was consumed before appending.
    if (start_ > 0 && start_ >= bytes_.size() / 2)
    {
        bytes_.erase(bytes_.begin(), bytes_.begin() + static_cast<std::ptrdiff_t>(start_));
        start_ = 0;
    }
    bytes_.insert(bytes_.end(), data, data + size);
}

int Mp3Stream::decode(std::vector<float> &out, bool final)
{
    // minimp3 wants a few frames of look-ahead to lock on; 16 KiB covers the
    // largest frames with room to spare.
    constexpr std::size_t kLookAhead = 16 * 1024;
    const std::size_t before = out.size();
    while (bytes_.size() - start_ > (final ? 0 : kLookAhead))
    {
        mp3dec_frame_info_t info{};
        const int samples =
            mp3dec_decode_frame(&decoder_->state, bytes_.data() + start_,
                                static_cast<int>(bytes_.size() - start_), pcm_.data(), &info);
        if (info.frame_bytes <= 0)
            break; // needs more data
        start_ += static_cast<std::size_t>(info.frame_bytes);
        if (samples <= 0)
        {
            skipped_ += static_cast<std::size_t>(info.frame_bytes);
            continue;
        }
        rate_ = info.hz;
        channels_ = info.channels;
        resampler_.set_rate(info.hz);
        const float *stereo = pcm_.data();
        if (info.channels == 1)
        {
            for (int i = 0; i < samples; ++i)
            {
                stereo_[static_cast<std::size_t>(i) * 2] = pcm_[static_cast<std::size_t>(i)];
                stereo_[static_cast<std::size_t>(i) * 2 + 1] = pcm_[static_cast<std::size_t>(i)];
            }
            stereo = stereo_.data();
        }
        resampler_.process(stereo, samples, out);
    }
    return static_cast<int>((out.size() - before) / 2);
}

// ---- Ogg ----

std::uint32_t OggReader::crc(const std::uint8_t *data, std::size_t size)
{
    static const auto kTable = []
    {
        std::array<std::uint32_t, 256> table{};
        for (std::uint32_t i = 0; i < 256; ++i)
        {
            std::uint32_t r = i << 24;
            for (int k = 0; k < 8; ++k)
                r = (r & 0x80000000u) != 0 ? (r << 1) ^ 0x04C11DB7u : r << 1;
            table[i] = r;
        }
        return table;
    }();
    std::uint32_t crc = 0;
    for (std::size_t i = 0; i < size; ++i)
        crc = (crc << 8) ^ kTable[((crc >> 24) ^ data[i]) & 0xFF];
    return crc;
}

void OggReader::feed(const std::uint8_t *data, std::size_t size)
{
    // Drop what has been read once it is most of the buffer.
    if (start_ > 0 && start_ * 2 > bytes_.size())
    {
        bytes_.erase(bytes_.begin(), bytes_.begin() + static_cast<std::ptrdiff_t>(start_));
        start_ = 0;
    }
    bytes_.insert(bytes_.end(), data, data + size);
}

bool OggReader::next(Page *page)
{
    for (;;)
    {
        const std::size_t left = bytes_.size() - start_;
        const std::uint8_t *p = bytes_.data() + start_;
        if (left < 27)
            return false;
        if (std::memcmp(p, "OggS", 4) != 0 || p[4] != 0)
        {
            // Out of step: to the next capture pattern.
            std::size_t skip = 1;
            while (skip + 4 <= left && std::memcmp(p + skip, "OggS", 4) != 0)
                ++skip;
            if (skip + 4 > left)
                skip = left - 3;
            start_ += skip;
            skipped_ += skip;
            continue;
        }
        const std::size_t segments = p[26];
        if (left < 27 + segments)
            return false;
        std::size_t body = 0;
        for (std::size_t i = 0; i < segments; ++i)
            body += p[27 + i];
        const std::size_t length = 27 + segments + body;
        if (left < length)
            return false;
        // The CRC is computed with its own field as zeros.
        std::vector<std::uint8_t> bytes(p, p + length);
        const std::uint32_t stored = static_cast<std::uint32_t>(bytes[22]) |
                                     static_cast<std::uint32_t>(bytes[23]) << 8 |
                                     static_cast<std::uint32_t>(bytes[24]) << 16 |
                                     static_cast<std::uint32_t>(bytes[25]) << 24;
        std::memset(bytes.data() + 22, 0, 4);
        if (crc(bytes.data(), bytes.size()) != stored)
        {
            start_ += 1;
            skipped_ += 1;
            continue;
        }
        std::memcpy(bytes.data() + 22, p + 22, 4);
        page->first = (p[5] & 0x02) != 0;
        page->continued = (p[5] & 0x01) != 0;
        page->serial = static_cast<std::uint32_t>(p[14]) | static_cast<std::uint32_t>(p[15]) << 8 |
                       static_cast<std::uint32_t>(p[16]) << 16 |
                       static_cast<std::uint32_t>(p[17]) << 24;
        page->segments = segments;
        page->body = 27 + segments;
        page->bytes = std::move(bytes);
        start_ += length;
        return true;
    }
}

OggStream::OggStream() = default;

OggStream::~OggStream()
{
    reset();
}

void OggStream::reset()
{
    if (opus_ != nullptr)
        opus_decoder_destroy(opus_);
    opus_ = nullptr;
    if (vorbis_ != nullptr)
        stb_vorbis_close(vorbis_);
    vorbis_ = nullptr;
    vorbis_bytes_.clear();
    packet_.clear();
    packets_ = 0;
    pre_skip_ = 0;
    kind_ = Kind::none;
}

void OggStream::feed(const std::uint8_t *data, std::size_t size)
{
    reader_.feed(data, size);
}

int OggStream::decode(std::vector<float> &out, bool final)
{
    (void) final;
    const std::size_t before = out.size();
    OggReader::Page p;
    while (reader_.next(&p))
        page(p, out);
    if (kind_ == Kind::vorbis)
        vorbis_decode(out);
    return static_cast<int>((out.size() - before) / 2);
}

bool OggStream::take_title(std::string *title)
{
    if (!title_new_)
        return false;
    title_new_ = false;
    *title = title_;
    return true;
}

void OggStream::comments(const std::vector<std::string> &fields)
{
    std::string artist;
    std::string title;
    for (const std::string &field : fields)
    {
        const std::size_t equals = field.find('=');
        if (equals == std::string::npos)
            continue;
        const std::string key = lower(field.substr(0, equals));
        if (key == "artist" && artist.empty())
            artist = field.substr(equals + 1);
        else if (key == "title" && title.empty())
            title = field.substr(equals + 1);
    }
    // "Artist - Title", as stations send in ICY.
    const std::string song = !artist.empty() && !title.empty() ? artist + " - " + title : title;
    if (!song.empty() && song != title_)
    {
        title_ = song;
        title_new_ = true;
    }
}

void OggStream::page(const OggReader::Page &p, std::vector<float> &out)
{
    if (p.first || (kind_ == Kind::none && packets_ == 0))
    {
        // A new logical stream (the next song of a chain): new headers.
        if (p.first)
        {
            if (kind_ == Kind::vorbis)
                vorbis_decode(out); // what is left of the last song
            reset();
            serial_ = p.serial;
        }
        const std::uint8_t *body = p.bytes.data() + p.body;
        const std::size_t size = p.bytes.size() - p.body;
        if (size >= 8 && std::memcmp(body, "OpusHead", 8) == 0)
            kind_ = Kind::opus;
        else if (size >= 7 && body[0] == 1 && std::memcmp(body + 1, "vorbis", 6) == 0)
            kind_ = Kind::vorbis;
    }
    if (p.serial != serial_ || kind_ == Kind::none)
    {
        ignored_ += p.bytes.size(); // another kind of stream, or one we joined midway
        return;
    }
    if (kind_ == Kind::vorbis)
    {
        vorbis_bytes_.insert(vorbis_bytes_.end(), p.bytes.begin(), p.bytes.end());
        return;
    }
    // Opus: put packets together from the page's segments.
    std::size_t at = p.body;
    if (!p.continued)
        packet_.clear();
    for (std::size_t i = 0; i < p.segments; ++i)
    {
        const std::size_t length = p.bytes[27 + i];
        packet_.insert(packet_.end(), p.bytes.begin() + static_cast<std::ptrdiff_t>(at),
                       p.bytes.begin() + static_cast<std::ptrdiff_t>(at + length));
        at += length;
        if (length < 255)
        {
            opus_packet(packet_, out);
            packet_.clear();
        }
    }
}

void OggStream::opus_packet(const std::vector<std::uint8_t> &packet, std::vector<float> &out)
{
    const int index = packets_++;
    if (index == 0)
    {
        // OpusHead: version, channels, pre-skip, rate, gain, mapping.
        if (packet.size() < 19)
        {
            kind_ = Kind::none;
            return;
        }
        const int channels = packet[9];
        pre_skip_ = packet[10] | packet[11] << 8;
        const int mapping = packet[18];
        if (mapping != 0 || channels < 1 || channels > 2)
        {
            kind_ = Kind::none; // surround: not for a radio
            return;
        }
        int error = 0;
        opus_ = opus_decoder_create(48000, channels, &error);
        if (opus_ == nullptr || error != OPUS_OK)
        {
            opus_ = nullptr;
            kind_ = Kind::none;
            return;
        }
        channels_ = channels;
        rate_ = 48000;
        return;
    }
    if (index == 1)
    {
        // OpusTags: a vendor string, then "KEY=value" fields.
        std::vector<std::string> fields;
        const auto le = [&](std::size_t at)
        {
            return static_cast<std::uint32_t>(packet[at]) |
                   static_cast<std::uint32_t>(packet[at + 1]) << 8 |
                   static_cast<std::uint32_t>(packet[at + 2]) << 16 |
                   static_cast<std::uint32_t>(packet[at + 3]) << 24;
        };
        std::size_t at = 8;
        if (packet.size() >= at + 4)
        {
            at += 4 + le(at);
            if (packet.size() >= at + 4)
            {
                const std::uint32_t count = le(at);
                at += 4;
                for (std::uint32_t i = 0; i < count && i < 256 && at + 4 <= packet.size(); ++i)
                {
                    const std::uint32_t length = le(at);
                    at += 4;
                    if (at + length > packet.size())
                        break;
                    fields.emplace_back(reinterpret_cast<const char *>(packet.data() + at), length);
                    at += length;
                }
            }
        }
        comments(fields);
        return;
    }
    if (opus_ == nullptr || packet.empty())
        return;
    constexpr int kMaxFrames = 5760; // 120 ms at 48 kHz
    pcm_.resize(static_cast<std::size_t>(kMaxFrames * channels_));
    const int frames = opus_decode_float(
        opus_, packet.data(), static_cast<opus_int32>(packet.size()), pcm_.data(), kMaxFrames, 0);
    if (frames <= 0)
    {
        ignored_ += packet.size();
        return;
    }
    // The first samples of a stream are the encoder's warm-up.
    const int skip = std::min(pre_skip_, frames);
    pre_skip_ -= skip;
    emit(pcm_.data() + static_cast<std::ptrdiff_t>(skip * channels_), frames - skip, channels_,
         out);
}

void OggStream::vorbis_decode(std::vector<float> &out)
{
    std::size_t used = 0;
    if (vorbis_ == nullptr)
    {
        int consumed = 0;
        int error = 0;
        vorbis_ =
            stb_vorbis_open_pushdata(vorbis_bytes_.data(), static_cast<int>(vorbis_bytes_.size()),
                                     &consumed, &error, nullptr);
        if (vorbis_ == nullptr)
        {
            if (error != VORBIS_need_more_data || vorbis_bytes_.size() > (1u << 20))
            {
                ignored_ += vorbis_bytes_.size();
                vorbis_bytes_.clear();
                kind_ = Kind::none; // headers that do not decode
            }
            return;
        }
        used = static_cast<std::size_t>(consumed);
        const stb_vorbis_info info = stb_vorbis_get_info(vorbis_);
        rate_ = static_cast<int>(info.sample_rate);
        channels_ = info.channels;
        resampler_.set_rate(rate_);
        const stb_vorbis_comment tags = stb_vorbis_get_comment(vorbis_);
        std::vector<std::string> fields;
        for (int i = 0; i < tags.comment_list_length; ++i)
            fields.emplace_back(tags.comment_list[i]);
        comments(fields);
    }
    for (;;)
    {
        int channels = 0;
        float **output = nullptr;
        int samples = 0;
        const int n = stb_vorbis_decode_frame_pushdata(
            vorbis_, vorbis_bytes_.data() + used, static_cast<int>(vorbis_bytes_.size() - used),
            &channels, &output, &samples);
        if (n == 0 && samples == 0)
            break; // more data needed
        used += static_cast<std::size_t>(n);
        if (samples <= 0 || output == nullptr || channels <= 0)
            continue;
        pcm_.resize(static_cast<std::size_t>(samples * std::min(channels, 2)));
        const int keep = std::min(channels, 2);
        for (int i = 0; i < samples; ++i)
            for (int c = 0; c < keep; ++c)
                pcm_[static_cast<std::size_t>(i * keep + c)] = output[c][i];
        emit(pcm_.data(), samples, keep, out);
    }
    vorbis_bytes_.erase(vorbis_bytes_.begin(),
                        vorbis_bytes_.begin() + static_cast<std::ptrdiff_t>(used));
}

void OggStream::emit(const float *interleaved, int frames, int channels, std::vector<float> &out)
{
    if (frames <= 0)
        return;
    stereo_.resize(static_cast<std::size_t>(frames) * 2);
    for (int i = 0; i < frames; ++i)
    {
        const float l = interleaved[i * channels];
        const float r = channels > 1 ? interleaved[i * channels + 1] : l;
        stereo_[static_cast<std::size_t>(i) * 2] = l;
        stereo_[static_cast<std::size_t>(i) * 2 + 1] = r;
    }
    resampler_.set_rate(rate_);
    resampler_.process(stereo_.data(), frames, out);
}

} // namespace hui::audio
