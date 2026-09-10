#pragma once

#include "studio/Forms.h"
#include "studio/Names.h"

#include <optional>
#include <string>
#include <string_view>

namespace BetterEnchantmentEffects::Studio {
[[nodiscard]] std::optional<std::string> CheckField(const FormField &a_field,
                                                    std::string_view a_text,
                                                    const Names &a_names);
[[nodiscard]] std::optional<std::string>
CheckSignalValue(std::string_view a_text, const Names &a_names);
[[nodiscard]] std::optional<std::string> CheckCurveText(std::string_view a_text,
                                                        const Names &a_names);
[[nodiscard]] std::optional<std::string> CheckMaskText(std::string_view a_text,
                                                       const Names &a_names);
}
