// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "diagnostics/WarningHistory.h"
#include "recipe/Recipe.h"

#include <string_view>

namespace BetterEnchantmentEffects {
void LogOnce(WarningHistory &a_history, Severity a_severity,
             std::string_view a_line);
}
