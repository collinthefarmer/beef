// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/GpuReduction.h"
#include "test_support.h"
#include <limits>
using namespace BetterEnchantmentEffects;
using test::Check;
int main() {
  const auto levels = ReductionLevels({2048, 2048});
  Check(levels ==
            std::vector<ReductionExtent>{
                {512, 512}, {128, 128}, {32, 32}, {8, 8}, {2, 2}, {1, 1}},
        "a square field reduces by blocks of four down to one texel");
  const auto uneven = ReductionLevels({100, 3});
  Check(uneven == std::vector<ReductionExtent>{{25, 1}, {7, 1}, {2, 1}, {1, 1}},
        "partial blocks round up so every texel is covered");
  Check(ReductionLevels({0, 64}).empty() && ReductionLevels({8192, 64}).empty(),
        "empty or oversized fields have no reduction chain");

  const ReductionExtent field{4, 2};
  for (const auto [kind, expected] :
       std::array{std::pair{ReductionKind::kMean, 1.5f},
                  std::pair{ReductionKind::kSum, 12.0f},
                  std::pair{ReductionKind::kMinimum, 12.0f},
                  std::pair{ReductionKind::kMaximum, 12.0f}}) {
    const auto result = DecodeReduction(kind, ValueType::kScalar,
                                        {12.0f, 7.0f, 7.0f, 0.0f}, field);
    Check(result && AsScalar(*result) == expected,
          "only the mean divides the reduced texel by the texel count");
  }
  const auto vector = DecodeReduction(ReductionKind::kMean, ValueType::kVec3,
                                      {8.0f, -16.0f, 4.0f, 0.0f}, field);
  Check(vector && *vector == Value{Vec3{1, -2, 0.5f}},
        "vector means are component-wise");
  const auto pair = DecodeReduction(ReductionKind::kMaximum, ValueType::kVec2,
                                    {3.0f, 4.0f, 9.0f, 0.0f}, field);
  Check(pair && *pair == Value{Vec2{3, 4}},
        "components beyond the value type are ignored");
  Check(!DecodeReduction(ReductionKind::kMean, ValueType::kScalar,
                         {0.0f, 0.0f, 0.0f, 1.0f}, field),
        "a flagged non-finite sample fails rather than defaulting");
  Check(!DecodeReduction(ReductionKind::kSum, ValueType::kScalar,
                         {std::numeric_limits<float>::infinity(), 0, 0, 0},
                         field),
        "an overflowed sum fails");
  Check(!DecodeReduction(ReductionKind::kMean, ValueType::kScalar,
                         {1.0f, 0, 0, std::numeric_limits<float>::quiet_NaN()},
                         field),
        "an unreadable flag fails");
  const auto zero = DecodeReduction(ReductionKind::kMinimum, ValueType::kScalar,
                                    {-0.0f, 0, 0, 0}, field);
  Check(zero && !std::signbit(AsScalar(*zero)), "signed zero is canonicalized");

  ReadbackRing ring;
  const auto first = ReserveReadback(ring);
  const auto second = ReserveReadback(ring);
  const auto third = ReserveReadback(ring);
  Check(PendingNewestFirst(ring) == std::vector{third, second, first},
        "pending readbacks are polled newest first");
  const auto fourth = ReserveReadback(ring);
  Check(fourth == first && PendingNewestFirst(ring).size() == kReadbackSlots,
        "a full ring reuses the slot of its oldest readback");
  Check(AcceptReadback(ring, second) &&
            PendingNewestFirst(ring) == std::vector{fourth, third},
        "an accepted readback discards nothing newer");
  Check(AcceptReadback(ring, fourth) && PendingNewestFirst(ring).empty(),
        "an accepted readback discards every older pending readback");
  const auto older = ReserveReadback(ring);
  const auto newer = ReserveReadback(ring);
  Check(AcceptReadback(ring, newer) && !AcceptReadback(ring, older),
        "a completion older than the accepted one is ignored");
  const auto dropped = ReserveReadback(ring);
  DropReadback(ring, dropped);
  Check(PendingNewestFirst(ring).empty() && !AcceptReadback(ring, dropped),
        "a dropped readback is no longer pending");
  Check(!AcceptReadback(ring, kReadbackSlots),
        "an out-of-range slot is ignored");
  return test::Finish("gpu reduction");
}
