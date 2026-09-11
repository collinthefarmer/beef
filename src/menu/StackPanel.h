#pragma once

#include "menu/Frame.h"
#include "studio/Panels.h"

#include <optional>

namespace BetterEnchantmentEffects::Menu {
void DrawStack(const std::optional<Studio::LayerStack> &a_stack,
               const std::optional<Studio::Inspector> &a_inspector,
               const Frame &a_frame);
}
