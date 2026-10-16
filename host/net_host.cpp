// Afterglow - Host (Linux/WSL) transport for the radio: the plain-socket client.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net/http.hpp"

namespace hui::net
{

OpenResult platform_open(const std::string &url, const std::atomic<bool> &cancel)
{
    return open_with_sockets(url, cancel);
}

} // namespace hui::net
