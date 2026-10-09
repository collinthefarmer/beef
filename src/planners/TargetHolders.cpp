// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/TargetHolders.h"

#include <utility>

namespace BetterEnchantmentEffects {
HolderSet &SetOf(HolderSets &a_sets, TargetHolder a_holder) noexcept {
  const std::size_t index = std::to_underlying(a_holder);
  return a_sets[index < kReachingHolders ? index : 0];
}

const HolderSet &SetOf(const HolderSets &a_sets,
                       TargetHolder a_holder) noexcept {
  const std::size_t index = std::to_underlying(a_holder);
  return a_sets[index < kReachingHolders ? index : 0];
}

HolderTallies TallyHolders(std::span<const LiveTarget> a_live,
                           const HolderSets &a_sets) {
  HolderTallies tallies{};
  for (const LiveTarget &target : a_live) {
    std::size_t holder = kReachingHolders;
    for (std::size_t i = 0; i < kReachingHolders; ++i) {
      if (a_sets[i].contains(target.id)) {
        holder = i;
        break;
      }
    }
    ++tallies[holder].targets;
    tallies[holder].bytes += target.bytes;
  }
  return tallies;
}

const HeldTally &TallyOf(const HolderTallies &a_tallies,
                         TargetHolder a_holder) noexcept {
  return a_tallies[std::to_underlying(a_holder)];
}

std::string_view HolderName(TargetHolder a_holder) noexcept {
  switch (a_holder) {
  case TargetHolder::kInstance:
    return "instance";
  case TargetHolder::kOutput:
    return "output";
  case TargetHolder::kSlot:
    return "slot";
  case TargetHolder::kMaterialInput:
    return "material_input";
  case TargetHolder::kRetired:
    return "retired";
  case TargetHolder::kCache:
    return "cache";
  case TargetHolder::kPreview:
    return "preview";
  case TargetHolder::kPool:
    return "pool";
  case TargetHolder::kResident:
    return "resident";
  case TargetHolder::kUnreached:
    return "unreached";
  }
  return "unreached";
}
}
