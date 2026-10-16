// Afterglow - Scene shaders ported from React Bits backgrounds, made audio-reactive.
// Copyright (C) 2026 DevvNirvana; original shaders Copyright (c) 2026 David Haz
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported and modified from React Bits (https://github.com/DavidHDev/react-bits,
// src/content/Backgrounds: Balatro, Prism, Orb) with the author's permission
// to include them in Afterglow under GPL-3.0. React Bits itself is MIT +
// Commons Clause; see THIRD_PARTY_NOTICES.md.
//
// Changes from the originals: GLSL 4.60 instead of WebGL, inputs driven by the
// music (bass, mid, beat) and the DualSense instead of the mouse, our palette
// instead of fixed colours, intro animations and light modes removed.

#pragma once

namespace hui::gfx
{

// Pasted between the shared helpers and main() of the backdrop shader.
constexpr const char *kPortedScenes = R"(
// ---- Swirl (from React Bits "Balatro") ----
vec3 rb_swirl(vec2 frag, vec2 res, float time, float turn, float spin_amount, float lighting,
              vec3 colour1, vec3 colour2, vec3 colour3)
{
    const float contrast = 3.5;
    float pixel_size = length(res) / 745.0;
    vec2 uv = (floor(frag / pixel_size) * pixel_size - 0.5 * res) / length(res);
    float uv_len = length(uv);
    float speed = -0.4 + 302.2 + turn;
    float angle = atan(uv.y, uv.x) + speed - 20.0 * (spin_amount * uv_len + (1.0 - spin_amount));
    vec2 mid = (res / length(res)) / 2.0;
    uv = vec2(uv_len * cos(angle) + mid.x, uv_len * sin(angle) + mid.y) - mid;
    uv *= 30.0;
    vec2 uv2 = vec2(uv.x + uv.y);
    for (int i = 0; i < 5; i++)
    {
        uv2 += sin(max(uv.x, uv.y)) + uv;
        uv += 0.5 * vec2(cos(5.1123314 + 0.353 * uv2.y + time * 0.131121), sin(uv2.x - 0.113 * time));
        uv -= cos(uv.x + uv.y) - sin(uv.x * 0.711 - uv.y);
    }
    float contrast_mod = 0.25 * contrast + 0.5 * spin_amount + 1.2;
    float paint = min(2.0, max(0.0, length(uv) * 0.035 * contrast_mod));
    float c1p = max(0.0, 1.0 - contrast_mod * abs(1.0 - paint));
    float c2p = max(0.0, 1.0 - contrast_mod * abs(paint));
    float c3p = 1.0 - min(1.0, c1p + c2p);
    float light = (lighting - 0.2) * max(c1p * 5.0 - 4.0, 0.0) + lighting * max(c2p * 5.0 - 4.0, 0.0);
    return (0.3 / contrast) * colour1 +
           (1.0 - 0.3 / contrast) * (colour1 * c1p + colour2 * c2p + c3p * colour3) + light;
}

// ---- Prism (from React Bits "Prism") ----
const vec4 RB_PHASE_COS = vec4(1.0, 0.5403023058681398, -0.4161468365471424, -0.9899924966004454);
const vec4 RB_PHASE_SIN = vec4(0.0, 0.8414709848078965, 0.9092974268256817, 0.1411200080598672);

float rb_pyramid(vec3 p)
{
    const float half_base = 2.75;
    const float height = 3.5;
    vec3 q = vec3(abs(p.x) / half_base, abs(p.y) / height, abs(p.z) / half_base);
    return max((q.x + q.y + q.z - 1.0) * min(half_base, height) * 0.5773502691896258, -p.y);
}

mat3 rb_rotation(float yaw, float pitch, float roll)
{
    float cy = cos(yaw);
    float sy = sin(yaw);
    float cx = cos(pitch);
    float sx = sin(pitch);
    float cz = cos(roll);
    float sz = sin(roll);
    return mat3(cy * cz + sy * sx * sz, cx * sz, -sy * cz + cy * sx * sz,
                -cy * sz + sy * sx * cz, cx * cz, sy * sz + cy * sx * cz,
                sy * cx, -sx, cy * cx);
}

vec3 rb_prism(vec2 frag, vec2 res, float t, float glow, vec2 tilt, float colour_frequency)
{
    // Zoomed out from the original's scale (3.6) so the whole pyramid shows.
    vec2 f = (frag - 0.5 * res) / (res.y * 0.1 * 2.0);
    float c0 = cos(t);
    mat3 spin = mat3(c0, 0.0, cos(t + 11.0), 0.0, 1.0, 0.0, cos(t + 33.0), 0.0, c0);
    mat3 ray = rb_rotation(tilt.x, tilt.y, 0.0) * spin;
    vec3 origin = ray * vec3(f, 0.0);
    vec3 axis = ray[2];
    float z = 5.0;
    vec4 o = vec4(0.0);
    for (int i = 0; i < 100; i++)
    {
        vec3 p = origin + axis * z;
        float d = 0.1 + 0.2 * abs(rb_pyramid(p + vec3(0.0, 3.5 * 0.25 + 0.6, 0.0)));
        z -= d;
        float phase = (p.y + z) * colour_frequency;
        o += (sin(phase) * RB_PHASE_COS + cos(phase) * RB_PHASE_SIN + 1.0) / d;
    }
    vec4 raw = o * o * glow / 1e5;
    vec4 e = exp(2.0 * raw);
    vec4 mapped = (e - 1.0) / (e + 1.0);
    vec3 c = clamp(mapped.rgb, 0.0, 1.0);
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = clamp(mix(vec3(luma), c, 1.5), 0.0, 1.0);
    return c * mapped.a;
}

// ---- Orb (from React Bits "Orb") ----
vec3 rb_hash33(vec3 p3)
{
    p3 = fract(p3 * vec3(0.1031, 0.11369, 0.13787));
    p3 += dot(p3, p3.yxz + 19.19);
    return -1.0 + 2.0 * fract(vec3(p3.x + p3.y, p3.x + p3.z, p3.y + p3.z) * p3.zyx);
}

float rb_snoise3(vec3 p)
{
    const float K1 = 0.333333333;
    const float K2 = 0.166666667;
    vec3 i = floor(p + (p.x + p.y + p.z) * K1);
    vec3 d0 = p - (i - (i.x + i.y + i.z) * K2);
    vec3 e = step(vec3(0.0), d0 - d0.yzx);
    vec3 i1 = e * (1.0 - e.zxy);
    vec3 i2 = 1.0 - e.zxy * (1.0 - e);
    vec3 d1 = d0 - (i1 - K2);
    vec3 d2 = d0 - (i2 - K1);
    vec3 d3 = d0 - 0.5;
    vec4 h = max(0.6 - vec4(dot(d0, d0), dot(d1, d1), dot(d2, d2), dot(d3, d3)), 0.0);
    vec4 n = h * h * h * h * vec4(dot(d0, rb_hash33(i)), dot(d1, rb_hash33(i + i1)),
                                  dot(d2, rb_hash33(i + i2)), dot(d3, rb_hash33(i + 1.0)));
    return dot(vec4(31.316), n);
}

float rb_light1(float intensity, float attenuation, float dist)
{
    return intensity / (1.0 + dist * attenuation);
}

float rb_light2(float intensity, float attenuation, float dist)
{
    return intensity / (1.0 + dist * dist * attenuation);
}

// The dark-background variant only. Returns colour and coverage.
vec4 rb_orb(vec2 uv, float time, vec2 reach_vec, float awake, float wobble, float glow,
            float swirl, float orbit, vec2 echo_in, vec3 colour1, vec3 colour2, vec3 colour3)
{
    const float bleed = 1.0;
    const float highlight = 1.0;
    const float trail_len = 1.0;
    float raw_len = length(uv);
    vec2 dir = raw_len > 0.0001 ? uv / raw_len : vec2(1.0, 0.0);
    float reach_amount = length(reach_vec);
    vec2 reach_dir = reach_amount > 0.00001 ? reach_vec / reach_amount : vec2(1.0, 0.0);
    float facing = exp((dot(dir, reach_dir) - 1.0) * 3.2);
    float len = max(raw_len - reach_amount * facing * smoothstep(0.25, 0.65, raw_len), 0.0);
    uv = dir * len;
    float ang = atan(dir.y, dir.x);
    float inv_len = len > 0.0 ? 1.0 / len : 0.0;
    float near = facing * min(reach_amount * 3.0, 1.0);

    float n0 = rb_snoise3(vec3(uv * 0.65, time * 0.5)) * 0.5 + 0.5;
    float r0 = mix(0.8 - 0.04 * wobble, 0.8 + 0.04 * wobble, n0);
    float d0 = distance(uv, (r0 * inv_len) * uv);
    float v0 = rb_light1(1.0, 10.0 / glow, d0);
    v0 *= smoothstep(r0 * 1.05, r0, len);
    float spread = max(bleed, 0.04);
    v0 *= smoothstep(r0 * (1.0 - 0.2 * spread), r0 * (1.0 - 0.05 * spread), len);
    float body = smoothstep(0.8 - 0.2 * spread, 0.8, len);
    float cl = cos(ang + swirl) * 0.5 + 0.5;

    vec2 head = vec2(cos(orbit), sin(orbit));
    float d = distance(uv, head * r0);
    float lag = atan(head.x * dir.y - head.y * dir.x, dot(dir, head));
    float trail = smoothstep(0.0, 0.35, lag) * exp(-max(lag, 0.0) * 2.6 / trail_len);
    float v1 = (rb_light2(1.5, 5.0, d) + trail * 0.6) * highlight;
    v1 *= rb_light1(1.0, 50.0, d0);
    float band = v0 * smoothstep(1.0, mix(0.6, 1.0, n0 * 0.5), len) * body;
    float heat = pow(clamp(band / 0.82, 0.0, 1.0), 3.0);
    float bloom = rb_light2(1.0, 160.0 / glow, max(len - r0, 0.0)) * smoothstep(0.4, 0.0, len - r0) *
                  step(r0, len);
    float v2 = smoothstep(1.0, mix(0.6, 1.0, n0 * 0.5), len);

    vec3 base = mix(colour1, colour2, cl);
    float echo_d = abs(len - r0 - echo_in.x);
    float echo = rb_light1(1.0, 60.0, echo_d) * smoothstep(0.12, 0.0, echo_d) * echo_in.y;

    vec3 c = mix(colour3, base, v0);
    c = (c + v1) * v2 * body * (1.0 + awake + near * 0.35) +
        mix(base, vec3(1.0), 0.25) * heat * (0.28 + near * 0.3);
    c = clamp(c + base * (bloom * 0.1 + echo), 0.0, 1.0);
    float alpha = max(max(c.r, c.g), c.b);
    return vec4(c, alpha);
}
)";

} // namespace hui::gfx
