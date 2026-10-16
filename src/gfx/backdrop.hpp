// ps5-homebrew-ui - Procedural full-screen backdrops and post overlays.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/backdrop_spec.hpp"

#include <GL/glcorearb.h>

namespace hui::gfx
{

// Programs that draw every mode over a full-screen triangle: the kit's and
// Afterglow's scenes in one, the trip, calm and gallery packs in their own.
class Backdrop
{
  public:
    Backdrop() = default;
    Backdrop(const Backdrop &) = delete;
    Backdrop &operator=(const Backdrop &) = delete;
    ~Backdrop();

    bool init();
    void release();
    // Draws into the bound framebuffer. opacity < 1 blends over what is there
    // (used to cross-fade two backdrops); post overlays always blend.
    void draw(const BackdropSpec &spec, int width, int height, float opacity = 1.0f);

  private:
    GLuint program_ = 0;
    GLuint trip_program_ = 0;    // modes 50 to 69 (gfx/backdrop_trip.hpp)
    GLuint calm_program_ = 0;    // modes 70 to 79 (gfx/backdrop_gallery.hpp)
    GLuint gallery_program_ = 0; // modes 80 to 89
    GLuint vao_ = 0;
};

} // namespace hui::gfx
