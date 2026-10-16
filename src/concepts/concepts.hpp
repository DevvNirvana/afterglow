// ps5-homebrew-ui - Factories of every UI design in the app.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/concept.hpp"

#include <memory>
#include <string>

namespace hui::concepts
{

// One function per design, each defined in its own file in this directory.
// To add a design: write the file, declare its factory here and list it in
// registry.cpp. Nothing else in the app needs to change.
std::unique_ptr<app::Concept> make_aurora(app::Context &context);
std::unique_ptr<app::Concept> make_paper(app::Context &context);
std::unique_ptr<app::Concept> make_neon(app::Context &context);
std::unique_ptr<app::Concept> make_editorial(app::Context &context);
std::unique_ptr<app::Concept> make_carousel(app::Context &context);
std::unique_ptr<app::Concept> make_radial(app::Context &context);
std::unique_ptr<app::Concept> make_hud(app::Context &context);
std::unique_ptr<app::Concept> make_dashboard(app::Context &context);
std::unique_ptr<app::Concept> make_player(app::Context &context);
std::unique_ptr<app::Concept> make_keyboard(app::Context &context);
std::unique_ptr<app::Concept> make_constellation(app::Context &context);
std::unique_ptr<app::Concept> make_terminal(app::Context &context);
std::unique_ptr<app::Concept> make_store(app::Context &context);
std::unique_ptr<app::Concept> make_trophies(app::Context &context);
std::unique_ptr<app::Concept> make_files(app::Context &context);
std::unique_ptr<app::Concept> make_inventory(app::Context &context);
std::unique_ptr<app::Concept> make_boot(app::Context &context);
std::unique_ptr<app::Concept> make_settings(app::Context &context);
std::unique_ptr<app::Concept> make_themes(app::Context &context);
std::unique_ptr<app::Concept> make_components(app::Context &context);
std::unique_ptr<app::Concept> make_toolbox(app::Context &context);
std::unique_ptr<app::Concept> make_afterglow(app::Context &context);
// Whether Afterglow has a scene of this name (the genre skins name scenes).
bool afterglow_has_scene(const char *name);
// Hands Afterglow a command as its command box would ("play techno"); it is
// carried out on the next update. design must come from make_afterglow.
// False when the words were not understood. For tests.
bool afterglow_command(app::Concept &design, const std::string &text);

} // namespace hui::concepts
