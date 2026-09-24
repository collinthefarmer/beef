// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "menu/Frame.h"
#include "studio/TermTemplates.h"

#include <span>
#include <string_view>

namespace BetterEnchantmentEffects::Menu {
void DrawPatternChooser(std::span<const Studio::TermOffer> a_offers,
                        std::string_view a_filter, bool a_full,
                        const Frame &a_frame);
}
