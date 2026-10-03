// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/RenderPlan.h"
#include "recipe/RecipeGraph.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace BetterEnchantmentEffects {
class RenderPlanCache {
public:
  [[nodiscard]] const RenderPlan *Find(const std::string &a_key);
  void Store(std::string a_key, RenderPlan a_plan,
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
  std::unordered_map<std::string, Entry> entries_;
  std::uint64_t clock_ = 0;
};
}
