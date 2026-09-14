#pragma once

#include "studio/Forms.h"
#include "studio/Names.h"

#include <optional>
#include <string>
#include <string_view>

namespace BetterEnchantmentEffects::Studio {
[[nodiscard]] std::optional<Diagnostic> CheckField(const FormField &a_field,
                                                   std::string_view a_text,
                                                   const Names &a_names);
[[nodiscard]] std::optional<Diagnostic>
CheckSignalValue(std::string_view a_text, const Names &a_names);
[[nodiscard]] std::optional<Diagnostic> CheckCurveText(std::string_view a_text,
                                                       const Names &a_names);
[[nodiscard]] std::optional<Diagnostic> CheckMaskText(std::string_view a_text,
                                                      const Names &a_names);
}
