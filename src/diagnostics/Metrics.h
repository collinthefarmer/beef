// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "Core.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace BetterEnchantmentEffects::Metrics {
enum class Section {
  kFrame,
  kTick,
  kSnapshot,
  kSignals,
  kRender,
  kLights,
  kPreviews,
  kSweep,
  kRenderInputs,
  kRenderSlots,
  kRenderShell,
  kRenderEvaluate,
  kCull,
  kCount
};
inline constexpr std::size_t kSectionCount =
    static_cast<std::size_t>(Section::kCount);
inline constexpr Named<Section> kSections[]{
    {Section::kFrame, "frame"},
    {Section::kTick, "tick"},
    {Section::kSnapshot, "snapshot"},
    {Section::kSignals, "signals"},
    {Section::kRender, "render"},
    {Section::kLights, "lights"},
    {Section::kPreviews, "previews"},
    {Section::kSweep, "sweep"},
    {Section::kRenderInputs, "render_inputs"},
    {Section::kRenderSlots, "render_slots"},
    {Section::kRenderShell, "render_shell"},
    {Section::kRenderEvaluate, "render_evaluate"},
    {Section::kCull, "cull"}};
static_assert(Complete(kSections, kSectionCount));
inline constexpr std::array<std::uint64_t, 3> kPluginSpikeMicros{8000, 16000,
                                                                 33000};
inline constexpr std::array<std::uint64_t, 2> kHitchMicros{50000, 100000};
inline constexpr std::size_t kStepKindSlots = 32;
using StepKindCounts = std::array<std::uint64_t, kStepKindSlots>;
struct SectionTime {
  std::uint64_t micros = 0;
  std::uint64_t maxMicros = 0;
};
struct Snapshot {
  std::uint64_t syncs = 0;
  std::uint64_t syncMicros = 0;
  std::uint64_t syncMaxMicros = 0;
  std::uint64_t syncFrameMaxMicros = 0;
  std::uint64_t syncDeferrals = 0;
  std::uint64_t installWaitMaxMillis = 0;
  std::uint64_t hiddenStacks = 0;
  std::uint64_t hiddenStacksTickMax = 0;
  std::uint64_t hiddenDrawMicros = 0;
  std::uint64_t hiddenDrawTickMaxMicros = 0;
  std::uint64_t sinkAdds = 0;
  std::uint64_t sinkRemoves = 0;
  std::uint64_t readbacks = 0;
  std::uint64_t readbackMicros = 0;
  std::uint64_t readbackMaxMicros = 0;
  std::uint64_t targets = 0;
  std::uint64_t targetsPeak = 0;
  std::uint64_t targetBytes = 0;
  std::uint64_t targetBytesPeak = 0;
  std::uint64_t targetsCreated = 0;
  std::uint64_t targetBytesCreated = 0;
  std::uint64_t targetBytesDestroyed = 0;
  std::uint64_t frames = 0;
  std::uint64_t renderEvaluations = 0;
  std::uint64_t stepExecutions = 0;
  StepKindCounts stepExecutionsByKind{};
  std::uint64_t stepCacheHits = 0;
  std::uint64_t stepReleases = 0;
  std::uint64_t stepRestores = 0;
  std::array<SectionTime, kSectionCount> sections{};
  std::array<std::uint64_t, kPluginSpikeMicros.size()> pluginSpikes{};
  std::array<std::uint64_t, kHitchMicros.size()> hitches{};
  std::uint64_t intervalMaxMicros = 0;
  std::uint64_t actorsInView = 0;
  std::uint64_t actorsOutOfView = 0;
  [[nodiscard]] SectionTime Of(Section a_section) const noexcept {
    const auto index = static_cast<std::size_t>(a_section);
    return index < sections.size() ? sections[index] : SectionTime{};
  }
  [[nodiscard]] bool Quiet() const noexcept {
    return syncs == 0 && sinkAdds == 0 && sinkRemoves == 0 && readbacks == 0 &&
           renderEvaluations == 0;
  }
};

void CountSync(std::uint64_t a_micros) noexcept;
void CountSyncFrame(std::uint64_t a_micros) noexcept;
void CountSyncDeferral() noexcept;
void CountInstallWait(std::uint64_t a_millis) noexcept;
void CountHiddenDraw(std::uint64_t a_stacks, std::uint64_t a_micros) noexcept;
void CountSinkAdd() noexcept;
void CountSinkRemove() noexcept;
void CountReadback(std::uint64_t a_micros) noexcept;
void CountTargetCreated(std::uint64_t a_bytes) noexcept;
void CountTargetDestroyed(std::uint64_t a_bytes) noexcept;
void CountFrame() noexcept;
void CountActorView(bool a_inView) noexcept;
void CountFrameAt(std::int64_t a_nowMicros) noexcept;
void ForgetFrameInterval() noexcept;
void CountRenderEvaluation() noexcept;
void CountStepExecution(std::size_t a_kind) noexcept;
void CountStepCacheHit() noexcept;
void CountStepReleases(std::uint64_t a_count) noexcept;
void CountStepRestores(std::uint64_t a_count) noexcept;
void CountSection(Section a_section, std::uint64_t a_micros) noexcept;
[[nodiscard]] Snapshot Drain() noexcept;

[[nodiscard]] constexpr std::uint64_t
MippedRgbaBytes(std::uint32_t a_side) noexcept {
  return static_cast<std::uint64_t>(a_side) * a_side * 4 * 4 / 3;
}

class Stopwatch {
public:
  [[nodiscard]] std::uint64_t Micros() const noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start_)
            .count());
  }

private:
  std::chrono::steady_clock::time_point start_ =
      std::chrono::steady_clock::now();
};
}
