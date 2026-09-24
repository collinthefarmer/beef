// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/Clock.h"
#include "engine/InstanceTime.h"
#include "test_support.h"

#include <cmath>
#include <cstdint>
#include <limits>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Equal;

namespace {
void IndependentInstances() {
  CarriedTimes times;
  const CarriedTimeKey first{1, "glow", 100};
  const CarriedTimeKey second{1, "glow", 200};
  const CarriedTimeKey unenchanted{1, "glow", 0};
  const CarriedTimeKey otherActor{2, "glow", 100};
  const CarriedTimeKey otherRecipe{1, "pulse", 100};
  times.Remember(first, 1.0f, 1000);
  times.Remember(second, 2.0f, 1000);
  times.Remember(unenchanted, 3.0f, 1000);
  times.Remember(otherActor, 4.0f, 1000);
  times.Remember(otherRecipe, 5.0f, 1000);
  Equal(times.Size(), std::size_t{5},
        "each actor/recipe/enchantment has a slot");
  Equal(times.Take(second, 1500).value_or(-1), 2.0f,
        "reordered rebuild retains the second enchantment's phase");
  Equal(times.Take(first, 1500).value_or(-1), 1.0f,
        "the first enchantment's phase was not overwritten");
  Equal(times.Take(unenchanted, 1500).value_or(-1), 3.0f,
        "unenchanted identity stays separate");
  Equal(times.Take(otherActor, 1500).value_or(-1), 4.0f,
        "actors remain isolated");
  Equal(times.Take(otherRecipe, 1500).value_or(-1), 5.0f,
        "recipes remain isolated");
  Check(!times.Take(first, 1500), "carried time is consumed only once");
  Equal(times.Size(), std::size_t{0}, "consumed entries release their storage");
}

void Lifetime() {
  CarriedTimes times;
  const CarriedTimeKey key{1, "glow", 100};
  times.Remember(key, 1.0f, 100);
  times.Remember(key, 2.0f, 200);
  Equal(times.Size(), std::size_t{1},
        "repeat retirement replaces its own entry");
  times.Expire(200 + kCarryWindowMS);
  Equal(times.Take(key, 200 + kCarryWindowMS).value_or(-1), 2.0f,
        "the carry window includes its boundary and uses latest retirement");
  times.Remember(key, 3.0f, 100);
  Check(!times.Take(key, 101 + kCarryWindowMS), "late reapply starts fresh");
  Equal(times.Size(), std::size_t{0}, "expired lookup removes the entry");
  times.Remember(key, 3.0f, 100);
  times.Expire(101 + kCarryWindowMS);
  Equal(times.Size(), std::size_t{0},
        "maintenance releases entries without a reapply or live actor");
  times.Remember(key, 4.0f, 5000);
  times.Clear();
  Check(times.Size() == 0 && !times.Take(key, 5001),
        "session clearing prevents old-session continuation");
  times.Remember(key, 5.0f, 5001);
  Equal(times.Take(key, 5002).value_or(-1), 5.0f,
        "a new session can reuse the same identities");
}

void ClockWrap() {
  CarriedTimes times;
  const CarriedTimeKey key{1, "glow", 100};
  const std::uint32_t retired = std::numeric_limits<std::uint32_t>::max() - 999;
  times.Remember(key, 1.0f, retired);
  times.Expire(999);
  Equal(times.Take(key, 1000).value_or(-1), 1.0f,
        "short carry survives the millisecond clock wrapping");
  times.Remember(key, 1.0f, retired);
  times.Expire(1001);
  Equal(times.Size(), std::size_t{0}, "expiry remains correct across wrapping");
}

void BoundedRetention() {
  CarriedTimes times;
  for (std::uint32_t actor = 1; actor <= kMaxCarriedInstanceTimes; ++actor) {
    times.Remember({actor, "glow", 100}, 1.0f, 1000);
  }
  const CarriedTimeKey overflow{
      static_cast<std::uint32_t>(kMaxCarriedInstanceTimes + 1), "glow", 100};
  times.Remember(overflow, 2.0f, 1000);
  Equal(times.Size(), kMaxCarriedInstanceTimes, "retention has a hard bound");
  Check(!times.Take(overflow, 1001),
        "excess entries restart instead of growing storage");
  times.Remember({1, "glow", 100}, 3.0f, 1001);
  Equal(times.Take({1, "glow", 100}, 1002).value_or(-1), 3.0f,
        "existing entries can update at capacity");
  times.Expire(1001 + kCarryWindowMS);
  Equal(times.Size(), std::size_t{0}, "a retired crowd releases all entries");
  times.Remember(overflow, 4.0f, 4000);
  Equal(times.Take(overflow, 4001).value_or(-1), 4.0f,
        "retention recovers after expiry");
}

void SafeOffsets() {
  Equal(ClockOffsetMS(3.0f, InstanceSpeed(2.0f, 0.5f, 3.0f)).value_or(99),
        std::uint32_t{1000},
        "carry and scrub account for all three speed factors");
  Equal(ClockOffsetMS(0.0f, 1.0f).value_or(99), std::uint32_t{0},
        "zero phase is valid");
  Equal(ClockOffsetMS(1.2345f, 1.0f).value_or(99), std::uint32_t{1234},
        "sub-millisecond remainder is truncated");
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float infinity = std::numeric_limits<float>::infinity();
  for (const float seconds : {-1.0f, nan, infinity, -infinity}) {
    Check(!ClockOffsetMS(seconds, 1.0f),
          "invalid phase is refused before conversion");
  }
  for (const float speed : {0.0f, -1.0f, nan, infinity, -infinity}) {
    Check(!ClockOffsetMS(1.0f, speed),
          "invalid speed is refused before conversion");
  }
  Check(!ClockOffsetMS(std::numeric_limits<float>::max(),
                       std::numeric_limits<float>::denorm_min()),
        "extreme phase/speed ratio is refused before integer conversion");
  Check(!ClockOffsetMS(4294967.5f, 1.0f),
        "offset beyond uint32 range is refused");
  Equal(ClockOffsetMS(4294967.0f, 1.0f).value_or(99), std::uint32_t{4294967000},
        "representable offset near the upper bound is accepted");
  Check(!ClockOffsetMS(
            1.0f, InstanceSpeed(std::numeric_limits<float>::max(), 2.0f, 1.0f)),
        "overflowed combined speed is refused");
  CarriedTimes times;
  const CarriedTimeKey key{1, "glow", 100};
  for (const float seconds : {-1.0f, nan, infinity}) {
    times.Remember(key, 1.0f, 100);
    times.Remember(key, seconds, 101);
    Check(times.Size() == 0 && !times.Take(key, 102),
          "invalid retirement discards an older phase for the same identity");
  }
}
}

int main() {
  IndependentInstances();
  Lifetime();
  ClockWrap();
  BoundedRetention();
  SafeOffsets();
  return test::Finish("engine instance time");
}
