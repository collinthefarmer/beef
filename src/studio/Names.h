#pragma once

#include "Core.h"
#include "studio/Snapshot.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
enum class RowKind {
  kSignal,
  kCurve,
  kSource,
  kMask,
};
inline constexpr std::size_t kRowKindCount = 4;

struct Names {
  std::vector<std::pair<std::string, ValueType>> signals;
  std::vector<std::string> curves;
  std::vector<std::pair<std::string, ValueType>> sources;
  std::vector<std::string> masks;
};

[[nodiscard]] Names NamesOf(const RecipeRow &a_recipe,
                            const GeometryRow &a_geometry);
[[nodiscard]] Names NamesOf(const RecipeRow &a_recipe);
[[nodiscard]] std::vector<std::string> TakenNames(RowKind a_kind,
                                                  const Names &a_names);

[[nodiscard]] std::string UniqueName(std::string_view a_stem,
                                     std::span<const std::string> a_taken);
[[nodiscard]] std::string ReferenceText(std::string_view a_name);
[[nodiscard]] std::string ReferenceName(std::string_view a_text);
[[nodiscard]] bool NameMatches(std::string_view a_name,
                               std::string_view a_filter) noexcept;
[[nodiscard]] std::string GeometryLabel(std::string_view a_name,
                                        std::string_view a_armorName);
}
