// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/RenderPlanCache.h"

#include <algorithm>
#include <utility>

namespace BetterEnchantmentEffects {
const RenderPlan *RenderPlanCache::Find(const std::string &a_key) {
  const auto found = entries_.find(a_key);
  if (found == entries_.end()) {
    return nullptr;
  }
  found->second.lastUse = ++clock_;
  return &found->second.plan;
}

void RenderPlanCache::Store(
    std::string a_key, RenderPlan a_plan,
    std::vector<std::shared_ptr<const RecipeGraph>> a_graphs) {
  if (entries_.size() >= kCapacity && !entries_.contains(a_key)) {
    const auto oldest = std::ranges::min_element(
        entries_, {}, [](const std::pair<const std::string, Entry> &a_entry) {
          return a_entry.second.lastUse;
        });
    entries_.erase(oldest);
  }
  entries_.insert_or_assign(
      std::move(a_key),
      Entry{std::move(a_plan), std::move(a_graphs), ++clock_});
}

void RenderPlanCache::Clear() noexcept { entries_.clear(); }

std::size_t RenderPlanCache::Size() const noexcept { return entries_.size(); }
}
