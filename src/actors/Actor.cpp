// GPL-3.0-only with the additional permission in COPYING.md.
#include "actors/Actor.h"

#include <algorithm>

namespace BetterEnchantmentEffects::Actors {
namespace {
template <class Entry, class Ref>
const Entry *FindByRef(const std::vector<Entry> &a_entries,
                       const Ref &a_ref) noexcept {
  const auto found = std::ranges::find(a_entries, a_ref, &Entry::ref);
  return found == a_entries.end() ? nullptr : &*found;
}
}

const PlannedInstance *Find(const ActorPlan &a_plan,
                            const InstanceRef &a_ref) noexcept {
  return FindByRef(a_plan.instances, a_ref);
}

const PlannedGeometry *Find(const ActorPlan &a_plan,
                            const GeometryRef &a_ref) noexcept {
  return FindByRef(a_plan.geometries, a_ref);
}

const PlannedPlacement *Find(const ActorPlan &a_plan,
                             const PlacementRef &a_ref) noexcept {
  return FindByRef(a_plan.placements, a_ref);
}
}
