// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/Manager.h"

#include "SettingsFile.h"
#include "actors/SyncLog.h"
#include "diagnostics/Metrics.h"
#include "diagnostics/Trace.h"
#include "engine/ActorBudget.h"
#include "engine/Clock.h"
#include "engine/LogOnce.h"
#include "engine/RegressionRun.h"
#include "engine/TargetCensus.h"
#include "planners/RenderBudget.h"
#include "planners/RenderMemory.h"
#include "render/Compositor.h"
#include "render/RenderInstance.h"
#include "render/TextureLab.h"

#include <expected>
#include <format>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace BetterEnchantmentEffects {
namespace {
void EmitGpuTimings() {
  TextureLab *lab = TextureLab::GetSingleton();
  if (!lab)
    return;
  const GpuTiming::Totals gpu = lab->DrainTimings();
  if (gpu.timedTicks == 0 && gpu.droppedTicks == 0 && gpu.discardedTicks == 0)
    return;
  Trace::EmitSafely(Trace::Event::kMetrics,
                    {{"action", "gpu_ticks"},
                     {"timed", std::to_string(gpu.timedTicks)},
                     {"dropped", std::to_string(gpu.droppedTicks)},
                     {"discarded", std::to_string(gpu.discardedTicks)},
                     {"untimed_spans", std::to_string(gpu.untimedSpans)}});
  for (const auto &[span, total] : gpu.spans)
    Trace::EmitSafely(
        Trace::Event::kMetrics,
        {{"action", "gpu_span"},
         {"span", span},
         {"count", std::to_string(total.count)},
         {"total_us", std::to_string(total.nanoseconds / 1000)},
         {"max_us", std::to_string(total.maxNanoseconds / 1000)}});
}

void EmitStepKinds(const Metrics::StepKindCounts &a_counts) {
  std::vector<Trace::Field> fields{{"action", "step_kinds"}};
  for (std::size_t kind = 0; kind < kRenderStepKindCount; ++kind) {
    if (a_counts[kind] > 0) {
      fields.push_back(
          {std::string{StepKindNameAt(kind)}, std::to_string(a_counts[kind])});
    }
  }
  if (fields.size() > 1) {
    Trace::EmitSafely(Trace::Event::kMetrics, fields);
  }
}

TargetCost
PlannedTargets(const std::unordered_map<RE::FormID, LiveActor> &a_live) {
  std::unordered_set<const RenderInstance *> seen;
  std::vector<HeldTarget> held;
  for (const auto &[actor, state] : a_live) {
    if (state.renderedFrame == 0) {
      continue;
    }
    for (const RenderInstance *render : InstancesOf(state)) {
      if (seen.insert(render).second) {
        const std::vector<HeldTarget> &now = HeldNow(state, render->Targets());
        held.insert(held.end(), now.begin(), now.end());
      }
    }
  }
  return CostOf(held);
}

ScreenSpread
ScreenSpreadOf(const std::unordered_map<RE::FormID, LiveActor> &a_live) {
  std::vector<float> fractions;
  for (const auto &[actor, state] : a_live) {
    if (state.inView && state.renderedFrame != 0 && state.screenFraction) {
      fractions.push_back(*state.screenFraction);
    }
  }
  return SpreadOf(std::move(fractions));
}

std::vector<std::string> RecipeIdsOf(const LiveActor &a_state) {
  std::vector<std::string> ids;
  for (const LiveInstance &instance : a_state.instances) {
    if (instance.recipe && !std::ranges::contains(ids, instance.recipe->id)) {
      ids.push_back(instance.recipe->id);
    }
  }
  return ids;
}

ActorTargets TargetsOfActor(RE::FormID a_actor, const LiveActor &a_state) {
  std::vector<HeldTarget> held;
  for (const RenderInstance *render : InstancesOf(a_state)) {
    const std::vector<HeldTarget> &now = HeldNow(a_state, render->Targets());
    held.insert(held.end(), now.begin(), now.end());
  }
  const RE::NiPointer<RE::Actor> actor = a_state.actor.get();
  const char *name = actor ? actor->GetName() : nullptr;
  return {a_actor, name ? std::string{name} : std::string{},
          a_state.screenFraction, RecipeIdsOf(a_state), std::move(held)};
}

std::vector<ActorTargets>
RenderedActorTargets(const std::unordered_map<RE::FormID, LiveActor> &a_live) {
  std::vector<ActorTargets> actors;
  for (const auto &[actor, state] : a_live) {
    if (state.renderedFrame != 0) {
      actors.push_back(TargetsOfActor(actor, state));
    }
  }
  return actors;
}

std::string Fraction(float a_value) { return std::format("{:.3f}", a_value); }

void AddBudgetFields(std::vector<Trace::Field> &a_fields,
                     const BudgetSample &a_sample, const BudgetState &a_state) {
  if (a_sample.memory) {
    a_fields.push_back(
        {"budget_bytes", std::to_string(a_sample.memory->budget)});
    a_fields.push_back({"usage_bytes", std::to_string(a_sample.memory->usage)});
  }
  if (a_state.allowance) {
    a_fields.push_back({"allowance_bytes", std::to_string(*a_state.allowance)});
  }
  a_fields.push_back({"planned_bytes", std::to_string(a_state.planned)});
  a_fields.push_back({"pressure", IsOver(a_state.pressure) ? "true" : "false"});
}

PerLevel<std::size_t>
LevelCounts(const std::unordered_map<RE::FormID, LiveActor> &a_live) {
  PerLevel<std::size_t> counts;
  for (const auto &[actor, state] : a_live) {
    ++counts.At(state.appliedResolution);
  }
  return counts;
}

void EmitLevelCounts(const PerLevel<std::size_t> &a_counts) {
  std::vector<Trace::Field> fields{{"action", "resolution_levels"}};
  for (const ResolutionLevel level : kResolutionLevels) {
    fields.push_back(
        {std::string{LevelName(level)}, std::to_string(a_counts.At(level))});
  }
  Trace::EmitSafely(Trace::Event::kMetrics, fields);
}

void EmitMetricsHeartbeat(const Metrics::Snapshot &a_measured,
                          const TargetCost &a_planned,
                          const ScreenSpread &a_screen,
                          const BudgetSample &a_sample,
                          const BudgetState &a_budget,
                          std::size_t a_waitingInstalls) {
  EmitGpuTimings();
  const TextureLab *lab = TextureLab::GetSingleton();
  const TextureLab::TargetHoldings held =
      lab ? lab->HeldTargets() : TextureLab::TargetHoldings{};
  const Compositor *compositor = Compositor::GetSingleton();
  EmitStepKinds(a_measured.stepExecutionsByKind);
  std::vector<Trace::Field> heartbeat{
      {"action", "heartbeat"},
      {"syncs", std::to_string(a_measured.syncs)},
      {"sync_us", std::to_string(a_measured.syncMicros)},
      {"sync_max_us", std::to_string(a_measured.syncMaxMicros)},
      {"sync_frame_max_us", std::to_string(a_measured.syncFrameMaxMicros)},
      {"sync_deferred", std::to_string(a_measured.syncDeferrals)},
      {"sync_waiting", std::to_string(a_waitingInstalls)},
      {"install_wait_max_ms", std::to_string(a_measured.installWaitMaxMillis)},
      {"hidden_stacks", std::to_string(a_measured.hiddenStacks)},
      {"hidden_stacks_tick_max",
       std::to_string(a_measured.hiddenStacksTickMax)},
      {"hidden_draw_us", std::to_string(a_measured.hiddenDrawMicros)},
      {"hidden_draw_tick_max_us",
       std::to_string(a_measured.hiddenDrawTickMaxMicros)},
      {"sink_adds", std::to_string(a_measured.sinkAdds)},
      {"sink_removes", std::to_string(a_measured.sinkRemoves)},
      {"readbacks", std::to_string(a_measured.readbacks)},
      {"readback_us", std::to_string(a_measured.readbackMicros)},
      {"readback_max_us", std::to_string(a_measured.readbackMaxMicros)},
      {"targets", std::to_string(a_measured.targets)},
      {"targets_peak", std::to_string(a_measured.targetsPeak)},
      {"target_bytes", std::to_string(a_measured.targetBytes)},
      {"target_bytes_peak", std::to_string(a_measured.targetBytesPeak)},
      {"targets_created", std::to_string(a_measured.targetsCreated)},
      {"target_bytes_created", std::to_string(a_measured.targetBytesCreated)},
      {"target_bytes_destroyed",
       std::to_string(a_measured.targetBytesDestroyed)},
      {"screen_fraction_min", Fraction(a_screen.least)},
      {"screen_fraction_median", Fraction(a_screen.median)},
      {"screen_fraction_max", Fraction(a_screen.most)},
      {"estimated_targets", std::to_string(a_planned.targets)},
      {"estimated_bytes", std::to_string(a_planned.bytes)},
      {"pool_targets", std::to_string(held.idleTargets)},
      {"pool_bytes", std::to_string(held.idleBytes)},
      {"resident_targets", std::to_string(compositor->ResidentTargets())},
      {"resident_bytes", std::to_string(compositor->ResidentBytes())},
      {"frames", std::to_string(a_measured.frames)},
      {"render_evaluations", std::to_string(a_measured.renderEvaluations)},
      {"step_executions", std::to_string(a_measured.stepExecutions)},
      {"step_cache_hits", std::to_string(a_measured.stepCacheHits)},
      {"step_releases", std::to_string(a_measured.stepReleases)},
      {"step_restores", std::to_string(a_measured.stepRestores)},
      {"interval_max_us", std::to_string(a_measured.intervalMaxMicros)},
      {"actors_in_view", std::to_string(a_measured.actorsInView)},
      {"actors_out_of_view", std::to_string(a_measured.actorsOutOfView)},
      {"hitches_50ms", std::to_string(a_measured.hitches[0])},
      {"hitches_100ms", std::to_string(a_measured.hitches[1])},
      {"plugin_over_8ms", std::to_string(a_measured.pluginSpikes[0])},
      {"plugin_over_16ms", std::to_string(a_measured.pluginSpikes[1])},
      {"plugin_over_33ms", std::to_string(a_measured.pluginSpikes[2])}};
  AddBudgetFields(heartbeat, a_sample, a_budget);
  for (const Named<Metrics::Section> &section : Metrics::kSections) {
    const Metrics::SectionTime time = a_measured.Of(section.value);
    heartbeat.push_back(
        {std::format("{}_us", section.name), std::to_string(time.micros)});
    heartbeat.push_back({std::format("{}_max_us", section.name),
                         std::to_string(time.maxMicros)});
  }
  Trace::EmitSafely(Trace::Event::kMetrics, heartbeat);
}

}

void Manager::FlushMetricsHeartbeat() {
  const std::uint32_t now = NowMS();
  const std::uint32_t intervalMs = now - lastMetricsMS_;
  lastMetricsMS_ = now;
  const Metrics::Snapshot measured = Metrics::Drain();
  const Settings settings = GetSettings();
  const BudgetSample sample = SampleBudget(measured, settings);
  UpdateBudget(sample, PlannedTargets(live_).bytes, settings);
  EmitHeartbeat(measured, sample, intervalMs);
}

void Manager::EmitHeartbeat(const Metrics::Snapshot &a_measured,
                            const BudgetSample &a_sample,
                            std::uint32_t a_intervalMs) {
  const TargetCost planned = PlannedTargets(live_);
  EmitMetricsHeartbeat(a_measured, planned, ScreenSpreadOf(live_), a_sample,
                       budget_, waitingInstalls_.size());
  EmitTargetHolders(live_, world_, disagreementCount_);
  const PerLevel<std::size_t> levels = LevelCounts(live_);
  EmitLevelCounts(levels);
  PublishBudgetView(
      Studio::BudgetViewOf(a_sample, budget_, budgetRadius_, levels));
  ReportRenderMemory(a_measured, planned, a_intervalMs);
}

std::vector<ActorTargets> Manager::RenderedTargets() const {
  return RenderedActorTargets(live_);
}

void Manager::ReportRenderMemory(const Metrics::Snapshot &a_measured,
                                 const TargetCost &a_planned,
                                 std::uint32_t a_intervalMs) {
  const HoldingStep next =
      NextHoldingStep(holdingStep_, a_measured.targetBytes);
  holdingStep_ = next.step;
  if (next.crossing != Crossing::kNone) {
    logger::info("{}",
                 HoldingLine({a_measured.targets, a_measured.targetBytes},
                             {a_planned.targets, a_planned.bytes},
                             LargestHoldings(HoldingsOf(RenderedTargets()),
                                             kReportedHoldings)));
  }
  const TargetBurst burst{a_measured.targetsCreated,
                          a_measured.targetBytesCreated,
                          a_measured.targetBytesDestroyed, a_intervalMs};
  if (IsBurst(burst) && GetSettings().verboseLogging) {
    logger::info("{}",
                 BurstLine(burst, Actors::SyncSummary(syncLog_, NowMS(),
                                                      Actors::kSyncLogMs)));
  }
}

BudgetSample Manager::SampleBudget(const Metrics::Snapshot &a_measured,
                                   const Settings &a_settings) {
  const std::expected<VideoMemory, std::string> memory = videoMemory_.Sample();
  if (!memory) {
    LogOnce(warningHistory_, Severity::kWarning,
            std::format("render target budget: video memory is unknown ({}); "
                        "the plugin does not respond to memory pressure",
                        memory.error()));
  }
  const std::uint64_t capMiB = RegressionRenderTargetBudgetMiB().value_or(
      a_settings.renderTargetBudgetMiB);
  return {memory ? std::optional<VideoMemory>{*memory} : std::nullopt,
          a_measured.targetBytes, capMiB * kMebibyte};
}
}
