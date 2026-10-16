// Afterglow - The listener's own music: songs on a USB drive.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The drives the console mounts (/mnt/usb0 to /mnt/usb7) are searched for
// songs; each becomes a Station in the "My Music" category whose address is
// file://<path>, so the radio's player plays it (with the same decoders,
// cross-fades, visuals, song cards, likes and history) and moves on to the
// next song when one ends. Titles, artists, albums and track numbers come
// from the songs' tags (ID3 in MP3 and AAC files, Vorbis comments in Ogg),
// read without loading the files: a scan of a few thousand songs reads a few
// hundred bytes of each. A cover inside a song is found the same way and
// fetched only when it is shown (embedded_art).

#pragma once

#include "audio/radio.hpp"
#include "net/http.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace hui::audio
{

inline constexpr const char *kLibraryCategory = "My Music";

struct TrackTags
{
    std::string title;
    std::string artist;
    std::string album;
    int track = 0;
    bool has_art = false; // a picture is embedded
};

// Reads `size` bytes at `offset` into out (fewer at the end); false on an error.
using ReadAt = std::function<bool(std::uint64_t offset, std::size_t size, std::string *out)>;

// The tags of a song, read through `read`: ID3v2 (2.2 to 2.4) at the start,
// else ID3v1 at the end (size: the file's length), else Vorbis comments in
// the first Ogg pages. False when there are none.
bool read_tags(const ReadAt &read, std::uint64_t size, TrackTags *tags);
// The bytes of the front cover embedded in a song (ID3 APIC/PIC), or "".
std::string embedded_art(const ReadAt &read);

bool is_music_file(const std::string &name);
// "file:///mnt/usb0/a.mp3" -> "/mnt/usb0/a.mp3"; "" if it is not a file address.
std::string file_path(const std::string &url);
// Where a song's cover is asked for (a station "logo" address).
std::string art_address(const std::string &url);
// The song a cover address belongs to, or "".
std::string art_owner(const std::string &address);

// Every song under the roots (at most max_tracks, max_depth folders deep),
// sorted by folder, then track number, then name. logos: the cover address
// of each song that has one, keyed by its own address.
std::vector<Station> scan_library(const std::vector<std::string> &roots,
                                  std::map<std::string, std::string> *logos,
                                  std::size_t max_tracks = 4000, int max_depth = 6);

// The tags of a song file ("" fields when it has none).
TrackTags file_tags(const std::string &path);
// The cover inside a song file, or "".
std::string file_art(const std::string &path);

// The USB drives mounted now.
std::vector<std::string> usb_roots();

// A file as a stream for the player (ID3v2 skipped: a big cover in it is not
// audio); null with an error when it cannot be opened.
net::OpenResult open_file(const std::string &url);

} // namespace hui::audio
