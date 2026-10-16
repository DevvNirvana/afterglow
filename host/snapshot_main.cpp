// ps5-homebrew-ui - Headless host renderer: runs the tour and writes PNG files.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// usage: hui_snapshots <assets dir> <output dir> [design id|all] [width height]
//
// Runs the same shell, designs and tour script as the console build, through
// Mesa's surfaceless EGL (llvmpipe), with a fixed 60 Hz clock. Only the frames
// the tour marks are rendered, so a full run takes seconds. With
// HUI_STRIP=<from>,<to> it instead writes every frame of the switch between
// two designs (for reviewing the transition).
//
// HUI_REEL=<dir> records instead of photographing: every third simulated frame
// is piped to ffmpeg, which writes animated WebP clips into <dir>.
//   HUI_REEL_SPLIT=1   one clip per tour picture (the Theme Lab: one per theme)
//   otherwise          one clip per design, its whole tour
//   HUI_REEL=<dir> HUI_REEL_SWITCH=1   one clip of L1/R1 cycling through all designs

#include "app/shell.hpp"
#include "app/tour.hpp"
#include "audio/demo_signal.hpp"
#include "audio/radio.hpp"
#include "concepts/genre_skin.hpp"
#include "core/save_file.hpp"
#include "demo/catalog.hpp"
#include "gfx/gl_program.hpp"
#include "gfx/renderer.hpp"
#include "manifest.hpp"
#include "preview_radio.hpp"
#include "ui/fonts.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb/stb_image_write.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{

bool open_context()
{
    auto get_platform_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    EGLDisplay display =
        get_platform_display != nullptr
            ? get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)
            : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0;
    EGLint minor = 0;
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor) ||
        !eglBindAPI(EGL_OPENGL_API))
        return false;
    const EGLint context_attributes[] = {EGL_CONTEXT_MAJOR_VERSION,
                                         4,
                                         EGL_CONTEXT_MINOR_VERSION,
                                         5,
                                         EGL_CONTEXT_OPENGL_PROFILE_MASK,
                                         EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                                         EGL_NONE};
    EGLContext context =
        eglCreateContext(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, context_attributes);
    return context != EGL_NO_CONTEXT &&
           eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context);
}

bool load_font(hui::gfx::Renderer &renderer, const std::string &path, hui::gfx::Font *font,
               hui::ui::FontRef *ref)
{
    std::string data;
    if (!hui::save::read_file(path, &data) || !font->load(data))
    {
        std::fprintf(stderr, "cannot load font %s\n", path.c_str());
        return false;
    }
    ref->font = font;
    ref->texture = renderer.batch().create_font_texture(*font);
    return true;
}

// Streams raw frames to ffmpeg and gives the finished clip its name on close
// (the name is only known once the tour reaches the picture that ends it).
class Recorder
{
  public:
    Recorder(std::string directory, int width, int height, int fps)
        : directory_(std::move(directory)), width_(width), height_(height), fps_(fps)
    {
    }
    ~Recorder()
    {
        // Frames after the last picture belong to no clip.
        if (pipe_ != nullptr)
        {
            pclose(pipe_);
            std::remove((directory_ + "/.recording.webp").c_str());
        }
    }
    void frame(const unsigned char *rgba)
    {
        if (pipe_ == nullptr)
        {
            char command[512];
            std::snprintf(command, sizeof(command),
                          "ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgba -s %dx%d -r %d -i - "
                          "-vf vflip -c:v libwebp_anim -lossless 0 -q:v 62 -compression_level 5 "
                          "-loop 0 '%s/.recording.webp'",
                          width_, height_, fps_, directory_.c_str());
            pipe_ = popen(command, "w");
            frames_ = 0;
        }
        if (pipe_ != nullptr)
        {
            std::fwrite(rgba, 1, static_cast<std::size_t>(width_) * height_ * 4, pipe_);
            ++frames_;
        }
    }
    void close(const std::string &name)
    {
        if (pipe_ == nullptr)
            return;
        pclose(pipe_);
        pipe_ = nullptr;
        const std::string from = directory_ + "/.recording.webp";
        const std::string to = directory_ + "/" + name + ".webp";
        std::rename(from.c_str(), to.c_str());
        std::fprintf(stderr, "recorded %s: %d frames\n", to.c_str(), frames_);
    }

  private:
    std::string directory_;
    int width_;
    int height_;
    int fps_;
    FILE *pipe_ = nullptr;
    int frames_ = 0;
};

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: %s <assets dir> <output dir> [design id|all] [width height]\n",
                     argv[0]);
        return 2;
    }
    const std::string assets = argv[1];
    // The station list and a stand-in player that reports a Techno station
    // playing (host/preview_radio.hpp): nothing is heard, but the radio
    // screens can be seen as they look with music on.
    std::string station_list;
    hui::save::read_file(assets + "/radio/stations.txt", &station_list, 1u << 20);
    static hui::host::PreviewRadio radio(hui::audio::parse_stations(station_list));
    hui::audio::set_radio(&radio);
    for (std::size_t i = 0; i < radio.stations().size(); ++i)
    {
        if (radio.stations()[i].category == "Techno")
        {
            radio.play(static_cast<int>(i));
            break;
        }
    }
    const std::string output = argv[2];
    const std::string only = argc > 3 && std::string(argv[3]) != "all" ? argv[3] : "";
    const int width = argc > 5 ? std::atoi(argv[4]) : 1920;
    const int height = argc > 5 ? std::atoi(argv[5]) : 1080;

    if (!open_context())
    {
        std::fprintf(stderr, "no surfaceless EGL OpenGL 4.5 context\n");
        return 1;
    }
    std::fprintf(stderr, "GL %s / %s\n", reinterpret_cast<const char *>(glGetString(GL_VERSION)),
                 reinterpret_cast<const char *>(glGetString(GL_RENDERER)));
    hui::gfx::set_glsl_prefix("#version 450 core\n");

    hui::gfx::Renderer renderer;
    hui::gfx::Font regular;
    hui::gfx::Font semibold;
    hui::gfx::Font display;
    hui::gfx::Font mono;
    hui::gfx::Font pixel;
    hui::gfx::Font hand;
    hui::ui::Fonts fonts;
    if (!renderer.init() ||
        !load_font(renderer, assets + "/fonts/inter-regular.huifont", &regular, &fonts.regular) ||
        !load_font(renderer, assets + "/fonts/inter-semibold.huifont", &semibold,
                   &fonts.semibold) ||
        !load_font(renderer, assets + "/fonts/montserrat-medium.huifont", &display,
                   &fonts.display) ||
        !load_font(renderer, assets + "/fonts/dejavu-sans-mono.huifont", &mono, &fonts.mono) ||
        !load_font(renderer, assets + "/fonts/press-start-2p.huifont", &pixel, &fonts.pixel) ||
        !load_font(renderer, assets + "/fonts/patrick-hand.huifont", &hand, &fonts.hand))
        return 1;
    // Album art from canned answers (no network here).
    hui::net::CoverArt art;
    art.init([root = assets + "/.."](const std::string &url, std::string *body,
                                     const std::atomic<bool> &)
             { return hui::host::preview_fetch(root, url, body); },
             [&renderer](int w, int h, const std::uint8_t *rgba)
             { return renderer.batch().create_texture(w, h, rgba); },
             [](std::uint32_t texture)
             {
                 const GLuint name = texture;
                 glDeleteTextures(1, &name);
             },
             "", 0.0);
    hui::net::set_cover_art(&art);
    // Station logos: an invented picture for every other station.
    hui::net::CoverArt logos;
    logos.init(
        [root = assets + "/.."](const std::string &, std::string *body, const std::atomic<bool> &)
        {
            return hui::save::read_file(root + "/tests/unit/data/cover-b.jpg", body)
                       ? hui::net::FetchResult::ok
                       : hui::net::FetchResult::failed;
        },
        [&renderer](int w, int h, const std::uint8_t *rgba)
        { return renderer.batch().create_texture(w, h, rgba); },
        [](std::uint32_t texture)
        {
            const GLuint name = texture;
            glDeleteTextures(1, &name);
        },
        "", 0.0, hui::net::ArtSource::images);
    hui::net::set_station_logos(&logos);
    hui::gfx::Font extra_fonts[hui::ui::Fonts::kExtra];
    for (int i = 0; i < hui::ui::Fonts::kExtra; ++i)
        load_font(renderer, assets + "/fonts/" + hui::concepts::kGenreFontFiles[i], &extra_fonts[i],
                  &fonts.extra[i]);
    hui::demo::Catalog catalog;
    if (!catalog.build_covers(renderer, fonts))
    {
        std::fprintf(stderr, "cover art failed\n");
        return 1;
    }

    // HUI_ICON=<file.png>: draw the 512x512 launcher icon with the kit and stop.
    if (const char *icon_path = std::getenv("HUI_ICON"))
    {
        constexpr int kIcon = 512;
        GLuint target = 0;
        GLuint storage = 0;
        glGenFramebuffers(1, &target);
        glGenRenderbuffers(1, &storage);
        glBindRenderbuffer(GL_RENDERBUFFER, storage);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, kIcon, kIcon);
        glBindFramebuffer(GL_FRAMEBUFFER, target);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, storage);
        using hui::gfx::Color;
        // Afterglow's icon: the Skyline scene frozen mid-song, with the wordmark.
        hui::gfx::BackdropSpec spec;
        spec.mode = hui::gfx::BackdropMode::skyline;
        spec.colors[0] = Color::rgb(0x0b0612);
        spec.colors[1] = Color::rgb(0x2a0a1e);
        spec.colors[2] = Color::rgb(0xff5a36);
        spec.colors[3] = Color::rgb(0xffc857);
        spec.time = 12.0f;
        spec.params[0] = 3.0f;
        spec.audio[0] = 0.55f;
        spec.audio[3] = 0.4f;
        constexpr float kShape[16] = {0.95f, 0.85f, 0.7f, 0.62f, 0.55f, 0.6f, 0.48f, 0.4f,
                                      0.45f, 0.36f, 0.3f, 0.34f, 0.26f, 0.2f, 0.16f, 0.12f};
        for (int i = 0; i < 16; ++i)
            spec.spectrum[i] = kShape[i];
        hui::gfx::DrawList list;
        list.gradient_rect({0, 330, 512, 182}, 0, Color::rgb(0x000000, 0.0f),
                           Color::rgb(0x000000, 0.7f));
        hui::ui::text(list, fonts.display, "AFTERGLOW", 256, 452, 52, Color::rgb(0xfff4dc),
                      hui::gfx::Align::center, 6.0f);
        renderer.begin();
        renderer.backdrop(spec);
        renderer.draw(list);
        renderer.present(target, kIcon, kIcon, 512.0f, 512.0f);
        std::vector<unsigned char> icon(static_cast<std::size_t>(kIcon * kIcon * 4));
        glReadPixels(0, 0, kIcon, kIcon, GL_RGBA, GL_UNSIGNED_BYTE, icon.data());
        for (std::size_t i = 3; i < icon.size(); i += 4)
            icon[i] = 255;
        stbi_flip_vertically_on_write(1);
        return stbi_write_png(icon_path, kIcon, kIcon, 4, icon.data(), kIcon * 4) != 0 ? 0 : 1;
    }

    GLuint framebuffer = 0;
    GLuint color = 0;
    glGenFramebuffers(1, &framebuffer);
    glGenRenderbuffers(1, &color);
    glBindRenderbuffer(GL_RENDERBUFFER, color);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 1;

    // Settings are not read from (or written to) a real save: a scratch folder.
    const std::string data_root = output + "/.data";
    hui::save::ensure_directory(output);
    hui::save::ensure_directory(data_root);
    std::remove((data_root + "/settings.bin").c_str());
    hui::app::Shell shell(fonts, catalog, data_root, renderer.glass_texture());
    shell.set_version("host");

    constexpr float kDt = 1.0f / 60.0f;
    // HUI_MANIFEST=<file.json>: describe every design and theme (host/manifest.cpp).
    if (const char *manifest = std::getenv("HUI_MANIFEST"))
        return hui::host::write_manifest(manifest, shell) ? 0 : 1;

    std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 4));
    stbi_flip_vertically_on_write(1);
    bool ok = true;
    // HUI_WARM=<frames>: draw this many frames before each picture, so
    // feedback trails (which grow from frame to frame) are seen grown.
    const int warm = std::getenv("HUI_WARM") != nullptr ? std::atoi(std::getenv("HUI_WARM")) : 0;
    // HUI_ONLY=<name>,<name>: only the pictures whose names end in one of
    // these (the tour still runs whole), for quick looks at a few states.
    std::vector<std::string> picked;
    if (const char *list = std::getenv("HUI_ONLY"))
    {
        std::string all(list);
        std::size_t at = 0;
        while (at <= all.size())
        {
            std::size_t comma = all.find(',', at);
            if (comma == std::string::npos)
                comma = all.size();
            if (comma > at)
                picked.push_back("-" + all.substr(at, comma - at));
            at = comma + 1;
        }
    }
    const auto wanted = [&](const std::string &name)
    {
        if (picked.empty())
            return true;
        for (const std::string &end : picked)
            if (name.size() >= end.size() &&
                name.compare(name.size() - end.size(), end.size(), end) == 0)
                return true;
        return false;
    };
    const auto render = [&](const std::string &name)
    {
        if (!wanted(name))
        {
            shell.rehearse();
            return;
        }
        for (int i = 0; i < warm; ++i)
        {
            hui::InputFrame idle;
            idle.connected = true;
            shell.update(idle, kDt);
            shell.compose(renderer);
            renderer.present(framebuffer, width, height);
        }
        shell.compose(renderer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        renderer.present(framebuffer, width, height);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        // The PNG is opaque whatever the blending left in the alpha channel.
        for (std::size_t i = 3; i < pixels.size(); i += 4)
            pixels[i] = 255;
        const std::string path = output + "/" + name + ".png";
        ok = stbi_write_png(path.c_str(), width, height, 4, pixels.data(), width * 4) != 0 && ok;
        std::fprintf(stderr, "wrote %s: %zu shapes, %zu draw calls, GL error 0x%x\n", path.c_str(),
                     renderer.last_instances(), renderer.last_draw_calls(), glGetError());
    };

    // HUI_POSTER=<file.png>[,center]: the home-screen artwork (pic0, pic1):
    // a feedback scene grown over a few seconds, the sliced sun and the name.
    if (const char *poster = std::getenv("HUI_POSTER"))
    {
        std::string path = poster;
        const bool centered = path.size() > 7 && path.substr(path.size() - 7) == ",center";
        if (centered)
            path.resize(path.size() - 7);
        hui::gfx::BackdropSpec spec;
        spec.mode = hui::gfx::BackdropMode::scope;
        const std::uint32_t colours[4] = {0x000000, 0x0c0418, 0xff5a36, 0xffc857};
        for (int i = 0; i < 4; ++i)
            spec.colors[i] = hui::gfx::Color::rgb(colours[i]);
        spec.fx.enabled = true;
        spec.fx.bloom = 0.8f;
        spec.fx.fringe = 0.25f;
        spec.fx.grain = 0.12f;
        spec.fx.vignette = 0.7f;
        spec.fx.feedback.enabled = true;
        spec.fx.feedback.zoom = 1.03f;
        spec.fx.feedback.rotate = 0.010f;
        spec.fx.feedback.warp = 0.3f;
        spec.fx.feedback.decay = 0.955f;
        spec.fx.feedback.hue = 0.004f;
        hui::gfx::DrawList list;
        for (int frame = 0; frame < 150; ++frame)
        {
            const double t = 20.0 + frame / 60.0;
            const auto bands = hui::audio::demo_spectrum(t);
            for (int i = 0; i < 16; ++i)
                spec.spectrum[i] = bands[static_cast<std::size_t>(i)];
            for (int i = 0; i < 64; ++i)
            {
                const float x = static_cast<float>(i) / 64.0f * 6.2831853f;
                spec.wave[i] = 0.6f * std::sin(x * 2.0f + static_cast<float>(t) * 1.3f) +
                               0.3f * std::sin(x * 5.0f - static_cast<float>(t) * 2.1f);
            }
            spec.audio[0] = (bands[0] + bands[1] + bands[2]) / 3.0f;
            spec.time = static_cast<float>(t);
            spec.params[1] = static_cast<float>(t) * 0.02f;
            list.clear();
            const float cx = centered ? 960.0f : 560.0f;
            if (!centered)
                list.gradient_rect_h({0.0f, 0.0f, 1300.0f, 1080.0f}, 0.0f,
                                     hui::gfx::Color::rgb(0x05020a, 0.92f),
                                     hui::gfx::Color::rgb(0x05020a, 0.0f));
            constexpr float kRadius = 150.0f;
            list.glow({cx - kRadius, 470.0f - kRadius, kRadius * 2.0f, kRadius * 2.0f}, kRadius,
                      110.0f, hui::gfx::Color::rgb(0xff5a36, 0.30f));
            hui::concepts::sliced_sun(list, cx, 470.0f, kRadius, hui::gfx::Color::rgb(0xffc857),
                                      hui::gfx::Color::rgb(0xff5a36));
            list.rounded_rect({cx - 360.0f, 621.0f, 720.0f, 2.0f}, 0.0f,
                              hui::gfx::Color::rgb(0xff5a36, 0.8f));
            hui::ui::text(list, fonts.display, "AFTERGLOW", cx, 730.0f, 80.0f,
                          hui::gfx::Color::rgb(0xf6f3ff), hui::gfx::Align::center, 12.0f);
            hui::ui::text(list, fonts.semibold, "RADIO  +  VISUALS", cx, 790.0f, 24.0f,
                          hui::gfx::Color::rgb(0xf6f3ff, 0.7f), hui::gfx::Align::center, 10.0f);
            renderer.begin();
            renderer.backdrop(spec);
            renderer.draw(list);
            renderer.present(framebuffer, width, height);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        for (std::size_t i = 3; i < pixels.size(); i += 4)
            pixels[i] = 255;
        return stbi_write_png(path.c_str(), width, height, 4, pixels.data(), width * 4) != 0 ? 0
                                                                                             : 1;
    }

    if (const char *strip = std::getenv("HUI_STRIP"))
    {
        int from = 0;
        int to = 1;
        std::sscanf(strip, "%d,%d", &from, &to);
        hui::InputFrame idle;
        idle.connected = true;
        shell.show(static_cast<std::size_t>(from), false);
        for (int frame = 0; frame < 120; ++frame)
            shell.update(idle, kDt);
        shell.show(static_cast<std::size_t>(to), true);
        for (int frame = 0; frame < 36; ++frame)
        {
            shell.update(idle, kDt);
            char name[32];
            std::snprintf(name, sizeof(name), "strip-%02d", frame);
            render(name);
        }
        return ok ? 0 : 1;
    }

    if (const char *reel = std::getenv("HUI_REEL"))
    {
        constexpr int kEvery = 3; // 60 Hz simulated, 20 frames per second recorded
        hui::save::ensure_directory(reel);
        Recorder recorder(reel, width, height, 60 / kEvery);
        const auto record = [&]()
        {
            shell.compose(renderer);
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            renderer.present(framebuffer, width, height);
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            recorder.frame(pixels.data());
        };
        hui::InputFrame idle;
        idle.connected = true;
        long count = 0;
        if (std::getenv("HUI_REEL_SWITCH") != nullptr)
        {
            // R1 through every design, then the info panel: the app in one clip.
            shell.show(0, false);
            for (std::size_t i = 0; i <= shell.concept_count(); ++i)
            {
                for (int frame = 0; frame < 66; ++frame, ++count)
                {
                    hui::InputFrame input = idle;
                    if (frame == 0 && i > 0)
                        input.pressed = hui::action_bit(hui::Action::page_next);
                    shell.update(input, kDt);
                    if (count % kEvery == 0)
                        record();
                }
            }
            recorder.close("switcher");
            return 0;
        }
        const bool split = std::getenv("HUI_REEL_SPLIT") != nullptr;
        hui::app::Tour tour(shell, only);
        std::size_t design = shell.current();
        char name[96];
        while (!tour.finished() && count < 60 * 60 * 60)
        {
            const hui::InputFrame input = tour.step(kDt);
            if (!split && shell.current() != design)
            {
                std::snprintf(name, sizeof(name), "%s", shell.concept_at(design).info().id);
                recorder.close(name);
                design = shell.current();
            }
            shell.update(input, kDt);
            radio.tick(kDt);
            art.pump(kDt);
            logos.pump(kDt);
            logos.pump(kDt);
            if (count % kEvery == 0)
                record();
            ++count;
            if (!tour.capture().empty())
            {
                if (split)
                    recorder.close(tour.capture());
                tour.capture_done();
            }
        }
        if (!split)
            recorder.close(shell.concept_at(design).info().id);
        return 0;
    }

    hui::app::Tour tour(shell, only);
    if (tour.finished())
    {
        std::fprintf(stderr, "no design named '%s'\n", only.c_str());
        return 2;
    }
    long frames = 0;
    while (!tour.finished() && frames < 60 * 60 * 60)
    {
        const hui::InputFrame input = tour.step(kDt);
        // Nothing is presented here, so the numbers a console would measure
        // are stood in for: 60 Hz with a little jitter and a rare long frame.
        hui::app::Telemetry &telemetry = shell.telemetry();
        const float jitter = static_cast<float>((frames * 7919) % 97) / 97.0f;
        telemetry.push(16.3f + jitter * 0.8f + (frames % 211 == 0 ? 5.5f : 0.0f));
        telemetry.fps = 59.9f;
        telemetry.average_ms = 16.7f;
        telemetry.voices = static_cast<int>(frames / 40 % 4);
        telemetry.draw_calls = renderer.last_draw_calls();
        telemetry.instances = renderer.last_instances();
        shell.update(input, kDt);
        radio.tick(kDt);
        art.pump(kDt);
        logos.pump(kDt);
        ++frames;
        if (!tour.capture().empty())
        {
            render(tour.capture());
            tour.capture_done();
        }
        else
        {
            shell.rehearse(); // as on the console, every frame is drawn
        }
    }
    std::fprintf(stderr, "tour: %ld frames simulated\n", frames);
    return ok ? 0 : 1;
}
