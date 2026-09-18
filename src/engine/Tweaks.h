#pragma once

#include "PCH.h"

#include <string>

namespace BetterEnchantmentEffects {
[[nodiscard]] std::string EditorIdOf(const RE::TESForm &a_form);

[[nodiscard]] bool TweaksEditorIdsAvailable();
}
