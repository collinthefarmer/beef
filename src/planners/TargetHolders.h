// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <unordered_set>

namespace BetterEnchantmentEffects {
enum class TargetHolder : std::uint8_t {
  kInstance,
  kOutput,
  kSlot,
  kMaterialInput,
  kRetired,
  kCache,
  kPreview,
  kPool,
  kResident,
  kUnreached
};
inline constexpr std::size_t kReachingHolders = 9;
inline constexpr std::array<TargetHolder, kReachingHolders + 1> kTargetHolders{
    TargetHolder::kInstance, TargetHolder::kOutput,
    TargetHolder::kSlot,     TargetHolder::kMaterialInput,
    TargetHolder::kRetired,  TargetHolder::kCache,
    TargetHolder::kPreview,  TargetHolder::kPool,
    TargetHolder::kResident, TargetHolder::kUnreached};

struct LiveTarget {
  std::uintptr_t id = 0;
  std::uint64_t bytes = 0;
  std::uint32_t side = 0;
};

using HolderSet = std::unordered_set<std::uintptr_t>;
using HolderSets = std::array<HolderSet, kReachingHolders>;

struct HeldTally {
  std::size_t targets = 0;
  std::uint64_t bytes = 0;
  [[nodiscard]] bool operator==(const HeldTally &) const = default;
};
using HolderTallies = std::array<HeldTally, kReachingHolders + 1>;

[[nodiscard]] HolderSet &SetOf(HolderSets &a_sets,
                               TargetHolder a_holder) noexcept;
[[nodiscard]] const HolderSet &SetOf(const HolderSets &a_sets,
                                     TargetHolder a_holder) noexcept;
[[nodiscard]] HolderTallies TallyHolders(std::span<const LiveTarget> a_live,
                                         const HolderSets &a_sets);
[[nodiscard]] const HeldTally &TallyOf(const HolderTallies &a_tallies,
                                       TargetHolder a_holder) noexcept;
[[nodiscard]] std::string_view HolderName(TargetHolder a_holder) noexcept;
}
