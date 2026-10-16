// ps5-homebrew-ui - The list of UI designs, in switcher order.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Afterglow ships one screen: the visualiser. The kit's gallery designs stay
// in the source tree (their tests still build them) but are not registered.

#include "concepts/concepts.hpp"

namespace hui::app
{

std::span<const ConceptFactory> concept_registry()
{
    static constexpr ConceptFactory kFactories[] = {
        concepts::make_afterglow,
    };
    return kFactories;
}

} // namespace hui::app
