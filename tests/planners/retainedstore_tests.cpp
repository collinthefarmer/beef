// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/RetainedStore.h"
#include "test_support.h"

#include <array>
#include <limits>
#include <memory>
#include <string>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
struct Target {
  int size = 0;
  std::shared_ptr<int> lease;
};
using Targets = RetainedStore<int, std::unique_ptr<Target>>;

std::unique_ptr<Target> Make(int a_size) {
  return std::make_unique<Target>(a_size, std::make_shared<int>(a_size));
}

constexpr RetentionPolicy kPool{
    .maxCount = 2, .maxBytes = 100, .maxIdle = 5000};

void StepOutputsFollowTheirRetention() {
  Check(StepOutputRetention(true, 10000) == Retention::kRenew,
        "a step output held elsewhere is renewed however long it was idle");
  Check(StepOutputRetention(false, kStepOutputIdleMs) == Retention::kKeep,
        "an unheld step output is kept up to the idle limit");
  Check(StepOutputRetention(false, kStepOutputIdleMs + 1) == Retention::kEvict,
        "an unheld step output is evicted past the idle limit");
}

void CountAndBytesEvictTheOldest() {
  Targets pool{kPool};
  auto first = Make(10);
  const std::weak_ptr<int> oldest = first->lease;
  Check(!pool.Insert(10, std::move(first), 40, 0) &&
            !pool.Insert(20, Make(20), 60, 1) &&
            pool.Sweep(1, KeepNone{}).empty(),
        "values fit exact count and byte bounds");
  Check(!pool.Insert(30, Make(30), 1, 2), "a third value is taken in");
  Targets::Evicted evicted = pool.Sweep(2, KeepNone{});
  Check(evicted.size() == 1 && evicted.front()->size == 10 &&
            pool.Size() == 2 && pool.Bytes() == 61,
        "the count bound evicts the oldest value");
  Check(!oldest.expired(), "the store does not destroy what it evicts");
  evicted.clear();
  Check(oldest.expired(), "the caller destroys what the store evicted");
  Check(!pool.Insert(40, Make(40), 100, 3) &&
            pool.Sweep(3, KeepNone{}).size() == 2 && pool.Bytes() == 100,
        "the byte bound evicts every older value the newest needs room for");
}

void OversizeAndZeroCapacityAreReturned() {
  Targets pool{kPool};
  const std::optional<std::unique_ptr<Target>> oversize =
      pool.Insert(1, Make(1), std::numeric_limits<std::uint64_t>::max(), 0);
  Check(oversize && *oversize && (*oversize)->size == 1 && pool.Size() == 0 &&
            pool.Bytes() == 0,
        "a value over the byte bound is returned without overflow");
  Targets none{{.maxCount = 0, .maxBytes = 100, .maxIdle = 5000}};
  Check(none.Insert(1, Make(1), 1, 0).has_value() && none.Size() == 0,
        "a store without room returns every value");
}

void TakeTransfersOwnership() {
  Targets pool{kPool};
  auto first = Make(10);
  const std::weak_ptr<int> lease = first->lease;
  Check(!pool.Insert(10, std::move(first), 40, 0), "a value fits");
  std::optional<std::unique_ptr<Target>> taken = pool.Take(10);
  Check(taken && pool.Size() == 0 && pool.Bytes() == 0 && !lease.expired(),
        "taking a key transfers ownership and its bytes");
  Check(!pool.Take(99), "an absent key takes nothing");
  Check(!pool.Insert(10, std::move(*taken), 40, 1),
        "a taken value can return to the store");
  Check(!pool.Insert(10, Make(11), 40, 2) && pool.Take(10).value()->size == 10,
        "a key held twice gives its oldest value first");
  Targets::Evicted cleared = pool.Clear();
  Check(cleared.size() == 1 && pool.Size() == 0 && pool.Bytes() == 0,
        "clearing returns every value");
}

void IdleValuesExpire() {
  Targets pool{{.maxCount = 4, .maxBytes = 100, .maxIdle = 5000}};
  Check(!pool.Insert(10, Make(10), 10, 1000) &&
            !pool.Insert(20, Make(20), 10, 3000),
        "two values are taken in");
  Check(pool.Sweep(5999, KeepNone{}).empty() && pool.Size() == 2,
        "nothing expires before its idle age");
  const Targets::Evicted expired = pool.Sweep(6000, KeepNone{});
  Check(expired.size() == 1 && expired.front()->size == 10 &&
            pool.Size() == 1 && pool.Bytes() == 10,
        "a value idle for the age bound expires");
  Check(pool.Sweep(500, KeepNone{}).empty() && pool.Size() == 1,
        "a clock behind the last use expires nothing");
  Check(pool.Sweep(5499, KeepNone{}).empty() &&
            pool.Sweep(5500, KeepNone{}).size() == 1,
        "a clock behind the last use restarts the idle age");
}

void KeptValuesAreRenewedAndExemptFromTheCount() {
  RetainedStore<int, std::shared_ptr<int>> cache{
      {.maxCount = 2, .maxBytes = kNoByteBound, .maxIdle = 30}};
  cache.Get(1, 0) = std::make_shared<int>(42);
  const std::shared_ptr<int> held = *cache.Find(1);
  const std::array keep{1};
  const auto kept = [&keep](const int &a_key, const std::shared_ptr<int> &) {
    return std::ranges::contains(keep, a_key);
  };
  for (int i = 2; i <= 8; ++i) {
    cache.Get(i, static_cast<Millis>(i)) = std::make_shared<int>(i);
  }
  const auto pressure = cache.Sweep(10, kept);
  Check(pressure.size() == 5 && cache.Size() == 3 && cache.Find(1) &&
            cache.Find(7) && cache.Find(8) && !cache.Find(2),
        "the count keeps kept values and the newest unkept values");
  Check(cache.Sweep(100, kept).size() == 2 && cache.Size() == 1,
        "kept values survive the age bound");
  Check(cache.Sweep(129, KeepNone{}).empty() && cache.Find(1),
        "a value no longer kept has the age bound from its last renewal");
  Check(cache.Sweep(130, KeepNone{}).size() == 1 && cache.Size() == 0 &&
            *held == 42,
        "eviction does not invalidate a value held elsewhere");
  RetainedStore<int, std::shared_ptr<int>> one{
      {.maxCount = 1, .maxBytes = kNoByteBound, .maxIdle = 30}};
  one.Get(1, 0);
  one.Get(2, 1);
  one.Get(1, 2);
  Check(one.Sweep(3, KeepNone{}).size() == 1 && one.Find(1) && !one.Find(2),
        "a use renews a value's place in the eviction order");
  RetainedStore<int, std::shared_ptr<int>> zero{
      {.maxCount = 0, .maxBytes = kNoByteBound, .maxIdle = 30}};
  zero.Get(1, 0);
  Check(zero.Sweep(4, kept).empty() && zero.Find(1),
        "kept values survive a zero count bound");
}

void UseRenewsWithoutAdding() {
  RetainedStore<int, float> means{
      {.maxCount = 4, .maxBytes = kNoByteBound, .maxIdle = 30}};
  Check(!means.Use(1, 0) && means.Size() == 0,
        "using an absent key adds nothing");
  Check(!means.Insert(1, 0.25f, 0, 0), "a value is taken in");
  const float *used = means.Use(1, 20);
  Check(used && *used == 0.25f, "using a key returns its value");
  Check(means.Sweep(49, KeepNone{}).empty() && means.Find(1),
        "a use renews the value's idle age");
}

void FailureRecordsExpireToo() {
  RetainedStore<int, std::string> failures{
      {.maxCount = 64, .maxBytes = kNoByteBound, .maxIdle = 30}};
  failures.Get(1, 0) = "temporary readback failure";
  Check(failures.Sweep(30, KeepNone{}).size() == 1 && !failures.Find(1),
        "an unkept failure record expires at the age bound");
}
}

int main() {
  StepOutputsFollowTheirRetention();
  CountAndBytesEvictTheOldest();
  OversizeAndZeroCapacityAreReturned();
  TakeTransfersOwnership();
  IdleValuesExpire();
  KeptValuesAreRenewedAndExemptFromTheCount();
  UseRenewsWithoutAdding();
  FailureRecordsExpireToo();
  return test::Finish("planners_retainedstore");
}
