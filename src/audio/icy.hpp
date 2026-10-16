// Afterglow - ICY metadata: the song titles a station puts between its audio.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Asked for with "Icy-MetaData: 1", a SHOUTcast or Icecast server sends
// "icy-metaint: N" and then, after every N bytes of audio, one length byte L
// and L x 16 bytes of text such as "StreamTitle='Artist - Song';". The reader
// splits the two: audio goes on to the decoder, titles to the screen.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hui::audio
{

class IcyReader
{
  public:
    explicit IcyReader(int metaint = 0) : metaint_(metaint > 0 ? metaint : 0), left_(metaint_)
    {
    }

    // Appends the audio bytes of data to audio.
    void feed(const std::uint8_t *data, std::size_t size, std::vector<std::uint8_t> &audio);

    // The newest title, once; false when nothing new arrived.
    bool take_title(std::string *title);

  private:
    void finish_block();

    int metaint_;
    int left_;           // audio bytes until the next length byte
    int meta_left_ = -1; // metadata bytes still to come; -1: in audio
    std::string block_;
    std::string title_;
    bool fresh_ = false;
};

// The StreamTitle of a metadata block ("" if it has none).
std::string icy_title(const std::string &block);

} // namespace hui::audio
