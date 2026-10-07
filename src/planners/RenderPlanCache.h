// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/RenderPlan.h"
#include "planners/RenderPlanKey.h"
#include "recipe/RecipeGraph.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace BetterEnchantmentEffects {
class RenderPlanCache {
public:
  [[nodiscard]] const RenderPlan *Find(const RenderPlanKey &a_key);
  void Store(RenderPlanKey a_key, RenderPlan a_plan,
             std::vector<std::shared_ptr<const RecipeGraph>> a_graphs);
  void Clear() noexcept;
  [[nodiscard]] std::size_t Size() const noexcept;

private:
  struct Entry {
    RenderPlan plan;
    std::vector<std::shared_ptr<const RecipeGraph>> graphs;
    std::uint64_t lastUse = 0;
  };
  static constexpr std::size_t kCapacity = 64;
  std::unordered_map<RenderPlanKey, Entry, RenderPlanKeyHash> entries_;
  std::uint64_t clock_ = 0;
};
}
