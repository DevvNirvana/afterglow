// Afterglow - The listener's own music (see library.hpp).
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/library.hpp"

#include "core/save_file.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

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

std::string extension(const std::string &name)
{
    const std::size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? std::string() : lower(name.substr(dot + 1));
}

std::uint32_t be32(const std::string &b, std::size_t at)
{
    return static_cast<std::uint32_t>(static_cast<unsigned char>(b[at])) << 24 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(b[at + 1])) << 16 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(b[at + 2])) << 8 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(b[at + 3]));
}

std::uint32_t syncsafe(const std::string &b, std::size_t at)
{
    return static_cast<std::uint32_t>(static_cast<unsigned char>(b[at]) & 0x7f) << 21 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(b[at + 1]) & 0x7f) << 14 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(b[at + 2]) & 0x7f) << 7 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(b[at + 3]) & 0x7f);
}

std::uint32_t le32(const std::string &b, std::size_t at)
{
    return static_cast<std::uint32_t>(static_cast<unsigned char>(b[at])) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(b[at + 1])) << 8 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(b[at + 2])) << 16 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(b[at + 3])) << 24;
}

void append_utf8(std::string *out, std::uint32_t cp)
{
    if (cp < 0x80)
        *out += static_cast<char>(cp);
    else if (cp < 0x800)
    {
        *out += static_cast<char>(0xC0 | (cp >> 6));
        *out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
        *out += static_cast<char>(0xE0 | (cp >> 12));
        *out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        *out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else
    {
        *out += static_cast<char>(0xF0 | (cp >> 18));
        *out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        *out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        *out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// An ID3 text (its encoding byte first) as UTF-8, up to the first nul.
std::string id3_text(const std::string &body)
{
    if (body.empty())
        return {};
    const int encoding = static_cast<unsigned char>(body[0]);
    std::string out;
    if (encoding == 0 || encoding == 3) // ISO-8859-1, UTF-8
    {
        for (std::size_t i = 1; i < body.size() && body[i] != '\0'; ++i)
        {
            const auto c = static_cast<unsigned char>(body[i]);
            if (encoding == 0)
                append_utf8(&out, c);
            else
                out += static_cast<char>(c);
        }
        return out;
    }
    // UTF-16, with a byte order mark (1) or big-endian (2).
    bool big = encoding == 2;
    std::size_t i = 1;
    if (encoding == 1 && body.size() >= 3)
    {
        const auto a = static_cast<unsigned char>(body[1]);
        const auto b = static_cast<unsigned char>(body[2]);
        if ((a == 0xFE && b == 0xFF) || (a == 0xFF && b == 0xFE))
        {
            big = a == 0xFE;
            i = 3;
        }
    }
    for (; i + 1 < body.size(); i += 2)
    {
        const auto hi = static_cast<unsigned char>(body[big ? i : i + 1]);
        const auto lo = static_cast<unsigned char>(body[big ? i + 1 : i]);
        std::uint32_t unit = static_cast<std::uint32_t>(hi) << 8 | lo;
        if (unit == 0)
            break;
        if (unit >= 0xD800 && unit < 0xDC00 && i + 3 < body.size())
        {
            const auto hi2 = static_cast<unsigned char>(body[big ? i + 2 : i + 3]);
            const auto lo2 = static_cast<unsigned char>(body[big ? i + 3 : i + 2]);
            const std::uint32_t low = static_cast<std::uint32_t>(hi2) << 8 | lo2;
            unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
            i += 2;
        }
        append_utf8(&out, unit);
    }
    return out;
}

std::string trim(std::string text)
{
    while (!text.empty() && (text.back() == ' ' || text.back() == '\0'))
        text.pop_back();
    std::size_t start = 0;
    while (start < text.size() && text[start] == ' ')
        ++start;
    return text.substr(start);
}

// Walks the frames of an ID3v2 tag through `read`. visit(id, offset of the
// body, size of the body) returns false to stop.
bool walk_id3(
    const ReadAt &read,
    const std::function<bool(const std::string &id, std::uint64_t at, std::uint32_t size)> &visit)
{
    std::string header;
    if (!read(0, 10, &header) || header.size() < 10 || header.compare(0, 3, "ID3") != 0)
        return false;
    const int major = static_cast<unsigned char>(header[3]);
    if (major < 2 || major > 4)
        return false;
    const std::uint32_t tag_size = syncsafe(header, 6);
    std::uint64_t at = 10;
    const std::uint64_t end = 10 + static_cast<std::uint64_t>(tag_size);
    if ((static_cast<unsigned char>(header[5]) & 0x40) != 0 && major >= 3)
    {
        std::string ext;
        if (!read(at, 4, &ext) || ext.size() < 4)
            return true;
        at += major == 4 ? syncsafe(ext, 0) : be32(ext, 0) + 4;
    }
    const std::size_t head = major == 2 ? 6 : 10;
    for (int frames = 0; frames < 512 && at + head <= end; ++frames)
    {
        std::string fh;
        if (!read(at, head, &fh) || fh.size() < head || fh[0] == '\0')
            break; // padding
        std::string id;
        std::uint32_t size = 0;
        if (major == 2)
        {
            id = fh.substr(0, 3);
            size = static_cast<std::uint32_t>(static_cast<unsigned char>(fh[3])) << 16 |
                   static_cast<std::uint32_t>(static_cast<unsigned char>(fh[4])) << 8 |
                   static_cast<std::uint32_t>(static_cast<unsigned char>(fh[5]));
        }
        else
        {
            id = fh.substr(0, 4);
            size = major == 4 ? syncsafe(fh, 4) : be32(fh, 4);
        }
        if (size == 0 || at + head + size > end)
            break;
        if (!visit(id, at + head, size))
            return true;
        at += head + size;
    }
    return true;
}

// Vorbis comments (Ogg Vorbis "\x03vorbis", Opus "OpusTags") in data.
bool vorbis_comments(const std::string &data, TrackTags *tags)
{
    std::size_t at = data.find("\x03vorbis");
    std::size_t skip = 7;
    if (at == std::string::npos)
    {
        at = data.find("OpusTags");
        skip = 8;
    }
    if (at == std::string::npos)
        return false;
    std::size_t p = at + skip;
    if (p + 4 > data.size())
        return false;
    p += 4 + le32(data, p); // the vendor string
    if (p + 4 > data.size())
        return false;
    const std::uint32_t count = le32(data, p);
    p += 4;
    bool any = false;
    for (std::uint32_t i = 0; i < count && i < 256 && p + 4 <= data.size(); ++i)
    {
        const std::uint32_t length = le32(data, p);
        p += 4;
        if (p + length > data.size())
            break;
        const std::string comment = data.substr(p, length);
        p += length;
        const std::size_t equals = comment.find('=');
        if (equals == std::string::npos)
            continue;
        const std::string key = lower(comment.substr(0, equals));
        const std::string value = trim(comment.substr(equals + 1));
        if (key == "title")
            tags->title = value;
        else if (key == "artist")
            tags->artist = value;
        else if (key == "album")
            tags->album = value;
        else if (key == "tracknumber")
            tags->track = std::atoi(value.c_str());
        else if (key == "metadata_block_picture")
            tags->has_art = true;
        else
            continue;
        any = true;
    }
    return any;
}

class FileStream final : public net::ByteStream
{
  public:
    explicit FileStream(int fd) : fd_(fd)
    {
    }
    ~FileStream() override
    {
        if (fd_ >= 0)
            ::close(fd_);
    }
    FileStream(const FileStream &) = delete;
    FileStream &operator=(const FileStream &) = delete;
    int read(std::uint8_t *data, int size) override
    {
        for (;;)
        {
            const ssize_t got = ::read(fd_, data, static_cast<std::size_t>(size));
            if (got < 0 && errno == EINTR)
                continue;
            return got < 0 ? -1 : static_cast<int>(got);
        }
    }

  private:
    int fd_ = -1;
};

ReadAt file_reader(int fd)
{
    return [fd](std::uint64_t offset, std::size_t size, std::string *out)
    {
        out->assign(size, '\0');
        std::size_t done = 0;
        while (done < size)
        {
            const ssize_t got =
                ::pread(fd, out->data() + done, size - done, static_cast<off_t>(offset + done));
            if (got < 0 && errno == EINTR)
                continue;
            if (got < 0)
                return false;
            if (got == 0)
                break;
            done += static_cast<std::size_t>(got);
        }
        out->resize(done);
        return true;
    };
}

std::string stem(const std::string &name)
{
    const std::size_t slash = name.find_last_of('/');
    std::string base = slash == std::string::npos ? name : name.substr(slash + 1);
    const std::size_t dot = base.find_last_of('.');
    return dot == std::string::npos ? base : base.substr(0, dot);
}

std::string format_of(const std::string &ext)
{
    if (ext == "mp3")
        return "MP3";
    if (ext == "aac")
        return "AAC";
    if (ext == "ogg" || ext == "oga")
        return "OGG";
    if (ext == "opus")
        return "OPUS";
    return lower(ext);
}

struct Found
{
    std::string path;
    std::string folder;
    TrackTags tags;
};

void walk(const std::string &folder, int depth, int max_depth, std::size_t max_tracks,
          std::vector<Found> *found)
{
    if (depth > max_depth || found->size() >= max_tracks)
        return;
    std::vector<std::string> names = save::list_files(folder);
    std::sort(names.begin(), names.end());
    for (const std::string &name : names)
    {
        if (found->size() >= max_tracks)
            return;
        if (name.empty() || name[0] == '.' || name == "System Volume Information" ||
            name == "$RECYCLE.BIN")
            continue;
        const std::string path = folder + "/" + name;
        struct stat info
        {
        };
        if (::stat(path.c_str(), &info) != 0)
            continue;
        if (S_ISDIR(info.st_mode))
        {
            walk(path, depth + 1, max_depth, max_tracks, found);
            continue;
        }
        if (!S_ISREG(info.st_mode) || !is_music_file(name))
            continue;
        Found song;
        song.path = path;
        song.folder = folder;
        const int fd = ::open(path.c_str(), O_RDONLY);
        if (fd >= 0)
        {
            read_tags(file_reader(fd), static_cast<std::uint64_t>(info.st_size), &song.tags);
            ::close(fd);
        }
        found->push_back(std::move(song));
    }
}

} // namespace

bool read_tags(const ReadAt &read, std::uint64_t size, TrackTags *tags)
{
    bool any = false;
    const bool id3 = walk_id3(read,
                              [&](const std::string &id, std::uint64_t at, std::uint32_t length)
                              {
                                  if (id == "APIC" || id == "PIC")
                                  {
                                      tags->has_art = true;
                                      return true;
                                  }
                                  const bool wanted = id == "TIT2" || id == "TT2" || id == "TPE1" ||
                                                      id == "TP1" || id == "TALB" || id == "TAL" ||
                                                      id == "TRCK" || id == "TRK";
                                  if (!wanted || length > 4096)
                                      return true;
                                  std::string body;
                                  if (!read(at, length, &body))
                                      return false;
                                  const std::string text = trim(id3_text(body));
                                  if (id == "TIT2" || id == "TT2")
                                      tags->title = text;
                                  else if (id == "TPE1" || id == "TP1")
                                      tags->artist = text;
                                  else if (id == "TALB" || id == "TAL")
                                      tags->album = text;
                                  else
                                      tags->track = std::atoi(text.c_str());
                                  any = any || !text.empty();
                                  return true;
                              });
    if (any)
        return true;
    // ID3v1: 128 bytes at the end.
    if (!id3 && size >= 128)
    {
        std::string v1;
        if (read(size - 128, 128, &v1) && v1.size() == 128 && v1.compare(0, 3, "TAG") == 0)
        {
            const auto field = [&](std::size_t at, std::size_t n)
            {
                std::string text;
                for (std::size_t i = at; i < at + n && v1[i] != '\0'; ++i)
                    append_utf8(&text, static_cast<unsigned char>(v1[i]));
                return trim(text);
            };
            tags->title = field(3, 30);
            tags->artist = field(33, 30);
            tags->album = field(63, 30);
            if (v1[125] == '\0' && v1[126] != '\0')
                tags->track = static_cast<unsigned char>(v1[126]);
            if (!tags->title.empty() || !tags->artist.empty())
                return true;
        }
    }
    // Ogg: the comment header sits in the first pages.
    std::string head;
    if (read(0, 64 * 1024, &head) && head.compare(0, 4, "OggS") == 0)
        return vorbis_comments(head, tags);
    return false;
}

std::string embedded_art(const ReadAt &read)
{
    std::string picture;
    walk_id3(read,
             [&](const std::string &id, std::uint64_t at, std::uint32_t length)
             {
                 if ((id != "APIC" && id != "PIC") || length > (8u << 20))
                     return true;
                 std::string body;
                 if (!read(at, length, &body) || body.size() < 4)
                     return false;
                 std::size_t p = 1; // the text encoding
                 if (id == "PIC")
                     p += 3; // an image format, "JPG"
                 else
                     p = body.find('\0', p) + 1; // the MIME type
                 if (p == 0 || p >= body.size())
                     return false;
                 const int type = static_cast<unsigned char>(body[p]);
                 ++p;
                 // The description: one nul, or two in UTF-16.
                 const bool wide = body[0] == 1 || body[0] == 2;
                 while (p + (wide ? 1 : 0) < body.size())
                 {
                     const bool end =
                         wide ? body[p] == '\0' && body[p + 1] == '\0' : body[p] == '\0';
                     p += wide ? 2 : 1;
                     if (end)
                         break;
                 }
                 if (p >= body.size())
                     return false;
                 // The front cover wins; any picture will do otherwise.
                 if (picture.empty() || type == 3)
                     picture = body.substr(p);
                 return type != 3;
             });
    return picture;
}

bool is_music_file(const std::string &name)
{
    const std::string ext = extension(name);
    return ext == "mp3" || ext == "aac" || ext == "ogg" || ext == "oga" || ext == "opus";
}

std::string file_path(const std::string &url)
{
    if (url.rfind("file://", 0) != 0)
        return {};
    std::string path = url.substr(7);
    const std::size_t hash = path.find('#');
    if (hash != std::string::npos)
        path.resize(hash);
    return path;
}

std::string art_address(const std::string &url)
{
    return url + "#art";
}

std::string art_owner(const std::string &address)
{
    const std::size_t at = address.rfind("#art");
    if (address.rfind("file://", 0) != 0 || at == std::string::npos || at + 4 != address.size())
        return {};
    return address.substr(0, at);
}

std::vector<Station> scan_library(const std::vector<std::string> &roots,
                                  std::map<std::string, std::string> *logos, std::size_t max_tracks,
                                  int max_depth)
{
    std::vector<Found> found;
    for (const std::string &root : roots)
        walk(root, 0, max_depth, max_tracks, &found);
    std::stable_sort(found.begin(), found.end(),
                     [](const Found &a, const Found &b)
                     {
                         if (a.folder != b.folder)
                             return a.folder < b.folder;
                         if (a.tags.track != b.tags.track && a.tags.track > 0 && b.tags.track > 0)
                             return a.tags.track < b.tags.track;
                         return a.path < b.path;
                     });
    std::vector<Station> songs;
    for (const Found &song : found)
    {
        Station station;
        station.category = kLibraryCategory;
        station.name = song.tags.title.empty() ? stem(song.path) : song.tags.title;
        const std::string folder = stem(song.folder);
        station.style = song.tags.artist.empty() ? folder : song.tags.artist;
        station.country = song.tags.album.empty() ? folder : song.tags.album;
        station.format = format_of(extension(song.path));
        station.url = "file://" + song.path;
        if (song.tags.has_art && logos != nullptr)
            (*logos)[station.url] = art_address(station.url);
        songs.push_back(std::move(station));
    }
    return songs;
}

TrackTags file_tags(const std::string &path)
{
    TrackTags tags;
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0)
        return tags;
    struct stat info
    {
    };
    const std::uint64_t size =
        ::fstat(fd, &info) == 0 ? static_cast<std::uint64_t>(info.st_size) : 0;
    read_tags(file_reader(fd), size, &tags);
    ::close(fd);
    return tags;
}

std::string file_art(const std::string &path)
{
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0)
        return {};
    std::string picture = embedded_art(file_reader(fd));
    ::close(fd);
    return picture;
}

std::vector<std::string> usb_roots()
{
    std::vector<std::string> roots;
    for (int i = 0; i < 8; ++i)
    {
        const std::string root = "/mnt/usb" + std::to_string(i);
        struct stat info
        {
        };
        if (::stat(root.c_str(), &info) == 0 && S_ISDIR(info.st_mode))
            roots.push_back(root);
    }
    return roots;
}

net::OpenResult open_file(const std::string &url)
{
    net::OpenResult result;
    const std::string path = file_path(url);
    const int fd = path.empty() ? -1 : ::open(path.c_str(), O_RDONLY);
    if (fd < 0)
    {
        result.error = "the song is not there any more (was the drive removed?)";
        return result;
    }
    // Past an ID3v2 tag: a cover inside it is not audio, and a big one would
    // look to the decoder like a stream that is not music.
    std::string header;
    if (file_reader(fd)(0, 10, &header) && header.size() == 10 && header.compare(0, 3, "ID3") == 0)
    {
        const std::uint64_t skip =
            10 + static_cast<std::uint64_t>(syncsafe(header, 6)) +
            ((static_cast<unsigned char>(header[5]) & 0x10) != 0 ? 10 : 0); // a footer
        ::lseek(fd, static_cast<off_t>(skip), SEEK_SET);
    }
    auto stream = std::make_unique<FileStream>(fd);
    const std::string ext = extension(path);
    stream->content_type = ext == "mp3"    ? "audio/mpeg"
                           : ext == "aac"  ? "audio/aac"
                           : ext == "opus" ? "audio/ogg; codecs=opus"
                                           : "audio/ogg";
    result.stream = std::move(stream);
    result.via = "file";
    return result;
}

} // namespace hui::audio
