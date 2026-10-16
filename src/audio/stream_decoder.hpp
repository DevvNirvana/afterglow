// Afterglow - Stream decoders for the radio: MP3, AAC and resampling to 48 kHz.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A decoder is fed the stream's bytes in pieces of any size and hands back
// 48 kHz stereo frames, whatever the station sends. MP3 is decoded by the
// vendored minimp3 everywhere; AAC (and AAC+, with SBR and parametric
// stereo) by FAAD2 on the console, where PacBrew provides it.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct OpusDecoder; // libopus
struct stb_vorbis;  // stb_vorbis

namespace hui::audio
{

enum class Codec : std::uint8_t
{
    unknown,
    mp3,
    aac,
    ogg, // Vorbis or Opus in Ogg pages (OggStream tells which)
};

const char *codec_name(Codec codec);

// From the Content-Type, else from the first bytes (an ID3 tag, an MPEG
// frame header or an ADTS header).
Codec detect_codec(const std::string &content_type, const std::uint8_t *data, std::size_t size);

// Types the radio cannot play (Ogg, Opus, FLAC, HLS playlists, web pages).
bool is_unsupported_type(const std::string &content_type);

// Turns stereo frames at any rate into 48 kHz stereo (linear interpolation).
class Resampler
{
  public:
    void set_rate(int rate);
    // Appends the resampled frames to out.
    void process(const float *stereo, int frames, std::vector<float> &out);

  private:
    int rate_ = 0;
    double position_ = 0.0; // into the incoming frames, carried across calls
    float last_[2] = {0.0f, 0.0f};
};

class StreamDecoder
{
  public:
    virtual ~StreamDecoder() = default;
    virtual void feed(const std::uint8_t *data, std::size_t size) = 0;
    // Decodes what it can; appends 48 kHz stereo frames to out. final: the
    // stream has ended, so a short tail is decoded too. Returns frames added.
    virtual int decode(std::vector<float> &out, bool final = false) = 0;
    // Of the last decoded frame (0 before the first).
    virtual int rate() const = 0;
    virtual int channels() const = 0;
    // Bytes that were not audio (tags, junk, damaged frames) so far.
    virtual std::size_t skipped() const = 0;
    // A song title the stream itself carries (Ogg streams send their tags in
    // the audio, not as ICY metadata): true once per new title.
    virtual bool take_title(std::string *title)
    {
        (void)title;
        return false;
    }
    // "MP3", "AAC", "Opus", "Vorbis".
    virtual const char *name() const = 0;
};

// MP3 or AAC; null for an unknown codec, or for AAC where there is no AAC
// decoder (the PC preview and the tests).
std::unique_ptr<StreamDecoder> make_decoder(Codec codec);

// Platform hook: FAAD2 on the console (platform/ps5/aac_faad.cpp), none on
// the PC (host/aac_host.cpp).
std::unique_ptr<StreamDecoder> make_aac_decoder();

class Mp3Stream final : public StreamDecoder
{
  public:
    Mp3Stream();
    ~Mp3Stream() override;
    Mp3Stream(const Mp3Stream &) = delete;
    Mp3Stream &operator=(const Mp3Stream &) = delete;

    void feed(const std::uint8_t *data, std::size_t size) override;
    int decode(std::vector<float> &out, bool final = false) override;
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
        return "MP3";
    }

  private:
    struct Decoder; // minimp3's state; its type is anonymous, so it is wrapped
    std::unique_ptr<Decoder> decoder_;
    std::vector<std::uint8_t> bytes_;
    std::size_t start_ = 0; // first byte not yet consumed
    std::vector<float> pcm_;
    std::vector<float> stereo_;
    Resampler resampler_;
    int rate_ = 0;
    int channels_ = 0;
    std::size_t skipped_ = 0;
};

// Ogg pages from a stream of bytes: sync, CRC check, one page at a time.
// Damaged bytes are skipped until the next page that checks out.
class OggReader
{
  public:
    struct Page
    {
        std::vector<std::uint8_t> bytes; // the whole page, header included
        std::uint32_t serial = 0;
        bool first = false;     // begins a logical stream (a new song, in a chain)
        bool continued = false; // its first packet continues the last page's
        std::size_t body = 0;   // where the packet data starts in bytes
        std::size_t segments = 0;
    };
    void feed(const std::uint8_t *data, std::size_t size);
    bool next(Page *page);
    std::size_t skipped() const
    {
        return skipped_;
    }
    static std::uint32_t crc(const std::uint8_t *data, std::size_t size);

  private:
    std::vector<std::uint8_t> bytes_;
    std::size_t start_ = 0;
    std::size_t skipped_ = 0;
};

// Ogg Vorbis (stb_vorbis) and Ogg Opus (libopus) radio streams and files,
// chains included: Icecast starts a new logical stream, with new headers and
// tags, for every song.
class OggStream final : public StreamDecoder
{
  public:
    OggStream();
    ~OggStream() override;
    OggStream(const OggStream &) = delete;
    OggStream &operator=(const OggStream &) = delete;

    void feed(const std::uint8_t *data, std::size_t size) override;
    int decode(std::vector<float> &out, bool final = false) override;
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
        return reader_.skipped() + ignored_;
    }
    bool take_title(std::string *title) override;
    const char *name() const override
    {
        return kind_ == Kind::opus ? "Opus" : "Vorbis";
    }

  private:
    enum class Kind : std::uint8_t
    {
        none,
        vorbis,
        opus,
    };
    void reset();
    void page(const OggReader::Page &page, std::vector<float> &out);
    void vorbis_decode(std::vector<float> &out);
    void opus_packet(const std::vector<std::uint8_t> &packet, std::vector<float> &out);
    void comments(const std::vector<std::string> &fields);
    void emit(const float *interleaved, int frames, int channels, std::vector<float> &out);

    OggReader reader_;
    Kind kind_ = Kind::none;
    std::uint32_t serial_ = 0;
    // Opus: packets put together from pages.
    ::OpusDecoder *opus_ = nullptr;
    std::vector<std::uint8_t> packet_;
    int packets_ = 0; // of the current logical stream
    int pre_skip_ = 0;
    // Vorbis: stb_vorbis decodes the stream's own pages.
    ::stb_vorbis *vorbis_ = nullptr;
    std::vector<std::uint8_t> vorbis_bytes_;
    std::vector<float> pcm_;
    std::vector<float> stereo_;
    Resampler resampler_;
    int rate_ = 0;
    int channels_ = 0;
    std::size_t ignored_ = 0; // packets of streams we do not decode
    std::string title_;
    bool title_new_ = false;
};

} // namespace hui::audio
