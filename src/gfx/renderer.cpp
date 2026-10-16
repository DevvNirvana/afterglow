// ps5-homebrew-ui - Frame composition: backdrops, draw lists and frosted glass.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gfx/renderer.hpp"

#include "gfx/gl_program.hpp"

#include <algorithm>
#include <utility>

namespace hui::gfx
{

namespace
{

// The glass copy is small on purpose: the blur hides the resolution, and a
// 480x270 target keeps both the replay and the blur passes cheap at 4K.
constexpr int kGlassWidth = 480;
constexpr int kGlassHeight = 270;

constexpr const char *kBlurVertex = R"(
out vec2 v_uv;
void main()
{
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    v_uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Nine-tap Gaussian from five bilinear fetches (weights after Rakos).
constexpr const char *kBlurFragment = R"(
layout(location = 0) uniform sampler2D u_texture;
layout(location = 1) uniform vec2 u_step;
in vec2 v_uv;
out vec4 frag_color;
void main()
{
    vec4 c = texture(u_texture, v_uv) * 0.227027;
    c += (texture(u_texture, v_uv + u_step * 1.384615) +
          texture(u_texture, v_uv - u_step * 1.384615)) * 0.316216;
    c += (texture(u_texture, v_uv + u_step * 3.230769) +
          texture(u_texture, v_uv - u_step * 3.230769)) * 0.070270;
    frag_color = vec4(c.rgb, 1.0);
}
)";

// The off-screen path for finished backdrops. Pass 0 is the bright-pass
// that feeds the bloom (into a small target); pass 1 composites the scene
// (cross-faded with the one it replaces) with the effects into the display.
constexpr const char *kFinishFragment = R"(
layout(location = 0) uniform sampler2D u_scene;
layout(location = 1) uniform sampler2D u_from;
layout(location = 2) uniform sampler2D u_bloom;
layout(location = 3) uniform int u_pass;
layout(location = 4) uniform float u_blend;
layout(location = 5) uniform vec4 u_fx;  // bloom, fringe, grain, scanlines
layout(location = 6) uniform vec4 u_fx2; // vignette, curve, time, opacity
layout(location = 7) uniform vec2 u_resolution;
in vec2 v_uv;
out vec4 frag_color;

vec3 scene(vec2 uv)
{
    vec3 c = texture(u_scene, uv).rgb;
    if (u_blend < 1.0)
        c = mix(texture(u_from, uv).rgb, c, u_blend);
    return c;
}

float hash(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

void main()
{
    if (u_pass == 0)
    {
        vec3 c = scene(v_uv);
        float peak = max(c.r, max(c.g, c.b));
        frag_color = vec4(c * smoothstep(0.35, 1.0, peak), 1.0);
        return;
    }
    vec2 uv = v_uv;
    vec2 d = uv - 0.5;
    // CRT glass: push the picture outwards with the square of the radius.
    uv = 0.5 + d * (1.0 + u_fx2.y * dot(d, d) * 1.6) / (1.0 + u_fx2.y * 0.4);
    vec2 e = uv - 0.5;
    vec3 c;
    if (u_fx.y > 0.0)
    {
        vec2 o = e * u_fx.y * 0.014;
        c = vec3(scene(uv + o).r, scene(uv).g, scene(uv - o).b);
    }
    else
    {
        c = scene(uv);
    }
    c += texture(u_bloom, uv).rgb * u_fx.x * 1.3;
    if (u_fx.w > 0.0)
    {
        // About 540 lines whatever the output size, like a CRT's.
        float line = 0.5 + 0.5 * cos(uv.y * 540.0 * 6.2831853);
        c *= 1.0 - u_fx.w * 0.42 * (1.0 - line);
        c *= 1.0 + u_fx.w * 0.12; // lines darken; give some of it back
    }
    c *= 1.0 - u_fx2.x * 0.75 * smoothstep(0.25, 0.85, length(e * vec2(1.0, 0.8)) * 1.15);
    c += (hash(gl_FragCoord.xy + fract(u_fx2.z * 7.13) * 97.0) - 0.5) * u_fx.z * 0.11;
    if (u_fx2.y > 0.0)
    {
        // Rounded black edges where the bulge leaves the picture.
        vec2 inside = smoothstep(vec2(0.0), vec2(0.004), uv) *
                      smoothstep(vec2(0.0), vec2(0.004), 1.0 - uv);
        c *= inside.x * inside.y;
    }
    frag_color = vec4(max(c, 0.0), u_fx2.w);
}
)";

// The feedback pass (gfx::FeedbackFx): the trail of earlier frames, moved
// and faded, under the fresh scene. Kaleidoscope folding happens before the
// turn and zoom, so the trail grows symmetric over a few frames.
constexpr const char *kFeedbackFragment = R"(
layout(location = 0) uniform sampler2D u_prev;
layout(location = 1) uniform sampler2D u_fresh;
layout(location = 2) uniform vec4 u_a; // zoom, rotate, warp, decay
layout(location = 3) uniform vec4 u_b; // hue, mirror, shift x, shift y
layout(location = 4) uniform vec4 u_c; // time, bass, blend, gain
layout(location = 5) uniform vec2 u_resolution;
in vec2 v_uv;
out vec4 frag_color;

vec3 hue_turn(vec3 c, float a)
{
    const vec3 k = vec3(0.57735027);
    float ca = cos(a);
    return c * ca + cross(k, c) * sin(a) + k * dot(k, c) * (1.0 - ca);
}

float dither(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

void main()
{
    float aspect = u_resolution.x / u_resolution.y;
    vec2 p = (v_uv - 0.5) * vec2(aspect, 1.0);
    float m = u_b.y;
    if (m > 0.5)
    {
        float r = length(p);
        float seg = 6.2831853 / m;
        float a = mod(atan(p.y, p.x), seg);
        a = abs(a - seg * 0.5);
        p = r * vec2(cos(a), sin(a));
    }
    float c = cos(u_a.y);
    float s = sin(u_a.y);
    p = mat2(c, -s, s, c) * p;
    p /= u_a.x * (1.0 + u_c.y * 0.02);
    float t = u_c.x;
    p += u_a.z * 0.010 *
         vec2(sin(p.y * 7.0 + t * 1.3) + sin(p.y * 3.1 - t * 0.7),
              cos(p.x * 6.0 - t * 1.1) + cos(p.x * 2.3 + t * 0.9));
    vec2 uv = p / vec2(aspect, 1.0) + 0.5 + u_b.zw;
    vec3 prev = vec3(0.0);
    if (uv.x >= 0.0 && uv.y >= 0.0 && uv.x <= 1.0 && uv.y <= 1.0)
        prev = texture(u_prev, uv).rgb;
    // Fade, with a dithered floor so eight-bit trails do not get stuck.
    prev = max(hue_turn(prev, u_b.x) * u_a.w - (1.0 + dither(gl_FragCoord.xy)) / 255.0, 0.0);
    vec3 fresh = texture(u_fresh, v_uv).rgb * u_c.w;
    vec3 col;
    if (u_c.z > 1.5)
        col = mix(prev, fresh, clamp(dot(fresh, vec3(0.3, 0.59, 0.11)) * 2.5, 0.0, 1.0));
    else if (u_c.z > 0.5)
        col = prev + fresh;
    else
        col = max(prev, fresh);
    frag_color = vec4(min(col, vec3(1.0)), 1.0);
}
)";

// The scene is drawn at most this large; a 4K display gets it upscaled. The
// scenes are soft, the effects hide the difference, and it keeps 4K cheap.
constexpr int kSceneMaxHeight = 1080;

int scaled_width(int height, int width, int out_height)
{
    return std::max(1, (width * out_height + height / 2) / std::max(1, height));
}

void ensure(Canvas &canvas, int width, int height)
{
    if (canvas.width() != width || canvas.height() != height || canvas.texture() == 0)
        canvas.create(width, height, 1);
}

} // namespace

bool Renderer::Trail::create(int w, int h)
{
    destroy();
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    // Sixteen-bit float keeps long trails smooth; eight bits if the target
    // cannot be drawn into.
    for (const GLenum format : {static_cast<GLenum>(GL_RGBA16F), static_cast<GLenum>(GL_RGBA8)})
    {
        glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(format), w, h, 0, GL_RGBA,
                     format == GL_RGBA8 ? GL_UNSIGNED_BYTE : GL_HALF_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (framebuffer == 0)
            glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
        {
            width = w;
            height = h;
            glViewport(0, 0, w, h);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            return true;
        }
    }
    destroy();
    return false;
}

void Renderer::Trail::destroy()
{
    if (framebuffer != 0)
        glDeleteFramebuffers(1, &framebuffer);
    if (texture != 0)
        glDeleteTextures(1, &texture);
    framebuffer = texture = 0;
    width = height = 0;
}

namespace
{

void set_filter(GLuint texture, bool nearest)
{
    glBindTexture(GL_TEXTURE_2D, texture);
    const GLint filter = nearest ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
}

} // namespace

Renderer::~Renderer()
{
    release();
}

bool Renderer::init()
{
    if (!batch_.init() || !backdrop_.init())
        return false;
    blur_program_ = build_program("blur", kBlurVertex, kBlurFragment);
    if (blur_program_ == 0)
        return false;
    finish_program_ = build_program("finish", kBlurVertex, kFinishFragment);
    if (finish_program_ == 0)
        return false;
    // Optional: without it feedback scenes are drawn without their trails.
    feedback_program_ = build_program("feedback", kBlurVertex, kFeedbackFragment);
    glGenVertexArrays(1, &blur_vao_);
    return bloom_a_.create(kGlassWidth, kGlassHeight, 1) &&
           bloom_b_.create(kGlassWidth, kGlassHeight, 1) &&
           glass_a_.create(kGlassWidth, kGlassHeight, 1) &&
           glass_b_.create(kGlassWidth, kGlassHeight, 1);
}

void Renderer::release()
{
    if (blur_vao_ != 0)
        glDeleteVertexArrays(1, &blur_vao_);
    if (blur_program_ != 0)
        glDeleteProgram(blur_program_);
    if (finish_program_ != 0)
        glDeleteProgram(finish_program_);
    if (feedback_program_ != 0)
        glDeleteProgram(feedback_program_);
    blur_vao_ = blur_program_ = finish_program_ = feedback_program_ = 0;
    trails_[0].destroy();
    trails_[1].destroy();
    scene_a_.destroy();
    scene_b_.destroy();
    bloom_a_.destroy();
    bloom_b_.destroy();
    glass_a_.destroy();
    glass_b_.destroy();
    backdrop_.release();
    batch_.release();
}

void Renderer::begin()
{
    layers_.clear();
}

void Renderer::backdrop(const BackdropSpec &spec, float opacity)
{
    if (spec.mode == BackdropMode::none || opacity <= 0.0f)
        return;
    Layer layer;
    layer.kind = Kind::backdrop;
    layer.spec = spec;
    layer.opacity = opacity;
    layers_.push_back(layer);
}

void Renderer::draw(const DrawList &list)
{
    if (list.empty())
        return;
    Layer layer;
    layer.kind = Kind::list;
    layer.list = &list;
    layers_.push_back(layer);
}

void Renderer::glass()
{
    Layer layer;
    layer.kind = Kind::glass;
    layers_.push_back(layer);
}

void Renderer::play(const Layer &layer, int width, int height, float virtual_width,
                    float virtual_height)
{
    if (layer.kind == Kind::backdrop)
    {
        backdrop_.draw(layer.spec, width, height, layer.opacity);
        ++draw_calls_;
    }
    else if (layer.kind == Kind::list)
    {
        batch_.draw(*layer.list, fit_viewport(width, height, virtual_width, virtual_height), width,
                    height);
        draw_calls_ += batch_.last_draw_calls();
    }
}

void Renderer::play_finished(const Layer &layer, GLuint target, int width, int height)
{
    const BackdropSpec &spec = layer.spec;
    const int full_height = std::min(height, kSceneMaxHeight);
    auto size_of = [&](int pixel_height)
    {
        const int h = pixel_height > 0 ? std::min(pixel_height, full_height) : full_height;
        return std::pair<int, int>{scaled_width(height, width, h), h};
    };

    const auto [aw, ah] = size_of(spec.fx.pixel_height);
    ensure(scene_a_, aw, ah);
    scene_a_.bind();
    backdrop_.draw(spec, aw, ah);
    ++draw_calls_;

    const bool fading = spec.blend < 1.0f && spec.from_mode != BackdropMode::none;
    if (fading)
    {
        const auto [bw, bh] = size_of(spec.from_pixel_height);
        ensure(scene_b_, bw, bh);
        scene_b_.bind();
        BackdropSpec from = spec;
        from.mode = spec.from_mode;
        backdrop_.draw(from, bw, bh);
        ++draw_calls_;
    }

    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(blur_vao_);

    // Feedback: the scene that has it (the arriving one first) feeds the
    // trail, and the trail stands in for that scene below.
    const bool fb_a = spec.fx.feedback.enabled && feedback_program_ != 0;
    const bool fb_b = !fb_a && fading && spec.from_feedback.enabled && feedback_program_ != 0;
    GLuint trail_texture = 0;
    if (fb_a || fb_b)
    {
        const Canvas &fresh = fb_a ? scene_a_ : scene_b_;
        const FeedbackFx &fb = fb_a ? spec.fx.feedback : spec.from_feedback;
        Trail &prev = trails_[trail_index_];
        Trail &next = trails_[trail_index_ ^ 1];
        const bool resized = prev.width != fresh.width() || prev.height != fresh.height();
        if (resized || !trail_live_)
        {
            prev.create(fresh.width(), fresh.height());
            next.create(fresh.width(), fresh.height());
        }
        if (prev.texture != 0 && next.texture != 0)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, next.framebuffer);
            glViewport(0, 0, next.width, next.height);
            glUseProgram(feedback_program_);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, fresh.texture());
            set_filter(fresh.texture(), false);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, prev.texture);
            glUniform1i(0, 0);
            glUniform1i(1, 1);
            glUniform4f(2, fb.zoom, fb.rotate, fb.warp, fb.decay);
            glUniform4f(3, fb.hue, static_cast<float>(fb.mirror), fb.shift_x, fb.shift_y);
            glUniform4f(4, spec.time, spec.audio[0], static_cast<float>(fb.blend), fb.gain);
            glUniform2f(5, static_cast<float>(next.width), static_cast<float>(next.height));
            glDrawArrays(GL_TRIANGLES, 0, 3);
            ++draw_calls_;
            trail_index_ ^= 1;
            trail_texture = next.texture;
        }
    }
    trail_live_ = trail_texture != 0;

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, bloom_a_.texture());
    glActiveTexture(GL_TEXTURE1);
    const bool a_nearest = spec.fx.pixel_height > 0 && !spec.fx.smooth;
    const bool b_nearest = spec.from_pixel_height > 0 && !spec.from_smooth;
    const GLuint a_texture = fb_a && trail_texture != 0 ? trail_texture : scene_a_.texture();
    const GLuint b_texture = fb_b && trail_texture != 0 ? trail_texture : scene_b_.texture();
    if (fading)
        set_filter(b_texture, b_nearest && !fb_b);
    else
        set_filter(a_texture, a_nearest && !fb_a);
    glActiveTexture(GL_TEXTURE0);
    set_filter(a_texture, a_nearest && !fb_a);

    glUseProgram(finish_program_);
    glUniform1i(0, 0);
    glUniform1i(1, 1);
    glUniform1i(2, 2);
    glUniform1f(4, fading ? spec.blend : 1.0f);
    glUniform4f(5, spec.fx.bloom, spec.fx.fringe, spec.fx.grain, spec.fx.scanlines);
    glUniform4f(6, spec.fx.vignette, spec.fx.curve, spec.time, layer.opacity);
    glUniform2f(7, static_cast<float>(width), static_cast<float>(height));
    if (spec.fx.bloom > 0.0f)
    {
        // Bright parts into the small target, then the glass blur, wider.
        bloom_a_.bind();
        glUniform1i(3, 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glUseProgram(blur_program_);
        glUniform1i(0, 0);
        glActiveTexture(GL_TEXTURE0);
        for (float step : {1.0f, 2.5f, 4.0f})
        {
            bloom_b_.bind();
            glBindTexture(GL_TEXTURE_2D, bloom_a_.texture());
            glUniform2f(1, step / static_cast<float>(kGlassWidth), 0.0f);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            bloom_a_.bind();
            glBindTexture(GL_TEXTURE_2D, bloom_b_.texture());
            glUniform2f(1, 0.0f, step / static_cast<float>(kGlassHeight));
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        draw_calls_ += 7;
        glBindTexture(GL_TEXTURE_2D, a_texture);
        glUseProgram(finish_program_);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, target);
    glViewport(0, 0, width, height);
    if (layer.opacity < 1.0f)
    {
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }
    glUniform1i(3, 1);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    ++draw_calls_;
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
}

void Renderer::capture_glass(std::size_t layers, float virtual_width, float virtual_height)
{
    glass_a_.bind();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    for (std::size_t i = 0; i < layers; ++i)
        play(layers_[i], kGlassWidth, kGlassHeight, virtual_width, virtual_height);

    // Two rounds of horizontal then vertical blur, the second one wider.
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glUseProgram(blur_program_);
    glUniform1i(0, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(blur_vao_);
    for (float step : {1.0f, 2.2f})
    {
        glass_b_.bind();
        glBindTexture(GL_TEXTURE_2D, glass_a_.texture());
        glUniform2f(1, step / static_cast<float>(kGlassWidth), 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glass_a_.bind();
        glBindTexture(GL_TEXTURE_2D, glass_b_.texture());
        glUniform2f(1, 0.0f, step / static_cast<float>(kGlassHeight));
        glDrawArrays(GL_TRIANGLES, 0, 3);
        draw_calls_ += 2;
    }
    glBindVertexArray(0);
}

void Renderer::present(GLuint framebuffer, int width, int height, float virtual_width,
                       float virtual_height)
{
    draw_calls_ = 0;
    instances_ = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, width, height);
    for (std::size_t i = 0; i < layers_.size(); ++i)
    {
        const Layer &layer = layers_[i];
        if (layer.kind == Kind::glass)
        {
            capture_glass(i, virtual_width, virtual_height);
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glViewport(0, 0, width, height);
            continue;
        }
        if (layer.kind == Kind::list)
            instances_ += layer.list->instances().size();
        if (layer.kind == Kind::backdrop && layer.spec.fx.enabled)
            play_finished(layer, framebuffer, width, height);
        else
            play(layer, width, height, virtual_width, virtual_height);
    }
}

} // namespace hui::gfx
