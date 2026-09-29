// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/GpuReduction.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace BetterEnchantmentEffects {
namespace {
std::uint32_t Reduced(std::uint32_t side) {
  return (side + kReductionBlock - 1) / kReductionBlock;
}
bool Finite(double value) {
  return std::isfinite(value) &&
         std::abs(value) <= std::numeric_limits<float>::max();
}
}
std::vector<ReductionExtent> ReductionLevels(ReductionExtent field) {
  std::vector<ReductionExtent> levels;
  if (field.width == 0 || field.height == 0 ||
      field.width > kReductionMaxSide || field.height > kReductionMaxSide)
    return levels;
  while (field.width > 1 || field.height > 1) {
    field = {Reduced(field.width), Reduced(field.height)};
    levels.push_back(field);
  }
  return levels;
}
std::uint32_t ReductionComponents(ValueType type) {
  switch (type) {
  case ValueType::kScalar:
    return 1;
  case ValueType::kVec2:
    return 2;
  case ValueType::kVec3:
    return 3;
  }
  return 0;
}
ReductionResult DecodeReduction(ReductionKind kind, ValueType type,
                                const ReducedTexel &texel,
                                ReductionExtent field) {
  const std::uint32_t components = ReductionComponents(type);
  if (components == 0)
    return std::unexpected("unsupported reduction value type");
  if (field.width == 0 || field.height == 0)
    return std::unexpected("reduction has no samples");
  if (!(texel[3] == 0.0f))
    return std::unexpected("reduction sample is not finite");
  const double count =
      static_cast<double>(field.width) * static_cast<double>(field.height);
  std::array<float, 3> result{};
  for (std::uint32_t i = 0; i < components; ++i) {
    const double value = kind == ReductionKind::kMean
                             ? static_cast<double>(texel[i]) / count
                             : static_cast<double>(texel[i]);
    if (!Finite(value))
      return std::unexpected("reduction result is not finite");
    result[i] = value == 0.0 ? 0.0f : static_cast<float>(value);
  }
  switch (type) {
  case ValueType::kScalar:
    return Value{result[0]};
  case ValueType::kVec2:
    return Value{Vec2{result[0], result[1]}};
  case ValueType::kVec3:
    return Value{Vec3{result[0], result[1], result[2]}};
  }
  return std::unexpected("unsupported reduction value type");
}
std::size_t ReserveReadback(ReadbackRing &ring) {
  std::size_t slot = 0;
  for (std::size_t i = 0; i < ring.pending.size(); ++i) {
    if (!ring.pending[i]) {
      slot = i;
      break;
    }
    if (*ring.pending[i] < *ring.pending[slot])
      slot = i;
  }
  ring.pending[slot] = ring.nextSequence++;
  return slot;
}
std::vector<std::size_t> PendingNewestFirst(const ReadbackRing &ring) {
  std::vector<std::size_t> slots;
  for (std::size_t slot = 0; slot < ring.pending.size(); ++slot)
    if (ring.pending[slot])
      slots.push_back(slot);
  std::ranges::sort(slots, [&](std::size_t a, std::size_t b) {
    return *ring.pending[a] > *ring.pending[b];
  });
  return slots;
}
bool AcceptReadback(ReadbackRing &ring, std::size_t slot) {
  if (slot >= ring.pending.size() || !ring.pending[slot])
    return false;
  const std::uint64_t sequence = *ring.pending[slot];
  ring.pending[slot].reset();
  if (sequence <= ring.acceptedSequence)
    return false;
  ring.acceptedSequence = sequence;
  for (auto &pending : ring.pending)
    if (pending && *pending < sequence)
      pending.reset();
  return true;
}
void DropReadback(ReadbackRing &ring, std::size_t slot) {
  if (slot < ring.pending.size())
    ring.pending[slot].reset();
}
}
