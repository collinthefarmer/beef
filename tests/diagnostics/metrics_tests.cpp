// GPL-3.0-only with the additional permission in COPYING.md.
#include "diagnostics/Metrics.h"
#include "test_support.h"

#include <chrono>
#include <thread>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Equal;

namespace {
void TestRatesAccumulateAndDrain() {
  static_cast<void>(Metrics::Drain());
  Metrics::CountRefresh(1000);
  Metrics::CountRefresh(3000);
  Metrics::CountSinkAdd();
  Metrics::CountSinkRemove();
  Metrics::CountSinkRemove();
  Metrics::CountReadback(500);
  const Metrics::Snapshot first = Metrics::Drain();
  Equal(first.refreshes, std::uint64_t{2}, "two refreshes counted");
  Equal(first.refreshMicros, std::uint64_t{4000}, "refresh micros summed");
  Equal(first.refreshMaxMicros, std::uint64_t{3000}, "refresh max kept");
  Equal(first.sinkAdds, std::uint64_t{1}, "one sink add");
  Equal(first.sinkRemoves, std::uint64_t{2}, "two sink removes");
  Equal(first.readbacks, std::uint64_t{1}, "one readback");
  Equal(first.readbackMaxMicros, std::uint64_t{500}, "readback max kept");
  Check(!first.Quiet(), "a snapshot with activity is not quiet");
  const Metrics::Snapshot second = Metrics::Drain();
  Equal(second.refreshes, std::uint64_t{0}, "drain resets the rates");
  Equal(second.refreshMaxMicros, std::uint64_t{0}, "drain resets the maxima");
  Check(second.Quiet(), "a drained snapshot with no activity is quiet");
}

void TestGaugesAndPeaksPersist() {
  Metrics::CountTargetCreated(100);
  Metrics::CountTargetCreated(200);
  Metrics::CountTargetDestroyed(100);
  const Metrics::Snapshot snapshot = Metrics::Drain();
  Equal(snapshot.targets, std::uint64_t{1}, "one target remains");
  Equal(snapshot.targetsPeak, std::uint64_t{2}, "the target peak is kept");
  Equal(snapshot.targetBytes, std::uint64_t{200}, "bytes follow the survivor");
  Equal(snapshot.targetBytesPeak, std::uint64_t{300}, "the byte peak is kept");
  const Metrics::Snapshot later = Metrics::Drain();
  Equal(later.targets, std::uint64_t{1}, "gauges survive a drain");
  Equal(later.targetsPeak, std::uint64_t{2}, "peaks survive a drain");
  Check(later.Quiet(), "gauges alone leave a snapshot quiet");
  Metrics::CountTargetDestroyed(200);
}

void TestStepExecutionsCountPerKind() {
  static_cast<void>(Metrics::Drain());
  Metrics::CountStepExecution(3);
  Metrics::CountStepExecution(3);
  Metrics::CountStepExecution(16);
  Metrics::CountStepExecution(Metrics::kStepKindSlots);
  const Metrics::Snapshot counted = Metrics::Drain();
  Equal(counted.stepExecutions, std::uint64_t{4},
        "every execution counts in the total");
  Equal(counted.stepExecutionsByKind[3], std::uint64_t{2},
        "executions of one kind accumulate");
  Equal(counted.stepExecutionsByKind[16], std::uint64_t{1},
        "each kind has its own count");
  const Metrics::Snapshot drained = Metrics::Drain();
  Equal(drained.stepExecutionsByKind[3], std::uint64_t{0},
        "drain resets the counts per kind");
}

void TestMippedBytes() {
  Equal(Metrics::MippedRgbaBytes(512), std::uint64_t{512} * 512 * 4 * 4 / 3,
        "a 512 target's bytes include the mip chain");
  Equal(Metrics::MippedRgbaBytes(0), std::uint64_t{0},
        "a zero side is zero bytes");
}
}

void TestPhasesSpikesAndHitches() {
  static_cast<void>(Metrics::Drain());
  Metrics::CountPhase(Metrics::Phase::kRender, 700);
  Metrics::CountPhase(Metrics::Phase::kRender, 300);
  Metrics::CountPhase(Metrics::Phase::kFrame, 20000);
  Metrics::CountPhase(Metrics::Phase::kFrame, 5000);
  Metrics::CountPhase(Metrics::Phase::kSignals, 40000);
  Metrics::CountFrame();
  std::this_thread::sleep_for(std::chrono::milliseconds(60));
  Metrics::CountFrame();
  const Metrics::Snapshot measured = Metrics::Drain();
  Equal(measured.Of(Metrics::Phase::kRender).micros, std::uint64_t{1000},
        "a phase sums its calls");
  Equal(measured.Of(Metrics::Phase::kRender).maxMicros, std::uint64_t{700},
        "a phase keeps its longest call");
  Equal(measured.pluginSpikes[0], std::uint64_t{1},
        "one frame took the plugin over 8 ms");
  Equal(measured.pluginSpikes[1], std::uint64_t{1},
        "one frame took the plugin over 16 ms");
  Equal(measured.pluginSpikes[2], std::uint64_t{0},
        "no frame took the plugin over 33 ms");
  Check(measured.hitches[0] >= 1 && measured.intervalMaxMicros >= 50000,
        "a frame interval over 50 ms counts as a hitch");
  Equal(measured.hitches[1], std::uint64_t{0},
        "a 60 ms interval is not a 100 ms hitch");
  const Metrics::Snapshot drained = Metrics::Drain();
  Check(drained.pluginSpikes[0] == 0 && drained.hitches[0] == 0 &&
            drained.intervalMaxMicros == 0,
        "draining resets spikes and hitches");
}

int main() {
  TestPhasesSpikesAndHitches();
  TestRatesAccumulateAndDrain();
  TestGaugesAndPeaksPersist();
  TestStepExecutionsCountPerKind();
  TestMippedBytes();
  return test::Finish("diagnostics_measure");
}
