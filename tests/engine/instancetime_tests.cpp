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
float SecondsOf(const std::optional<CarriedInstance> &a_carried) {
  return a_carried ? a_carried->seconds : -1.0f;
}

void IndependentInstances() {
  CarriedInstances times;
  const CarriedInstanceKey first{1, "glow", 100};
  const CarriedInstanceKey second{1, "glow", 200};
  const CarriedInstanceKey unenchanted{1, "glow", 0};
  const CarriedInstanceKey otherActor{2, "glow", 100};
  const CarriedInstanceKey otherRecipe{1, "pulse", 100};
  const CarriedInstanceKey otherEffect{1, "glow", 100, "magicEffect:secondary"};
  times.Remember(otherEffect, {6.0f, {}}, 1000);
  times.Remember(first, {1.0f, {}}, 1000);
  times.Remember(second, {2.0f, {}}, 1000);
  times.Remember(unenchanted, {3.0f, {}}, 1000);
  times.Remember(otherActor, {4.0f, {}}, 1000);
  times.Remember(otherRecipe, {5.0f, {}}, 1000);
  Equal(times.Size(), std::size_t{6},
        "each actor/recipe/enchantment/effect context has a slot");
  Equal(SecondsOf(times.Take(second, 1500)), 2.0f,
        "reordered rebuild retains the second enchantment's phase");
  Equal(SecondsOf(times.Take(first, 1500)), 1.0f,
        "the first enchantment's phase was not overwritten");
  Equal(SecondsOf(times.Take(unenchanted, 1500)), 3.0f,
        "unenchanted identity stays separate");
  Equal(SecondsOf(times.Take(otherActor, 1500)), 4.0f,
        "actors remain isolated");
  Equal(SecondsOf(times.Take(otherRecipe, 1500)), 5.0f,
        "recipes remain isolated");
  Equal(SecondsOf(times.Take(otherEffect, 1500)), 6.0f,
        "different selected effects retain independent phases");
  Check(!times.Take(first, 1500), "carried time is consumed only once");
  Equal(times.Size(), std::size_t{0}, "consumed entries release their storage");
}

void Lifetime() {
  CarriedInstances times;
  const CarriedInstanceKey key{1, "glow", 100};
  times.Remember(key, {1.0f, {}}, 100);
  times.Remember(key, {2.0f, {}}, 200);
  Equal(times.Size(), std::size_t{1},
        "repeat retirement replaces its own entry");
  times.Expire(200 + kCarryWindowMS);
  Equal(SecondsOf(times.Take(key, 200 + kCarryWindowMS)), 2.0f,
        "the carry window includes its boundary and uses latest retirement");
  times.Remember(key, {3.0f, {}}, 100);
  Check(!times.Take(key, 101 + kCarryWindowMS), "late reapply starts fresh");
  Equal(times.Size(), std::size_t{0}, "expired lookup removes the entry");
  times.Remember(key, {3.0f, {}}, 100);
  times.Expire(101 + kCarryWindowMS);
  Equal(times.Size(), std::size_t{0},
        "maintenance releases entries without a reapply or live actor");
  times.Remember(key, {4.0f, {}}, 5000);
  times.Clear();
  Check(times.Size() == 0 && !times.Take(key, 5001),
        "session clearing prevents old-session continuation");
  times.Remember(key, {5.0f, {}}, 5001);
  Equal(SecondsOf(times.Take(key, 5002)), 5.0f,
        "a new session can reuse the same identities");
}

void ClockWrap() {
  CarriedInstances times;
  const CarriedInstanceKey key{1, "glow", 100};
  const std::uint32_t retired = std::numeric_limits<std::uint32_t>::max() - 999;
  times.Remember(key, {1.0f, {}}, retired);
  times.Expire(999);
  Equal(SecondsOf(times.Take(key, 1000)), 1.0f,
        "short carry survives the millisecond clock wrapping");
  times.Remember(key, {1.0f, {}}, retired);
  times.Expire(1001);
  Equal(times.Size(), std::size_t{0}, "expiry remains correct across wrapping");
}

void BoundedRetention() {
  CarriedInstances times;
  for (std::uint32_t actor = 1; actor <= kMaxCarriedInstances; ++actor) {
    times.Remember({actor, "glow", 100}, {1.0f, {}}, 1000);
  }
  const CarriedInstanceKey overflow{
      static_cast<std::uint32_t>(kMaxCarriedInstances + 1), "glow", 100};
  times.Remember(overflow, {2.0f, {}}, 1000);
  Equal(times.Size(), kMaxCarriedInstances, "retention has a hard bound");
  Check(!times.Take(overflow, 1001),
        "excess entries restart instead of growing storage");
  times.Remember({1, "glow", 100}, {3.0f, {}}, 1001);
  Equal(SecondsOf(times.Take({1, "glow", 100}, 1002)), 3.0f,
        "existing entries can update at capacity");
  times.Expire(1001 + kCarryWindowMS);
  Equal(times.Size(), std::size_t{0}, "a retired crowd releases all entries");
  times.Remember(overflow, {4.0f, {}}, 4000);
  Equal(SecondsOf(times.Take(overflow, 4001)), 4.0f,
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
  CarriedInstances times;
  const CarriedInstanceKey key{1, "glow", 100};
  for (const float seconds : {-1.0f, nan, infinity}) {
    times.Remember(key, {1.0f, {}}, 100);
    times.Remember(key, {seconds, {}}, 101);
    Check(times.Size() == 0 && !times.Take(key, 102),
          "invalid retirement discards an older phase for the same identity");
  }
}
void SignalMemoryTravelsWithTheClock() {
  CarriedInstances times;
  const CarriedInstanceKey key{1, "glow", 100};
  times.Remember(key, {2.0f, {{"pulse", SignalState::WaveState{0.25f}}}}, 1000);
  const std::optional<CarriedInstance> carried = times.Take(key, 1500);
  Check(carried && carried->seconds == 2.0f && carried->signals.size() == 1 &&
            carried->signals.front().name == "pulse",
        "the signal memory is taken with the clock");
}
}

int main() {
  IndependentInstances();
  Lifetime();
  ClockWrap();
  BoundedRetention();
  SafeOffsets();
  SignalMemoryTravelsWithTheClock();
  return test::Finish("engine instance time");
}
