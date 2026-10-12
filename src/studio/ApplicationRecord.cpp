// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/ApplicationRecord.h"

#include "Core.h"

#include <algorithm>
#include <array>

namespace BetterEnchantmentEffects {
namespace {
constexpr std::array<std::string_view, kApplicationPhaseCount> kPhaseNames{
    "queued",    "prepared",  "rendered",   "failed",
    "unmatched", "cancelled", "out of view"};
}

std::string_view ApplicationPhaseName(ApplicationPhase a_phase) noexcept {
  const auto index = IndexOf(a_phase);
  return index < kPhaseNames.size() ? kPhaseNames[index] : "unknown";
}

bool IsSettling(const ApplicationRecord &a_record) noexcept {
  return std::ranges::any_of(
      a_record.actors, [](const ApplicationActor &a_actor) {
        return a_actor.phase == ApplicationPhase::kQueued ||
               a_actor.phase == ApplicationPhase::kPrepared;
      });
}
}
