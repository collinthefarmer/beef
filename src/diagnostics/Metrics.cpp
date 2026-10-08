// GPL-3.0-only with the additional permission in COPYING.md.
#include "diagnostics/Metrics.h"

#include <atomic>

namespace BetterEnchantmentEffects::Metrics {
namespace {
struct Counters {
  std::atomic<std::uint64_t> syncs{0};
  std::atomic<std::uint64_t> syncMicros{0};
  std::atomic<std::uint64_t> syncMaxMicros{0};
  std::atomic<std::uint64_t> syncFrameMaxMicros{0};
  std::atomic<std::uint64_t> syncDeferrals{0};
  std::atomic<std::uint64_t> installWaitMaxMillis{0};
  std::atomic<std::uint64_t> hiddenStacks{0};
  std::atomic<std::uint64_t> hiddenStacksTickMax{0};
  std::atomic<std::uint64_t> hiddenDrawMicros{0};
  std::atomic<std::uint64_t> hiddenDrawTickMaxMicros{0};
  std::atomic<std::uint64_t> sinkAdds{0};
  std::atomic<std::uint64_t> sinkRemoves{0};
  std::atomic<std::uint64_t> readbacks{0};
  std::atomic<std::uint64_t> readbackMicros{0};
  std::atomic<std::uint64_t> readbackMaxMicros{0};
  std::atomic<std::uint64_t> targets{0};
  std::atomic<std::uint64_t> targetsPeak{0};
  std::atomic<std::uint64_t> targetBytes{0};
  std::atomic<std::uint64_t> targetBytesPeak{0};
  std::atomic<std::uint64_t> targetsCreated{0};
  std::atomic<std::uint64_t> targetBytesCreated{0};
  std::atomic<std::uint64_t> targetBytesDestroyed{0};
  std::atomic<std::uint64_t> frames{0};
  std::atomic<std::uint64_t> renderEvaluations{0};
  std::atomic<std::uint64_t> stepExecutions{0};
  std::atomic<std::uint64_t> stepExecutionsByKind[kStepKindSlots]{};
  std::atomic<std::uint64_t> stepCacheHits{0};
  std::atomic<std::uint64_t> stepReleases{0};
  std::atomic<std::uint64_t> stepRestores{0};
  std::atomic<std::uint64_t> sectionMicros[kSectionCount]{};
  std::atomic<std::uint64_t> sectionMaxMicros[kSectionCount]{};
  std::atomic<std::uint64_t> pluginSpikes[kPluginSpikeMicros.size()]{};
  std::atomic<std::uint64_t> hitches[kHitchMicros.size()]{};
  std::atomic<std::uint64_t> intervalMaxMicros{0};
  std::atomic<std::uint64_t> actorsInView{0};
  std::atomic<std::uint64_t> actorsOutOfView{0};
  std::atomic<std::int64_t> lastFrameMicros{0};
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

void CountSync(std::uint64_t a_micros) noexcept {
  Counters &state = State();
  state.syncs.fetch_add(1, std::memory_order_relaxed);
  state.syncMicros.fetch_add(a_micros, std::memory_order_relaxed);
  RaiseTo(state.syncMaxMicros, a_micros);
}

void CountSyncFrame(std::uint64_t a_micros) noexcept {
  RaiseTo(State().syncFrameMaxMicros, a_micros);
}

void CountSyncDeferral() noexcept {
  State().syncDeferrals.fetch_add(1, std::memory_order_relaxed);
}

void CountInstallWait(std::uint64_t a_millis) noexcept {
  RaiseTo(State().installWaitMaxMillis, a_millis);
}

void CountHiddenDraw(std::uint64_t a_stacks, std::uint64_t a_micros) noexcept {
  Counters &state = State();
  state.hiddenStacks.fetch_add(a_stacks, std::memory_order_relaxed);
  RaiseTo(state.hiddenStacksTickMax, a_stacks);
  state.hiddenDrawMicros.fetch_add(a_micros, std::memory_order_relaxed);
  RaiseTo(state.hiddenDrawTickMaxMicros, a_micros);
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
  state.targetsCreated.fetch_add(1, std::memory_order_relaxed);
  state.targetBytesCreated.fetch_add(a_bytes, std::memory_order_relaxed);
}

void CountTargetDestroyed(std::uint64_t a_bytes) noexcept {
  Counters &state = State();
  state.targets.fetch_sub(1, std::memory_order_relaxed);
  state.targetBytes.fetch_sub(a_bytes, std::memory_order_relaxed);
  state.targetBytesDestroyed.fetch_add(a_bytes, std::memory_order_relaxed);
}

void CountFrame() noexcept {
  CountFrameAt(std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
                   .count());
}

void CountActorView(bool a_inView) noexcept {
  Counters &state = State();
  (a_inView ? state.actorsInView : state.actorsOutOfView)
      .fetch_add(1, std::memory_order_relaxed);
}

void ForgetFrameInterval() noexcept {
  State().lastFrameMicros.store(0, std::memory_order_relaxed);
}

void CountFrameAt(std::int64_t a_nowMicros) noexcept {
  Counters &state = State();
  state.frames.fetch_add(1, std::memory_order_relaxed);
  const std::int64_t now = a_nowMicros;
  const std::int64_t last =
      state.lastFrameMicros.exchange(now, std::memory_order_relaxed);
  if (last == 0 || now <= last) {
    return;
  }
  const auto interval = static_cast<std::uint64_t>(now - last);
  RaiseTo(state.intervalMaxMicros, interval);
  for (std::size_t i = 0; i < kHitchMicros.size(); ++i) {
    if (interval > kHitchMicros[i]) {
      state.hitches[i].fetch_add(1, std::memory_order_relaxed);
    }
  }
}
void CountRenderEvaluation() noexcept {
  State().renderEvaluations.fetch_add(1, std::memory_order_relaxed);
}
void CountStepCacheHit() noexcept {
  State().stepCacheHits.fetch_add(1, std::memory_order_relaxed);
}

void CountStepExecution(std::size_t a_kind) noexcept {
  Counters &state = State();
  state.stepExecutions.fetch_add(1, std::memory_order_relaxed);
  if (a_kind < kStepKindSlots) {
    state.stepExecutionsByKind[a_kind].fetch_add(1, std::memory_order_relaxed);
  }
}
void CountStepReleases(std::uint64_t a_count) noexcept {
  State().stepReleases.fetch_add(a_count, std::memory_order_relaxed);
}
void CountStepRestores(std::uint64_t a_count) noexcept {
  State().stepRestores.fetch_add(a_count, std::memory_order_relaxed);
}
void CountSection(Section a_section, std::uint64_t a_micros) noexcept {
  const auto index = static_cast<std::size_t>(a_section);
  if (index >= kSectionCount)
    return;
  Counters &state = State();
  state.sectionMicros[index].fetch_add(a_micros, std::memory_order_relaxed);
  RaiseTo(state.sectionMaxMicros[index], a_micros);
  if (a_section != Section::kFrame) {
    return;
  }
  for (std::size_t i = 0; i < kPluginSpikeMicros.size(); ++i) {
    if (a_micros > kPluginSpikeMicros[i]) {
      state.pluginSpikes[i].fetch_add(1, std::memory_order_relaxed);
    }
  }
}
Snapshot Drain() noexcept {
  Counters &state = State();
  Snapshot out;
  out.syncs = state.syncs.exchange(0, std::memory_order_relaxed);
  out.syncMicros = state.syncMicros.exchange(0, std::memory_order_relaxed);
  out.syncMaxMicros =
      state.syncMaxMicros.exchange(0, std::memory_order_relaxed);
  out.syncFrameMaxMicros =
      state.syncFrameMaxMicros.exchange(0, std::memory_order_relaxed);
  out.syncDeferrals =
      state.syncDeferrals.exchange(0, std::memory_order_relaxed);
  out.installWaitMaxMillis =
      state.installWaitMaxMillis.exchange(0, std::memory_order_relaxed);
  out.hiddenStacks = state.hiddenStacks.exchange(0, std::memory_order_relaxed);
  out.hiddenStacksTickMax =
      state.hiddenStacksTickMax.exchange(0, std::memory_order_relaxed);
  out.hiddenDrawMicros =
      state.hiddenDrawMicros.exchange(0, std::memory_order_relaxed);
  out.hiddenDrawTickMaxMicros =
      state.hiddenDrawTickMaxMicros.exchange(0, std::memory_order_relaxed);
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
  out.targetsCreated =
      state.targetsCreated.exchange(0, std::memory_order_relaxed);
  out.targetBytesCreated =
      state.targetBytesCreated.exchange(0, std::memory_order_relaxed);
  out.targetBytesDestroyed =
      state.targetBytesDestroyed.exchange(0, std::memory_order_relaxed);
  out.frames = state.frames.exchange(0, std::memory_order_relaxed);
  out.renderEvaluations =
      state.renderEvaluations.exchange(0, std::memory_order_relaxed);
  out.stepExecutions =
      state.stepExecutions.exchange(0, std::memory_order_relaxed);
  for (std::size_t kind = 0; kind < kStepKindSlots; ++kind) {
    out.stepExecutionsByKind[kind] =
        state.stepExecutionsByKind[kind].exchange(0, std::memory_order_relaxed);
  }
  out.stepCacheHits =
      state.stepCacheHits.exchange(0, std::memory_order_relaxed);
  out.stepReleases = state.stepReleases.exchange(0, std::memory_order_relaxed);
  out.stepRestores = state.stepRestores.exchange(0, std::memory_order_relaxed);
  for (std::size_t i = 0; i < kSectionCount; ++i) {
    out.sections[i].micros =
        state.sectionMicros[i].exchange(0, std::memory_order_relaxed);
    out.sections[i].maxMicros =
        state.sectionMaxMicros[i].exchange(0, std::memory_order_relaxed);
  }
  for (std::size_t i = 0; i < kPluginSpikeMicros.size(); ++i) {
    out.pluginSpikes[i] =
        state.pluginSpikes[i].exchange(0, std::memory_order_relaxed);
  }
  for (std::size_t i = 0; i < kHitchMicros.size(); ++i) {
    out.hitches[i] = state.hitches[i].exchange(0, std::memory_order_relaxed);
  }
  out.intervalMaxMicros =
      state.intervalMaxMicros.exchange(0, std::memory_order_relaxed);
  out.actorsInView = state.actorsInView.exchange(0, std::memory_order_relaxed);
  out.actorsOutOfView =
      state.actorsOutOfView.exchange(0, std::memory_order_relaxed);
  return out;
}
}
