// GPL-3.0-only with the additional permission in COPYING.md.
#include "diagnostics/Metrics.h"

#include <atomic>

namespace BetterEnchantmentEffects::Metrics {
namespace {
struct Counters {
  std::atomic<std::uint64_t> refreshes{0};
  std::atomic<std::uint64_t> refreshMicros{0};
  std::atomic<std::uint64_t> refreshMaxMicros{0};
  std::atomic<std::uint64_t> sinkAdds{0};
  std::atomic<std::uint64_t> sinkRemoves{0};
  std::atomic<std::uint64_t> readbacks{0};
  std::atomic<std::uint64_t> readbackMicros{0};
  std::atomic<std::uint64_t> readbackMaxMicros{0};
  std::atomic<std::uint64_t> targets{0};
  std::atomic<std::uint64_t> targetsPeak{0};
  std::atomic<std::uint64_t> targetBytes{0};
  std::atomic<std::uint64_t> targetBytesPeak{0};
  std::atomic<std::uint64_t> frames{0};
  std::atomic<std::uint64_t> renderEvaluations{0};
  std::atomic<std::uint64_t> stepExecutions{0};
  std::atomic<std::uint64_t> sharedStepHits{0};
  std::atomic<std::uint64_t> stepReleases{0};
  std::atomic<std::uint64_t> stepRestores{0};
  std::atomic<std::uint64_t> phaseMicros[3]{};
  std::atomic<std::uint64_t> phaseMaxMicros[3]{};
};

Counters &State() noexcept {
  static Counters counters;
  return counters;
}

void RaiseTo(std::atomic<std::uint64_t> &a_peak,
             std::uint64_t a_value) noexcept {
  std::uint64_t seen = a_peak.load(std::memory_order_relaxed);
  while (seen < a_value && !a_peak.compare_exchange_weak(
                               seen, a_value, std::memory_order_relaxed)) {
  }
}
}

void CountRefresh(std::uint64_t a_micros) noexcept {
  Counters &state = State();
  state.refreshes.fetch_add(1, std::memory_order_relaxed);
  state.refreshMicros.fetch_add(a_micros, std::memory_order_relaxed);
  RaiseTo(state.refreshMaxMicros, a_micros);
}

void CountSinkAdd() noexcept {
  State().sinkAdds.fetch_add(1, std::memory_order_relaxed);
}

void CountSinkRemove() noexcept {
  State().sinkRemoves.fetch_add(1, std::memory_order_relaxed);
}

void CountReadback(std::uint64_t a_micros) noexcept {
  Counters &state = State();
  state.readbacks.fetch_add(1, std::memory_order_relaxed);
  state.readbackMicros.fetch_add(a_micros, std::memory_order_relaxed);
  RaiseTo(state.readbackMaxMicros, a_micros);
}

void CountTargetCreated(std::uint64_t a_bytes) noexcept {
  Counters &state = State();
  const std::uint64_t targets =
      state.targets.fetch_add(1, std::memory_order_relaxed) + 1;
  const std::uint64_t bytes =
      state.targetBytes.fetch_add(a_bytes, std::memory_order_relaxed) + a_bytes;
  RaiseTo(state.targetsPeak, targets);
  RaiseTo(state.targetBytesPeak, bytes);
}

void CountTargetDestroyed(std::uint64_t a_bytes) noexcept {
  Counters &state = State();
  state.targets.fetch_sub(1, std::memory_order_relaxed);
  state.targetBytes.fetch_sub(a_bytes, std::memory_order_relaxed);
}

void CountFrame() noexcept {
  State().frames.fetch_add(1, std::memory_order_relaxed);
}
void CountRenderEvaluation() noexcept {
  State().renderEvaluations.fetch_add(1, std::memory_order_relaxed);
}
void CountSharedStepHit() noexcept {
  State().sharedStepHits.fetch_add(1, std::memory_order_relaxed);
}

void CountStepExecution() noexcept {
  State().stepExecutions.fetch_add(1, std::memory_order_relaxed);
}
void CountStepReleases(std::uint64_t a_count) noexcept {
  State().stepReleases.fetch_add(a_count, std::memory_order_relaxed);
}
void CountStepRestores(std::uint64_t a_count) noexcept {
  State().stepRestores.fetch_add(a_count, std::memory_order_relaxed);
}
void CountPhase(Phase a_phase, std::uint64_t a_micros) noexcept {
  const auto index = static_cast<std::size_t>(a_phase);
  if (index >= 3)
    return;
  Counters &state = State();
  state.phaseMicros[index].fetch_add(a_micros, std::memory_order_relaxed);
  RaiseTo(state.phaseMaxMicros[index], a_micros);
}
Snapshot Drain() noexcept {
  Counters &state = State();
  Snapshot out;
  out.refreshes = state.refreshes.exchange(0, std::memory_order_relaxed);
  out.refreshMicros =
      state.refreshMicros.exchange(0, std::memory_order_relaxed);
  out.refreshMaxMicros =
      state.refreshMaxMicros.exchange(0, std::memory_order_relaxed);
  out.sinkAdds = state.sinkAdds.exchange(0, std::memory_order_relaxed);
  out.sinkRemoves = state.sinkRemoves.exchange(0, std::memory_order_relaxed);
  out.readbacks = state.readbacks.exchange(0, std::memory_order_relaxed);
  out.readbackMicros =
      state.readbackMicros.exchange(0, std::memory_order_relaxed);
  out.readbackMaxMicros =
      state.readbackMaxMicros.exchange(0, std::memory_order_relaxed);
  out.targets = state.targets.load(std::memory_order_relaxed);
  out.targetsPeak = state.targetsPeak.load(std::memory_order_relaxed);
  out.targetBytes = state.targetBytes.load(std::memory_order_relaxed);
  out.targetBytesPeak = state.targetBytesPeak.load(std::memory_order_relaxed);
  out.frames = state.frames.exchange(0, std::memory_order_relaxed);
  out.renderEvaluations =
      state.renderEvaluations.exchange(0, std::memory_order_relaxed);
  out.stepExecutions =
      state.stepExecutions.exchange(0, std::memory_order_relaxed);
  out.sharedStepHits =
      state.sharedStepHits.exchange(0, std::memory_order_relaxed);
  out.stepReleases = state.stepReleases.exchange(0, std::memory_order_relaxed);
  out.stepRestores = state.stepRestores.exchange(0, std::memory_order_relaxed);
  PhaseTime *phases[3]{&out.frame, &out.tick, &out.snapshot};
  for (std::size_t i = 0; i < 3; ++i) {
    phases[i]->micros =
        state.phaseMicros[i].exchange(0, std::memory_order_relaxed);
    phases[i]->maxMicros =
        state.phaseMaxMicros[i].exchange(0, std::memory_order_relaxed);
  }
  return out;
}
}
