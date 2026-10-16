// Afterglow - Host (PC) AAC hook: the PC preview and the tests have no AAC decoder.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/stream_decoder.hpp"

namespace hui::audio
{

std::unique_ptr<StreamDecoder> make_aac_decoder()
{
    return nullptr;
}

} // namespace hui::audio
