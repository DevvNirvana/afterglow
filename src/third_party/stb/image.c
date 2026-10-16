// Afterglow - The single compilation unit for the vendored stb_image.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Album art arrives as JPEG or PNG in memory, station logos also as GIF or
// BMP (icons are unpacked in net/cover_art.cpp); stdio is left out.

#define STB_IMAGE_IMPLEMENTATION 1
#define STBI_ONLY_JPEG 1
#define STBI_ONLY_PNG 1
#define STBI_ONLY_GIF 1
#define STBI_ONLY_BMP 1
#define STBI_NO_STDIO 1
#define STBI_NO_LINEAR 1
#define STBI_NO_HDR 1
#define STBI_MAX_DIMENSIONS 4096
#include "stb_image.inc"
