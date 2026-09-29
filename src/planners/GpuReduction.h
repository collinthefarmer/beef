// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "Core.h"
#include "recipe/Reduction.h"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects {
inline constexpr std::uint32_t kReductionBlock = 4;
inline constexpr std::uint32_t kReductionMaxSide = 4096;
inline constexpr std::size_t kReadbackSlots = 3;
struct ReductionExtent {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  [[nodiscard]] bool operator==(const ReductionExtent &) const = default;
};
using ReducedTexel = std::array<float, 4>;
using ReductionResult = std::expected<Value, std::string>;
struct ReadbackRing {
  std::array<std::optional<std::uint64_t>, kReadbackSlots> pending{};
  std::uint64_t nextSequence = 1;
  std::uint64_t acceptedSequence = 0;
};
[[nodiscard]] std::vector<ReductionExtent>
ReductionLevels(ReductionExtent field);
[[nodiscard]] std::uint32_t ReductionComponents(ValueType type);
[[nodiscard]] ReductionResult DecodeReduction(ReductionKind kind,
                                              ValueType type,
                                              const ReducedTexel &texel,
                                              ReductionExtent field);
[[nodiscard]] std::size_t ReserveReadback(ReadbackRing &ring);
[[nodiscard]] std::vector<std::size_t>
PendingNewestFirst(const ReadbackRing &ring);
[[nodiscard]] bool AcceptReadback(ReadbackRing &ring, std::size_t slot);
void DropReadback(ReadbackRing &ring, std::size_t slot);
}
