// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/Manager.h"

#include "SettingsFile.h"
#include "diagnostics/Metrics.h"
#include "diagnostics/Trace.h"
#include "engine/ActorBudget.h"
#include "engine/ActorObservation.h"
#include "engine/ActorResize.h"
#include "engine/Clock.h"
#include "engine/LogOnce.h"
#include "orchestration/Sync.h"
#include "planners/InstallPacing.h"
#include "planners/ResolutionLevel.h"
#include "render/RenderInstance.h"
#include "render/TextureRef.h"

#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects {
namespace {
using Orchestration::ActorEvent;
using Orchestration::FormOf;

std::string NameOf(const LiveActor &a_state) {
  const RE::NiPointer<RE::Actor> actor = a_state.actor.get();
  const char *name = actor ? actor->GetName() : nullptr;
  return name && *name ? std::string{name} : std::string{"unnamed"};
}

void TraceSwap(RE::FormID a_actorID, const LiveActor &a_state,
               const ResizeSwap &a_swap, bool a_verbose) {
  Trace::EmitSafely(
      Trace::Event::kMetrics,
      {{"action", "resolution_swap"},
       {"actor", std::to_string(a_actorID)},
       {"from", std::string{LevelName(a_swap.from)}},
       {"level", std::string{LevelName(a_swap.to)}},
       {"stacks", std::to_string(a_swap.stacks)},
       {"drawn", std::to_string(a_swap.drawn)},
       {"targets", std::to_string(a_swap.targets.targets)},
       {"target_bytes", std::to_string(a_swap.targets.bytes)},
       {"live_targets", std::to_string(LiveTextureTargets().size())},
       {"live_instances", std::to_string(RenderInstance::LiveCount())}});
  if (a_verbose) {
    logger::info("actor {:08X} ({}) resized: drawn at {} resolution", a_actorID,
                 NameOf(a_state), LevelName(a_swap.to));
  }
}

void TraceRelease(RE::FormID a_actorID,
                  const Orchestration::ReleaseIntermediates &a_command,
                  std::size_t a_steps) {
  if (a_command.reason == Orchestration::ReleaseReason::kOutOfView) {
    Trace::EmitSafely(Trace::Event::kMetrics,
                      {{"action", "hidden_release"},
                       {"actor", std::to_string(a_actorID)},
                       {"steps", std::to_string(a_steps)}});
    return;
  }
  Trace::EmitSafely(Trace::Event::kMetrics,
                    {{"action", "budget_release"},
                     {"actor", std::to_string(a_actorID)},
                     {"steps", std::to_string(a_steps)},
                     {"bytes", std::to_string(a_command.bytes)}});
}

void TraceRestage(RE::FormID a_actor, std::string_view a_result,
                  std::string_view a_reason) {
  Trace::EmitSafely(Trace::Event::kMetrics,
                    {{"action", "restage"},
                     {"actor", std::to_string(a_actor)},
                     {"result", std::string{a_result}},
                     {"reason", std::string{a_reason}}});
}

CommandResults Reported(Orchestration::ActorId a_actor,
                        Orchestration::RenderEvent a_event) {
  CommandResults results;
  results.emplace_back(ActorEvent{a_actor, std::move(a_event)});
  return results;
}

void Append(CommandResults &a_into, CommandResults a_from) {
  a_into.insert(a_into.end(), std::make_move_iterator(a_from.begin()),
                std::make_move_iterator(a_from.end()));
}
}

CommandResults
Manager::Execute(const std::vector<Orchestration::Command> &a_commands,
                 const TickContext &a_context) {
  CommandResults results;
  std::uint64_t installUs = 0;
  for (const Orchestration::Command &command : a_commands) {
    const Metrics::Stopwatch watch;
    Append(results,
           Match(
               command,
               [&](const Orchestration::TickActor &a_command) {
                 return RunTickActor(
                     a_command, TickFrame{a_context.settings, a_context.view,
                                          a_context.nowMS, a_context.resuming});
               },
               [&](const Orchestration::StartResize &a_command) {
                 return RunStartResize(a_command);
               },
               [&](const Orchestration::SwapResize &a_command) {
                 return RunSwapResize(a_command, a_context);
               },
               [&](const Orchestration::AbandonResize &a_command) {
                 return RunAbandonResize(a_command);
               },
               [&](const Orchestration::ReleaseIntermediates &a_command) {
                 return RunReleaseIntermediates(a_command);
               },
               [&](const Orchestration::SetAppliedLevel &a_command) {
                 return RunSetAppliedLevel(a_command);
               },
               [&](const Orchestration::DrawUnpublished &a_command) {
                 return RunDrawUnpublished(a_command, a_context);
               },
               [&](const Orchestration::BuildAwaitedRender &a_command) {
                 return RunBuildAwaitedRender(a_command, a_context);
               },
               [&](const Orchestration::FailAwaitedRender &a_command) {
                 return RunFailAwaitedRender(a_command);
               },
               [&](const Orchestration::CancelPlanJob &a_command) {
                 return RunCancelPlanJob(a_command);
               },
               [&](const Orchestration::Install &a_command) {
                 return RunInstall(a_command, a_context);
               },
               [&](const Orchestration::Restage &a_command) {
                 return RunRestage(a_command, a_context);
               },
               [&](const Orchestration::SwapRestage &a_command) {
                 return RunSwapRestage(a_command);
               },
               [&](const Orchestration::AbandonRestage &a_command) {
                 return RunAbandonRestage(a_command);
               },
               [&](const Orchestration::Retire &a_command) {
                 return RunRetire(a_command, a_context);
               },
               [&](const Orchestration::Forget &a_command) {
                 return RunForget(a_command);
               },
               [&](const Orchestration::Observe &a_command) {
                 return RunObserve(a_command);
               },
               [&](const Orchestration::KeepModels &a_command) {
                 return RunKeepModels(a_command, a_context);
               },
               [&](const Orchestration::SettleApplications &a_command) {
                 return RunSettleApplications(a_command, a_context);
               }));
    if (Is<Orchestration::Install>(command)) {
      installUs += watch.Micros();
    }
  }
  Metrics::CountSyncFrame(installUs);
  return results;
}

CommandResults
Manager::RunStartResize(const Orchestration::StartResize &a_command) {
  const RE::FormID id = FormOf(a_command.actor);
  const auto found = live_.find(id);
  if (found == live_.end() || InFlightOf(found->second)) {
    ReportDisagreement(id, {"command", "start_resize"},
                       found == live_.end() ? "no resources to resize"
                                            : "a resize is already in flight");
    return Reported(a_command.actor,
                    Orchestration::ResizeNotStarted{"no render to resize"});
  }
  const std::expected<std::size_t, std::string> started =
      StartResize(found->second, a_command.to);
  if (!started) {
    LogOnce(warningHistory_, Severity::kWarning,
            std::format("actor {:08X} keeps its resolution: {}", id,
                        started.error()));
    return Reported(a_command.actor,
                    Orchestration::ResizeNotStarted{started.error()});
  }
  const ResizeInFlight *inFlight = InFlightOf(found->second);
  return Reported(a_command.actor,
                  Orchestration::ResizeStarted{
                      *started, inFlight ? DrawnStacks(*inFlight) : 0});
}

CommandResults
Manager::RunSwapResize(const Orchestration::SwapResize &a_command,
                       const TickContext &a_context) {
  const RE::FormID id = FormOf(a_command.actor);
  const auto found = live_.find(id);
  if (found == live_.end()) {
    ReportDisagreement(id, {"command", "swap_resize"}, "no resources to swap");
    return Reported(a_command.actor, Orchestration::ResizeNotSwapped{});
  }
  const std::expected<ResizeSwap, std::string> swap =
      FinishResize(found->second);
  if (!swap) {
    LogOnce(
        warningHistory_, Severity::kWarning,
        std::format("actor {:08X} keeps its resolution: {}", id, swap.error()));
    return Reported(a_command.actor, Orchestration::ResizeNotSwapped{});
  }
  TraceSwap(id, found->second, *swap, a_context.settings.verboseLogging);
  return Reported(a_command.actor, Orchestration::ResizeSwapped{});
}

CommandResults
Manager::RunAbandonResize(const Orchestration::AbandonResize &a_command) {
  const RE::FormID id = FormOf(a_command.actor);
  const auto found = live_.find(id);
  if (found == live_.end() || !InFlightOf(found->second)) {
    ReportDisagreement(id, {"command", "abandon_resize"},
                       "no resize to abandon");
    return {};
  }
  DropResize(found->second);
  return {};
}

CommandResults Manager::RunReleaseIntermediates(
    const Orchestration::ReleaseIntermediates &a_command) {
  const RE::FormID id = FormOf(a_command.actor);
  const auto found = live_.find(id);
  if (found == live_.end() || !AttachedRender(found->second)) {
    ReportDisagreement(id, {"command", "release_intermediates"},
                       "no render to release");
    return {};
  }
  TraceRelease(id, a_command, ReleaseIntermediates(found->second));
  return {};
}

CommandResults
Manager::RunSetAppliedLevel(const Orchestration::SetAppliedLevel &a_command) {
  const RE::FormID id = FormOf(a_command.actor);
  const auto found = live_.find(id);
  if (found == live_.end()) {
    ReportDisagreement(id, {"command", "set_applied_level"},
                       "no instances to give the level");
    return {};
  }
  SetAppliedResolution(found->second, a_command.level);
  return {};
}

CommandResults Manager::RunBuildAwaitedRender(
    const Orchestration::BuildAwaitedRender &a_command,
    const TickContext &a_context) {
  const RE::FormID id = FormOf(a_command.actor);
  const auto found = live_.find(id);
  if (found == live_.end()) {
    ReportDisagreement(id, {"command", "build_awaited_render"},
                       "no resources await a plan");
    return Reported(a_command.actor, Orchestration::InstalledWithoutRender{});
  }
  LiveActor &published = found->second;
  LiveActor &state = published.restage && published.restage->awaitedPlan
                         ? *published.restage
                         : published;
  if (!state.awaitedPlan) {
    ReportDisagreement(id, {"command", "build_awaited_render"},
                       "the actor awaits no plan job");
  }
  if (!state.actor.get()) {
    state.awaitedPlan.reset();
    return Reported(a_command.actor, Orchestration::InstalledWithoutRender{});
  }
  return Reported(a_command.actor,
                  BuildAwaitedRender(id, state, published, a_command.bounds,
                                     a_context.settings));
}

CommandResults Manager::RunFailAwaitedRender(
    const Orchestration::FailAwaitedRender &a_command) {
  const RE::FormID id = FormOf(a_command.actor);
  const auto found = live_.find(id);
  if (found == live_.end()) {
    ReportDisagreement(id, {"command", "fail_awaited_render"},
                       "no resources await a plan");
    return {};
  }
  FailAwaitedRender(found->second, a_command.problem);
  return {};
}

CommandResults
Manager::RunCancelPlanJob(const Orchestration::CancelPlanJob &a_command) {
  const JobId job = JobIdOf(a_command.job);
  const bool queued = planQueue_.Cancel(job);
  if (queued) {
    planJobs_ = Closed(std::move(planJobs_), job).book;
  }
  Trace::EmitSafely(Trace::Event::kMetrics,
                    {{"action", "render_plan_job"},
                     {"result", queued ? "cancelled" : "cancel_too_late"},
                     {"job", std::to_string(static_cast<std::uint64_t>(job))},
                     {"unfinished", std::to_string(planQueue_.Unfinished())}});
  return {};
}

CommandResults Manager::RunInstall(const Orchestration::Install &a_command,
                                   const TickContext &a_context) {
  const RE::FormID id = FormOf(a_command.actor);
  CommandResults results;
  const RE::NiPointer<RE::Actor> actor{RE::TESForm::LookupByID<RE::Actor>(id)};
  if (!actor) {
    results.emplace_back(Orchestration::InstallFailed{
        a_command.actor, std::string{"the actor is not loaded"}});
    return results;
  }
  if (live_.contains(id)) {
    ReportDisagreement(id, {"command", "install"},
                       "an install over installed resources");
    (void)RetireActorResources(id);
  }
  if (const std::optional<std::uint32_t> waited =
          WaitedMS(a_context.inputs.waiting, id,
                   Orchestration::PlannerMs(a_context.inputs.facts.now))) {
    Metrics::CountInstallWait(*waited);
  }
  results.push_back(InstallLiveActor(*actor, a_command, a_context));
  return results;
}

CommandResults Manager::RunRestage(const Orchestration::Restage &a_command,
                                   const TickContext &a_context) {
  const RE::FormID id = FormOf(a_command.actor);
  const auto found = live_.find(id);
  const RE::NiPointer<RE::Actor> actor{RE::TESForm::LookupByID<RE::Actor>(id)};
  if (found == live_.end() || !actor) {
    return ReinstallFor(a_command, a_context, "no published render to keep");
  }
  std::expected<Orchestration::RenderEvent, std::string> staged =
      Restaged(*actor, found->second, a_command, a_context);
  if (!staged) {
    return ReinstallFor(a_command, a_context, staged.error());
  }
  TraceRestage(id, "staged", "");
  return Reported(a_command.actor, std::move(*staged));
}

CommandResults Manager::ReinstallFor(const Orchestration::Restage &a_command,
                                     const TickContext &a_context,
                                     std::string_view a_reason) {
  TraceRestage(FormOf(a_command.actor), "reinstalled", a_reason);
  logger::info("actor {:08X}: reinstalled instead of restaged ({})",
               FormOf(a_command.actor), a_reason);
  CommandResults results =
      RunRetire(Orchestration::Retire{a_command.actor}, a_context);
  Append(
      results,
      RunInstall({a_command.actor, a_command.facts, a_command.planned,
                  Orchestration::InstallKind::kReinstall, a_command.bounds, 0},
                 a_context));
  return results;
}

CommandResults
Manager::RunSwapRestage(const Orchestration::SwapRestage &a_command) {
  const RE::FormID id = FormOf(a_command.actor);
  const auto found = live_.find(id);
  const RE::NiPointer<RE::Actor> actor{RE::TESForm::LookupByID<RE::Actor>(id)};
  if (found == live_.end() || !actor) {
    ReportDisagreement(id, {"command", "swap_restage"}, "no resources to swap");
    return Reported(a_command.actor, Orchestration::RestageNotSwapped{});
  }
  if (const std::expected<void, std::string> swapped =
          SwapRestaged(*actor, found->second);
      !swapped) {
    TraceRestage(id, "not_swapped", swapped.error());
    return Reported(a_command.actor, Orchestration::RestageNotSwapped{});
  }
  TraceRestage(id, "swapped", "");
  return Reported(a_command.actor, Orchestration::RestageSwapped{});
}

CommandResults
Manager::RunAbandonRestage(const Orchestration::AbandonRestage &a_command) {
  const RE::FormID id = FormOf(a_command.actor);
  const auto found = live_.find(id);
  if (found == live_.end() || !found->second.restage) {
    ReportDisagreement(id, {"command", "abandon_restage"},
                       "no staged render to abandon");
    return {};
  }
  found->second.restage.reset();
  TraceRestage(id, "abandoned", "");
  return {};
}

CommandResults Manager::RunRetire(const Orchestration::Retire &a_command,
                                  const TickContext &a_context) {
  const RE::FormID id = FormOf(a_command.actor);
  if (const std::size_t retired = RetireActorResources(id);
      retired > 0 && a_context.settings.verboseLogging) {
    logger::info("actor {:08X}: retired {} recipe(s)", id, retired);
  }
  CommandResults results;
  results.emplace_back(Orchestration::Retired{a_command.actor});
  return results;
}

CommandResults Manager::RunForget(const Orchestration::Forget &a_command) {
  ForgetActorResources(FormOf(a_command.actor));
  return {};
}

CommandResults Manager::RunObserve(const Orchestration::Observe &a_command) {
  applications_.SyncNow(FormOf(a_command.actor));
  return {};
}

CommandResults
Manager::RunKeepModels(const Orchestration::KeepModels &a_command,
                       const TickContext &a_context) {
  KeepModels(models_[FormOf(a_command.actor)], a_command.plan,
             a_context.inputs.observedAtMS);
  return {};
}

CommandResults Manager::RunSettleApplications(
    const Orchestration::SettleApplications &a_command,
    const TickContext &a_context) {
  const RE::FormID id = FormOf(a_command.actor);
  if (a_command.reinstall) {
    applications_.ClearReinstall(id);
  }
  if (a_command.equip) {
    (void)applications_.TakeEquipped(id);
  }
  if (const auto tokens = a_context.inputs.tokens.find(id);
      tokens != a_context.inputs.tokens.end()) {
    PrepareApplications(id, tokens->second);
  }
  ReconcileAnimationEvents(id);
  return {};
}
}
