// GPL-3.0-only with the additional permission in COPYING.md.
#include "diagnostics/GpuTiming.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects::GpuTiming;
using test::Check;

namespace {
ResolvedTick Tick(std::uint64_t frequency, std::vector<Stamps> spans,
                  bool disjoint = false) {
  return {disjoint, frequency, std::move(spans)};
}
}

int main() {
  Ring ring;
  Totals totals;
  const auto first = BeginTick(ring, totals);
  Check(first.has_value(), "a free slot records a tick");
  Check(!BeginTick(ring, totals).has_value() && totals.droppedTicks == 0,
        "a second tick cannot begin while one is recording");
  Check(OpenSpan(ring, totals, "tick") == std::optional<std::size_t>{0} &&
            OpenSpan(ring, totals, "EvaluateProgram 2048") ==
                std::optional<std::size_t>{1},
        "spans are numbered in the order they open");
  EndTick(ring);
  Check(!OpenSpan(ring, totals, "late").has_value(),
        "no span opens outside a recording tick");
  Check(PendingOldestFirst(ring) == std::vector{*first},
        "an ended tick waits for its queries");
  Resolve(ring, totals, *first, Tick(1'000'000, {{0, 16'000}, {1'000, 3'000}}));
  Check(totals.timedTicks == 1 &&
            totals.spans["tick"].nanoseconds == 16'000'000 &&
            totals.spans["EvaluateProgram 2048"].nanoseconds == 2'000'000,
        "a resolved tick converts timestamps to nanoseconds by frequency");
  Check(PendingOldestFirst(ring).empty(), "a resolved slot is free again");

  for (std::size_t i = 0; i < kFrameSlots; ++i) {
    Check(BeginTick(ring, totals).has_value(), "each free slot records");
    EndTick(ring);
  }
  Check(!BeginTick(ring, totals).has_value() && totals.droppedTicks == 1,
        "a tick with no free slot is dropped, never waited for");
  const auto pending = PendingOldestFirst(ring);
  Check(pending.size() == kFrameSlots, "all slots are pending");
  Resolve(ring, totals, pending[1], Tick(1'000'000, {}, true));
  Check(totals.discardedTicks == 1 &&
            PendingOldestFirst(ring).size() == kFrameSlots - 1,
        "a disjoint tick is discarded and frees its slot");
  Resolve(ring, totals, pending[0], Tick(0, {}));
  Check(totals.discardedTicks == 2, "a zero frequency is discarded");
  Check(PendingOldestFirst(ring).front() == pending[2],
        "later slots stay ordered after out-of-order resolution");
  Resolve(ring, totals, pending[0], Tick(1'000'000, {}));
  Check(totals.discardedTicks == 2 && totals.timedTicks == 1,
        "resolving a free slot changes nothing");

  Ring full;
  Totals limited;
  static_cast<void>(BeginTick(full, limited));
  for (std::size_t i = 0; i < kMaxSpans + 3; ++i)
    static_cast<void>(OpenSpan(full, limited, "step"));
  Check(limited.untimedSpans == 3, "spans beyond the limit are counted");
  EndTick(full);
  std::vector<Stamps> stamps(kMaxSpans, Stamps{10, 5});
  stamps.front() = {0, 1};
  Resolve(full, limited, 0, Tick(1'000'000'000, stamps));
  Check(limited.spans["step"].count == 1,
        "a span whose end precedes its begin is skipped");
  Resolve(full, limited, 0, Tick(1'000'000'000, {}));

  Ring mismatched;
  Totals counted;
  static_cast<void>(BeginTick(mismatched, counted));
  static_cast<void>(OpenSpan(mismatched, counted, "one"));
  EndTick(mismatched);
  Resolve(mismatched, counted, 0, Tick(1'000, {}));
  Check(counted.discardedTicks == 1 && counted.spans.empty(),
        "a tick whose span count does not match is discarded");

  const auto drained = Drain(totals);
  Check(drained.timedTicks == 1 && totals.timedTicks == 0 &&
            totals.spans.empty(),
        "draining returns the totals and resets them");
  return test::Finish("gpu timing");
}
