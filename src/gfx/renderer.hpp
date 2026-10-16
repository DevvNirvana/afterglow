// ps5-homebrew-ui - Frame composition: backdrops, draw lists and frosted glass.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/backdrop.hpp"
#include "gfx/canvas.hpp"
#include "gfx/draw_list.hpp"
#include "gfx/gl_batch.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hui::gfx
{

// Composes one frame from a short list of layers, drawn back to front:
//
//   backdrop(spec)   a procedural full-screen shader (or a post overlay)
//   draw(list)       a DrawList of shapes, text and images
//   glass()          blurs everything queued so far into glass_texture(), so
//                    that later lists can draw frosted panels over it
//
// The glass copy is made by replaying the earlier layers into a small
// off-screen canvas and blurring that, so it costs nothing on frames that do
// not ask for it and never reads the display surface back.
class Renderer
{
  public:
    Renderer() = default;
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;
    ~Renderer();

    bool init();
    void release();

    GlBatch &batch()
    {
        return batch_;
    }
    Backdrop &backdrops()
    {
        return backdrop_;
    }
    // The texture later lists pass to DrawList::glass(). The name is stable
    // from init() on; its contents are those of the last frame that queued
    // glass().
    std::uint32_t glass_texture() const
    {
        return glass_a_.texture();
    }

    // ---- queue a frame ----
    void begin();
    void backdrop(const BackdropSpec &spec, float opacity = 1.0f);
    void draw(const DrawList &list);
    void glass();

    // Draws the queued layers into framebuffer (0 is the display). The
    // virtual size is the coordinate space of the lists.
    void present(GLuint framebuffer, int width, int height, float virtual_width = kVirtualWidth,
                 float virtual_height = kVirtualHeight);

    std::size_t last_draw_calls() const
    {
        return draw_calls_;
    }
    std::size_t last_instances() const
    {
        return instances_;
    }

  private:
    enum class Kind : std::uint8_t
    {
        backdrop,
        list,
        glass,
    };
    struct Layer
    {
        Kind kind = Kind::list;
        BackdropSpec spec;
        float opacity = 1.0f;
        const DrawList *list = nullptr;
    };

    void play(const Layer &layer, int width, int height, float virtual_width, float virtual_height);
    // A backdrop with fx.enabled: drawn off-screen, then finished into target.
    void play_finished(const Layer &layer, GLuint target, int width, int height);
    void capture_glass(std::size_t layers, float virtual_width, float virtual_height);

    GlBatch batch_;
    Backdrop backdrop_;
    Canvas glass_a_;
    Canvas glass_b_;
    GLuint blur_program_ = 0;
    GLuint blur_vao_ = 0;
    // The off-screen path: the scene, the one it cross-fades from, and bloom.
    Canvas scene_a_;
    Canvas scene_b_;
    Canvas bloom_a_;
    Canvas bloom_b_;
    GLuint finish_program_ = 0;
    // Feedback trails (gfx::FeedbackFx): two targets drawn into in turn.
    struct Trail
    {
        GLuint framebuffer = 0;
        GLuint texture = 0;
        int width = 0;
        int height = 0;
        bool create(int w, int h);
        void destroy();
    };
    Trail trails_[2];
    int trail_index_ = 0;
    bool trail_live_ = false; // fed last frame; otherwise it starts black
    GLuint feedback_program_ = 0;
    std::vector<Layer> layers_;
    std::size_t draw_calls_ = 0;
    std::size_t instances_ = 0;
};

} // namespace hui::gfx
