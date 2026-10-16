// Afterglow - Tests for the JSON reader and the album art lookup.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/save_file.hpp"
#include "net/cover_art.hpp"
#include "net/json.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace
{

using hui::net::ArtState;
using hui::net::CoverArt;
using hui::net::FetchResult;
using hui::net::Json;

TEST(Json, ReadsNestedValues)
{
    Json root;
    ASSERT_TRUE(Json::parse(R"({"a": [1, 2.5, -3e2], "b": {"c": "x\"yé😀"},
                               "t": true, "f": false, "n": null})",
                            &root));
    EXPECT_TRUE(root.is_object());
    EXPECT_EQ(root["a"].size(), 3u);
    EXPECT_DOUBLE_EQ(root["a"].at(1).number(), 2.5);
    EXPECT_DOUBLE_EQ(root["a"].at(2).number(), -300.0);
    EXPECT_EQ(root["b"]["c"].str(), "x\"y\xC3\xA9\xF0\x9F\x98\x80");
    EXPECT_TRUE(root["t"].boolean());
    EXPECT_FALSE(root["f"].boolean(true));
    EXPECT_EQ(root["n"].type(), Json::Type::null);
    // Missing things read as a null, never a crash.
    EXPECT_EQ(root["missing"]["deeper"].at(4).str(), "");
}

TEST(Json, RefusesBrokenDocuments)
{
    Json root;
    for (const char *bad : {"", "{", "[1,]", "{\"a\" 1}", "\"open", "tru", "[1] x", "{\"a\":01x}"})
        EXPECT_FALSE(Json::parse(bad, &root)) << bad;
    // Deep nesting is refused instead of exhausting the stack.
    EXPECT_FALSE(Json::parse(std::string(500, '[') + std::string(500, ']'), &root));
}

TEST(CoverArt, SplitsSongsAndSkipsWhatIsNotOne)
{
    std::string artist;
    std::string title;
    ASSERT_TRUE(
        CoverArt::split_song("Nova Kessler - Concrete Halls (Original Mix)", &artist, &title));
    EXPECT_EQ(artist, "Nova Kessler");
    EXPECT_EQ(title, "Concrete Halls");
    ASSERT_TRUE(CoverArt::split_song("A & B - Title - Label", &artist, &title));
    EXPECT_EQ(title, "Title");
    EXPECT_FALSE(CoverArt::split_song("Techno.FM", &artist, &title));
    EXPECT_FALSE(CoverArt::split_song(" - Title", &artist, &title));
    EXPECT_FALSE(CoverArt::split_song("Station - Advert", &artist, &title));
    EXPECT_EQ(CoverArt::key("Nova Kessler feat. Someone - Concrete Halls"),
              CoverArt::key("NOVA KESSLER - concrete halls [Label]"));
}

TEST(CoverArt, SearchesMusicBrainzWithEscapedWords)
{
    const std::string url = CoverArt::search_url("AC/DC & Friends", "Hell's \"Bells\"");
    EXPECT_EQ(url.rfind("https://musicbrainz.org/ws/2/recording/?fmt=json", 0), 0u);
    EXPECT_EQ(url.find('"'), std::string::npos);
    EXPECT_EQ(url.find(' '), std::string::npos);
    EXPECT_NE(url.find("AC%20DC"), std::string::npos); // the slash became a space
}

const char *kAnswer = R"json({"recordings":[
  {"score":100,"title":"Other Song","artist-credit":[{"name":"Nova Kessler"}],
   "releases":[{"id":"wrong","status":"Official","release-group":{"id":"wrong-group"}}]},
  {"score":95,"title":"Concrete Halls (Extended Mix)","artist-credit":[{"name":"Nova Kessler"}],
   "releases":[
     {"id":"comp","status":"Official","release-group":{"id":"comp-group","secondary-types":["Compilation"]}},
     {"id":"single","status":"Official","release-group":{"id":"single-group"}}]},
  {"score":50,"title":"Concrete Halls","artist-credit":[{"name":"Nova Kessler"}],
   "releases":[{"id":"low","status":"Official","release-group":{"id":"low-group"}}]}]})json";

TEST(CoverArt, PicksMatchingOfficialReleasesFirst)
{
    const auto urls = CoverArt::cover_urls(kAnswer, "Nova Kessler", "Concrete Halls");
    ASSERT_EQ(urls.size(), 4u);
    EXPECT_EQ(urls[0], "https://coverartarchive.org/release-group/single-group/front-250");
    EXPECT_EQ(urls[1], "https://coverartarchive.org/release-group/comp-group/front-250");
    EXPECT_EQ(urls[2], "https://coverartarchive.org/release/single/front-250");
    for (const std::string &url : urls)
    {
        EXPECT_EQ(url.find("wrong"), std::string::npos); // another song
        EXPECT_EQ(url.find("low"), std::string::npos);   // a poor match
    }
    EXPECT_TRUE(CoverArt::cover_urls(kAnswer, "Someone Else", "Concrete Halls").empty());
    EXPECT_TRUE(CoverArt::cover_urls("not json", "a", "b").empty());
}

std::string cover_bytes()
{
    std::string data;
    hui::save::read_file(std::string(HUI_SOURCE_DIR) + "/tests/unit/data/cover-a.jpg", &data);
    return data;
}

// A Windows icon holding one 32-bit bitmap of side x side: red, the top
// half see-through.
std::string make_icon(int side)
{
    std::string ico;
    const auto put16 = [&](int v)
    {
        ico.push_back(static_cast<char>(v & 0xff));
        ico.push_back(static_cast<char>((v >> 8) & 0xff));
    };
    const auto put32 = [&](std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            ico.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
    };
    const std::uint32_t pixels = static_cast<std::uint32_t>(side * side * 4);
    const std::uint32_t mask = static_cast<std::uint32_t>(((side + 31) / 32) * 4 * side);
    const std::uint32_t size = 40 + pixels + mask;
    put16(0);
    put16(1);
    put16(1);
    ico.push_back(static_cast<char>(side >= 256 ? 0 : side));
    ico.push_back(static_cast<char>(side >= 256 ? 0 : side));
    ico.push_back(0);
    ico.push_back(0);
    put16(1);
    put16(32);
    put32(size);
    put32(22);
    put32(40);
    put32(static_cast<std::uint32_t>(side));
    put32(static_cast<std::uint32_t>(side * 2));
    put16(1);
    put16(32);
    for (int i = 0; i < 6; ++i)
        put32(0);
    for (int y = 0; y < side; ++y) // bottom row first
    {
        const bool top_half = y >= side / 2;
        for (int x = 0; x < side; ++x)
        {
            ico.push_back(0);                                      // blue
            ico.push_back(0);                                      // green
            ico.push_back(static_cast<char>(0xff));                // red
            ico.push_back(static_cast<char>(top_half ? 0 : 0xff)); // alpha
        }
    }
    ico.append(mask, '\0');
    return ico;
}

TEST(CoverArt, ReadsIconsAsStationsServeThem)
{
    hui::net::ArtImage image;
    ASSERT_TRUE(CoverArt::decode(make_icon(64), &image));
    EXPECT_EQ(image.width, 64);
    EXPECT_EQ(image.height, 64);
    // Top left: see-through. Bottom left: opaque red.
    EXPECT_EQ(image.rgba[3], 0);
    const std::size_t bottom = (63u * 64u) * 4u;
    EXPECT_EQ(image.rgba[bottom], 255);
    EXPECT_EQ(image.rgba[bottom + 1], 0);
    EXPECT_EQ(image.rgba[bottom + 3], 255);
    std::string broken = make_icon(64);
    broken.resize(60);
    EXPECT_FALSE(CoverArt::decode(broken, &image));
}

TEST(CoverArt, FetchesLogosByAddressAndRefusesTinyOnes)
{
    std::atomic<int> fetches{0};
    CoverArt logos;
    logos.init(
        [&](const std::string &url, std::string *body, const std::atomic<bool> &)
        {
            ++fetches;
            if (url.find("big") != std::string::npos)
                *body = make_icon(64);
            else if (url.find("tiny") != std::string::npos)
                *body = make_icon(16); // a favicon: too small to show
            else
                *body = "<html>not a picture</html>";
            return FetchResult::ok;
        },
        [](int w, int h, const std::uint8_t *) { return static_cast<std::uint32_t>(w * 1000 + h); },
        [](std::uint32_t) {}, "", 0.0, hui::net::ArtSource::images);
    const std::string big = "https://radio.example/big.ico";
    const std::string tiny = "https://radio.example/tiny.ico";
    const std::string page = "https://radio.example/";
    EXPECT_EQ(logos.view("not an address").state, ArtState::none);
    logos.request("not an address");
    for (const std::string &url : {big, tiny, page})
        logos.request(url);
    for (int i = 0; i < 400; ++i)
    {
        logos.pump(0.016f);
        if (logos.view(big).state == ArtState::ready &&
            logos.view(tiny).state == ArtState::missing &&
            logos.view(page).state == ArtState::missing)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto view = logos.view(big);
    ASSERT_EQ(view.state, ArtState::ready);
    EXPECT_EQ(view.width, 64);
    EXPECT_EQ(view.height, 64);
    EXPECT_EQ(logos.view(tiny).state, ArtState::missing);
    EXPECT_EQ(logos.view(page).state, ArtState::missing);
    EXPECT_EQ(fetches.load(), 3);
}

TEST(CoverArt, DecodesACoverAndFindsItsColours)
{
    hui::net::ArtImage image;
    ASSERT_TRUE(CoverArt::decode(cover_bytes(), &image));
    EXPECT_EQ(image.width, 150); // 300 halved to fit 256
    EXPECT_EQ(image.height, 150);
    EXPECT_EQ(image.rgba.size(), 150u * 150u * 4u);
    // The test cover runs from deep purple to pink with gold rings.
    EXPECT_GT(image.vivid.r, image.vivid.b);
    EXPECT_FALSE(CoverArt::decode("not an image", &image));
}

TEST(CoverArt, LooksUpUploadsAndRemembers)
{
    std::atomic<int> searches{0};
    std::atomic<int> downloads{0};
    std::vector<std::uint32_t> released;
    const std::string bytes = cover_bytes();
    const std::string cache =
        ::testing::TempDir() + "/afterglow-art-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    CoverArt art;
    art.init(
        [&](const std::string &url, std::string *body, const std::atomic<bool> &)
        {
            if (url.find("musicbrainz") != std::string::npos)
            {
                ++searches;
                *body = kAnswer;
                return FetchResult::ok;
            }
            ++downloads;
            if (url.find("single-group") == std::string::npos)
                return FetchResult::not_found;
            *body = bytes;
            return FetchResult::ok;
        },
        [](int w, int h, const std::uint8_t *) { return static_cast<std::uint32_t>(w * 1000 + h); },
        [&](std::uint32_t texture) { released.push_back(texture); }, cache, 0.0);

    const std::string song = "Nova Kessler - Concrete Halls";
    EXPECT_EQ(art.view(song).state, ArtState::none);
    art.request(song);
    for (int i = 0; i < 400 && art.view(song).state != ArtState::ready; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        art.pump(0.016f);
    }
    const auto view = art.view(song);
    ASSERT_EQ(view.state, ArtState::ready);
    EXPECT_EQ(view.texture, 150u * 1000u + 150u);
    EXPECT_EQ(searches.load(), 1);
    EXPECT_EQ(downloads.load(), 1);
    art.request(song); // known: nothing new is fetched
    EXPECT_EQ(searches.load(), 1);
    art.shutdown();
    EXPECT_EQ(released.size(), 1u);

    // A second run finds the picture in the cache without the network.
    CoverArt again;
    again.init(
        [&](const std::string &, std::string *, const std::atomic<bool> &)
        {
            ++searches;
            return FetchResult::failed;
        },
        [](int, int, const std::uint8_t *) { return 7u; }, [](std::uint32_t) {}, cache, 0.0);
    again.request(song);
    for (int i = 0; i < 400 && again.view(song).state != ArtState::ready; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        again.pump(0.016f);
    }
    EXPECT_EQ(again.view(song).state, ArtState::ready);
    EXPECT_EQ(searches.load(), 1);

    // A song nobody has art for is reported missing.
    const std::string unknown = "Nobody - Nothing";
    again.request(unknown);
    for (int i = 0; i < 400 && again.view(unknown).state == ArtState::searching; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_EQ(again.view(unknown).state, ArtState::missing);
}

} // namespace
