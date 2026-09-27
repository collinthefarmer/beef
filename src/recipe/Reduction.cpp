// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/Reduction.h"

#include <limits>

namespace BetterEnchantmentEffects {
namespace {
std::size_t Components(ValueType type) {
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
}
FieldReduction::FieldReduction(ReductionKind kind, ValueType type)
    : kind_(kind), type_(type) {
  if (Components(type) == 0)
    problem_ = "unsupported reduction value type";
  switch (kind) {
  case ReductionKind::kMean:
  case ReductionKind::kSum:
  case ReductionKind::kMinimum:
  case ReductionKind::kMaximum:
    break;
  default:
    problem_ = "unsupported reduction kind";
    break;
  }
}
std::expected<void, std::string> FieldReduction::Add(const Value &sample) {
  if (!problem_.empty())
    return std::unexpected(problem_);
  if (TypeOf(sample) != type_)
    problem_ = "reduction samples have incompatible types";
  if (count_ >= 4096u * 4096u)
    problem_ = "reduction exceeds the sample limit";
  const Vec3 vector = AsVec3(sample);
  const std::array<float, 3> components{vector.x, vector.y, vector.z};
  for (std::size_t i = 0; i < Components(type_); ++i)
    if (!std::isfinite(components[i]))
      problem_ = "reduction sample is not finite";
  if (!problem_.empty())
    return std::unexpected(problem_);
  for (std::size_t i = 0; i < Components(type_); ++i) {
    const double value = components[i];
    switch (kind_) {
    case ReductionKind::kMean:
    case ReductionKind::kSum:
      values_[i] += value;
      break;
    case ReductionKind::kMinimum:
      values_[i] = count_ == 0 ? value : std::min(values_[i], value);
      break;
    case ReductionKind::kMaximum:
      values_[i] = count_ == 0 ? value : std::max(values_[i], value);
      break;
    }
  }
  ++count_;
  return {};
}
std::expected<Value, std::string> FieldReduction::Result() const {
  if (!problem_.empty())
    return std::unexpected(problem_);
  if (count_ == 0 && kind_ != ReductionKind::kSum)
    return std::unexpected("reduction has no samples");
  std::array<float, 3> result{};
  for (std::size_t i = 0; i < Components(type_); ++i) {
    const double value = kind_ == ReductionKind::kMean
                             ? values_[i] / static_cast<double>(count_)
                             : values_[i];
    if (!std::isfinite(value) ||
        std::abs(value) > std::numeric_limits<float>::max())
      return std::unexpected("reduction result is not finite");
    result[i] = value == 0.0 ? 0.0f : static_cast<float>(value);
  }
  switch (type_) {
  case ValueType::kScalar:
    return Value{result[0]};
  case ValueType::kVec2:
    return Value{Vec2{result[0], result[1]}};
  case ValueType::kVec3:
    return Value{Vec3{result[0], result[1], result[2]}};
  }
  return std::unexpected("unsupported reduction value type");
}
std::expected<Value, std::string> ReduceValues(ReductionKind kind,
                                               ValueType type,
                                               std::span<const Value> samples) {
  FieldReduction reduction{kind, type};
  for (const auto &sample : samples)
    if (auto added = reduction.Add(sample); !added)
      return std::unexpected(added.error());
  return reduction.Result();
}
}
