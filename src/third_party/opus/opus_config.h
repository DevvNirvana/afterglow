/* Afterglow - Build settings for the vendored libopus (decoder use, float).
 * Copyright (C) 2026 DevvNirvana
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Stands in for the config.h that libopus's own build would generate. Every
 * .c file here includes it first (the only change made to them, besides
 * flattening the tree into one folder). */

#ifndef AFTERGLOW_OPUS_CONFIG_H
#define AFTERGLOW_OPUS_CONFIG_H

#define OPUS_BUILD 1
#define VAR_ARRAYS 1
#define HAVE_LRINT 1
#define HAVE_LRINTF 1
#define PACKAGE_VERSION "1.5.2"

#endif
