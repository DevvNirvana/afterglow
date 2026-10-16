// Afterglow - Tests for the audio analysis and the visualiser screen.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/analysis.hpp"
#include "audio/demo_signal.hpp"
#include "audio/monitor.hpp"
#include "audio/radio.hpp"
#include "audio/spectrum.hpp"
#include "concept_fixture.hpp"
#include "concepts/concepts.hpp"
#include "concepts/genre_skin.hpp"
#include "concepts/radio_screen.hpp"
#include "concepts/text_fit.hpp"
#include "core/save_file.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <cmath>
#include <vector>

namespace
{

using hui::Action;
using hui::Direction;
using hui::audio::BandAnalyzer;
using hui::audio::Cue;
using hui::audio::SpectrumAnalyzer;

constexpr float kFrame = 1.0f / 60.0f;

std::array<float, 16> flat(float level)
{
    std::array<float, 16> bands{};
    bands.fill(level);
    return bands;
}

TEST(BandAnalyzer, RisesFastAndFallsSlowly)
{
    BandAnalyzer analyzer;
    for (int i = 0; i < 6; ++i) // 0.1 s of full signal
        analyzer.update(flat(1.0f), kFrame);
    EXPECT_GT(analyzer.levels().bands[0], 0.9f);
    for (int i = 0; i < 6; ++i) // 0.1 s of silence
        analyzer.update(flat(0.0f), kFrame);
    EXPECT_GT(analyzer.levels().bands[0], 0.5f);
    for (int i = 0; i < 120; ++i)
        analyzer.update(flat(0.0f), kFrame);
    EXPECT_LT(analyzer.levels().bands[0], 0.01f);
}

TEST(BandAnalyzer, FindsTheBeatsOfTheDemoTrack)
{
    BandAnalyzer analyzer;
    int onsets = 0;
    const double seconds = 8.0; // inside the first section, where the drums play
    for (int frame = 0; frame < static_cast<int>(seconds * 60.0); ++frame)
    {
        analyzer.update(hui::audio::demo_spectrum(frame / 60.0), kFrame);
        onsets += analyzer.levels().onset ? 1 : 0;
    }
    const double expected = seconds * static_cast<double>(hui::audio::kDemoBpm) / 60.0;
    EXPECT_GE(onsets, static_cast<int>(expected) - 2);
    EXPECT_LE(onsets, static_cast<int>(expected) + 1);
}

TEST(BandAnalyzer, SustainedBassIsNotADrumRoll)
{
    BandAnalyzer analyzer;
    int onsets = 0;
    for (int i = 0; i < 240; ++i)
    {
        analyzer.update(flat(0.8f), kFrame);
        onsets += analyzer.levels().onset ? 1 : 0;
    }
    EXPECT_EQ(onsets, 1); // only the moment it started
}

std::vector<float> sine(float hz, float amplitude)
{
    std::vector<float> samples(SpectrumAnalyzer::kWindow);
    for (std::size_t i = 0; i < samples.size(); ++i)
        samples[i] = amplitude * std::sin(2.0f * 3.14159265f * hz * static_cast<float>(i) /
                                          SpectrumAnalyzer::kSampleRate);
    return samples;
}

int loudest_band(const std::array<float, 16> &bands)
{
    return static_cast<int>(std::max_element(bands.begin(), bands.end()) - bands.begin());
}

int band_of(float hz)
{
    int band = 0;
    while (band < 15 && SpectrumAnalyzer::band_edge(band + 1) <= hz)
        ++band;
    return band;
}

TEST(SpectrumAnalyzer, PutsATuneInItsBand)
{
    for (float hz : {60.0f, 220.0f, 1000.0f, 5000.0f, 12000.0f})
    {
        SpectrumAnalyzer analyzer;
        const auto samples = sine(hz, 0.5f);
        const auto bands = analyzer.analyze(samples.data(), kFrame);
        EXPECT_NEAR(loudest_band(bands), band_of(hz), 1) << hz << " Hz";
        EXPECT_GT(bands[static_cast<std::size_t>(loudest_band(bands))], 0.9f) << hz << " Hz";
    }
}

TEST(SpectrumAnalyzer, SilenceStaysDark)
{
    SpectrumAnalyzer analyzer;
    const std::vector<float> silence(SpectrumAnalyzer::kWindow, 0.0f);
    for (float level : analyzer.analyze(silence.data(), kFrame))
        EXPECT_LT(level, 0.01f);
}

TEST(Monitor, ReturnsTheNewestSamplesInOrder)
{
    hui::audio::Monitor monitor;
    std::vector<float> out(4);
    EXPECT_FALSE(monitor.latest(out.data(), out.size()));
    std::vector<float> ramp(10000);
    for (std::size_t i = 0; i < ramp.size(); ++i)
        ramp[i] = static_cast<float>(i);
    monitor.push(ramp.data(), 6000);
    monitor.push(ramp.data() + 6000, 4000); // wraps the ring
    ASSERT_TRUE(monitor.latest(out.data(), out.size()));
    EXPECT_EQ(out[0], 9996.0f);
    EXPECT_EQ(out[3], 9999.0f);
    EXPECT_EQ(monitor.written(), 10000u);
}

class Afterglow : public hui::testing::ConceptFixture
{
  protected:
    void SetUp() override
    {
        send(*design_, idle(), 3.2f); // past the opening titles
    }

    std::unique_ptr<hui::app::Concept> design_ = hui::concepts::make_afterglow(context_);

    hui::gfx::BackdropMode mode() const
    {
        hui::app::Frame frame;
        design_->draw(frame);
        return frame.backdrop.mode;
    }
};

TEST_F(Afterglow, TheDPadChangesTheScene)
{
    EXPECT_EQ(mode(), hui::gfx::BackdropMode::swirl); // Paint Swirl opens the app
    send(*design_, nav(Direction::right));
    EXPECT_TRUE(played(Cue::tab));
    EXPECT_EQ(mode(), hui::gfx::BackdropMode::orb);
    send(*design_, nav(Direction::right));
    EXPECT_EQ(mode(), hui::gfx::BackdropMode::nebula);
    send(*design_, nav(Direction::left));
    EXPECT_EQ(mode(), hui::gfx::BackdropMode::orb);
    send(*design_, nav(Direction::left));
    EXPECT_EQ(mode(), hui::gfx::BackdropMode::swirl);
    send(*design_, nav(Direction::left)); // wraps round to the last scene
    EXPECT_EQ(mode(), hui::gfx::BackdropMode::plasma);
}

constexpr int kScenes = 56;

TEST_F(Afterglow, EverySceneIsAnAudioSceneWithItsFinish)
{
    int trails = 0;
    int trip = 0;
    int calm = 0;
    int gallery = 0;
    for (int i = 0; i < kScenes; ++i)
    {
        hui::app::Frame frame;
        design_->draw(frame);
        EXPECT_TRUE(hui::gfx::is_audio_scene(frame.backdrop.mode)) << i;
        EXPECT_TRUE(frame.backdrop.fx.enabled) << i;
        trails += frame.backdrop.fx.feedback.enabled ? 1 : 0;
        trip += hui::gfx::is_trip_scene(frame.backdrop.mode) ? 1 : 0;
        calm += hui::gfx::is_calm_scene(frame.backdrop.mode) ? 1 : 0;
        gallery += hui::gfx::is_gallery_scene(frame.backdrop.mode) ? 1 : 0;
        send(*design_, nav(Direction::right), 0.1f);
    }
    EXPECT_EQ(mode(), hui::gfx::BackdropMode::swirl); // every press went all the way round
    EXPECT_GE(trails, 12);                            // the feedback presets
    EXPECT_GE(trip, 18);                              // drawn by the trip program
    EXPECT_EQ(calm, 10);                              // the calm program
    EXPECT_EQ(gallery, 10);                           // the gallery program
}

TEST_F(Afterglow, TrailsAreSaneAtEveryTripLevel)
{
    for (int level = 0; level < 3; ++level)
    {
        for (int i = 0; i < kScenes; ++i)
        {
            hui::app::Frame frame;
            design_->draw(frame);
            const hui::gfx::FeedbackFx &fb = frame.backdrop.fx.feedback;
            if (fb.enabled)
            {
                EXPECT_GE(fb.decay, 0.5f) << i;
                EXPECT_LT(fb.decay, 0.98f) << i; // a trail must fade, or it burns in
                EXPECT_GT(fb.zoom, 0.9f) << i;
                EXPECT_LT(fb.zoom, 1.1f) << i;
            }
            send(*design_, nav(Direction::right), 0.0f);
        }
        send(*design_, nav(level == 0 ? Direction::down : Direction::up), 0.0f);
    }
}

TEST_F(Afterglow, TheWaveformReachesTheShader)
{
    send(*design_, idle(), 2.0f);
    hui::app::Frame frame;
    design_->draw(frame);
    float energy = 0.0f;
    for (float v : frame.backdrop.wave)
    {
        EXPECT_GE(v, -1.0f);
        EXPECT_LE(v, 1.0f);
        energy += std::abs(v);
    }
    EXPECT_GT(energy, 1.0f);
}

TEST_F(Afterglow, ASceneChangeCrossFadesTwoLiveScenes)
{
    send(*design_, nav(Direction::right), 0.2f);
    hui::app::Frame frame;
    design_->draw(frame);
    EXPECT_EQ(frame.backdrop.from_mode, hui::gfx::BackdropMode::swirl);
    EXPECT_GT(frame.backdrop.blend, 0.0f);
    EXPECT_LT(frame.backdrop.blend, 1.0f);
    send(*design_, idle(), 2.0f);
    hui::app::Frame later;
    design_->draw(later);
    EXPECT_EQ(later.backdrop.from_mode, hui::gfx::BackdropMode::none);
    EXPECT_EQ(later.backdrop.blend, 1.0f);
}

TEST_F(Afterglow, L2CyclesThePalette)
{
    // Signature, Album art, Genre (both the signature without a radio),
    // Ember, then Abyss: blue.
    for (int i = 0; i < 4; ++i)
        send(*design_, press(Action::jump_prev), 0.0f);
    EXPECT_TRUE(played(Cue::toggle));
    send(*design_, idle(), 3.0f);
    hui::app::Frame frame;
    design_->draw(frame);
    EXPECT_GT(frame.backdrop.colors[2].b, 0.7f);
    EXPECT_LT(frame.backdrop.colors[2].r, 0.3f);
}

TEST_F(Afterglow, TheLookIsInTheSettings)
{
    hui::app::Frame scene_look;
    design_->draw(scene_look);
    EXPECT_EQ(scene_look.backdrop.fx.scanlines, 0.0f); // Paint Swirl's own look has none
    send(*design_, press(Action::menu), 0.1f);
    for (int i = 0; i < 3; ++i) // Mode, Trip, Palette, Look
        send(*design_, nav(Direction::down), 0.05f);
    send(*design_, nav(Direction::right), 0.05f);
    send(*design_, nav(Direction::right), 0.05f); // Scene look, Clean, CRT
    send(*design_, press(Action::menu), 0.3f);
    hui::app::Frame crt;
    design_->draw(crt);
    EXPECT_GT(crt.backdrop.fx.scanlines, 0.5f);
    EXPECT_GT(crt.backdrop.fx.curve, 0.0f);
}

TEST_F(Afterglow, CrossOpensTheSceneListWhichPreviewsAsItMoves)
{
    send(*design_, press(Action::confirm), 0.3f);
    EXPECT_TRUE(played(Cue::open));
    hui::app::Frame open;
    design_->draw(open);
    EXPECT_TRUE(open.glass);
    EXPECT_GT(open.overlay.instances().size(), 100u);
    send(*design_, nav(Direction::down), 0.1f);
    EXPECT_EQ(mode(), hui::gfx::BackdropMode::orb); // playing behind the list
    send(*design_, nav(Direction::down), 0.1f);
    EXPECT_EQ(mode(), hui::gfx::BackdropMode::nebula);
    send(*design_, press(Action::back), 0.3f); // Circle: back to where it was
    EXPECT_EQ(mode(), hui::gfx::BackdropMode::swirl);
    hui::app::Frame closed;
    design_->draw(closed);
    EXPECT_FALSE(closed.glass);
    // Cross chooses.
    send(*design_, press(Action::confirm), 0.3f);
    send(*design_, nav(Direction::down), 0.1f);
    send(*design_, press(Action::confirm), 0.3f);
    EXPECT_EQ(mode(), hui::gfx::BackdropMode::orb);
    hui::app::Frame chosen;
    design_->draw(chosen);
    EXPECT_FALSE(chosen.glass);
    // The list skips its pack headers: down from the last Afterglow scene
    // lands on the first Trip scene, the Wormhole (a scope with trails).
    send(*design_, press(Action::confirm), 0.3f);
    for (int i = 0; i < 7; ++i)
        send(*design_, nav(Direction::down), 0.05f);
    EXPECT_EQ(mode(), hui::gfx::BackdropMode::scope);
}

TEST_F(Afterglow, TheSignaturePaletteFollowsTheScene)
{
    for (int i = 0; i < 52; ++i) // to Green Rain
        send(*design_, nav(Direction::right), 0.0f);
    send(*design_, idle(), 3.0f);
    hui::app::Frame frame;
    design_->draw(frame);
    EXPECT_EQ(frame.backdrop.mode, hui::gfx::BackdropMode::rain);
    const auto trail = frame.backdrop.colors[2];
    EXPECT_GT(trail.g, 0.8f);
    EXPECT_LT(trail.r, 0.3f);
}

TEST_F(Afterglow, PeakCapsSitOnTopOfTheBars)
{
    send(*design_, idle(), 2.0f);
    hui::app::Frame frame;
    design_->draw(frame);
    for (int i = 0; i < hui::gfx::BackdropSpec::kBands; ++i)
        EXPECT_GE(frame.backdrop.peaks[i], frame.backdrop.spectrum[i]) << i;
}

TEST_F(Afterglow, TheTripLevelStopsAtItsLimitsWithARefusal)
{
    send(*design_, nav(Direction::up), 0.0f); // Normal to Melt
    EXPECT_TRUE(played(Cue::slider));
    send(*design_, nav(Direction::up), 0.0f); // Melt is the top
    EXPECT_TRUE(played(Cue::error));
    EXPECT_FALSE(played(Cue::slider));
    send(*design_, nav(Direction::down), 0.0f);
    send(*design_, nav(Direction::down), 0.0f); // Calm
    send(*design_, nav(Direction::down), 0.0f);
    EXPECT_TRUE(played(Cue::error));
}

TEST_F(Afterglow, R3StartsAutoVjWhichPicksAScene)
{
    EXPECT_EQ(mode(), hui::gfx::BackdropMode::swirl);
    send(*design_, press(Action::r3), 0.2f);
    EXPECT_TRUE(played(Cue::open));
    // With nothing on the radio, the default skin's list: Paint Swirl is
    // showing, so the next of its scenes comes up.
    EXPECT_EQ(mode(), hui::gfx::BackdropMode::orb);
    send(*design_, press(Action::r3), 0.2f);
    EXPECT_TRUE(played(Cue::back));
}

TEST_F(Afterglow, OptionsOpensTheSettingsAndOptionsClosesThem)
{
    send(*design_, press(Action::menu), 0.3f);
    EXPECT_TRUE(played(Cue::open));
    hui::app::Frame open;
    design_->draw(open);
    EXPECT_TRUE(open.glass);
    // Right on the first row (Mode) goes to Showcase; the scene stays.
    send(*design_, nav(Direction::right), 0.1f);
    send(*design_, press(Action::menu), 0.3f);
    EXPECT_TRUE(played(Cue::back));
    hui::app::Frame closed;
    design_->draw(closed);
    EXPECT_FALSE(closed.glass);
    EXPECT_EQ(closed.backdrop.mode, hui::gfx::BackdropMode::swirl);
}

TEST_F(Afterglow, AmbientDisplayDimsAndHidesTheOverlay)
{
    send(*design_, press(Action::menu), 0.1f);
    for (int i = 0; i < 5; ++i) // down to Display
        send(*design_, nav(Direction::down), 0.05f);
    send(*design_, nav(Direction::right), 0.05f);
    send(*design_, nav(Direction::right), 0.05f); // Normal, Party, Ambient
    send(*design_, press(Action::menu), 2.0f);
    hui::app::Frame frame;
    design_->draw(frame);
    EXPECT_EQ(frame.post.mode, hui::gfx::BackdropMode::vignette);
}

TEST_F(Afterglow, TheSceneReceivesTheMusic)
{
    send(*design_, idle(), 2.0f);
    hui::app::Frame frame;
    design_->draw(frame);
    float total = 0.0f;
    for (float band : frame.backdrop.spectrum)
        total += band;
    EXPECT_GT(total, 0.5f);                    // the demo track (or real audio) reaches the shader
    EXPECT_GT(frame.backdrop.params[0], 0.0f); // the tunnel has travelled
}

TEST_F(Afterglow, TheBeatClockReachesTheShader)
{
    send(*design_, idle(), 10.0f); // the demo track: a steady beat to lock on
    hui::app::Frame a;
    design_->draw(a);
    send(*design_, idle(), 0.25f);
    hui::app::Frame b;
    design_->draw(b);
    for (const hui::app::Frame *frame : {&a, &b})
    {
        EXPECT_GE(frame->backdrop.aux[3], 0.0f);
        EXPECT_LT(frame->backdrop.aux[3], 64.0f);
    }
    // A quarter of a second is about half a beat at dance tempos.
    float moved = b.backdrop.aux[3] - a.backdrop.aux[3];
    if (moved < 0.0f)
        moved += 64.0f;
    EXPECT_GT(moved, 0.2f);
    EXPECT_LT(moved, 1.0f);
}

TEST_F(Afterglow, BeatRumbleIsOffUntilTheSettingsTurnItOn)
{
    int rumbles = 0;
    for (int i = 0; i < 240; ++i)
    {
        feedback_.clear();
        design_->update(idle(), kFrame, feedback_);
        rumbles += feedback_.rumble_seconds > 0.0f ? 1 : 0;
    }
    EXPECT_EQ(rumbles, 0);
    send(*design_, press(Action::menu), 0.1f);
    for (int i = 0; i < 15; ++i) // down to Beat rumble (the SOUND header is skipped)
        send(*design_, nav(Direction::down), 0.05f);
    send(*design_, press(Action::confirm), 0.05f);
    send(*design_, press(Action::menu), 0.1f);
    for (int i = 0; i < 240; ++i)
    {
        feedback_.clear();
        design_->update(idle(), kFrame, feedback_);
        rumbles += feedback_.rumble_seconds > 0.0f ? 1 : 0;
    }
    EXPECT_GT(rumbles, 2);
}

TEST(GenreSkins, EveryGenreHasItsSkinAndItsScenesExist)
{
    for (const char *genre :
         {"Techno", "House", "Trance", "Drum & Bass", "Bass & Dubstep", "Hard Dance", "EDM & Dance",
          "Synthwave", "Chill & Electronica", "Pop & Hits", "Hip-Hop & Rap", "R&B & Soul",
          "Bollywood & Desi", "Latin", "K-Pop", "Afrobeats", "Arabic"})
    {
        const hui::concepts::GenreSkin &skin = hui::concepts::skin_for(genre);
        EXPECT_STREQ(skin.id, genre);
        for (const char *scene : skin.scenes)
            EXPECT_TRUE(hui::concepts::afterglow_has_scene(scene)) << genre << ": " << scene;
    }
    EXPECT_EQ(&hui::concepts::skin_for("Polka"), &hui::concepts::default_skin());
    for (const char *scene : hui::concepts::default_skin().scenes)
        EXPECT_TRUE(hui::concepts::afterglow_has_scene(scene)) << scene;
}

TEST(GenreSkins, AMorphArrivesAndAChangeMidwayStartsFromWhatShows)
{
    hui::concepts::SkinMorph morph;
    EXPECT_TRUE(morph.settled());
    morph.set(hui::concepts::skin_for("Techno"));
    EXPECT_FALSE(morph.settled());
    EXPECT_EQ(&morph.from(), &hui::concepts::default_skin());
    morph.update(0.4f); // more than half way
    morph.set(hui::concepts::skin_for("House"));
    EXPECT_STREQ(morph.from().id, "Techno");
    morph.update(1.0f);
    EXPECT_TRUE(morph.settled());
    EXPECT_STREQ(morph.to().id, "House");
    const auto accent = morph.accent();
    EXPECT_NEAR(accent.r, hui::gfx::Color::rgb(hui::concepts::skin_for("House").accent).r, 1e-5f);
}

TEST(GenreSkins, EveryMotifDraws)
{
    hui::ui::Fonts fonts;
    hui::gfx::Font regular;
    std::string data;
    ASSERT_TRUE(hui::save::read_file(
        std::string(HUI_SOURCE_DIR) + "/assets/fonts/inter-regular.huifont", &data));
    ASSERT_TRUE(regular.load(data));
    for (hui::ui::FontRef *ref : {&fonts.regular, &fonts.semibold, &fonts.display, &fonts.mono})
    {
        ref->font = &regular;
        ref->texture = 1;
    }
    std::array<float, 16> bands{};
    bands.fill(0.6f);
    hui::concepts::Pulse pulse;
    pulse.time = 12.3f;
    pulse.bass = 0.7f;
    pulse.beat = 0.9f;
    pulse.bands = &bands;
    auto skins = std::vector<const hui::concepts::GenreSkin *>{&hui::concepts::default_skin()};
    for (const auto &skin : hui::concepts::genre_skins())
        skins.push_back(&skin);
    for (const hui::concepts::GenreSkin *skin : skins)
    {
        hui::gfx::DrawList list;
        const hui::gfx::Rect r{96.0f, 70.0f, 1728.0f, 176.0f};
        hui::concepts::draw_skin_room(list, {0.0f, 0.0f, 1920.0f, 1080.0f}, *skin, 1.0f, pulse);
        hui::concepts::draw_banner(list, fonts, r, *skin, "", "", 1.0f, pulse);
        hui::concepts::draw_generated_cover(list, fonts, {100.0f, 300.0f, 270.0f, 270.0f}, *skin,
                                            "Some Station FM", 7u, 1.0f, pulse);
        hui::concepts::draw_disc(list, 500.0f, 500.0f, 120.0f, *skin, 1.0f, false, 1.0f);
        EXPECT_GT(list.instances().size(), 20u) << skin->title;
        for (const auto &instance : list.instances())
            for (float v : instance.rect)
                ASSERT_TRUE(std::isfinite(v)) << skin->title;
    }
}

// A radio that only records what it is asked.
class FakeRadio final : public hui::audio::RadioControl
{
  public:
    std::vector<hui::audio::Station> list{
        {"Techno", "One FM", "Minimal", "USA", "MP3 128", "http://1.example/"},
        {"Techno", "Two FM", "Hard Techno", "Germany", "MP3 192", "http://2.example/"},
        {"House", "Three", "Deep House", "France", "AAC 64", "http://3.example/"}};
    int played = -100;
    std::vector<int> queue;
    int nexts = 0;
    int previouses = 0;
    int pauses = 0;
    int stops = 0;
    bool songs = false;
    std::set<int> favourites;
    hui::audio::PlayerStatus now;
    const std::vector<hui::audio::Station> &stations() const override
    {
        return list;
    }
    void play(int index, std::vector<int> q) override
    {
        played = index;
        queue = std::move(q);
        now.source = hui::audio::Source::radio;
        now.station = index;
        now.state = hui::audio::RadioState::playing;
    }
    void play_songs() override
    {
        songs = true;
    }
    void toggle_pause() override
    {
        ++pauses;
    }
    void stop() override
    {
        ++stops;
    }
    void next() override
    {
        ++nexts;
    }
    void previous() override
    {
        ++previouses;
    }
    hui::audio::PlayerStatus status() const override
    {
        return now;
    }
    bool favorite(int index) const override
    {
        return favourites.count(index) != 0;
    }
    void set_favorite(int index, bool on) override
    {
        if (on)
            favourites.insert(index);
        else
            favourites.erase(index);
    }
    std::vector<int> recents() const override
    {
        return played >= 0 ? std::vector<int>{played} : std::vector<int>{};
    }
    hui::audio::Health health(int index) const override
    {
        return index == 1 ? hui::audio::Health::failed : hui::audio::Health::unknown;
    }
    void start_check() override
    {
        checking = true;
    }
    void cancel_check() override
    {
        checking = false;
    }
    hui::audio::CheckProgress check_progress() const override
    {
        hui::audio::CheckProgress progress;
        progress.running = checking;
        return progress;
    }
    std::string setting(const std::string &key) const override
    {
        const auto it = settings.find(key);
        return it == settings.end() ? std::string() : it->second;
    }
    void set_setting(const std::string &key, const std::string &value) override
    {
        settings[key] = value;
    }
    std::vector<hui::audio::SongEntry> history() const override
    {
        return heard;
    }
    std::vector<hui::audio::SongEntry> liked() const override
    {
        return kept;
    }
    bool is_liked(const std::string &title) const override
    {
        return std::any_of(kept.begin(), kept.end(),
                           [&](const hui::audio::SongEntry &s) { return s.title == title; });
    }
    bool set_liked(const hui::audio::SongEntry &song, bool on) override
    {
        if (song.title.empty() || is_liked(song.title) == on)
            return false;
        if (on)
            kept.insert(kept.begin(), song);
        else
            kept.erase(std::remove_if(kept.begin(), kept.end(), [&](const hui::audio::SongEntry &s)
                                      { return s.title == song.title; }),
                       kept.end());
        return true;
    }
    bool checking = false;
    std::map<std::string, std::string> settings{{"warned", "yes"}};
    std::vector<hui::audio::SongEntry> heard;
    std::vector<hui::audio::SongEntry> kept;
};

class RadioBrowser : public hui::testing::ConceptFixture
{
  protected:
    void SetUp() override
    {
        hui::audio::set_radio(&radio_);
        design_->enter();
        send(*design_, idle(), 3.2f); // past the opening titles
    }
    void TearDown() override
    {
        hui::audio::set_radio(nullptr);
    }
    FakeRadio radio_;
    std::unique_ptr<hui::app::Concept> design_ = hui::concepts::make_afterglow(context_);

    hui::gfx::BackdropMode mode() const
    {
        hui::app::Frame frame;
        design_->draw(frame);
        return frame.backdrop.mode;
    }

    void send_to(hui::concepts::RadioScreen &screen, const hui::InputFrame &input)
    {
        feedback_.clear();
        screen.handle(input, feedback_);
        last_cues_ = feedback_.cues;
        for (int i = 0; i < 12; ++i)
            screen.update(1.0f / 60.0f);
    }
    static void screen_type(hui::concepts::RadioScreen &screen, char c)
    {
        screen.type(std::string(1, c));
    }
};

TEST_F(RadioBrowser, CircleOpensItFrostedAndCircleCloses)
{
    send(*design_, press(Action::back), 0.3f);
    EXPECT_TRUE(played(Cue::open));
    hui::app::Frame open;
    design_->draw(open);
    EXPECT_TRUE(open.glass);
    EXPECT_FALSE(open.overlay.empty());
    send(*design_, press(Action::back), 0.3f);
    hui::app::Frame closed;
    design_->draw(closed);
    EXPECT_FALSE(closed.glass);
}

TEST_F(RadioBrowser, ChannelsFilterAndCrossPlaysWithTheChannelAsTheQueue)
{
    hui::concepts::RadioScreen screen(context_);
    screen.open();
    // Nothing plays yet: "All stations" is chosen, the focus on its stations.
    EXPECT_EQ(screen.visible().size(), 3u);
    EXPECT_EQ(screen.column(), 1);
    send_to(screen, nav(Direction::left));
    EXPECT_EQ(screen.column(), 0);
    // My Music (no drive here: empty, and it says what to do), then past the
    // header to the first genre: Techno.
    send_to(screen, nav(Direction::down));
    EXPECT_TRUE(screen.visible().empty());
    send_to(screen, nav(Direction::down));
    EXPECT_EQ(screen.visible(), (std::vector<int>{0, 1}));
    send_to(screen, nav(Direction::right));
    EXPECT_EQ(screen.column(), 1);
    send_to(screen, nav(Direction::down));
    send_to(screen, press(Action::confirm));
    EXPECT_EQ(radio_.played, 1);
    EXPECT_EQ(radio_.queue, (std::vector<int>{0, 1})); // next and previous stay in Techno
}

TEST_F(RadioBrowser, SquareStarsAStationAndFavoritesShowsIt)
{
    hui::concepts::RadioScreen screen(context_);
    screen.open();
    send_to(screen, nav(Direction::down)); // Two FM
    send_to(screen, press(Action::west));
    EXPECT_TRUE(radio_.favorite(1));
    EXPECT_TRUE(played(Cue::favorite_on));
    send_to(screen, nav(Direction::left));
    for (int i = 0; i < 4; ++i) // Song history, Liked songs, Recently played, Favorites
        send_to(screen, nav(Direction::up));
    EXPECT_EQ(screen.visible(), (std::vector<int>{1}));
}

TEST_F(RadioBrowser, LikedSongsAndTheHistoryTuneInAndUnlike)
{
    hui::audio::SongEntry a;
    a.title = "Nova Kessler - Concrete Halls";
    a.station = "Three";
    a.url = "http://3.example/";
    a.when = 1792000000;
    hui::audio::SongEntry gone = a;
    gone.title = "Mira Sol - Glasshouse";
    gone.station = "Old FM";
    gone.url = "http://old.example/";
    radio_.kept = {a, gone};
    radio_.heard = {gone, a};
    hui::concepts::RadioScreen screen(context_);
    screen.open();
    send_to(screen, nav(Direction::left));
    send_to(screen, nav(Direction::up)); // Song history
    ASSERT_EQ(screen.songs().size(), 2u);
    EXPECT_EQ(screen.songs()[0].title, "Mira Sol - Glasshouse");
    send_to(screen, nav(Direction::up)); // Liked songs
    ASSERT_EQ(screen.songs().size(), 2u);
    EXPECT_EQ(screen.visible(), (std::vector<int>{2})); // only Three is still listed
    send_to(screen, nav(Direction::right));
    EXPECT_EQ(screen.column(), 1);
    send_to(screen, press(Action::confirm)); // tunes in to the song's station
    EXPECT_EQ(radio_.played, 2);
    send_to(screen, nav(Direction::down));
    send_to(screen, press(Action::confirm)); // its station is gone: refused
    EXPECT_TRUE(std::any_of(last_cues_.begin(), last_cues_.end(),
                            [](const hui::audio::CueEvent &e) { return e.cue == Cue::error; }));
    send_to(screen, press(Action::west)); // let it go
    ASSERT_EQ(radio_.kept.size(), 1u);
    EXPECT_EQ(radio_.kept[0].title, "Nova Kessler - Concrete Halls");
    EXPECT_EQ(screen.songs().size(), 1u);
    hui::app::Frame frame;
    hui::concepts::Pulse pulse;
    screen.draw(frame, pulse);
    EXPECT_GT(frame.overlay.instances().size(), 200u);
}

TEST_F(RadioBrowser, R3HidesStationsThatFailedTheCheck)
{
    hui::concepts::RadioScreen screen(context_);
    screen.open();
    EXPECT_EQ(screen.visible().size(), 3u);
    send_to(screen, press(Action::r3));
    EXPECT_EQ(screen.visible(), (std::vector<int>{0, 2})); // Two FM failed
}

TEST_F(RadioBrowser, SearchFiltersAsYouType)
{
    hui::concepts::RadioScreen screen(context_);
    screen.open();
    send_to(screen, press(Action::north)); // the keyboard
    // Type "deep" through the keyboard's own text hook, as its keys do.
    for (char c : std::string("deep"))
        screen_type(screen, c);
    EXPECT_EQ(screen.query(), "deep");
    EXPECT_EQ(screen.visible(), (std::vector<int>{2}));
    send_to(screen, press(Action::back)); // closes the keyboard, keeps the results
    EXPECT_EQ(screen.column(), 1);
    send_to(screen, press(Action::confirm));
    EXPECT_EQ(radio_.played, 2);
}

TEST_F(RadioBrowser, TheTransportWorksInTheBrowserAndOverTheVisualiser)
{
    hui::concepts::RadioScreen screen(context_);
    screen.open();
    send_to(screen, press(Action::page_next));
    send_to(screen, press(Action::page_prev));
    send_to(screen, press(Action::jump_next));
    send_to(screen, press(Action::jump_prev));
    EXPECT_EQ(radio_.nexts, 1);
    EXPECT_EQ(radio_.previouses, 1);
    EXPECT_EQ(radio_.pauses, 1);
    EXPECT_EQ(radio_.stops, 1);
    send_to(screen, press(Action::menu));
    EXPECT_TRUE(radio_.checking);
    // Over the visualiser: L1 / R1 and L3.
    send(*design_, press(Action::page_next), 0.1f);
    send(*design_, press(Action::l3), 0.1f);
    EXPECT_EQ(radio_.nexts, 2);
    EXPECT_EQ(radio_.pauses, 2);
}

TEST_F(RadioBrowser, TheScreenWearsTheSkinOfTheChannel)
{
    hui::concepts::RadioScreen screen(context_);
    screen.open();
    EXPECT_EQ(&screen.skin(), &hui::concepts::default_skin()); // All stations, nothing playing
    send_to(screen, nav(Direction::left));
    send_to(screen, nav(Direction::down)); // My Music
    send_to(screen, nav(Direction::down)); // Techno
    EXPECT_STREQ(screen.skin().id, "Techno");
    send_to(screen, nav(Direction::down)); // House
    EXPECT_STREQ(screen.skin().id, "House");
    hui::app::Frame frame;
    hui::concepts::Pulse pulse;
    screen.draw(frame, pulse);
    EXPECT_TRUE(frame.glass);
    EXPECT_GT(frame.overlay.instances().size(), 200u);
}

TEST_F(RadioBrowser, AStationChangeShowsItsIdentAndPicksItsScenes)
{
    send(*design_, press(Action::r3), 0.2f); // Auto-VJ
    radio_.play(0, {});                      // One FM, Techno
    send(*design_, idle(), 0.5f);
    hui::app::Frame frame;
    design_->draw(frame);
    // Techno's first scene is the Rave Tunnel.
    EXPECT_EQ(frame.backdrop.mode, hui::gfx::BackdropMode::rave);
    EXPECT_GT(frame.scene.instances().size(), 100u); // the ident
    radio_.play(2, {});                              // Three, House
    send(*design_, idle(), 0.5f);
    hui::app::Frame house;
    design_->draw(house);
    EXPECT_EQ(house.backdrop.mode, hui::gfx::BackdropMode::swirl);
}

TEST_F(RadioBrowser, SquareLikesTheSongOnAirAndAgainLetsItGo)
{
    send(*design_, press(Action::west), 0.1f); // nothing on air: refused
    EXPECT_TRUE(played(Cue::error));
    EXPECT_TRUE(radio_.kept.empty());
    radio_.play(0, {});
    radio_.now.title = "Nova Kessler - Concrete Halls";
    send(*design_, idle(), 0.2f);
    send(*design_, press(Action::west), 0.1f);
    EXPECT_TRUE(played(Cue::favorite_on));
    ASSERT_EQ(radio_.kept.size(), 1u);
    EXPECT_EQ(radio_.kept[0].title, "Nova Kessler - Concrete Halls");
    EXPECT_EQ(radio_.kept[0].station, "One FM");
    EXPECT_EQ(radio_.kept[0].url, "http://1.example/");
    hui::app::Frame toast;
    design_->draw(toast);
    EXPECT_GT(toast.overlay.instances().size(), 4u); // the note at the top
    send(*design_, press(Action::west), 0.1f);
    EXPECT_TRUE(played(Cue::favorite_off));
    EXPECT_TRUE(radio_.kept.empty());
    send(*design_, idle(), 3.0f);
    hui::app::Frame gone;
    design_->draw(gone);
    EXPECT_TRUE(gone.overlay.empty());
}

TEST_F(RadioBrowser, ANewSongIsAnnouncedAfterTheIdent)
{
    radio_.play(0, {});
    radio_.now.title = "Nova Kessler - Concrete Halls";
    send(*design_, idle(), 12.0f); // the ident and the first card have gone
    hui::app::Frame before;
    design_->draw(before);
    radio_.now.title = "Mira Sol - Glasshouse";
    send(*design_, idle(), 1.0f);
    hui::app::Frame card;
    design_->draw(card);
    send(*design_, idle(), 7.0f);
    hui::app::Frame after;
    design_->draw(after);
    // The card adds its cover, text and plate over the scene.
    EXPECT_GT(card.scene.instances().size(), after.scene.instances().size() + 30u);
    EXPECT_GT(card.scene.instances().size(), before.scene.instances().size() + 30u);
}

TEST_F(RadioBrowser, TheSongCardShowsWhileTheControlsAreHidden)
{
    radio_.play(0, {});
    radio_.now.title = "Nova Kessler - Concrete Halls";
    send(*design_, press(Action::north), 0.1f); // hide the controls
    send(*design_, idle(), 12.0f);
    hui::app::Frame hidden;
    design_->draw(hidden);
    radio_.now.title = "Mira Sol - Glasshouse";
    send(*design_, idle(), 1.0f);
    hui::app::Frame card;
    design_->draw(card);
    EXPECT_GT(card.scene.instances().size(), hidden.scene.instances().size() + 30u);
}

TEST_F(RadioBrowser, TheControlsAreBackAfterTheRadio)
{
    send(*design_, idle(), 8.0f); // the overlay fades after a while
    hui::app::Frame faded;
    design_->draw(faded);
    send(*design_, press(Action::back), 0.3f); // the radio
    send(*design_, idle(), 12.0f);             // a long browse
    send(*design_, press(Action::back), 0.6f); // back to the visuals
    hui::app::Frame back;
    design_->draw(back);
    EXPECT_GT(back.scene.instances().size(), faded.scene.instances().size() + 40u);
}

TEST_F(RadioBrowser, TriangleBringsFadedControlsBackAndHidesShownOnes)
{
    send(*design_, idle(), 8.0f);
    hui::app::Frame faded;
    design_->draw(faded);
    send(*design_, press(Action::north), 1.0f); // faded: Triangle shows them
    hui::app::Frame shown;
    design_->draw(shown);
    EXPECT_GT(shown.scene.instances().size(), faded.scene.instances().size() + 40u);
    send(*design_, press(Action::north), 1.0f); // shown: Triangle hides them
    hui::app::Frame hidden;
    design_->draw(hidden);
    EXPECT_LT(hidden.scene.instances().size(), shown.scene.instances().size() - 40u);
    send(*design_, press(Action::west), 3.0f); // other buttons leave them hidden
    hui::app::Frame still;
    design_->draw(still);
    EXPECT_LT(still.scene.instances().size(), shown.scene.instances().size() - 40u);
}

TEST_F(RadioBrowser, LongTextShrinksWrapsThenEndsInAnEllipsis)
{
    const hui::ui::FontRef &font = context_.fonts.semibold;
    const std::string shortish = "Nova Kessler";
    const hui::concepts::TextBlock same =
        hui::concepts::fit_block(font, shortish, 52.0f, 900.0f, 2);
    EXPECT_FLOAT_EQ(same.size, 52.0f);
    ASSERT_EQ(same.lines.size(), 1u);
    const std::string longer = "Nova Kessler and The Paper Moons feat. Mira Sol - Concrete Halls "
                               "(Extended Club Mix) [Remastered 2026 Edition]";
    const hui::concepts::TextBlock two = hui::concepts::fit_block(font, longer, 52.0f, 600.0f, 2);
    ASSERT_GE(two.lines.size(), 1u);
    ASSERT_LE(two.lines.size(), 2u);
    EXPECT_LT(two.size, 52.0f);
    EXPECT_GE(two.size, 52.0f * 0.6f - 0.01f);
    for (const std::string &line : two.lines)
        EXPECT_LE(font.measure(line, two.size), 600.5f);
    const std::string &tail = two.lines.back(); // too long even for two lines: an ellipsis
    EXPECT_TRUE(tail.find("...") != std::string::npos ||
                tail.find("\xE2\x80\xA6") != std::string::npos);
    const hui::concepts::TextBlock one = hui::concepts::fit_line(font, longer, 27.0f, 400.0f);
    ASSERT_EQ(one.lines.size(), 1u);
    EXPECT_LE(font.measure(one.lines[0], one.size), 400.5f);
}

TEST_F(RadioBrowser, SongRevealOffShowsNoCard)
{
    radio_.settings["reveal"] = "0";
    auto design = hui::concepts::make_afterglow(context_);
    design->enter();
    send(*design, idle(), 3.2f);
    radio_.play(0, {});
    radio_.now.title = "Nova Kessler - Concrete Halls";
    send(*design, idle(), 9.0f); // the ident and the overlay have gone
    hui::app::Frame quiet;
    design->draw(quiet);
    radio_.now.title = "Mira Sol - Glasshouse";
    send(*design, idle(), 1.0f);
    hui::app::Frame still_quiet;
    design->draw(still_quiet);
    EXPECT_NEAR(static_cast<double>(still_quiet.scene.instances().size()),
                static_cast<double>(quiet.scene.instances().size()), 20.0);
}

TEST_F(RadioBrowser, CalmAutoVjPlaysOnlyTheCalmPack)
{
    radio_.settings["vjfav"] = "2";
    auto design = hui::concepts::make_afterglow(context_);
    design->enter();
    send(*design, idle(), 3.2f);
    radio_.play(0, {}); // Techno, whose own scenes are anything but calm
    send(*design, press(Action::r3), 0.2f);
    std::set<int> seen;
    for (int i = 0; i < 24; ++i)
    {
        send(*design, idle(), 5.0f);
        hui::app::Frame frame;
        design->draw(frame);
        EXPECT_TRUE(hui::gfx::is_calm_scene(frame.backdrop.mode)) << i;
        seen.insert(static_cast<int>(frame.backdrop.mode));
    }
    EXPECT_GE(seen.size(), 2u); // it still moves on
}

TEST_F(RadioBrowser, TheQuietDefaultVolumeIsLiftedOnce)
{
    EXPECT_GE(context_.settings.music_volume, 8);
    EXPECT_EQ(radio_.settings["vol5"], "1");
    // A volume chosen later is the listener's and stays.
    context_.settings.music_volume = 3;
    auto design = hui::concepts::make_afterglow(context_);
    design->enter();
    EXPECT_EQ(context_.settings.music_volume, 3);
}

TEST_F(RadioBrowser, HoldingTriangleOpensTheCommandBox)
{
    hui::app::Frame before;
    design_->draw(before);
    EXPECT_FALSE(before.glass);
    hui::InputFrame hold = press(Action::north);
    for (int i = 0; i < 45; ++i) // three quarters of a second
    {
        feedback_.clear();
        design_->update(hold, 1.0f / 60.0f, feedback_);
        hold.pressed = 0;
    }
    hui::app::Frame open;
    design_->draw(open);
    EXPECT_TRUE(open.glass); // the radio, its keyboard up
    // A tap still only shows or hides the controls.
    send(*design_, press(Action::back), 0.3f); // close the keyboard
    send(*design_, press(Action::back), 0.3f); // and the radio
    hui::app::Frame closed;
    design_->draw(closed);
    EXPECT_FALSE(closed.glass);
}

TEST_F(RadioBrowser, CommandsPlayLikeAndChangeTheScene)
{
    ASSERT_TRUE(hui::concepts::afterglow_command(*design_, "play house"));
    send(*design_, idle(), 0.2f);
    EXPECT_EQ(radio_.played, 2); // Three, the only House station
    EXPECT_EQ(radio_.queue, (std::vector<int>{2}));
    ASSERT_TRUE(hui::concepts::afterglow_command(*design_, "play one fm"));
    send(*design_, idle(), 0.2f);
    EXPECT_EQ(radio_.played, 0);
    EXPECT_EQ(radio_.queue, (std::vector<int>{0, 1})); // next stays in Techno
    ASSERT_TRUE(hui::concepts::afterglow_command(*design_, "next station"));
    send(*design_, idle(), 0.2f);
    EXPECT_EQ(radio_.nexts, 1);
    radio_.now.title = "Nova Kessler - Concrete Halls";
    ASSERT_TRUE(hui::concepts::afterglow_command(*design_, "like this song"));
    send(*design_, idle(), 0.2f);
    ASSERT_EQ(radio_.kept.size(), 1u);
    ASSERT_TRUE(hui::concepts::afterglow_command(*design_, "love this")); // already liked: kept
    send(*design_, idle(), 0.2f);
    EXPECT_EQ(radio_.kept.size(), 1u);
    ASSERT_TRUE(hui::concepts::afterglow_command(*design_, "show calm visuals"));
    send(*design_, idle(), 2.0f);
    EXPECT_TRUE(hui::gfx::is_calm_scene(mode()));
    ASSERT_TRUE(hui::concepts::afterglow_command(*design_, "scene kintsugi"));
    send(*design_, idle(), 2.0f);
    EXPECT_EQ(mode(), hui::gfx::BackdropMode::kintsugi);
    const int volume = context_.settings.music_volume;
    ASSERT_TRUE(hui::concepts::afterglow_command(*design_, "turn it down"));
    send(*design_, idle(), 0.2f);
    EXPECT_EQ(context_.settings.music_volume, volume - 1);
    // What is not understood stays a search.
    EXPECT_FALSE(hui::concepts::afterglow_command(*design_, "flibber jabber"));
    hui::app::Frame toast;
    design_->draw(toast);
    EXPECT_FALSE(toast.overlay.empty());
}

TEST_F(RadioBrowser, TheSceneIsRememberedByName)
{
    send(*design_, nav(Direction::right), 0.1f); // Orb
    EXPECT_EQ(radio_.settings["scenename"], "Orb");
    auto design = hui::concepts::make_afterglow(context_);
    design->enter();
    send(*design, idle(), 3.2f);
    hui::app::Frame frame;
    design->draw(frame);
    EXPECT_EQ(frame.backdrop.mode, hui::gfx::BackdropMode::orb);
}

TEST_F(RadioBrowser, StarredScenesBecomeAutoVjsSet)
{
    send(*design_, press(Action::confirm), 0.3f); // the scene list, on Paint Swirl
    send(*design_, press(Action::west), 0.1f);    // star it
    EXPECT_TRUE(played(Cue::favorite_on));
    for (int i = 0; i < 5; ++i)
        send(*design_, nav(Direction::down), 0.05f); // Cathedral Rays
    send(*design_, press(Action::west), 0.1f);
    send(*design_, press(Action::confirm), 0.3f);
    EXPECT_EQ(radio_.settings["scenefav"], "Paint Swirl,Cathedral Rays");
    // Settings: Auto-VJ scenes to Favorites.
    send(*design_, press(Action::menu), 0.1f);
    for (int i = 0; i < 7; ++i)
        send(*design_, nav(Direction::down), 0.05f);
    send(*design_, nav(Direction::right), 0.05f);
    send(*design_, press(Action::menu), 0.2f);
    EXPECT_EQ(radio_.settings["vjfav"], "1");
    send(*design_, press(Action::r3), 0.2f);
    EXPECT_EQ(mode(), hui::gfx::BackdropMode::swirl); // its first starred scene
    // A new design reads the stars back.
    auto design = hui::concepts::make_afterglow(context_);
    design->enter();
    send(*design, idle(), 3.2f);
    send(*design, press(Action::r3), 0.2f);
    hui::app::Frame frame;
    design->draw(frame);
    EXPECT_TRUE(frame.backdrop.mode == hui::gfx::BackdropMode::swirl ||
                frame.backdrop.mode == hui::gfx::BackdropMode::rays);
}

TEST(RadioText, ClockAndColours)
{
    EXPECT_EQ(hui::concepts::clock_text(65.0), "1:05");
    EXPECT_EQ(hui::concepts::clock_text(3725.0), "1:02:05");
    const auto a = hui::concepts::channel_colour("Techno");
    const auto b = hui::concepts::channel_colour("Techno");
    EXPECT_EQ(a.r, b.r);
    const auto other = hui::concepts::channel_colour("Some Owner Genre");
    EXPECT_GT(other.r + other.g + other.b, 0.5f);
}

} // namespace
