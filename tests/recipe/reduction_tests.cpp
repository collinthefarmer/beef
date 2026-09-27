// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/Reduction.h"
#include "test_support.h"
#include <limits>
using namespace BetterEnchantmentEffects;
using test::Check;
int main() {
  const std::vector<Value> values{-2.0f, 5.0f, 3.0f};
  for (const auto [kind, expected] :
       std::array{std::pair{ReductionKind::kMean, 2.0f},
                  std::pair{ReductionKind::kSum, 6.0f},
                  std::pair{ReductionKind::kMinimum, -2.0f},
                  std::pair{ReductionKind::kMaximum, 5.0f}}) {
    const auto result = ReduceValues(kind, ValueType::kScalar, values);
    Check(result && AsScalar(*result) == expected,
          "scalar reductions preserve signed and greater-than-one samples");
  }
  const std::vector<Value> vectors{Vec3{-2, 8, 1}, Vec3{4, 2, 3}};
  auto mean = ReduceValues(ReductionKind::kMean, ValueType::kVec3, vectors);
  Check(mean && *mean == Value{Vec3{1, 5, 2}},
        "vector mean is component-wise without luminance conversion");
  auto minimum =
      ReduceValues(ReductionKind::kMinimum, ValueType::kVec3, vectors);
  Check(minimum && *minimum == Value{Vec3{-2, 2, 1}},
        "vector extrema preserve component type");
  const std::vector<Value> cancellation{100000000.0f, 1.0f, -100000000.0f};
  auto total =
      ReduceValues(ReductionKind::kSum, ValueType::kScalar, cancellation);
  Check(total && *total == Value{1.0f},
        "double accumulation retains a term lost by float accumulation");
  auto empty = ReduceValues(ReductionKind::kSum, ValueType::kVec2, {});
  Check(empty && *empty == Value{Vec2{}}, "empty sum returns the typed zero");
  for (auto kind :
       {ReductionKind::kMean, ReductionKind::kMinimum, ReductionKind::kMaximum})
    Check(!ReduceValues(kind, ValueType::kScalar, {}),
          "empty mean and extrema are unavailable");
  const std::vector<Value> mixed{1.0f, Vec2{1, 2}};
  Check(!ReduceValues(ReductionKind::kMean, ValueType::kScalar, mixed),
        "mixed sample types fail");
  const std::vector<Value> invalid{std::numeric_limits<float>::quiet_NaN()};
  Check(!ReduceValues(ReductionKind::kMean, ValueType::kScalar, invalid),
        "nonfinite samples fail rather than default");
  const std::vector<Value> overflow{std::numeric_limits<float>::max(),
                                    std::numeric_limits<float>::max()};
  Check(!ReduceValues(ReductionKind::kSum, ValueType::kScalar, overflow),
        "nonfinite final sums fail");
  Check(ReduceValues(ReductionKind::kMean, ValueType::kScalar, overflow)
            .has_value(),
        "large finite mean does not overflow its accumulator");
  const std::vector<Value> zero{-0.0f};
  auto z = ReduceValues(ReductionKind::kMinimum, ValueType::kScalar, zero);
  Check(z && !std::signbit(AsScalar(*z)), "signed zero is canonicalized");
  FieldReduction sticky{ReductionKind::kSum, ValueType::kScalar};
  Check(!sticky.Add(invalid[0]) && !sticky.Add(1.0f) && !sticky.Result(),
        "a failed sample cannot produce a partial success");
  return test::Finish("field reductions");
}
