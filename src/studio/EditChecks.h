// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "Core.h"
#include "recipe/Recipe.h"
#include "recipe/Signals.h"

#include <optional>
#include <string>
#include <string_view>

namespace BetterEnchantmentEffects::Studio {
struct CheckContext {
  const RowTypes &rows;
  std::string_view where;
};

[[nodiscard]] std::optional<Diagnostic> CheckText(const std::string &a_where,
                                                  const std::string &a_text);
[[nodiscard]] std::optional<Diagnostic>
CheckCurveText(const std::string &a_where, const CurveRef &a_curve);
[[nodiscard]] std::optional<Diagnostic>
CheckScalarRef(const CheckContext &a_ctx, std::string_view a_field,
               const Param &a_param);
[[nodiscard]] std::optional<Diagnostic>
CheckVectorRef(const CheckContext &a_ctx, std::string_view a_field,
               const Vec3Param &a_param, bool a_color);
}
