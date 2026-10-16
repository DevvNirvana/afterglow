// ps5-homebrew-ui - Procedural full-screen backdrops and post overlays.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gfx/backdrop.hpp"

#include "gfx/backdrop_gallery.hpp"
#include "gfx/backdrop_ported.hpp"
#include "gfx/backdrop_trip.hpp"
#include "gfx/gl_program.hpp"

#include <string>

namespace hui::gfx
{

namespace
{

// One triangle that covers the whole target; no vertex buffer is needed.
constexpr const char *kVertex = R"(
void main()
{
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

constexpr const char *kFragment = R"(
layout(location = 0) uniform vec2 u_resolution;
layout(location = 1) uniform float u_time;
layout(location = 2) uniform int u_mode;
layout(location = 3) uniform vec4 u_colors[4];
layout(location = 7) uniform vec4 u_params;
layout(location = 8) uniform float u_opacity;
layout(location = 9) uniform vec4 u_audio;          // bass, mid, treble, beat
layout(location = 10) uniform vec4 u_aux;           // steer x, steer y, drive
layout(location = 11) uniform float u_spectrum[16]; // occupies 11..26
layout(location = 27) uniform float u_peaks[16];    // occupies 27..42
out vec4 frag_color;

float band(int i)
{
    return u_spectrum[clamp(i, 0, 15)];
}

float hash(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}

float fbm(vec2 p)
{
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 4; ++i)
    {
        value += amplitude * noise(p);
        p = p * 2.03 + 17.1;
        amplitude *= 0.5;
    }
    return value;
}

float star_layer(vec2 p, float scale, float seed)
{
    vec2 g = p * scale;
    vec2 id = floor(g);
    vec2 f = fract(g) - 0.5;
    float h = hash(id + seed);
    vec2 o = (vec2(hash(id + seed + 3.1), hash(id + seed + 7.7)) - 0.5) * 0.7;
    float twinkle = 0.6 + 0.4 * sin(u_time * (1.0 + 3.0 * h) + h * 40.0);
    float size = 0.03 + 0.05 * fract(h * 17.0);
    return step(0.86, h) * smoothstep(size, 0.0, length(f - o)) * twinkle;
}


mat2 rot(float a)
{
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c);
}

// 4x4 ordered dither threshold in 0..1, for the retro scenes.
float bayer4(vec2 p)
{
    ivec2 i = ivec2(mod(p, 4.0));
    int index = i.x + i.y * 4;
    const float m[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0,
                                  3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
    return (m[index] + 0.5) / 16.0;
}

// Colour reduced to `levels` steps per channel with an ordered dither.
vec3 posterize(vec3 c, float levels, vec2 pixel)
{
    return floor(c * (levels - 1.0) + bayer4(pixel)) / (levels - 1.0);
}

// One layer of glyph rain. Returns the brightness of the trails in x and
// of the heads in y.
vec2 rain_layer(vec2 uv, float columns, float rows, float seed, float travel, float beat)
{
    vec2 g = vec2(uv.x * columns, uv.y * rows);
    vec2 id = floor(g);
    vec2 f = fract(g);
    float h = hash(vec2(id.x, seed));
    float speed = 0.55 + h * 1.1;
    float length_rows = 8.0 + h * 22.0;
    float cycle = rows + length_rows + 6.0;
    float head = mod(travel * speed * 6.0 + h * 97.0, cycle) - 3.0;
    float behind = head - id.y;
    float trail = behind >= 0.0 && behind < length_rows ? 1.0 - behind / length_rows : 0.0;
    float energy = u_spectrum[int(mod(id.x + seed, 16.0))];
    // A glyph: a 5x7 pattern, mirrored for a written look, that changes now
    // and then (faster near the head).
    vec2 cell = (f - vec2(0.18, 0.1)) / vec2(0.64, 0.8);
    float glyph = 0.0;
    if (cell.x >= 0.0 && cell.x < 1.0 && cell.y >= 0.0 && cell.y < 1.0)
    {
        vec2 bit = floor(cell * vec2(5.0, 7.0));
        bit.x = min(bit.x, 4.0 - bit.x);
        float change = floor(u_time * (1.0 + 6.0 * hash(id + seed)) + hash(id) * 10.0);
        glyph = step(0.48, hash(bit * 1.7 + hash(id + change) * 31.0));
    }
    float is_head = step(behind, 0.999) * step(0.0, behind);
    return vec2(glyph * pow(trail, 1.6) * (0.6 + 1.1 * energy),
                glyph * is_head * (0.7 + 0.6 * beat));
}

float sd_octahedron(vec3 p, float s)
{
    p = abs(p);
    return (p.x + p.y + p.z - s) * 0.57735027;
}

float sd_box(vec3 p, vec3 b)
{
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

// The polygon scene's world: a spinning gem and three orbiting cubes. The
// angles are quantised, like the jitter of an old fixed-point renderer.
float polygon_world(vec3 p, out float material)
{
    float spin = floor((u_params.y * 30.0 + u_params.z) * 48.0) / 48.0;
    vec3 q = p;
    q.xz = rot(spin) * q.xz;
    q.xy = rot(spin * 0.6) * q.xy;
    float d = sd_octahedron(q, 0.9 + u_audio.x * 0.28);
    material = 0.0;
    for (int i = 0; i < 3; ++i)
    {
        float a = float(i) * 2.0943951 + floor(u_time * 24.0) / 24.0 * 0.8;
        vec3 c = p - vec3(cos(a) * 1.9, sin(a * 1.3) * 0.45, sin(a) * 1.9);
        c.xy = rot(a * 2.0) * c.xy;
        c.yz = rot(a * 1.5) * c.yz;
        float b = sd_box(c, vec3(0.2 + u_spectrum[4 + i * 3] * 0.14));
        if (b < d)
        {
            d = b;
            material = 1.0 + float(i);
        }
    }
    return d;
}

vec3 polygon_normal(vec3 p)
{
    float m;
    vec2 e = vec2(0.002, 0.0);
    return normalize(vec3(polygon_world(p + e.xyy, m) - polygon_world(p - e.xyy, m),
                          polygon_world(p + e.yxy, m) - polygon_world(p - e.yxy, m),
                          polygon_world(p + e.yyx, m) - polygon_world(p - e.yyx, m)));
}

)";

// main() follows the ported scenes (gfx/backdrop_ported.hpp).
constexpr const char *kFragmentMain = R"(
void main()
{
    // uv: 0..1 with the origin at the top-left, like the virtual canvas.
    vec2 uv = vec2(gl_FragCoord.x / u_resolution.x, 1.0 - gl_FragCoord.y / u_resolution.y);
    float aspect = u_resolution.x / u_resolution.y;
    vec2 centred = (uv - 0.5) * vec2(aspect, 1.0);
    vec3 c0 = u_colors[0].rgb;
    vec3 c1 = u_colors[1].rgb;
    vec3 c2 = u_colors[2].rgb;
    vec3 c3 = u_colors[3].rgb;
    vec3 col = vec3(0.0);
    float alpha = 1.0;

    if (u_mode == 1) // gradient
    {
        col = mix(c0, c1, smoothstep(0.0, 1.0, uv.y));
        vec2 d = (uv - u_params.xy) * vec2(aspect, 1.0);
        col += c2 * u_params.z * exp(-dot(d, d) * 3.0);
    }
    else if (u_mode == 2) // aurora
    {
        vec2 p = uv * vec2(aspect, 1.0);
        float t = u_time * 0.05;
        vec2 q = p * 1.4 + vec2(fbm(p * 1.2 + t), fbm(p * 1.2 - t + 7.3));
        float a = fbm(q + vec2(t * 0.7, -t * 0.4));
        float b = fbm(q * 1.7 - vec2(t * 0.3, t * 0.6) + 3.1);
        col = mix(c0, c1, smoothstep(0.1, 0.9, uv.y));
        col = mix(col, c2, smoothstep(0.42, 0.85, a) * 0.62);
        col = mix(col, c3, smoothstep(0.48, 0.9, b) * 0.45);
        col *= 1.0 - 0.35 * smoothstep(0.45, 1.1, length(centred));
    }
    else if (u_mode == 3) // grid
    {
        float horizon = 0.56;
        if (uv.y < horizon)
        {
            float t = uv.y / horizon;
            col = mix(c0, c1, pow(t, 2.2));
            col += vec3(0.8) * step(0.9975, hash(floor(uv * vec2(640.0, 360.0)))) * (1.0 - t);
            vec2 s = vec2((uv.x - 0.5) * aspect, uv.y - (horizon - 0.13));
            float r = length(s);
            float sun = smoothstep(0.204, 0.2, r);
            float gap = clamp(s.y / 0.2, 0.0, 1.0);
            float cut = step(fract(s.y * 26.0 - u_time * 0.12), gap * 0.62) * step(0.0, s.y);
            vec3 sun_colour = mix(c3, c2, clamp((s.y + 0.2) / 0.4, 0.0, 1.0));
            col = mix(col, sun_colour, sun * (1.0 - cut));
            col += c3 * 0.22 * exp(-r * r * 16.0);
        }
        else
        {
            float d = uv.y - horizon;
            float z = 0.08 / d;
            vec2 q = vec2((uv.x - 0.5) * aspect * z * 6.0, z * 2.2 + u_time * 0.6);
            vec2 f = abs(fract(q - 0.5) - 0.5);
            vec2 aa = fwidth(q) * 1.5;
            vec2 l = 1.0 - smoothstep(vec2(0.018), vec2(0.018) + aa, f);
            float line = max(l.x, l.y) * clamp(d * 7.0, 0.0, 1.0);
            col = mix(c0 * 0.4, c2, line);
            col += c2 * 0.12 * (1.0 - clamp(d * 3.0, 0.0, 1.0));
        }
        col += c2 * 0.45 * exp(-abs(uv.y - horizon) * 46.0);
    }
    else if (u_mode == 4) // stars
    {
        vec2 cam = u_params.xy;
        col = mix(c0, c1, uv.y);
        float n = fbm(centred * 1.6 + cam * 0.15 + vec2(u_time * 0.01, 0.0));
        float n2 = fbm(centred * 2.3 + cam * 0.1 + 5.2);
        col += c2 * smoothstep(0.45, 0.9, n) * 0.35;
        col += c3 * smoothstep(0.5, 0.95, n2) * 0.25;
        col += vec3(0.9) * star_layer(centred + cam * 0.05, 14.0, 1.0);
        col += vec3(0.6) * star_layer(centred + cam * 0.12, 26.0, 9.0);
        col += vec3(0.35) * star_layer(centred + cam * 0.25, 44.0, 23.0);
    }
    else if (u_mode == 5) // waves
    {
        col = mix(c0, c1, uv.y);
        for (int i = 0; i < 5; ++i)
        {
            float fi = float(i);
            float y = 0.42 + fi * 0.1 +
                      0.05 * sin(uv.x * (2.0 + fi * 0.7) + u_time * (0.25 + 0.07 * fi) + fi * 1.7) +
                      0.025 * sin(uv.x * (5.0 + fi) - u_time * 0.2 + fi);
            vec3 ribbon = mix(c2, c3, fi / 4.0);
            col = mix(col, ribbon, smoothstep(y - 0.002, y + 0.002, uv.y) * 0.16);
            col += ribbon * 0.3 * exp(-abs(uv.y - y) * 220.0);
        }
    }
    else if (u_mode == 6) // bokeh
    {
        col = mix(c0, c1, uv.y);
        vec2 p = vec2(uv.x * aspect, uv.y);
        for (int i = 0; i < 16; ++i)
        {
            float fi = float(i);
            float h1 = hash(vec2(fi, 1.3));
            float h2 = hash(vec2(fi, 7.9));
            float h3 = hash(vec2(fi, 13.1));
            vec2 c = vec2(h1 * aspect + 0.05 * sin(u_time * 0.2 + fi),
                          fract(h2 - u_time * (0.01 + 0.02 * h3)) * 1.3 - 0.15);
            float r = 0.05 + 0.13 * h3;
            col += mix(c2, c3, h1) * smoothstep(r, r * 0.75, length(p - c)) * (0.05 + 0.07 * h2);
        }
    }
    else if (u_mode == 7) // phosphor
    {
        col = c0 + c1 * exp(-dot(centred, centred) * 1.6);
        col += c1 * 0.03 * hash(floor(uv * vec2(480.0, 270.0)) + floor(u_time * 24.0));
    }
    else if (u_mode == 8) // paper
    {
        col = mix(c0, c1, uv.y);
        vec2 d = (uv - vec2(0.15, 0.0)) * vec2(aspect, 1.0);
        col += c2 * 0.5 * exp(-dot(d, d) * 1.2);
        col += (hash(floor(uv * vec2(1920.0, 1080.0))) - 0.5) * 0.03;
    }
    else if (u_mode == 9) // dots
    {
        col = mix(c0, c1, uv.y);
        vec2 p = vec2(uv.x * aspect, uv.y) * 36.0;
        vec2 f = fract(p) - 0.5;
        vec2 id = floor(p);
        float pulse = 0.5 + 0.5 * sin(length(id - vec2(18.0 * aspect, 18.0)) * 0.5 - u_time * 1.2);
        float r = 0.06 + 0.08 * pulse;
        float fade = 1.0 - smoothstep(0.2, 0.9, length(centred));
        col += c2 * smoothstep(r, r - 0.04, length(f)) * (0.1 + 0.25 * pulse) *
               (0.35 + 0.65 * fade);
    }
    else if (u_mode == 10) // vista
    {
        col = mix(c0, c1, smoothstep(0.0, 0.75, uv.y));
        vec2 s = vec2((uv.x - 0.72) * aspect, uv.y - 0.3);
        col += vec3(1.0, 0.9, 0.7) * 0.55 * exp(-dot(s, s) * 26.0);
        col = mix(col, vec3(1.0, 0.96, 0.84), smoothstep(0.062, 0.058, length(s)));
        for (int i = 0; i < 4; ++i)
        {
            float fi = float(i);
            float depth = fi / 3.0;
            float x = uv.x * (1.2 + fi * 0.6) + u_time * u_params.x * (0.02 + 0.05 * fi * fi) +
                      fi * 13.7;
            float h = 0.5 + 0.11 * fi + (fbm(vec2(x * 1.6, fi * 3.1)) - 0.5) * (0.28 - 0.03 * fi);
            vec3 ridge = mix(mix(c2, c3, depth), c1, (1.0 - depth) * 0.55);
            col = mix(col, ridge, smoothstep(h - 0.002, h + 0.002, uv.y));
        }
    }
    else if (u_mode == 11) // tunnel
    {
        float bass = u_audio.x;
        float mid = u_audio.y;
        float treble = u_audio.z;
        float beat = u_audio.w;
        vec2 p = centred - u_aux.xy * vec2(0.22, 0.14);
        // A slow wobble keeps the vanishing point alive when nobody steers.
        p += 0.03 * vec2(sin(u_time * 0.31), cos(u_time * 0.23));
        float r = max(length(p), 1e-3);
        float depth = 0.35 / r;
        // p0 is the distance travelled, integrated on the CPU so a change in
        // speed never makes the rings jump; p1 is a slow spin.
        float x = depth * 1.5 + u_params.x;
        float twist = atan(p.y, p.x) / 6.2831853 + 0.5 + depth * 0.04 + u_params.y;
        float fog = smoothstep(0.02, 0.35, r); // hides the singular centre

        int lane_index = int(floor(fract(twist) * 16.0));
        float energy = band(lane_index);
        float lane = smoothstep(0.5, 0.40, abs(fract(twist * 16.0) - 0.5));
        vec3 wall = mix(c1, c0, smoothstep(0.0, 0.9, r));
        col = wall * (0.55 + 0.45 * lane * (0.3 + energy));
        col += mix(c2, c3, float(lane_index) / 15.0) * lane * energy * energy * 0.6 * fog;

        float d = abs(fract(x) - 0.5);
        float w = 0.035 + bass * 0.07 + fwidth(x) * 1.5;
        float ring = smoothstep(0.5 - w, 0.5, d);
        vec3 ring_colour = mix(c2, c3, 0.5 + 0.5 * sin(floor(x) * 1.7 + mid * 2.0));
        col += ring_colour * ring * (0.5 + bass * 1.3) * fog;

        // Treble sparks: small dots in sparse cells along the deeper wall
        // (near the screen edge the cells would be huge), flashing with the hats.
        vec2 spark_cell = vec2(fract(twist) * 48.0, x * 3.0);
        float spark = step(0.95, hash(floor(spark_cell))) *
                      smoothstep(0.3, 0.05, length(fract(spark_cell) - 0.5)) *
                      (1.0 - smoothstep(0.35, 0.7, r));
        col += mix(c3, vec3(1.0), 0.5) * spark * treble * 1.2 * fog;
        // The light at the end of the tunnel flares on each beat.
        col += c3 * exp(-r * r * 45.0) * (0.22 + beat * 0.9);
        col *= 1.0 - 0.45 * smoothstep(0.55, 1.15, length(centred));
    }
    else if (u_mode == 12) // skyline
    {
        float bass = u_audio.x;
        float treble = u_audio.z;
        float beat = u_audio.w;
        float horizon = 0.66;
        float sx = (uv.x - 0.5) * aspect - u_aux.x * 0.12;
        float sy = uv.y - horizon;
        if (sy < 0.0)
        {
            float t = uv.y / horizon;
            col = mix(c0, c1, pow(t, 1.8));
            col += vec3(0.85) * star_layer(centred + vec2(u_time * 0.005, 0.0), 30.0, 4.0) *
                   (1.0 - t) * (0.5 + treble * 1.5);
            vec2 s = vec2(sx, uv.y - (horizon - 0.2));
            float rs = length(s);
            float sun_radius = 0.19 + bass * 0.025;
            vec3 sun = mix(c3, c2, clamp((s.y + sun_radius) / (2.0 * sun_radius), 0.0, 1.0));
            float cut = step(0.0, s.y) *
                        step(fract(s.y * 22.0 - u_time * 0.15), clamp(s.y / sun_radius, 0.0, 1.0) * 0.55);
            col = mix(col, sun, smoothstep(sun_radius + 0.003, sun_radius, rs) * (1.0 - cut));
            col += c3 * exp(-rs * rs * 9.0) * (0.18 + beat * 0.45);
        }
        else
        {
            col = mix(c1 * 0.35, c0 * 0.6, clamp(sy * 2.5, 0.0, 1.0));
            float z = 0.07 / max(sy, 0.002);
            vec2 q = vec2(sx * z * 5.0, z * 2.0 + u_params.x);
            vec2 f = abs(fract(q - 0.5) - 0.5);
            vec2 aa = fwidth(q) * 1.5;
            vec2 l = 1.0 - smoothstep(vec2(0.02), vec2(0.02) + aa, f);
            col += c2 * max(l.x, l.y) * clamp(sy * 6.0, 0.0, 1.0) * (0.25 + bass * 0.35);
        }
        // Mirrored bars: the low bands in the middle, the high ones outside.
        float fx = abs(sx) / (0.5 * aspect);
        float slot = fx * 16.0;
        int bar_index = int(floor(slot));
        float cell = fract(slot);
        float aa_x = fwidth(slot) * 1.2;
        float in_bar = smoothstep(0.14, 0.14 + aa_x, cell) *
                       (1.0 - smoothstep(0.86 - aa_x, 0.86, cell)) * step(fx, 1.0);
        float energy = band(bar_index);
        float height = 0.03 + energy * 0.40 * (1.0 + u_aux.z * 0.35);
        vec3 bar_colour = mix(c2, c3, clamp(-sy / 0.45, 0.0, 1.0));
        if (sy < 0.0)
        {
            float aa_y = fwidth(sy) * 1.2;
            float bar = in_bar * smoothstep(-height - aa_y, -height + aa_y, sy);
            col = mix(col, bar_colour * (0.75 + 0.5 * energy), bar);
            col += bar_colour * in_bar * exp(-max(-height - sy, 0.0) * 40.0) * 0.18 *
                   step(sy, -height);
        }
        else
        {
            col += bar_colour * in_bar * (1.0 - smoothstep(0.0, height * 0.7 + 0.001, sy)) * 0.28;
        }
        col += c2 * exp(-abs(sy) * 90.0) * (0.35 + beat * 0.8);
        col *= 1.0 - 0.35 * smoothstep(0.6, 1.2, length(centred));
    }
    else if (u_mode == 13) // nebula
    {
        float bass = u_audio.x;
        float mid = u_audio.y;
        float treble = u_audio.z;
        float beat = u_audio.w;
        vec2 cam = u_aux.xy * 0.35;
        vec2 p = centred * (1.5 - bass * 0.22) + cam;
        float t = u_params.x * 0.05;
        vec2 q = vec2(fbm(p * 1.3 + vec2(t, -t * 0.6)), fbm(p * 1.3 + vec2(5.2 - t * 0.4, 1.3 + t)));
        float n = fbm(p * 1.8 + q * (1.6 + mid * 1.4) + vec2(t * 1.5, -t));
        float m = fbm(p * 2.6 - q * 1.2 + 9.1);
        col = mix(c0, c1, smoothstep(0.0, 1.0, uv.y + (q.y - 0.5) * 0.3));
        col = mix(col, c2, smoothstep(0.38, 0.82, n) * (0.55 + bass * 0.55));
        col = mix(col, c3, smoothstep(0.5, 0.9, m * n * 1.7) * (0.4 + mid * 0.6));
        vec2 core = centred - cam * 0.2;
        col += c3 * exp(-dot(core, core) * 3.5) * beat * 0.4;
        col += vec3(0.95) * star_layer(centred + cam * 0.08, 20.0, 3.0) * (0.35 + treble * 1.4);
        col += vec3(0.6) * star_layer(centred + cam * 0.15, 38.0, 11.0) * (0.25 + treble);
        col *= 1.0 - 0.4 * smoothstep(0.5, 1.2, length(centred));
    }
    else if (u_mode == 30) // rain
    {
        float beat = u_audio.w;
        vec2 p = uv + vec2(u_aux.x * 0.03, 0.0);
        col = mix(c0, c1, uv.y * 0.7);
        vec2 far = rain_layer(p + vec2(0.37, 0.0), 120.0, 46.0, 11.0, u_params.x * 0.6, beat);
        vec2 near = rain_layer(p, 64.0, 24.0, 3.0, u_params.x, beat);
        col += c2 * far.x * 0.3 + c3 * far.y * 0.35;
        col += c2 * near.x + c3 * near.y;
        col += c2 * 0.05 * beat;
    }
    else if (u_mode == 31) // polygon
    {
        float beat = u_audio.w;
        float horizon = 0.58;
        vec3 sky = mix(c0, c1, smoothstep(0.0, horizon, uv.y)) + c3 * beat * 0.12;
        col = sky;
        if (uv.y > horizon)
        {
            // A checker floor with the swim of affine texture mapping.
            float d = uv.y - horizon;
            float z = 0.12 / d;
            vec2 q = vec2((uv.x - 0.5) * aspect * z * 3.0, z + u_params.x * 0.8);
            q += 0.06 * sin(vec2(q.y * 2.0, q.x * 1.5) + u_time * 0.7);
            float check = mod(floor(q.x) + floor(q.y), 2.0);
            int lane = int(mod(floor(q.x) + 8.0, 16.0));
            vec3 tile = mix(c0 * 1.6 + 0.04, c1 * 1.3, check);
            tile += c2 * u_spectrum[lane] * check * 0.6;
            col = mix(tile, sky, smoothstep(0.25, 0.0, d) * 0.9);
        }
        vec3 ro = vec3(u_aux.x * 0.6, 0.25 - u_aux.y * 0.4, -4.6);
        vec3 rd = normalize(vec3(centred.x, -centred.y + 0.06, 1.35));
        float t = 0.0;
        float material = 0.0;
        bool hit = false;
        for (int i = 0; i < 48; ++i)
        {
            float m;
            float d = polygon_world(ro + rd * t, m);
            if (d < 0.003)
            {
                hit = true;
                material = m;
                break;
            }
            t += d;
            if (t > 9.0)
                break;
        }
        if (hit)
        {
            vec3 n = polygon_normal(ro + rd * t);
            vec3 light = normalize(vec3(-0.5, 0.8, -0.6));
            float diffuse = max(dot(n, light), 0.0);
            vec3 base = material < 0.5 ? mix(c2, c3, 0.5 + 0.5 * n.y) : mix(c3, c2, material / 3.0);
            col = base * (0.25 + 0.85 * diffuse) +
                  c3 * pow(max(dot(reflect(-light, n), -rd), 0.0), 12.0) * 0.4;
            col *= 1.0 + beat * 0.25;
        }
        col = posterize(col, 32.0, gl_FragCoord.xy);
    }
    else if (u_mode == 32) // mode7
    {
        float beat = u_audio.w;
        float horizon = 0.4;
        float heading = u_params.z;
        if (uv.y < horizon)
        {
            float t = uv.y / horizon;
            col = mix(c0, mix(c0, c3, 0.6), floor(t * 9.0) / 9.0); // banded, few colours
            float x = uv.x * aspect * 1.4 + heading * 1.3;
            float cloud = fbm(vec2(uv.x * aspect * 2.5 + heading * 0.5 + u_time * 0.02, uv.y * 7.0));
            float ridge = horizon - 0.07 - 0.1 * fbm(vec2(x * 1.5, 2.0));
            float hills = horizon - 0.02 - 0.05 * fbm(vec2(x * 3.0 + heading * 0.7, 5.0));
            col = mix(col, vec3(1.0), step(0.6, cloud) * step(uv.y, ridge) * 0.85);
            if (uv.y > ridge)
                col = mix(c1, c0, 0.55) * 0.75;
            if (uv.y > hills)
                col = c1 * 0.7;
        }
        else
        {
            float d = uv.y - horizon;
            float z = 0.16 / d;
            vec2 w = rot(heading) * vec2((uv.x - 0.5) * aspect * z, z) * 8.0;
            w.y += u_params.x * 12.0;
            vec2 tile = floor(w);
            vec2 f = fract(w);
            float check = mod(tile.x + tile.y, 2.0);
            col = mix(c1, c1 * 0.62, check);
            // A road along the world's y axis with dashed lines; turning the
            // heading swings it across the screen.
            float road = step(abs(w.x), 0.9);
            col = mix(col, vec3(0.42, 0.42, 0.48), road);
            col = mix(col, c3, road * step(abs(w.x), 0.08) * step(0.5, fract(w.y * 0.25)));
            col = mix(col, c2, step(abs(abs(w.x) - 0.9), 0.08));
            // Panels light up with the band they belong to.
            int lane = int(mod(tile.x * 3.0 + tile.y * 7.0, 16.0));
            float energy = u_spectrum[lane];
            float lit = step(hash(tile) * 0.9, energy * 1.2) * (1.0 - road);
            float panel = step(0.12, f.x) * step(f.x, 0.88) * step(0.12, f.y) * step(f.y, 0.88);
            col = mix(col, mix(c2, c3, energy), lit * panel);
            col = mix(col, mix(c0, c3, 0.5), smoothstep(0.05, 0.0, d) * 0.8);
        }
        col += c3 * beat * 0.08;
        col = posterize(col, 16.0, gl_FragCoord.xy);
    }
    else if (u_mode == 33) // bars
    {
        float beat = u_audio.w;
        // A dim swirl behind, like the ambience screens of old desktop players.
        vec2 p = centred * 1.4;
        float r = length(p);
        float a = atan(p.y, p.x) + u_params.y * 4.0 + r * (1.5 + u_audio.y);
        float swirl = fbm(vec2(cos(a), sin(a)) * r * 2.0 + u_params.x * 0.05);
        col = mix(c0, c1, swirl) * 0.8;
        float left = 0.14;
        float right = 0.86;
        float base = 0.74;
        float top = 0.36;
        float x = (uv.x - left) / (right - left);
        if (x >= 0.0 && x < 1.0)
        {
            float slot = x * 16.0;
            int i = int(floor(slot));
            float cell = fract(slot);
            float aa = fwidth(slot) * 1.2;
            float in_bar = smoothstep(0.1, 0.1 + aa, cell) * (1.0 - smoothstep(0.9 - aa, 0.9, cell));
            float level = u_spectrum[i];
            float h = (base - uv.y) / (base - top); // 0 at the shelf, 1 at the top
            if (h >= 0.0 && h <= 1.0)
            {
                float in_seg = step(0.24, fract(h * 24.0));
                vec3 led = mix(c2, c3, smoothstep(0.35, 0.95, h));
                float on = step(h, level);
                col = mix(col, led * (0.12 + 0.88 * on), in_bar * in_seg);
                float cap = step(abs(h - u_peaks[i] - 0.014), 0.012);
                col = mix(col, mix(c3, vec3(1.0), 0.4), in_bar * cap);
            }
            else if (h < 0.0 && h > -0.35)
            {
                float m = -h; // the reflection in the shelf
                float in_seg = step(0.24, fract(m * 24.0));
                vec3 led = mix(c2, c3, smoothstep(0.35, 0.95, m));
                col += led * step(m, level) * in_bar * in_seg * 0.22 * (1.0 - m / 0.35);
            }
        }
        col += c2 * 0.35 * exp(-abs(uv.y - base) * 400.0) * step(left, uv.x) * step(uv.x, right);
        // A scope line drawn from the bands.
        float wave = 0.0;
        for (int k = 0; k < 8; ++k)
        {
            float fk = float(k);
            wave += u_spectrum[k * 2] *
                    sin(uv.x * (6.0 + fk * 9.0) * 3.14159 + u_time * (2.0 + fk * 1.3) + fk) /
                    (1.0 + fk * 0.4);
        }
        float sy = 0.27 + wave * 0.035;
        col += c3 * exp(-abs(uv.y - sy) * 600.0) * 0.9 + c2 * exp(-abs(uv.y - sy) * 80.0) * 0.15;
        col += c2 * beat * 0.04;
    }
    else if (u_mode == 34) // candles
    {
        float bass = u_audio.x;
        float treble = u_audio.z;
        float beat = u_audio.w;
        vec2 cam = u_aux.xy * vec2(0.04, 0.02);
        // The ceiling: a night sky with slow cloud.
        col = mix(c0, c1, smoothstep(0.0, 1.0, uv.y));
        float cloud = fbm(centred * 2.0 + vec2(u_time * 0.015, 0.0) + cam);
        col += c1 * smoothstep(0.45, 0.85, cloud) * 0.6 * (1.0 - uv.y);
        col += vec3(0.9, 0.9, 1.0) * star_layer(centred + cam * 0.5, 24.0, 5.0) * (1.0 - uv.y) *
               (0.5 + beat * 0.8);
        // Tall pointed windows at the sides.
        for (int w = 0; w < 4; ++w)
        {
            float fw = float(w);
            float wx = w < 2 ? 0.06 + fw * 0.11 : 0.94 - (fw - 2.0) * 0.11;
            vec2 q = vec2((uv.x - wx) * aspect - cam.x * 2.0, uv.y);
            float half_w = 0.035;
            float body = step(abs(q.x), half_w) * step(0.42, q.y) * step(q.y, 0.95);
            float arch = step(length(vec2(abs(q.x) + half_w, q.y - 0.42)), 2.0 * half_w) *
                         step(q.y, 0.42);
            float window = max(body, arch);
            float mullion = step(abs(q.x), 0.003) + step(abs(fract(q.y * 12.0) - 0.5), 0.04);
            col = mix(col, mix(c1 * 1.8, c2, 0.12) * (0.6 + u_audio.y * 0.5),
                      window * (1.0 - min(mullion, 1.0)) * 0.55);
        }
        vec3 glow = vec3(0.0);
        for (int i = 0; i < 40; ++i)
        {
            float fi = float(i);
            float h1 = hash(vec2(fi, 2.1));
            float h2 = hash(vec2(fi, 5.3));
            float h3 = hash(vec2(fi, 9.7));
            float depth = 0.45 + 0.55 * h3;
            vec2 c = vec2(h1 * aspect, 0.1 + h2 * 0.5);
            c += cam * depth * 4.0;
            c.y += sin(u_time * (0.4 + h3 * 0.4) + fi) * 0.012;
            vec2 p = vec2(uv.x * aspect, uv.y) - c;
            float body = step(abs(p.x), 0.0045 * depth) * step(0.006 * depth, p.y) *
                         step(p.y, 0.045 * depth);
            col = mix(col, mix(vec3(0.85, 0.8, 0.7), c3, 0.2) * (0.35 + 0.4 * depth), body);
            float flick = 0.75 + 0.25 * sin(u_time * (9.0 + h1 * 7.0) + fi) * (0.5 + treble);
            vec2 fp = p + vec2(0.0, 0.006 * depth);
            fp.y *= 0.55; // flames are taller than wide
            glow += mix(c2, c3, 0.6) * exp(-dot(fp, fp) / (depth * depth) * 9000.0) * flick * 1.2;
            glow += c2 * exp(-length(fp) / depth * 28.0) * (0.06 + bass * 0.12 + beat * 0.1) * flick;
        }
        col += glow;
    }
    else if (u_mode == 35) // rays
    {
        float beat = u_audio.w;
        vec2 source = vec2(0.5 + u_aux.x * 0.25, -0.15 - u_aux.y * 0.1);
        vec2 d = (uv - source) * vec2(aspect, 1.0);
        float angle = atan(d.x, d.y); // 0 is straight down
        float dist = length(d);
        col = mix(c0, c1, smoothstep(0.0, 1.2, uv.y));
        // Sixteen shafts, the low bands in the middle, mirrored outwards.
        float across = clamp(angle / 1.15, -1.0, 1.0) * 0.5 + 0.5;
        float slot = across * 16.0;
        int i = int(min(floor(slot), 15.0));
        int b = int(abs(float(i) - 7.5) * 2.0);
        float level = u_spectrum[b];
        float shaft = pow(smoothstep(0.5, 0.0, abs(fract(slot) - 0.5)), 1.5);
        float n = noise(vec2(angle * 26.0, u_time * 0.25 + u_params.x * 0.05));
        float light = shaft * (0.12 + level * 0.95) * exp(-dist * 0.85) * (0.55 + 0.45 * n);
        light += 0.12 * exp(-abs(angle) * 1.6) * exp(-dist * 0.7);
        col += mix(c3, c2, clamp(dist * 0.6, 0.0, 1.0)) * light;
        col += vec3(1.0) * star_layer(centred + vec2(u_time * 0.004, -u_time * 0.01), 34.0, 7.0) *
               light * 2.5;
        col += c3 * exp(-dist * dist * 9.0) * (0.3 + beat * 0.6);
    }
    else if (u_mode == 36) // plasma
    {
        float beat = u_audio.w;
        vec2 p = centred * (6.0 + u_audio.x * 2.0) + u_aux.xy * 0.8;
        float t = u_params.x * 0.35 + u_time * 0.2;
        float v = sin(p.x * 2.0 + t);
        v += sin(2.0 * (p.x * sin(t * 0.5) + p.y * cos(t * 0.33)) + t);
        vec2 c = p + vec2(sin(t * 0.7), cos(t * 0.5)) * 2.0;
        v += sin(sqrt(dot(c, c) * (1.0 + u_audio.y * 2.0) + 1.0) + t);
        v += sin(p.y * 1.5 - t * 1.3);
        // A four-stop gradient map, cycled: c0, c2, c3, c1 and round again.
        float k = fract(v * 0.18 + u_params.y * 3.0) * 4.0;
        float s = smoothstep(0.0, 1.0, fract(k));
        col = k < 1.0 ? mix(c0, c2, s) : k < 2.0 ? mix(c2, c3, s) : k < 3.0 ? mix(c3, c1, s)
                                                                            : mix(c1, c0, s);
        col *= 1.0 + beat * 0.3;
        col = posterize(col, 24.0, gl_FragCoord.xy);
    }
    else if (u_mode == 37) // ember
    {
        float bass = u_audio.x;
        float treble = u_audio.z;
        float beat = u_audio.w;
        float sx = (uv.x - 0.5) * aspect - u_aux.x * 0.05;
        col = mix(c0, c1, smoothstep(0.0, 0.9, uv.y));
        float smoke = fbm(vec2(sx * 1.6 + u_time * 0.01, uv.y * 2.0 - u_time * 0.05));
        col += c2 * smoothstep(0.4, 0.9, smoke) * 0.35 * smoothstep(0.1, 0.8, uv.y);
        float ridge = 0.32 + abs(sx) * 0.9 + (fbm(vec2(sx * 4.0, 1.0)) - 0.5) * 0.12;
        float near_ridge = 0.8 + (fbm(vec2(sx * 2.5 + 9.0, 3.0)) - 0.5) * 0.1;
        vec2 s = vec2(sx, uv.y - 0.32);
        col += c3 * exp(-dot(s, s) * 60.0) * (0.35 + bass * 0.6 + beat * 0.8);
        if (uv.y > ridge)
        {
            // Cracks: thin noise contours glowing with the bass, hotter higher up.
            vec3 rock = c0 * 0.5;
            float n = noise(vec2(sx * 9.0, uv.y * 7.0 - u_params.x * 0.02));
            float crack = smoothstep(0.06, 0.0, abs(n - 0.5));
            float heat = (0.3 + bass * 1.2 + beat * 0.6) * smoothstep(0.95, 0.35, uv.y);
            rock += mix(c2, c3, crack) * crack * heat;
            rock += c2 * exp(-(uv.y - ridge) * 60.0) * 0.3;
            col = rock;
        }
        if (uv.y > near_ridge)
            col = c0 * 0.25;
        for (int layer = 0; layer < 2; ++layer)
        {
            float fl = float(layer);
            vec2 g = vec2(uv.x * aspect, uv.y + u_params.x * (0.08 + fl * 0.05) + u_time * 0.02) *
                     (14.0 + fl * 10.0);
            vec2 id = floor(g);
            vec2 f = fract(g) - 0.5;
            float h = hash(id + fl * 7.0);
            vec2 o = vec2(sin(u_time * (1.0 + h * 2.0) + h * 30.0) * 0.3, (hash(id + 3.0) - 0.5) * 0.6);
            float e = step(0.82 - treble * 0.15, h) * smoothstep(0.08, 0.0, length(f - o));
            col += mix(c2, c3, h) * e * (0.6 + treble) * (1.0 - fl * 0.4);
        }
    }
    else if (u_mode == 40) // swirl (ported)
    {
        float t = u_time * 2.5 + u_params.x * 3.0;
        float turn = u_params.z * 0.5 + u_aux.x * 0.3;
        col = rb_swirl(gl_FragCoord.xy, u_resolution, t, turn, 0.25 + u_audio.x * 0.3,
                       0.4 + u_audio.w * 0.5, c2, c3, c0);
    }
    else if (u_mode == 41) // prism (ported)
    {
        float t = u_time * 0.25 + u_params.x * 0.2;
        float glow = 0.3 + u_audio.x * 0.45 + u_audio.w * 0.3;
        vec3 light = rb_prism(gl_FragCoord.xy, u_resolution, t, glow,
                              vec2(u_aux.x * 0.6, -u_aux.y * 0.4), 1.0 + u_audio.y * 0.6);
        if (u_params.w > 0.5)
        {
            col = c0 + light; // the signature look: the prism's own rainbow
        }
        else
        {
            float l = dot(light, vec3(0.2126, 0.7152, 0.0722));
            col = c0 + mix(c2, c3, smoothstep(0.0, 1.0, light.r - light.b + 0.5)) * l * 1.6 +
                  vec3(pow(l, 4.0));
        }
    }
    else if (u_mode == 42) // orb (ported)
    {
        vec2 p = (gl_FragCoord.xy - u_resolution * 0.5) / min(u_resolution.x, u_resolution.y) * 2.0;
        p /= 1.0 + u_audio.x * 0.12;
        float r = u_params.y * 6.0;
        p = mat2(cos(r), sin(r), -sin(r), cos(r)) * p;
        float t = u_time + u_params.x * 0.3;
        float beat = u_audio.w;
        vec4 orb = rb_orb(p, t, vec2(u_aux.x, -u_aux.y) * 0.3, u_aux.z * 0.18 + u_audio.y * 0.25,
                          1.0 + u_audio.x * 2.0, 1.0 + u_audio.x * 0.6, t * 0.5,
                          t * (0.9 + u_audio.y), vec2((1.0 - beat) * 0.55, beat * beat),
                          c2, c3, c1);
        col = mix(c0, orb.rgb, orb.a);
    }
    else if (u_mode == 20) // scanlines (post)
    {
        float line = 0.5 + 0.5 * cos(uv.y * 1080.0 * 3.14159265 / 1.5);
        alpha = u_params.x * line;
        alpha = max(alpha, u_params.y * smoothstep(0.45, 1.05, length(centred)));
    }
    else if (u_mode == 21) // vignette (post)
    {
        col = c0;
        alpha = u_params.x * smoothstep(0.45, 1.1, length(centred));
    }

    // Holding R2 "drives" the audio scenes brighter.
    bool audio_scene = (u_mode >= 11 && u_mode <= 13) || (u_mode >= 30 && u_mode <= 49);
    if (audio_scene)
        col *= 1.0 + u_aux.z * 0.3;
    // A little noise hides the banding of slow gradients on TVs.
    if (u_mode < 20 || u_mode >= 30)
        col += (hash(gl_FragCoord.xy + fract(u_time) * 61.0) - 0.5) / 255.0;
    frag_color = vec4(col, alpha * u_opacity);
}
)";

} // namespace

Backdrop::~Backdrop()
{
    release();
}

void Backdrop::release()
{
    if (vao_ != 0)
        glDeleteVertexArrays(1, &vao_);
    if (program_ != 0)
        glDeleteProgram(program_);
    for (GLuint *extra : {&trip_program_, &calm_program_, &gallery_program_})
    {
        if (*extra != 0)
            glDeleteProgram(*extra);
        *extra = 0;
    }
    vao_ = program_ = 0;
}

bool Backdrop::init()
{
    const std::string fragment = std::string(kFragment) + kPortedScenes + kFragmentMain;
    program_ = build_program("backdrop", kVertex, fragment.c_str());
    if (program_ == 0)
        return false;
    // The trip pack is optional: without it those scenes draw nothing.
    const std::string trip = std::string(kFragment) + kTripScenes + kTripMain;
    trip_program_ = build_program("backdrop-trip", kVertex, trip.c_str());
    // So are the calm and gallery packs.
    const std::string calm = std::string(kFragment) + kSceneKit + kCalmScenes + kCalmMain;
    calm_program_ = build_program("backdrop-calm", kVertex, calm.c_str());
    const std::string gallery = std::string(kFragment) + kSceneKit + kGalleryScenes + kGalleryMain;
    gallery_program_ = build_program("backdrop-gallery", kVertex, gallery.c_str());
    glGenVertexArrays(1, &vao_);
    return true;
}

void Backdrop::draw(const BackdropSpec &spec, int width, int height, float opacity)
{
    if (spec.mode == BackdropMode::none || program_ == 0 || opacity <= 0.0f)
        return;
    const bool post = is_post_overlay(spec.mode);
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    if (post || opacity < 1.0f)
    {
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }
    else
    {
        glDisable(GL_BLEND);
    }
    const bool trip = is_trip_scene(spec.mode);
    GLuint program = program_;
    if (trip)
        program = trip_program_;
    else if (is_calm_scene(spec.mode))
        program = calm_program_;
    else if (is_gallery_scene(spec.mode))
        program = gallery_program_;
    if (program == 0)
        return;
    glUseProgram(program);
    glUniform2f(0, static_cast<float>(width), static_cast<float>(height));
    glUniform1f(1, spec.time);
    glUniform1i(2, static_cast<int>(spec.mode));
    float colors[16];
    for (int i = 0; i < 4; ++i)
    {
        colors[i * 4 + 0] = spec.colors[i].r;
        colors[i * 4 + 1] = spec.colors[i].g;
        colors[i * 4 + 2] = spec.colors[i].b;
        colors[i * 4 + 3] = spec.colors[i].a;
    }
    glUniform4fv(3, 4, colors);
    glUniform4f(7, spec.params[0], spec.params[1], spec.params[2], spec.params[3]);
    glUniform1f(8, opacity);
    glUniform4f(9, spec.audio[0], spec.audio[1], spec.audio[2], spec.audio[3]);
    glUniform4f(10, spec.aux[0], spec.aux[1], spec.aux[2], spec.aux[3]);
    glUniform1fv(11, BackdropSpec::kBands, spec.spectrum);
    glUniform1fv(27, BackdropSpec::kBands, spec.peaks);
    if (trip)
        glUniform1fv(43, BackdropSpec::kWave, spec.wave);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

} // namespace hui::gfx
