// Afterglow - The trip pack: psychedelic scenes for a second backdrop program.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later
//
// These scenes live in their own program (Backdrop::init builds it from the
// shared helpers in backdrop.cpp, this source and its own main) so that the
// first program does not grow: a shader's registers are sized for its most
// demanding branch, and the fractal and metaball loops would have slowed every
// other scene down. The modes are 50 to 69 (gfx::is_trip_scene).
//
// Inputs are those of the audio scenes plus u_wave, 64 samples of the
// waveform. With u_params.w > 0.5 the colours cycle through the rainbow
// instead of the palette. Most scenes are fine on their own; ring, sparks
// and scope are sparse on purpose: they are seeds for the feedback pass
// (gfx::FeedbackFx), which turns their strokes into tunnels and blooms.

#pragma once

namespace hui::gfx
{

constexpr const char *kTripScenes = R"(
layout(location = 43) uniform float u_wave[64];

vec3 hsv2rgb(vec3 c)
{
    vec3 p = abs(fract(c.xxx + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

// A loop through the palette's colours (or the rainbow): 0..1 wraps.
vec3 trip_pal(float x)
{
    x = fract(x);
    if (u_params.w > 0.5)
        return hsv2rgb(vec3(x, 0.8, 1.0));
    vec3 a = u_colors[1].rgb;
    vec3 b = u_colors[2].rgb;
    vec3 c = u_colors[3].rgb;
    float k = x * 3.0;
    if (k < 1.0)
        return mix(a, b, smoothstep(0.0, 1.0, k));
    if (k < 2.0)
        return mix(b, c, smoothstep(0.0, 1.0, k - 1.0));
    return mix(c, a, smoothstep(0.0, 1.0, k - 2.0));
}

float wave_at(float x)
{
    float f = clamp(x, 0.0, 1.0) * 63.0;
    int i = int(f);
    int j = min(i + 1, 63);
    return mix(u_wave[i], u_wave[j], f - float(i));
}

float spec_at(float x)
{
    float f = clamp(x, 0.0, 1.0) * 15.0;
    int i = int(f);
    int j = min(i + 1, 15);
    return mix(u_spectrum[i], u_spectrum[j], f - float(i));
}

vec3 scene_kaleido(vec2 p, float t)
{
    float r = length(p);
    float a = atan(p.y, p.x) + u_params.y * 2.0 + u_aux.x * 0.5;
    float seg = 6.2831853 / 8.0;
    a = mod(a, seg);
    a = abs(a - seg * 0.5);
    vec2 q = r * vec2(cos(a), sin(a));
    q *= 1.8 - u_audio.x * 0.5;
    q -= vec2(u_params.x * 0.18, 0.0);
    float n = fbm(q * 2.2 + vec2(t * 0.11, -t * 0.07));
    float n2 = fbm(q * 4.5 - n * 1.5 + t * 0.05);
    float rings = 0.5 + 0.5 * sin(r * 22.0 - u_params.x * 3.0 + n * 6.0);
    float v = n * 0.6 + n2 * 0.5 + rings * 0.15 + t * 0.03;
    vec3 col = trip_pal(v) * (0.30 + 1.0 * smoothstep(0.25, 0.85, n2));
    float lines = smoothstep(0.05, 0.0, abs(fract(n2 * 5.0) - 0.5) - 0.42);
    col += trip_pal(v + 0.33) * lines * (0.35 + u_audio.z * 0.8);
    col *= 1.0 + u_audio.w * 0.35;
    col *= smoothstep(1.5, 0.15, r);
    return col;
}

float chrome_h(vec2 p, float t)
{
    vec2 w = vec2(fbm(p * 0.7 + vec2(t * 0.06, 0.0)), fbm(p * 0.7 + vec2(3.1, -t * 0.05)));
    return fbm(p * 1.1 + w * 1.6 + vec2(t * 0.08, t * 0.05));
}

// Liquid metal: a smooth moving surface reflecting a sky with a bright
// horizon, the way chrome lettering is painted.
vec3 scene_chrome(vec2 p, float t)
{
    float amp = 0.30 + u_audio.x * 0.35;
    float e = 0.006;
    float h = chrome_h(p, t);
    vec2 g = vec2(chrome_h(p + vec2(e, 0.0), t) - h, chrome_h(p + vec2(0.0, e), t) - h) / e;
    vec3 n = normalize(vec3(-g * amp, 1.0));
    // Seen from a little above: a flat patch reflects the sky, slopes
    // facing down catch the bright horizon and the dark ground.
    float y = 2.0 * n.z * n.y - 0.35 + u_aux.y * 0.15;
    vec3 horizon = u_colors[2].rgb * 1.2;
    vec3 ground = u_colors[0].rgb + u_colors[1].rgb * 0.3;
    vec3 col;
    if (y < 0.0)
    {
        float k = smoothstep(0.0, -0.9, y);
        vec3 sky = mix(u_colors[3].rgb * 0.75, u_colors[1].rgb * 0.9, smoothstep(0.35, 1.0, k));
        col = mix(horizon, sky, smoothstep(0.0, 0.3, k));
    }
    else
    {
        col = mix(horizon * 0.45, ground, smoothstep(0.0, 0.25, y));
    }
    col += vec3(1.0) * exp(-abs(y) * 60.0) * 0.6 * (0.7 + u_audio.w * 0.6);
    float fresnel = pow(1.0 - n.z, 2.0);
    col = mix(col, vec3(1.0), fresnel * 0.2);
    vec3 l = normalize(vec3(-0.5, -0.6, 0.6));
    vec3 r = reflect(vec3(0.0, 0.0, -1.0), n);
    col += pow(max(dot(r, l), 0.0), 60.0) * 1.2;
    return col;
}

vec3 scene_fractal(vec2 p, float t)
{
    float zoom = 1.3 - 0.15 * sin(t * 0.1) - u_audio.x * 0.15;
    vec2 z = rot(u_params.y * 1.5) * p * zoom * 1.7;
    float a = t * 0.06 + u_params.x * 0.03 + u_aux.x * 0.2;
    vec2 c = 0.7885 * vec2(cos(a), sin(a));
    float trap = 1e9;
    float i = 0.0;
    for (int k = 0; k < 96; ++k)
    {
        z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
        trap = min(trap, abs(length(z) - 0.5));
        if (dot(z, z) > 64.0)
            break;
        i += 1.0;
    }
    vec3 glow = trip_pal(trap * 1.5 + 0.5) * exp(-trap * 14.0) * (0.5 + u_audio.w);
    if (i >= 96.0)
        return u_colors[0].rgb + glow * 0.6;
    float smooth_i = i - log2(log2(dot(z, z))) + 4.0;
    float v = sqrt(max(smooth_i, 0.0) / 96.0);
    vec3 col = trip_pal(v * 2.5 + t * 0.04 - u_params.x * 0.02);
    col *= smoothstep(0.0, 0.3, v) * (0.8 + 0.6 * u_audio.y);
    return col + glow;
}

vec3 scene_lasers(vec2 p, float t, float aspect)
{
    float fog = 0.30 + 0.70 * fbm(p * 2.5 + vec2(t * 0.12, -t * 0.05));
    vec3 col = u_colors[0].rgb * 0.5 + u_colors[1].rgb * 0.25 * fog;
    for (int i = 0; i < 8; ++i)
    {
        float fi = float(i);
        vec2 o = vec2(((fi + 0.5) / 8.0 - 0.5) * aspect * 0.9, -0.56);
        float a = sin(t * (0.35 + 0.09 * fi) + fi * 1.7) * 0.65 + u_aux.x * 0.5;
        vec2 dir = vec2(sin(a), cos(a));
        vec2 v = p - o;
        float along = dot(v, dir);
        float d = length(v - dir * max(along, 0.0));
        float energy = 0.35 + band(i * 2) * 1.6;
        float beam = exp(-d * 260.0) + exp(-d * 30.0) * 0.18 * fog;
        vec3 c = mod(fi, 2.0) < 1.0 ? u_colors[2].rgb : u_colors[3].rgb;
        if (u_params.w > 0.5)
            c = hsv2rgb(vec3(fract(fi / 8.0 + t * 0.05), 0.8, 1.0));
        col += c * beam * energy * step(0.0, along);
    }
    // A haze where the beams meet the floor, and a flash on hard beats.
    col += u_colors[2].rgb * smoothstep(0.1, 0.55, p.y) * 0.12 * (0.5 + u_audio.x);
    col += vec3(1.0) * pow(u_audio.w, 4.0) * 0.22;
    return col;
}

vec3 scene_rave(vec2 p, float t)
{
    p = rot(sin(t * 0.3) * 0.3 + u_aux.x * 0.4) * p;
    float r = length(p);
    float a = atan(p.y, p.x);
    float depth = 0.35 / max(r, 0.02) + u_params.x * 1.2;
    float ang = a / 3.14159265 + depth * 0.12 * sin(t * 0.2) + u_params.y;
    vec2 tuv = vec2(ang * 6.0, depth * 2.0);
    float check = mod(floor(tuv.x) + floor(tuv.y), 2.0);
    float stripe = smoothstep(0.42, 0.5, abs(fract(tuv.y) - 0.5));
    vec3 col = trip_pal(depth * 0.12 + t * 0.05 + check * 0.5) * (0.10 + 0.75 * check);
    col += trip_pal(depth * 0.12 + 0.5) * stripe * (0.35 + u_audio.w * 0.6);
    col *= clamp(r * 2.4, 0.0, 1.0);
    col += trip_pal(t * 0.1) * exp(-r * 9.0) * (0.6 + u_audio.x);
    return col;
}

vec3 scene_silk(vec2 p, float t)
{
    vec3 col = mix(u_colors[0].rgb, u_colors[1].rgb, 0.5 + 0.8 * p.y);
    for (int i = 0; i < 5; ++i)
    {
        float fi = float(i);
        float w = fbm(vec2(p.x * 0.8 + t * 0.05 + fi * 3.1, fi * 1.7 + t * 0.03));
        float y = (fi - 2.0) * 0.12 + (w - 0.5) * 0.9 +
                  sin(p.x * 2.0 + t * 0.2 + fi) * 0.08 * (1.0 + u_audio.x);
        float d = p.y - y;
        float width = 0.05 + 0.03 * sin(p.x * 3.0 + t * 0.3 + fi * 2.0);
        float band_ = exp(-d * d / (width * width));
        vec3 c = trip_pal(fi * 0.2 + p.x * 0.1 + t * 0.02);
        col = mix(col, c * (0.45 + 0.55 * band_), band_ * 0.65);
        col += pow(band_, 8.0) * 0.45 * (0.6 + u_audio.y);
    }
    return col;
}

vec3 scene_bubbles(vec2 p, float t)
{
    float field = 0.0;
    vec3 tint = vec3(0.0);
    for (int i = 0; i < 9; ++i)
    {
        float fi = float(i);
        vec2 c = vec2(sin(t * (0.21 + fi * 0.037) + fi * 2.1) * 0.75,
                      cos(t * (0.17 + fi * 0.041) + fi * 1.3) * 0.36);
        float rr = 0.085 + 0.04 * sin(fi * 1.7) + band(i + 2) * 0.07 + u_audio.x * 0.03;
        vec2 d = p - c;
        float f = rr * rr / max(dot(d, d), 1e-4);
        field += f;
        tint += trip_pal(fi / 9.0 + t * 0.02) * f;
    }
    tint /= max(field, 1e-4);
    float inside = smoothstep(0.95, 1.05, field);
    vec2 grad = vec2(dFdx(field), dFdy(field)) * u_resolution.y * 0.004;
    vec3 n = normalize(vec3(-grad, 1.0));
    vec3 bg = mix(u_colors[0].rgb, u_colors[1].rgb, 0.4 + 0.6 * p.y);
    float shade = 0.55 + 0.45 * n.z;
    vec3 col = mix(bg, tint * shade, inside);
    float spec = pow(max(dot(n, normalize(vec3(-0.5, -0.6, 0.8))), 0.0), 24.0);
    col += spec * inside * 0.8;
    float rim = smoothstep(0.85, 1.0, field) * smoothstep(1.3, 1.0, field);
    col += rim * (tint * 0.6 + 0.2);
    return col;
}

vec3 scene_scope(vec2 p, float t, float aspect)
{
    vec3 col = u_colors[0].rgb * 0.4;
    float amp = 0.20 * (0.7 + u_audio.x * 0.8);
    float x = p.x / aspect + 0.5;
    float dx = 1.0 / 64.0;
    float y = wave_at(x) * amp;
    float slope = (wave_at(x + dx) - wave_at(x - dx)) / (2.0 * dx) / aspect * amp;
    float d = abs(p.y - y) / sqrt(1.0 + slope * slope);
    col += u_colors[2].rgb * (exp(-d * 420.0) * 1.3 + exp(-d * 45.0) * 0.25);
    float r = length(p);
    float a = fract(atan(p.y, p.x) / 6.2831853 + 0.5 + u_params.y);
    float ring_r = 0.26 + wave_at(a) * 0.07 + u_audio.x * 0.03;
    float dr = abs(r - ring_r);
    col += u_colors[3].rgb * (exp(-dr * 380.0) * 1.1 + exp(-dr * 40.0) * 0.2);
    return col;
}

vec3 scene_ring(vec2 p, float t)
{
    float r = length(p);
    float a = fract(atan(p.y, p.x) / 6.2831853 + 0.5 + u_params.y);
    float m = abs(a * 2.0 - 1.0);
    float level = spec_at(1.0 - m);
    float inner = 0.15 + u_audio.x * 0.03;
    float cell = fract(a * 96.0);
    float mask = smoothstep(0.0, 0.12, cell) * smoothstep(0.75, 0.6, cell);
    float outer = inner + 0.015 + level * 0.24;
    float on = smoothstep(inner - 0.003, inner, r) * smoothstep(outer + 0.003, outer, r) * mask;
    vec3 col = trip_pal(a + t * 0.05) * on * (0.6 + level);
    col += trip_pal(t * 0.1) * smoothstep(0.006, 0.0, abs(r - inner + 0.012)) * 1.2;
    return col;
}

vec3 scene_sparks(vec2 p, float t)
{
    vec3 col = vec3(0.0);
    for (int i = 0; i < 40; ++i)
    {
        float fi = float(i);
        float h = hash(vec2(fi, 3.7));
        float ang = h * 6.2831853 + u_params.y * 3.0;
        float life = fract(t * (0.25 + 0.25 * hash(vec2(fi, 9.1))) + h);
        float dist = life * (0.6 + u_audio.x * 0.4);
        vec2 pos = vec2(cos(ang), sin(ang)) * dist;
        float d = length(p - pos);
        float size = 0.004 + 0.007 * (1.0 - life);
        col += trip_pal(h + t * 0.05) * (size * size) / (d * d + size * size * 0.3) *
               (1.0 - life) * (0.6 + u_audio.w);
    }
    col += trip_pal(t * 0.1) * exp(-length(p) * 12.0) * (0.3 + u_audio.x);
    return col;
}

vec3 scene_smoke(vec2 p, float t)
{
    vec2 q = vec2(fbm(p * 1.4 + t * 0.04), fbm(p * 1.4 + vec2(5.2, 1.3) - t * 0.03));
    vec2 r = vec2(fbm(p * 1.4 + 4.0 * q + vec2(1.7, 9.2) + t * 0.05),
                  fbm(p * 1.4 + 4.0 * q + vec2(8.3, 2.8) - t * 0.04));
    float f = fbm(p * 1.4 + 4.0 * r);
    float wisp = pow(1.0 - abs(f * 2.0 - 1.0), 6.0);
    vec3 col = u_colors[0].rgb;
    col = mix(col, trip_pal(length(q) * 0.6 + t * 0.02) * 0.8, clamp(f * f * 2.2, 0.0, 1.0));
    col += trip_pal(r.x + 0.3) * wisp * (0.7 + u_audio.y * 0.9);
    col *= 0.75 + u_audio.x * 0.5;
    return col;
}

vec3 scene_spirals(vec2 p, float t)
{
    vec3 col = u_colors[0].rgb * 0.5;
    for (int i = 0; i < 8; ++i)
    {
        float fi = float(i);
        float sides = 3.0 + mod(fi, 4.0);
        float dir = mod(fi, 2.0) < 1.0 ? 1.0 : -1.0;
        vec2 q = rot(t * (0.12 + 0.06 * fi) * dir + u_params.y) * p;
        float a = atan(q.y, q.x);
        float seg = 6.2831853 / sides;
        float poly = cos(floor(0.5 + a / seg) * seg - a) * length(q);
        float size = 0.05 + fi * 0.052 + band(i * 2) * 0.035 + u_audio.x * 0.02;
        float d = abs(poly - size);
        float line = smoothstep(0.005, 0.0, d - 0.0025) + exp(-d * 55.0) * 0.3;
        col += trip_pal(fi / 8.0 + t * 0.04) * line * (0.7 + u_audio.w * 0.5);
    }
    return col;
}
)";

constexpr const char *kTripMain = R"(
void main()
{
    vec2 uv = vec2(gl_FragCoord.x / u_resolution.x, 1.0 - gl_FragCoord.y / u_resolution.y);
    float aspect = u_resolution.x / u_resolution.y;
    vec2 p = (uv - 0.5) * vec2(aspect, 1.0);
    p += vec2(u_aux.x, u_aux.y) * 0.04;
    float t = u_time;
    vec3 col = vec3(0.0);
    if (u_mode == 50)
        col = scene_kaleido(p, t);
    else if (u_mode == 51)
        col = scene_chrome(p, t);
    else if (u_mode == 52)
        col = scene_fractal(p, t);
    else if (u_mode == 53)
        col = scene_lasers(p, t, aspect);
    else if (u_mode == 54)
        col = scene_rave(p, t);
    else if (u_mode == 55)
        col = scene_silk(p, t);
    else if (u_mode == 56)
        col = scene_bubbles(p, t);
    else if (u_mode == 57)
        col = scene_scope(p, t, aspect);
    else if (u_mode == 58)
        col = scene_ring(p, t);
    else if (u_mode == 59)
        col = scene_sparks(p, t);
    else if (u_mode == 60)
        col = scene_smoke(p, t);
    else if (u_mode == 61)
        col = scene_spirals(p, t);
    col *= 1.0 + u_aux.z * 0.3;
    col += (hash(gl_FragCoord.xy + fract(u_time) * 61.0) - 0.5) / 255.0;
    frag_color = vec4(max(col, 0.0), u_opacity);
}
)";

} // namespace hui::gfx
