// Afterglow - Fitting song titles and names into the boxes that show them.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Station metadata has no length limit: a title can be "Artist feat. Artist
// - Title (Extended Club Mix) [Label]". A box first shrinks the text a
// little, then wraps it onto more lines at a smaller size, and only then cuts
// it with an ellipsis, so nothing ever runs past its edge.

#pragma once

#include "ui/fonts.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace hui::concepts
{

struct TextBlock
{
    float size = 0.0f;
    std::vector<std::string> lines;
};

// `text` at `size` or smaller in at most `max_lines` lines no wider than
// `width`. One line may shrink to 80 % before the text wraps; wrapped lines
// shrink to `min_scale` of the size, and what still does not fit ends in an
// ellipsis on the last line.
inline TextBlock fit_block(const ui::FontRef &font, const std::string &text, float size,
                           float width, int max_lines, float min_scale = 0.6f,
                           float tracking = 0.0f)
{
    TextBlock block;
    block.size = size;
    if (font.font == nullptr || text.empty() || width <= 0.0f)
    {
        block.lines.push_back(text);
        return block;
    }
    const float w = font.measure(text, size, tracking);
    if (w <= width)
    {
        block.lines.push_back(text);
        return block;
    }
    const float one_line = size * width / w * 0.995f;
    if (one_line >= size * 0.8f || (max_lines <= 1 && one_line >= size * min_scale))
    {
        block.size = one_line;
        block.lines.push_back(text);
        return block;
    }
    if (max_lines >= 2)
    {
        const int steps = static_cast<int>((1.0f - min_scale) / 0.05f + 0.5f);
        for (int k = 0; k <= steps; ++k)
        {
            const float s = size * (1.0f - 0.05f * static_cast<float>(k));
            std::vector<std::string> lines = font.font->wrap(text, s, width);
            if (!lines.empty() && lines.size() <= static_cast<std::size_t>(max_lines))
            {
                block.size = s;
                block.lines = std::move(lines);
                return block;
            }
        }
    }
    block.size = size * min_scale;
    const std::vector<std::string> lines =
        max_lines >= 2 ? font.font->wrap(text, block.size, width) : std::vector<std::string>{text};
    const std::size_t keep = static_cast<std::size_t>(std::max(1, max_lines));
    for (std::size_t i = 0; i + 1 < keep && i < lines.size(); ++i)
        block.lines.push_back(lines[i]);
    std::string rest;
    for (std::size_t i = block.lines.size(); i < lines.size(); ++i)
        rest += (rest.empty() ? "" : " ") + lines[i];
    block.lines.push_back(font.font->fit(rest, block.size, width, tracking));
    return block;
}

// One line: shrinks to `min_scale`, then ends in an ellipsis.
inline TextBlock fit_line(const ui::FontRef &font, const std::string &text, float size, float width,
                          float min_scale = 0.75f, float tracking = 0.0f)
{
    return fit_block(font, text, size, width, 1, min_scale, tracking);
}

} // namespace hui::concepts
