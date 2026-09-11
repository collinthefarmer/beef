#pragma once

#include "menu/Frame.h"
#include "studio/Snapshot.h"

#include <string_view>

namespace BetterEnchantmentEffects::Menu {
void DrawPaintHead(const Frame &a_frame);
void DrawMaskRule(std::string_view a_title, const Frame &a_frame);
void DrawMaskStack(const Frame &a_frame);
void RebuildScratch(const Frame &a_frame);
void EditMaskAsTerms(const Studio::TextRow &a_mask, const Frame &a_frame);
}
