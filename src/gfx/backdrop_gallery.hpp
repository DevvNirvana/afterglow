// Afterglow - The calm pack and the gallery pack: two more backdrop programs.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later
//
// CALM (modes 70 to 79) are slow, soft scenes for winding down: water, sky,
// lanterns, fireflies. GALLERY (modes 80 to 89) are pieces that look like
// art objects rather than light shows: gold-mended pottery, paper cuts,
// stained glass, ferrofluid. Each pack is its own program, for the reason
// given in backdrop_trip.hpp: a shader's registers are sized for its most
// demanding branch.
//
// Both read the audio uniforms of the other scenes plus the beat clock in
// u_aux.w: beats counted (wrapping at 64), whose fraction is where in the
// beat the music is. Loops that last a beat, a bar (4 beats) or a phrase
// therefore land on the music. Without a pulse the clock runs at 120 a
// minute, so the scenes still move.

#pragma once

namespace hui::gfx
{

// Helpers shared by the two packs (after the kit's own: hash, noise, fbm,
// star_layer, rot).
constexpr const char *kSceneKit = R"(
vec3 gpal(float x)
{
    x = fract(x);
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

float beat_phase()
{
    return fract(u_aux.w);
}

float bar_phase()
{
    return fract(u_aux.w * 0.25);
}

// 1 on each beat, fading over it.
float beat_glow()
{
    return exp(-fract(u_aux.w) * 5.0);
}

float band_at(float x)
{
    float f = clamp(x, 0.0, 1.0) * 15.0;
    int i = int(f);
    return mix(band(i), band(min(i + 1, 15)), f - float(i));
}

// A hash of a grid cell in integer arithmetic. The float hash is chaotic, so
// a compiler that adds n + g + 0.3 in another order would give one cell two
// values (a cell split by a straight line); integers cannot drift.
float cell_hash(vec2 c, uint salt)
{
    uvec2 q = uvec2(ivec2(round(c)) + 4096);
    uint h = q.x * 1597334677u ^ q.y * 3812015801u ^ salt * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return float(h & 0xffffffu) / 16777215.0;
}

// Cellular noise: x the distance to the nearest point, y the distance to the
// nearest cell border, z a value per cell. `move` sets the points drifting.
vec3 cells(vec2 x, float move)
{
    vec2 n = floor(x);
    vec2 f = fract(x);
    vec2 mg = vec2(0.0);
    vec2 mr = vec2(0.0);
    float md = 8.0;
    for (int j = -1; j <= 1; ++j)
        for (int i = -1; i <= 1; ++i)
        {
            vec2 g = vec2(float(i), float(j));
            vec2 o = vec2(cell_hash(n + g, 1u), cell_hash(n + g, 2u));
            o = 0.5 + 0.45 * sin(move + 6.2831853 * o);
            vec2 r = g + o - f;
            float d = dot(r, r);
            if (d < md)
            {
                md = d;
                mr = r;
                mg = g;
            }
        }
    float ed = 8.0;
    for (int j = -2; j <= 2; ++j)
        for (int i = -2; i <= 2; ++i)
        {
            vec2 g = mg + vec2(float(i), float(j));
            vec2 o = vec2(cell_hash(n + g, 1u), cell_hash(n + g, 2u));
            o = 0.5 + 0.45 * sin(move + 6.2831853 * o);
            vec2 r = g + o - f;
            if (dot(mr - r, mr - r) > 0.00001)
                ed = min(ed, dot(0.5 * (mr + r), normalize(r - mr)));
        }
    return vec3(sqrt(md), ed, cell_hash(n + mg, 3u));
}

float segment(vec2 p, vec2 a, vec2 b)
{
    vec2 pa = p - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    return length(pa - ba * h);
}
)";

constexpr const char *kCalmScenes = R"(
// ---- Tide Pool: light dancing on a sandy floor under shallow water ----
// Caustics: water focuses sunlight into a bright moving net. Two layers of
// drifting cell borders, one finer, multiplied, give that net.
float caustic(vec2 q, float t)
{
    vec2 warp = vec2(noise(q * 1.3 + t * 0.2), noise(q * 1.3 - t * 0.17 + 4.0)) * 0.6;
    float a = cells(q + warp, t * 0.55).y;
    float b = cells(q * 1.7 - warp * 1.4 + 3.1, -t * 0.45).y;
    return exp(-a * 9.0) * 0.75 + exp(-b * 11.0) * 0.45 + exp(-a * 9.0) * exp(-b * 11.0) * 1.2;
}

vec3 scene_tidepool(vec2 p, float t)
{
    // A drop falls once a bar; its ring spreads over the pool.
    vec2 drop = vec2(0.18, -0.06);
    float r = length(p - drop);
    float front = bar_phase() * 1.4;
    float ripple = sin((r - front) * 38.0) * exp(-abs(r - front) * 10.0) * (1.0 - bar_phase());
    vec2 q = p * 3.2 + (p - drop) / max(r, 1e-3) * ripple * 0.08;
    float c = caustic(q, t * 0.8);
    float sand = fbm(p * 7.0) * 0.6 + fbm(p * 28.0) * 0.25;
    vec3 col = mix(u_colors[0].rgb, u_colors[1].rgb, 0.35 + sand * 0.8);
    vec3 light = mix(u_colors[2].rgb, u_colors[3].rgb, 0.5 + 0.5 * sin(t * 0.07 + p.x * 1.3));
    col += light * c * (0.5 + u_audio.x * 0.5);
    col += light * ripple * 0.05;
    // A slow shadow of something drifting overhead.
    col *= 0.8 + 0.2 * smoothstep(0.25, 0.65, fbm(p * 1.3 + vec2(t * 0.025, t * 0.01)));
    return col;
}

// ---- Aurora Veil: curtains of light over a dark ridge ----
vec3 scene_veil(vec2 p, float t)
{
    vec3 col = mix(u_colors[0].rgb, u_colors[1].rgb * 0.55, smoothstep(-0.5, 0.35, p.y));
    col += vec3(star_layer(p + vec2(t * 0.002, 0.0), 70.0, 1.0)) * 0.55 * smoothstep(0.25, -0.35, p.y);
    float sway = sin(bar_phase() * 6.2831853) * 0.04;
    for (int i = 0; i < 3; ++i)
    {
        float fi = float(i);
        float x = p.x * (1.1 + fi * 0.35) + t * (0.015 + fi * 0.008) + fi * 3.7;
        float fold = fbm(vec2(x, fi * 2.0 + t * 0.04));
        float edge = -0.02 + fi * 0.08 + (fold - 0.5) * 0.55 + sway * (fi - 1.0);
        float above = edge - p.y;
        float curtain = smoothstep(0.0, 0.025, above) * exp(-max(above, 0.0) * (3.6 - u_audio.y * 1.4));
        float rays = 0.55 + 0.45 * sin(x * 38.0 + fold * 9.0);
        vec3 c = gpal(0.35 + fi * 0.2 + above * 0.5 + t * 0.008);
        col += c * curtain * rays * (0.35 + 0.55 * band(3 + i * 3));
    }
    float ridge = 0.3 + 0.06 * fbm(vec2(p.x * 2.6, 1.0)) + 0.015 * noise(vec2(p.x * 22.0, 4.0));
    col = mix(col, u_colors[0].rgb * 0.25, smoothstep(ridge - 0.003, ridge + 0.003, p.y));
    return col;
}

// ---- Slow Ink: drops of colour blooming in still water ----
vec3 scene_ink(vec2 p, float t)
{
    vec3 col = mix(u_colors[1].rgb * 0.3, u_colors[0].rgb, smoothstep(0.0, 0.9, length(p)));
    for (int i = 0; i < 3; ++i)
    {
        float fi = float(i);
        // Each bloom lasts four bars, the three staggered.
        float clock = u_aux.w / 16.0 + fi / 3.0;
        float age = fract(clock);
        float id = floor(clock) + fi * 13.0;
        vec2 c = (vec2(hash(vec2(id, fi)), hash(vec2(fi, id + 7.0))) - 0.5) * vec2(1.1, 0.55);
        vec2 q = p - c;
        vec2 w = vec2(fbm(q * 3.0 + t * 0.05 + fi), fbm(q * 3.0 - t * 0.04 + fi * 5.0));
        float r = length(q + (w - 0.5) * 0.4 * age);
        float radius = 0.04 + 0.42 * sqrt(age);
        float ink = smoothstep(radius, radius - 0.14, r);
        float edge = exp(-abs(r - radius) * 28.0);
        float life = smoothstep(0.0, 0.08, age) * (1.0 - smoothstep(0.65, 1.0, age));
        vec3 tint = gpal(fi * 0.33 + 0.12 + floor(clock) * 0.17);
        col = mix(col, tint * (0.55 + 0.75 * fbm(q * 5.0 + w * 2.0)), ink * life * 0.85);
        col += tint * edge * life * (0.35 + u_audio.y * 0.45);
    }
    return col;
}

// ---- Lantern Drift: paper lanterns rising over a still lake ----
vec3 lantern_sky(vec2 p, float t, float bright)
{
    vec3 col = mix(u_colors[0].rgb, u_colors[1].rgb * 0.7, smoothstep(-0.5, 0.25, p.y));
    for (int l = 0; l < 3; ++l)
    {
        float fl = float(l);
        float scale = 5.0 + fl * 4.0;
        vec2 q = vec2(p.x * scale + fl * 7.1, p.y * scale + t * (0.25 - fl * 0.05));
        vec2 id = floor(q);
        vec2 f = fract(q) - 0.5;
        float h = hash(id + fl * 11.0);
        if (h < 0.68)
            continue;
        vec2 o = (vec2(hash(id + 2.3), hash(id + 5.9)) - 0.5) * 0.5;
        o.x += sin(t * 0.4 + h * 20.0) * 0.06;
        vec2 d = f - o;
        // A paper lantern: a rounded box a little wider at the top, lit
        // from the flame at its open bottom.
        float half_w = 0.085 + 0.015 * smoothstep(0.12, -0.12, d.y);
        vec2 box = abs(d) - vec2(half_w, 0.12);
        float sd = length(max(box, 0.0)) + min(max(box.x, box.y), 0.0) - 0.03;
        float body = smoothstep(0.01, -0.01, sd);
        float flicker = 0.85 + 0.15 * noise(vec2(t * 3.0, h * 50.0));
        vec3 warm = mix(u_colors[2].rgb, u_colors[3].rgb, h);
        float near = 1.0 - fl * 0.3;
        float lit = 0.55 + 0.7 * smoothstep(-0.12, 0.14, d.y);
        float ribs = 0.92 + 0.08 * sin(d.y * 120.0);
        col = mix(col, warm * lit * ribs * flicker * bright, body * near);
        col += warm * exp(-max(sd, 0.0) * 14.0) * 0.28 * flicker * near * bright;
    }
    return col;
}

vec3 scene_lanterns(vec2 p, float t)
{
    float lake = 0.27;
    float bright = 0.85 + 0.25 * u_audio.y + 0.1 * beat_glow();
    if (p.y < lake)
        return lantern_sky(p, t, bright);
    // The reflection, broken by small waves.
    vec2 m = vec2(p.x + sin(p.y * 90.0 + t * 1.3) * 0.006 * (1.0 + u_audio.x), 2.0 * lake - p.y);
    vec3 col = lantern_sky(m, t, bright) * 0.45;
    col = mix(col, u_colors[0].rgb * 0.5, smoothstep(lake, 0.55, p.y) * 0.6);
    return col;
}

// ---- Rain on Glass: drops on a window over the lights of a city ----
vec3 city_lights(vec2 p, float t)
{
    vec3 col = mix(u_colors[0].rgb, u_colors[1].rgb * 0.6, smoothstep(-0.5, 0.5, p.y));
    for (int l = 0; l < 2; ++l)
    {
        float fl = float(l);
        vec2 q = p * (4.0 + fl * 3.0) + vec2(fl * 3.3, 0.0);
        vec2 id = floor(q);
        vec2 f = fract(q) - 0.5;
        float h = hash(id + fl * 5.0);
        vec2 o = (vec2(hash(id + 1.7), hash(id + 8.3)) - 0.5) * 0.6;
        float r = length(f - o);
        float size = 0.22 + 0.15 * h;
        float disc = smoothstep(size, size - 0.05, r) * step(0.35, h);
        vec3 c = gpal(h + 0.15 * fl);
        col += c * disc * (0.25 + 0.35 * band(int(h * 15.0))) * (0.8 + 0.3 * u_audio.x);
    }
    return col;
}

vec3 scene_rainglass(vec2 p, float t)
{
    vec2 off = vec2(0.0);
    float wet = 0.0;
    // Drops sliding down in columns, each leaving a trail.
    float columns = 14.0;
    float cx = p.x * columns;
    float cid = floor(cx);
    float fx = fract(cx) - 0.5;
    float h = hash(vec2(cid, 4.1));
    float speed = 0.05 + 0.08 * h;
    float y = fract(t * speed + h) * 1.5 - 0.75;
    float wobble = sin(p.y * 20.0 + h * 30.0) * 0.12;
    vec2 local = vec2((fx - wobble * 0.5) / columns, p.y - y);
    float d = length(local * vec2(1.0, 0.7));
    float drop = smoothstep(0.016, 0.012, d);
    off += local * drop * 6.0;
    float trail = smoothstep(0.008, 0.0, abs(local.x)) * step(local.y, 0.0) * exp(local.y * 5.0);
    float beads = step(0.7, fract((p.y - y) * 30.0 + h)) * trail;
    wet += drop + trail * 0.3;
    // Small drops sitting still.
    vec2 g = p * vec2(36.0, 22.0);
    vec2 gid = floor(g);
    vec2 gf = fract(g) - 0.5;
    float gh = hash(gid + 0.7);
    vec2 go = (vec2(hash(gid + 2.2), hash(gid + 9.1)) - 0.5) * 0.6;
    float gd = length(gf - go);
    float small = smoothstep(0.16, 0.1, gd) * step(0.72, gh);
    off += (gf - go) * small * 0.04;
    wet += small;
    // Through a drop the world is upside down and sharper.
    vec3 col = city_lights(p * 0.9 + vec2(0.0, 0.05), t) * 0.75;
    vec3 seen = city_lights(p * 0.9 - off * 1.4, t);
    col = mix(col, seen * 1.6 + 0.03, clamp(wet, 0.0, 1.0));
    col += vec3(0.9) * (drop * smoothstep(0.004, 0.0, length(local + vec2(0.003, 0.006))) +
                        beads * 0.15 + small * smoothstep(0.06, 0.0, length(gf - go + 0.05)) * 0.6);
    return col;
}

// ---- Moon Tide: a full moon and its path of light across the sea ----
vec3 scene_moontide(vec2 p, float t)
{
    float horizon = 0.08;
    vec3 col = mix(u_colors[0].rgb, u_colors[1].rgb * 0.75, smoothstep(-0.5, horizon, p.y));
    col += vec3(star_layer(p, 80.0, 3.0)) * 0.5 * smoothstep(horizon, -0.35, p.y);
    vec2 mc = vec2(0.32, -0.21);
    float md = length(p - mc);
    vec3 moon = mix(u_colors[3].rgb, vec3(1.0), 0.65);
    float disc = smoothstep(0.102, 0.098, md);
    float maria = fbm((p - mc) * 14.0 + 3.0);
    col = mix(col, moon * (0.72 + 0.28 * smoothstep(0.3, 0.7, maria)), disc);
    col += moon * exp(-md * 7.0) * 0.22 * (0.8 + 0.4 * u_audio.y);
    if (p.y < horizon)
        return col;
    float depth = p.y - horizon;
    float z = 0.09 / max(depth, 0.002);
    vec2 s = vec2(p.x * z, z);
    float waves = sin(s.x * 3.0 + s.y * 2.0 - t * 0.7) * 0.5 + sin(s.x * 5.7 - s.y * 3.3 + t * 0.9) * 0.3 +
                  noise(s * 4.0 + t * 0.25) * 0.6;
    waves *= 1.0 + u_audio.x * 0.7;
    vec3 sea = mix(u_colors[1].rgb * 0.3, u_colors[0].rgb * 0.8, smoothstep(0.0, 0.4, depth));
    float path = exp(-abs(p.x - mc.x) * (5.0 + depth * 6.0));
    float glint = smoothstep(0.6, 1.0, sin(waves * 3.0 + s.x * 2.0)) * path;
    sea += moon * glint * (0.75 + 0.3 * beat_glow());
    sea += moon * path * 0.07;
    return mix(sea, col, exp(-depth * 70.0) * 0.6);
}

// ---- Fireflies: a meadow at dusk, some flies blinking on the beat ----
vec3 scene_fireflies(vec2 p, float t)
{
    vec3 col = mix(u_colors[0].rgb, u_colors[1].rgb * 0.8, smoothstep(-0.5, 0.0, p.y));
    col += u_colors[2].rgb * 0.08 * exp(-abs(p.y + 0.02) * 8.0); // the last light of the day
    float trees = -0.02 - 0.08 * fbm(vec2(p.x * 3.5, 0.0)) - 0.035 * noise(vec2(p.x * 22.0, 1.0));
    col = mix(col, u_colors[0].rgb * 0.35, smoothstep(trees - 0.003, trees + 0.003, p.y));
    for (int i = 0; i < 48; ++i)
    {
        float fi = float(i);
        vec2 home = vec2(hash(vec2(fi, 1.0)) - 0.5, hash(vec2(fi, 2.0)) * 0.6 - 0.28);
        home.x *= 1.9;
        vec2 pos = home + vec2(noise(vec2(t * 0.15, fi)) - 0.5, noise(vec2(fi, t * 0.12)) - 0.5) * 0.12;
        float blink = mod(fi, 3.0) < 1.0 ? exp(-beat_phase() * 4.0)
                                         : pow(0.5 + 0.5 * sin(t * (0.7 + hash(vec2(fi, 3.0))) + fi * 2.0), 6.0);
        float d = length(p - pos);
        vec3 c = mix(u_colors[2].rgb, u_colors[3].rgb, hash(vec2(fi, 5.0)));
        col += c * (exp(-d * 220.0) * 1.8 + exp(-d * 26.0) * 0.2) * blink * (0.8 + 0.5 * u_audio.z);
    }
    // Grass in front.
    float grass = 0.34 + 0.05 * noise(vec2(p.x * 55.0, 0.0)) + 0.02 * sin(p.x * 180.0 + sin(t * 0.8 + p.x * 3.0) * 2.0);
    col = mix(col, u_colors[0].rgb * 0.2, smoothstep(grass - 0.004, grass + 0.004, p.y));
    return col;
}

// ---- Deep Sea Glow: jellyfish pulsing in the dark ----
vec3 scene_deepsea(vec2 p, float t)
{
    vec3 col = mix(u_colors[1].rgb * 0.22, u_colors[0].rgb * 0.5, smoothstep(-0.5, 0.5, p.y));
    col += vec3(star_layer(p + vec2(0.0, t * 0.012), 45.0, 5.0)) * 0.22;
    for (int i = 0; i < 7; ++i)
    {
        float fi = float(i);
        float rise = fract(-t * 0.012 * (1.0 + fi * 0.15) + hash(vec2(fi, 1.0)));
        vec2 c = vec2(sin(t * 0.05 + fi * 2.4) * 0.7, rise * 1.3 - 0.75);
        float size = 0.08 + 0.05 * hash(vec2(fi, 9.0));
        // The bell squeezes every other beat, the five out of step.
        float pulse = exp(-fract(u_aux.w * 0.5 + fi * 0.21) * 3.0);
        vec2 q = (p - c) / size;
        q.x *= 1.0 + 0.16 * pulse;
        q.y *= 1.0 - 0.1 * pulse;
        float bell = length(q * vec2(1.0, 1.3));
        float dome = smoothstep(1.0, 0.82, bell) * step(q.y, 0.15);
        float rim = exp(-abs(bell - 0.95) * 12.0) * step(q.y, 0.22);
        float tent = 0.0;
        for (int k = 0; k < 4; ++k)
        {
            float fk = float(k);
            float wave = sin(q.y * 2.6 - t * 1.6 + fk * 1.3) * 0.16 * q.y;
            float d = abs(q.x - (-0.6 + fk * 0.4) - wave);
            tent += exp(-d * 16.0) * smoothstep(3.6, 0.3, q.y) * step(0.12, q.y);
        }
        vec3 tint = gpal(fi * 0.23 + 0.4);
        float glow = 0.6 + 0.6 * pulse + band(4 + i) * 0.6;
        col += tint * (dome * 0.22 + rim * 0.85 + tent * 0.3) * glow;
        col += tint * exp(-length(q) * 1.3) * 0.1 * glow;
    }
    return col;
}

// ---- Sand Ripples: dunes under a low sun ----
vec3 scene_dunes(vec2 p, float t)
{
    float horizon = -0.1;
    vec2 sun = vec2(-0.38, horizon - 0.06);
    vec3 sky = mix(u_colors[2].rgb * 0.75, u_colors[1].rgb * 0.8, smoothstep(horizon, -0.55, p.y));
    sky += u_colors[3].rgb * exp(-length(p - sun) * 10.0) * 0.35;
    sky = mix(sky, mix(u_colors[3].rgb, vec3(1.0), 0.5), smoothstep(0.045, 0.04, length(p - sun)));
    float ridge = horizon - 0.035 - 0.05 * fbm(vec2(p.x * 1.4 + t * 0.008, 2.0));
    if (p.y < ridge)
        return sky;
    if (p.y < horizon)
    {
        float k = (p.y - ridge) / max(horizon - ridge, 1e-3);
        return mix(mix(u_colors[2].rgb * 0.7, sky, 0.4), u_colors[1].rgb * 0.7, k * 0.5);
    }
    float depth = p.y - horizon;
    float z = 0.1 / depth;
    vec2 g = vec2(p.x * z + t * 0.03, z + t * 0.015);
    float dune = fbm(g * 0.5);
    float ripples = sin(g.x * 12.0 + g.y * 5.0 + dune * 14.0 + band_at(fract(g.x * 0.05)) * 2.0);
    float facing = 0.5 + 0.5 * sin(g.x * 1.3 + dune * 6.0);
    vec3 col = mix(u_colors[0].rgb, u_colors[2].rgb * 0.85, 0.25 + 0.5 * facing + 0.1 * ripples * exp(-z * 0.08));
    // Sand blown off the crests on the bass.
    col += u_colors[3].rgb * smoothstep(0.75, 1.0, noise(g * 30.0 + vec2(t * 2.0, 0.0))) * u_audio.x * 0.2;
    return mix(col, sky, exp(-depth * 18.0) * 0.6);
}

// ---- Cloud Sea: above the clouds at sunrise ----
vec3 scene_cloudsea(vec2 p, float t)
{
    float horizon = -0.05;
    vec3 sky = mix(u_colors[2].rgb * 0.7, u_colors[1].rgb * 0.7, smoothstep(horizon, -0.55, p.y));
    vec2 sun = vec2(0.08, horizon - 0.02);
    float sd = length((p - sun) * vec2(1.0, 1.25));
    vec3 light = mix(u_colors[3].rgb, vec3(1.0), 0.4);
    sky += light * (exp(-sd * 18.0) * 0.8 + exp(-sd * 4.0) * 0.12) * (0.9 + 0.25 * u_audio.x);
    if (p.y < horizon)
        return sky;
    float depth = p.y - horizon;
    float z = 0.12 / depth;
    vec2 c = vec2(p.x * z, z + t * 0.05);
    float cloud = fbm(c * 0.7 + vec2(t * 0.01, 0.0)) * 0.6 + fbm(c * 1.9 - t * 0.02) * 0.4;
    float lit = smoothstep(0.32, 0.8, cloud);
    vec3 col = mix(u_colors[1].rgb * 0.5, light * 0.72, lit);
    col += light * exp(-abs(p.x - sun.x) * 3.0) * 0.22 * exp(-depth * 3.0) * (0.85 + 0.3 * beat_glow() * u_audio.x);
    return mix(col, sky, exp(-depth * 22.0) * 0.8);
}
)";

constexpr const char *kCalmMain = R"(
void main()
{
    vec2 uv = vec2(gl_FragCoord.x / u_resolution.x, 1.0 - gl_FragCoord.y / u_resolution.y);
    float aspect = u_resolution.x / u_resolution.y;
    vec2 p = (uv - 0.5) * vec2(aspect, 1.0);
    p += vec2(u_aux.x, u_aux.y) * 0.02;
    float t = u_time;
    vec3 col = vec3(0.0);
    if (u_mode == 70)
        col = scene_tidepool(p, t);
    else if (u_mode == 71)
        col = scene_veil(p, t);
    else if (u_mode == 72)
        col = scene_ink(p, t);
    else if (u_mode == 73)
        col = scene_lanterns(p, t);
    else if (u_mode == 74)
        col = scene_rainglass(p, t);
    else if (u_mode == 75)
        col = scene_moontide(p, t);
    else if (u_mode == 76)
        col = scene_fireflies(p, t);
    else if (u_mode == 77)
        col = scene_deepsea(p, t);
    else if (u_mode == 78)
        col = scene_dunes(p, t);
    else if (u_mode == 79)
        col = scene_cloudsea(p, t);
    col *= 1.0 + u_aux.z * 0.2;
    col += (hash(gl_FragCoord.xy + fract(u_time) * 61.0) - 0.5) / 255.0;
    frag_color = vec4(max(col, 0.0), u_opacity);
}
)";

constexpr const char *kGalleryScenes = R"(
// ---- Kintsugi: a glazed bowl mended with gold, the gold flowing each phrase ----
vec3 scene_kintsugi(vec2 p, float t)
{
    vec2 q = p * 3.0 + vec2(t * 0.008, 0.0);
    vec3 v = cells(q, 0.0);
    float glaze = fbm(p * 5.0) * 0.55 + fbm(p * 24.0) * 0.18;
    vec3 col = mix(u_colors[0].rgb, u_colors[1].rgb, 0.25 + glaze);
    col *= 0.65 + 0.55 * smoothstep(0.9, -0.3, length(p - vec2(-0.45, -0.3)));
    float width = 0.01 + 0.007 * noise(q * 7.0);
    float crack = smoothstep(width, width * 0.25, v.y);
    // The gold runs out from the middle over a phrase (32 beats).
    float reach = fract(u_aux.w / 32.0) * 1.5;
    float filled = max(smoothstep(reach, reach - 0.2, length(p)), 0.3);
    vec3 gold = mix(vec3(0.86, 0.64, 0.24), u_colors[3].rgb, 0.2);
    float shine = 0.55 + 0.35 * sin(q.x * 2.3 + q.y * 1.7 + t * 0.4) + band(int(v.z * 15.99)) * 0.7;
    col = mix(col, gold * shine, crack * filled);
    col += gold * exp(-v.y * 55.0) * 0.2 * (0.3 + u_audio.x) * filled;
    // A sheen of the glaze, as on a curved bowl.
    col += vec3(1.0) * pow(max(0.0, 1.0 - length(p - vec2(-0.5, -0.32)) * 2.2), 6.0) * 0.18;
    return col;
}

// ---- Ridgelines: a mountain range drawn by the music at dusk, ridge after
// ridge, each filled and outlined, the nearer hiding the farther ----
vec3 scene_ridges(vec2 p, float t, float aspect)
{
    vec3 col = mix(u_colors[1].rgb * 0.6, u_colors[0].rgb, smoothstep(-0.5, 0.1, p.y));
    col += u_colors[3].rgb * 0.25 * exp(-length((p - vec2(0.0, -0.18)) * vec2(0.6, 2.0)) * 3.0);
    for (int i = 0; i < 18; ++i)
    {
        float fi = float(i);
        float k = fi / 17.0; // 0 the nearest, 1 the farthest
        float base = 0.46 - 0.62 * pow(k, 0.7);
        float x = p.x * (1.0 + (1.0 - k) * 0.6) + fi * 4.7;
        float hills = fbm(vec2(x * 1.4, fi * 1.3 + t * 0.03));
        float s = band_at(fract(x * 0.12 + 0.5));
        float h = (0.02 + 0.2 * smoothstep(0.3, 0.75, hills) + 0.08 * s) * (1.0 - k * 0.5);
        float d = p.y - (base - h);
        float w = 0.0016 + 0.0016 * (1.0 - k);
        vec3 c = mix(u_colors[2].rgb, u_colors[3].rgb, k);
        float line = smoothstep(w, 0.0, abs(d)) * (0.75 + 0.35 * beat_glow()) + exp(-abs(d) * 120.0) * 0.15;
        col += c * line * (1.0 - k * 0.4);
        if (d > 0.0)
        {
            // Inside this ridge: its own face, darker towards us.
            vec3 face = mix(u_colors[0].rgb, u_colors[1].rgb * 0.8, k) * (1.0 - smoothstep(0.0, 0.25, d) * 0.4);
            col = mix(face, col, smoothstep(w * 3.0, 0.0, d));
            break;
        }
    }
    return col;
}

// ---- Origami: a folded paper sheet, its facets turning to the light ----
vec3 scene_origami(vec2 p, float t)
{
    vec2 q = p * 5.0 + vec2(t * 0.04, 0.0);
    vec2 s = vec2(q.x - q.y * 0.57735, q.y * 1.1547);
    vec2 id = floor(s);
    vec2 f = fract(s);
    float upper = step(1.0, f.x + f.y);
    float h = hash(id * 2.0 + vec2(upper * 7.1, upper));
    float wave = sin(dot(id, vec2(0.45, 0.3)) - t * 0.5 + bar_phase() * 6.2831853);
    float fold = (0.35 + 0.45 * (0.5 + 0.5 * wave)) * (1.0 + u_audio.x * 0.6);
    vec3 n = normalize(vec3(vec2(cos(h * 6.2831853), sin(h * 6.2831853)) * fold, 1.0));
    vec3 l = normalize(vec3(-0.45, -0.6, 0.7));
    float shade = clamp(dot(n, l), 0.0, 1.0);
    vec3 paper = mix(u_colors[1].rgb, gpal(h * 0.35 + 0.25), 0.6);
    vec3 col = paper * (0.18 + 1.05 * shade * shade);
    col += gpal(h + 0.5) * band(int(h * 15.99)) * 0.18;
    float crease = min(min(f.x, f.y), abs(f.x + f.y - 1.0));
    col += vec3(1.0) * smoothstep(0.03, 0.0, crease) * 0.08;
    col *= 0.92 + 0.08 * fbm(p * 60.0);
    return col;
}

// ---- Halftone Pop: shapes printed in three screens of dots, a little off register ----
float halftone_image(vec2 p, float t)
{
    float v = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        float fi = float(i);
        vec2 c = vec2(sin(t * (0.13 + fi * 0.04) + fi * 2.0) * 0.6, cos(t * (0.11 + fi * 0.05) + fi) * 0.28);
        float r = length(p - c);
        v += exp(-r * r * (9.0 - band(i * 4) * 4.0));
    }
    return clamp(v * 0.8 + fbm(p * 2.0 + t * 0.05) * 0.25, 0.0, 1.0);
}

vec3 scene_halftone(vec2 p, float t)
{
    vec3 col = mix(vec3(0.97, 0.95, 0.9), u_colors[3].rgb, 0.08);
    float cell = 0.024 * (1.0 - beat_glow() * 0.06);
    for (int j = 0; j < 3; ++j)
    {
        float fj = float(j);
        vec2 shift = vec2(cos(fj * 2.1), sin(fj * 2.1)) * 0.012 * (1.0 + u_audio.x * 2.0);
        float value = halftone_image(p + shift, t + fj * 0.4);
        vec2 q = rot(0.26 + fj * 0.52) * p / cell;
        vec2 f = fract(q) - 0.5;
        float r = sqrt(value) * 0.62;
        float dotm = smoothstep(r, r - 0.08, length(f));
        vec3 ink = j == 0 ? u_colors[2].rgb : (j == 1 ? u_colors[3].rgb : mix(u_colors[1].rgb, u_colors[2].rgb, 0.5));
        col *= mix(vec3(1.0), ink, dotm * 0.85);
    }
    return col;
}

// ---- Coral Labyrinth: a Turing pattern that slowly rearranges itself ----
vec3 scene_labyrinth(vec2 p, float t)
{
    vec2 q = p * 3.0;
    vec2 w = vec2(fbm(q * 0.6 + t * 0.03), fbm(q * 0.6 + 5.2 - t * 0.025));
    float f = fbm(q * 1.1 + w * 1.6);
    float g = fbm(q * 3.0 - w + t * 0.02);
    // Contours of a smooth field, close together: stripes of one width
    // that wander and branch like a Turing pattern.
    float v = sin(f * 46.0 + t * 0.15) + u_audio.x * 0.3;
    float stripe = smoothstep(-0.12, 0.12, v);
    vec3 a = mix(u_colors[0].rgb, u_colors[1].rgb, f);
    vec3 b = gpal(f * 0.5 + t * 0.01);
    vec3 col = mix(a, b * (0.55 + 0.5 * g), stripe);
    col += gpal(0.6 + f) * pow(max(1.0 - abs(v), 0.0), 24.0) * (0.3 + u_audio.y * 0.5 + beat_glow() * 0.1);
    return col;
}

// ---- Stained Glass: a window lit from behind, a band of sun moving across it ----
vec3 scene_glass(vec2 p, float t)
{
    vec2 q = p * 4.0;
    vec3 v = cells(q, 0.0);
    vec3 glass = gpal(v.z);
    glass = mix(glass, glass * glass * 1.6, 0.4);
    float light = 0.75 + 0.6 * band(int(v.z * 15.99));
    float beam = exp(-pow(p.x + p.y * 0.6 - sin(t * 0.06) * 0.9, 2.0) * 5.0);
    float tex = 0.82 + 0.18 * fbm(q * 5.0 + v.z * 10.0);
    vec3 col = glass * light * tex * (0.75 + beam * 0.9);
    col += glass * beat_glow() * 0.12;
    float lead = smoothstep(0.055, 0.03, v.y);
    vec3 came = vec3(0.035, 0.035, 0.045) + vec3(0.12) * smoothstep(0.03, 0.0, abs(v.y - 0.025));
    return mix(col, came, lead);
}

// ---- Lissajous Loom: three threads of light woven to the music ----
vec3 scene_loom(vec2 p, float t)
{
    vec3 col = u_colors[0].rgb * 0.45;
    for (int c = 0; c < 3; ++c)
    {
        float fc = float(c);
        float a = 3.0 + fc;
        float b = 2.0 + fc;
        float delta = t * (0.07 + fc * 0.02) + fc * 1.3;
        vec2 size = vec2(0.66, 0.38) * (0.9 + 0.1 * u_audio.x + 0.04 * beat_glow());
        float best = 1e9;
        vec2 prev = vec2(sin(delta), 0.0) * size;
        for (int i = 1; i <= 90; ++i)
        {
            float s = float(i) / 90.0 * 6.2831853;
            vec2 pt = vec2(sin(a * s + delta), sin(b * s)) * size;
            best = min(best, segment(p, prev, pt));
            prev = pt;
        }
        vec3 tint = gpal(fc * 0.33 + t * 0.015);
        col += tint * (smoothstep(0.003, 0.0, best) * 0.9 + exp(-best * 35.0) * 0.14) * (0.6 + band(c * 5) * 0.9);
    }
    return col;
}

// ---- Crystal Growth: a snowflake breathing with the bars ----
vec3 scene_crystal(vec2 p, float t)
{
    vec3 col = mix(u_colors[1].rgb * 0.45, u_colors[0].rgb, smoothstep(0.0, 0.8, length(p)));
    col += vec3(star_layer(p * 1.3 + vec2(t * 0.004, t * 0.006), 30.0, 7.0)) * 0.35;
    vec2 q = rot(t * 0.025) * p;
    float r = length(q);
    float a = atan(q.y, q.x);
    float seg = 3.14159265 / 3.0;
    a = abs(mod(a + seg * 0.5, seg) - seg * 0.5);
    vec2 s = r * vec2(cos(a), sin(a));
    float grow = 0.3 + 0.05 * sin(bar_phase() * 6.2831853) + 0.04 * u_audio.x;
    float d = s.x < grow ? s.y : length(s - vec2(grow, 0.0));
    vec2 dir = vec2(0.5, 0.8660254);
    for (int k = 1; k < 7; ++k)
    {
        float x0 = float(k) * grow / 7.0;
        float len = (grow - x0) * (0.42 + 0.1 * band(k * 2));
        vec2 v = s - vec2(x0, 0.0);
        d = min(d, length(v - dir * clamp(dot(v, dir), 0.0, len)));
    }
    float hex = max(s.y * 1.7320508 + s.x, s.x * 2.0) * 0.5;
    float plate = smoothstep(0.065, 0.06, hex) - smoothstep(0.05, 0.045, hex) * 0.6;
    vec3 ice = mix(u_colors[2].rgb, vec3(1.0), 0.55);
    col += ice * (smoothstep(0.005, 0.001, d) + exp(-d * 55.0) * 0.28) * (0.75 + beat_glow() * 0.35);
    col += ice * plate * 0.5;
    return col;
}

// ---- Ferrofluid: a pool of black liquid that rises into spikes on the bass ----
vec2 hex_local(vec2 q)
{
    vec2 r = vec2(1.0, 1.7320508);
    vec2 h = r * 0.5;
    vec2 a = mod(q, r) - h;
    vec2 b = mod(q - h, r) - h;
    return dot(a, a) < dot(b, b) ? a : b;
}

float ferro_height(vec2 p)
{
    float r = length(p);
    float dome = sqrt(max(0.0, 0.17 - r * r));
    float spikes = 0.04 + 0.11 * u_audio.x + 0.04 * beat_glow();
    float k = max(0.0, 1.0 - length(hex_local(p * 11.0)) * 2.1);
    return dome * 0.6 + pow(k, 1.6) * spikes * smoothstep(0.41, 0.05, r);
}

vec3 scene_ferro(vec2 p, float t)
{
    vec3 col = mix(u_colors[1].rgb * 0.5, u_colors[0].rgb, smoothstep(-0.4, 0.6, p.y));
    // The pool turns slowly; the slope is measured on the screen (not in
    // the turned frame) so the lights stay where they are.
    mat2 turn = rot(t * 0.03);
    float r = length(p);
    if (r > 0.415)
    {
        col *= 1.0 - 0.5 * exp(-(r - 0.41) * 9.0); // its shadow on the table
        return col;
    }
    float e = 0.002;
    float h = ferro_height(turn * p);
    vec2 g = vec2(ferro_height(turn * (p + vec2(e, 0.0))) - h, ferro_height(turn * (p + vec2(0.0, e))) - h) / e;
    vec3 n = normalize(vec3(-g, 1.0));
    vec3 view = vec3(0.0, 0.0, -1.0);
    vec3 refl = reflect(view, n);
    // A studio of two coloured lights and a white key light.
    // The key light sits near the camera, so each spike's tip catches it.
    float key = pow(max(dot(refl, normalize(vec3(-0.15, -0.25, 0.95))), 0.0), 60.0);
    float left = pow(max(dot(refl, normalize(vec3(-0.9, 0.2, 0.4))), 0.0), 8.0);
    float right = pow(max(dot(refl, normalize(vec3(0.9, 0.1, 0.4))), 0.0), 8.0);
    // Light from the top left shades the spikes' sides, so they stand up.
    float diffuse = clamp(dot(n, normalize(vec3(-0.5, -0.6, 0.6))), 0.0, 1.0);
    vec3 liquid = vec3(0.012) * (0.4 + diffuse) + u_colors[2].rgb * left * 0.35 + u_colors[3].rgb * right * 0.35 +
                  vec3(1.0) * key * 1.6 + vec3(0.06) * diffuse * diffuse;
    liquid *= 0.55 + 0.45 * diffuse;
    float rim = smoothstep(0.415, 0.405, r);
    return mix(col, liquid, rim);
}

// ---- Paper Cut: layered paper hills, the near ones casting a soft shadow ----
vec3 scene_papercut(vec2 p, float t)
{
    vec3 col = mix(mix(u_colors[3].rgb, vec3(1.0), 0.25), u_colors[2].rgb * 0.9, smoothstep(-0.5, 0.1, p.y));
    vec2 sun = vec2(0.3, -0.2);
    col = mix(col, mix(u_colors[3].rgb, vec3(1.0), 0.7), smoothstep(0.075, 0.07, length(p - sun)));
    for (int i = 0; i < 6; ++i)
    {
        float fi = float(i);
        float k = fi / 5.0;
        float x = p.x * (0.9 + k * 0.7) + t * (0.006 + k * 0.02) + fi * 7.3;
        float sway = band(2 + i * 2) * 0.02 * sin(x * 7.0 + t);
        float hill = -0.16 + k * 0.12 + 0.2 * (fbm(vec2(x * 1.0, fi)) - 0.5) + sway;
        hill -= 0.01 * beat_glow() * (1.0 - k);
        float edge = p.y - hill;
        // The layer in front shades the paper just behind its edge.
        col *= 1.0 - 0.45 * smoothstep(-0.03, 0.0, edge) * (1.0 - step(0.0, edge));
        vec3 paper = mix(mix(u_colors[3].rgb, u_colors[2].rgb, 0.6), u_colors[0].rgb * 1.4, pow(k, 0.8));
        paper *= 0.94 + 0.06 * fbm(p * 45.0 + fi * 3.0);
        paper *= 0.9 + 0.1 * smoothstep(0.08, 0.0, edge); // a lit rim along the cut
        col = mix(col, paper, smoothstep(-0.0015, 0.0015, edge));
    }
    return col;
}

)";

constexpr const char *kGalleryMain = R"(
void main()
{
    vec2 uv = vec2(gl_FragCoord.x / u_resolution.x, 1.0 - gl_FragCoord.y / u_resolution.y);
    float aspect = u_resolution.x / u_resolution.y;
    vec2 p = (uv - 0.5) * vec2(aspect, 1.0);
    p += vec2(u_aux.x, u_aux.y) * 0.03;
    float t = u_time;
    vec3 col = vec3(0.0);
    if (u_mode == 80)
        col = scene_kintsugi(p, t);
    else if (u_mode == 81)
        col = scene_ridges(p, t, aspect);
    else if (u_mode == 82)
        col = scene_origami(p, t);
    else if (u_mode == 83)
        col = scene_halftone(p, t);
    else if (u_mode == 84)
        col = scene_labyrinth(p, t);
    else if (u_mode == 85)
        col = scene_glass(p, t);
    else if (u_mode == 86)
        col = scene_loom(p, t);
    else if (u_mode == 87)
        col = scene_crystal(p, t);
    else if (u_mode == 88)
        col = scene_ferro(p, t);
    else if (u_mode == 89)
        col = scene_papercut(p, t);
    col *= 1.0 + u_aux.z * 0.3;
    col += (hash(gl_FragCoord.xy + fract(u_time) * 61.0) - 0.5) / 255.0;
    frag_color = vec4(max(col, 0.0), u_opacity);
}
)";

} // namespace hui::gfx
