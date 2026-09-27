// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "recipe/RecipeGraph.h"

#include <compare>
#include <functional>

namespace BetterEnchantmentEffects {
struct ValueIdentity {
  std::string canonical;
  [[nodiscard]] auto operator<=>(const ValueIdentity &) const = default;
};
struct ValueBindings {
  std::function<std::expected<std::string, std::string>(const ExternalSource &)>
      external;
  std::function<std::string(NodeId)> state;
};
[[nodiscard]] std::expected<ValueIdentity, std::string>
IdentifyValue(const RecipeGraph &graph, OutputRef value,
              const ValueBindings &bindings);
}
