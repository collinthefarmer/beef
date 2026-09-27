// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "Core.h"

#include <array>
#include <expected>
#include <span>

namespace BetterEnchantmentEffects {
enum class ReductionKind { kMean, kSum, kMinimum, kMaximum };
class FieldReduction {
public:
  FieldReduction(ReductionKind kind, ValueType type);
  [[nodiscard]] std::expected<void, std::string> Add(const Value &sample);
  [[nodiscard]] std::expected<Value, std::string> Result() const;

private:
  ReductionKind kind_;
  ValueType type_;
  std::array<double, 3> values_{};
  std::size_t count_ = 0;
  std::string problem_;
};
[[nodiscard]] std::expected<Value, std::string>
ReduceValues(ReductionKind kind, ValueType type,
             std::span<const Value> samples);
}
