// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/PhaseChanges.h"
#include "test_support.h"

#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Orchestration;
using test::Check;

namespace {
constexpr ActorId kActor{20};
constexpr PlanJobId kJob{7};
constexpr PlanJobId kOtherJob{8};
constexpr ResolutionLevel kFull = ResolutionLevel::kFull;
constexpr ResolutionLevel kHalf = ResolutionLevel::kHalf;
constexpr ResolutionLevel kQuarter = ResolutionLevel::kQuarter;

Live LiveWith(Intermediates a_intermediates) {
  return Live{
      kFull, std::move(a_intermediates), {}, std::nullopt, std::nullopt};
}
RenderPhase LiveHeld() { return LiveWith(IntermediatesHeld{}); }
RenderPhase LiveReleased() { return LiveWith(IntermediatesReleased{}); }
RenderPhase Bare() { return WithoutRender{}; }
RenderPhase Awaiting() { return AwaitingPlan{kJob}; }
RenderPhase Drawing() {
  return FirstDrawing{kFull, UnpublishedDraw{3, 0, 1, 0}, std::nullopt};
}
RenderPhase Queued() {
  return Resizing{kFull, kHalf, IntermediatesHeld{}, ResizeQueued{}};
}
RenderPhase Running() {
  return Resizing{kFull, kHalf, IntermediatesReleased{},
                  ResizeRunning{UnpublishedDraw{2, 0, 10, 0}}};
}

SeenActor OutOfView(Millis a_now) {
  return {false, false, kFull, a_now, 5000, 64};
}

bool Commands(const PhaseChange &a_change, std::size_t a_count) {
  return a_change.commands.size() == a_count;
}

void EveryEventHasItsNextPhase() {
  const std::array<std::pair<std::string_view, std::function<RenderPhase()>>, 7>
      phases{{{"live held", LiveHeld},
              {"live released", LiveReleased},
              {"without render", Bare},
              {"awaiting", Awaiting},
              {"drawing", Drawing},
              {"queued", Queued},
              {"running", Running}}};
  const std::array<std::pair<std::string_view, RenderEvent>, 8> events{{
      {"plan awaited", PlanAwaited{kOtherJob}},
      {"installed", Installed{kHalf, 1, {}, 0}},
      {"installed without render", InstalledWithoutRender{}},
      {"unpublished drawn", UnpublishedDrawn{2, true}},
      {"resize started", ResizeStarted{4}},
      {"resize not started", ResizeNotStarted{"no stack"}},
      {"resize swapped", ResizeSwapped{}},
      {"resize not swapped", ResizeNotSwapped{}},
  }};
  const std::array<std::array<std::string_view, 8>, 7> expected{{
      {"awaiting_plan", "first_drawing", "without_render", "live", "live",
       "live", "live", "live"},
      {"awaiting_plan", "first_drawing", "without_render", "live", "live",
       "live", "live", "live"},
      {"awaiting_plan", "first_drawing", "without_render", "without_render",
       "without_render", "without_render", "without_render", "without_render"},
      {"awaiting_plan", "first_drawing", "without_render", "awaiting_plan",
       "awaiting_plan", "awaiting_plan", "awaiting_plan", "awaiting_plan"},
      {"awaiting_plan", "first_drawing", "without_render", "first_drawing",
       "first_drawing", "first_drawing", "first_drawing", "first_drawing"},
      {"awaiting_plan", "first_drawing", "without_render", "resize_queued",
       "resizing", "live", "resize_queued", "resize_queued"},
      {"awaiting_plan", "first_drawing", "without_render", "resizing",
       "resizing", "resizing", "live", "resize_queued"},
  }};
  for (std::size_t p = 0; p < phases.size(); ++p) {
    for (std::size_t e = 0; e < events.size(); ++e) {
      const PhaseChange next =
          OnEvent(kActor, phases[p].second(), events[e].second, 30);
      Check(PhaseName(next.phase) == expected[p][e],
            std::string{phases[p].first} + " + " +
                std::string{events[e].first} + " gives " +
                std::string{expected[p][e]} + ", not " +
                std::string{PhaseName(next.phase)});
    }
  }
}

void InstallsSetTheAppliedLevel() {
  const PhaseChange installed =
      OnEvent(kActor, Awaiting(), Installed{kHalf, 5, {}, 0}, 30);
  const UnpublishedDraw *draw = UnpublishedDrawOf(installed.phase);
  Check(draw && draw->total == 5 && draw->beganFrame == 30 &&
            AppliedLevel(installed.phase) == kHalf && Commands(installed, 1) &&
            Is<SetAppliedLevel>(installed.commands.front()),
        "an install starts its first draw and sets the applied level");
}

void DrawsCountUnitsAndTicks() {
  PhaseChange step = OnEvent(kActor, Drawing(), UnpublishedDrawn{1, true}, 31);
  step = OnEvent(kActor, std::move(step.phase), UnpublishedDrawn{1, false}, 32);
  const UnpublishedDraw *draw = UnpublishedDrawOf(step.phase);
  Check(draw && draw->drawn == 1 && draw->drawTicks == 1,
        "a draw records its units and counts only ticks that drew");
  const PhaseChange started = OnEvent(kActor, Queued(), ResizeStarted{4}, 40);
  const UnpublishedDraw *resize = UnpublishedDrawOf(started.phase);
  Check(resize && resize->total == 4 && resize->beganFrame == 40,
        "a started resize draws its units from the frame it began");
}

void FailedResizeKeepsTheAppliedLevel() {
  const PhaseChange failed =
      OnEvent(kActor, Queued(), ResizeNotStarted{"no stack"}, 30);
  Check(TargetLevel(failed.phase) == AppliedLevel(failed.phase) &&
            AppliedLevel(failed.phase) == kFull,
        "after a resize that did not start the target is the applied level");
  const PhaseChange again = WantLevel(kActor, failed.phase, kHalf);
  Check(PhaseName(again.phase) == "live",
        "the failed level is not asked for again while it stays wanted");
  const PhaseChange moved =
      SeeActor(kActor, failed.phase, {true, false, kQuarter, 0, 5000, 0});
  const PhaseChange retried = WantLevel(kActor, moved.phase, kHalf);
  Check(PhaseName(retried.phase) == "resize_queued",
        "a change of the wanted level allows the failed level again");
}

void OnlyHeldLiveMayRelease() {
  Check(MayRelease(LiveHeld()) && !MayRelease(LiveReleased()) &&
            !MayRelease(Awaiting()) && !MayRelease(Drawing()) &&
            !MayRelease(Queued()) && !MayRelease(Running()),
        "only a live actor that holds its intermediates may release them");
  Check(!BudgetBusy(LiveHeld()) && BudgetBusy(Awaiting()) &&
            BudgetBusy(Drawing()) && BudgetBusy(Running()),
        "every phase other than live is busy for the budget");
  for (const RenderPhase &phase :
       {Awaiting(), Drawing(), Queued(), Running()}) {
    Check(Commands(ReleaseForBudget(kActor, phase, 64), 0),
          "a budget release outside live releases nothing");
  }
  const PhaseChange released = ReleaseForBudget(kActor, LiveHeld(), 64);
  const ReleaseIntermediates *command =
      Commands(released, 1)
          ? Get<ReleaseIntermediates>(released.commands.front())
          : nullptr;
  Check(command && command->reason == ReleaseReason::kBudget &&
            command->bytes == 64 && !MayRelease(released.phase),
        "a budget release of a held live actor releases once");
  Check(Commands(SeeActor(kActor, Drawing(), OutOfView(100000)), 0),
        "a first-drawing actor out of view releases nothing");
}

void SwapEndsHeldAtTheNewLevel() {
  const PhaseChange swapped = OnEvent(kActor, Running(), ResizeSwapped{}, 50);
  Check(HoldingOf(swapped.phase) == IntermediateHolding::kHeld &&
            AppliedLevel(swapped.phase) == kHalf && Commands(swapped, 1) &&
            Is<SetAppliedLevel>(swapped.commands.front()),
        "a swap from released intermediates ends held at the new level");
  Check(HoldingOf(Running()) == IntermediateHolding::kReleased,
        "a resize keeps the attached render's intermediates until the swap");
}

void OutOfViewReleaseWaitsForTheHold() {
  PhaseChange step = SeeActor(kActor, LiveHeld(), OutOfView(1000));
  Check(Commands(step, 0), "leaving the view starts the hold");
  step = SeeActor(kActor, std::move(step.phase), OutOfView(5999));
  Check(Commands(step, 0), "nothing is released before the hold ends");
  step = SeeActor(kActor, std::move(step.phase), OutOfView(6000));
  const ReleaseIntermediates *command =
      Commands(step, 1) ? Get<ReleaseIntermediates>(step.commands.front())
                        : nullptr;
  Check(command && command->reason == ReleaseReason::kOutOfView &&
            HoldingOf(step.phase) == IntermediateHolding::kReleased,
        "the hold releases the intermediates once");
  step = SeeActor(kActor, std::move(step.phase), OutOfView(9000));
  Check(Commands(step, 0), "a released actor is not released again");
  step = SeeActor(kActor, std::move(step.phase),
                  {true, false, kFull, 9100, 5000, 0});
  Check(HoldingOf(step.phase) == IntermediateHolding::kHeld,
        "coming into view holds the intermediates again");
}

void FirstDrawQueuesALevel() {
  const PhaseChange wanted = WantLevel(kActor, Drawing(), kQuarter);
  Check(TargetLevel(wanted.phase) == kQuarter &&
            AppliedLevel(wanted.phase) == kFull,
        "a level wanted during the first draw becomes the target");
  const PhaseChange ended = EndFirstDraw(wanted.phase);
  Check(PhaseName(ended.phase) == "resize_queued" &&
            TargetLevel(ended.phase) == kQuarter,
        "the first draw's end queues the wanted resize");
  Check(PhaseName(EndFirstDraw(Drawing()).phase) == "live",
        "a first draw without a wanted level ends live");
}

void ALevelChangeAbandonsARunningResize() {
  const PhaseChange changed = WantLevel(kActor, Running(), kQuarter);
  Check(PhaseName(changed.phase) == "resize_queued" &&
            TargetLevel(changed.phase) == kQuarter && Commands(changed, 1) &&
            Is<AbandonResize>(changed.commands.front()),
        "a new level during a running resize abandons its work");
  Check(Commands(WantLevel(kActor, Running(), kHalf), 0),
        "asking again for the running resize's level changes nothing");
  Check(Commands(WantLevel(kActor, Queued(), kQuarter), 0),
        "a queued resize has no work to abandon");
}

void LiveStateResetsWithANewLive() {
  RenderPhase phase = LiveHeld();
  if (Live *live = Get<Live>(phase)) {
    live->stepUpBlockedFrom = kFull;
    live->smallOnScreen = Hold{}.Update(true, 100);
  }
  const PhaseChange queued = WantLevel(kActor, std::move(phase), kHalf);
  const PhaseChange running =
      OnEvent(kActor, queued.phase, ResizeStarted{1}, 60);
  const PhaseChange swapped =
      OnEvent(kActor, running.phase, ResizeSwapped{}, 61);
  const Live *live = Get<Live>(swapped.phase);
  Check(live && !live->stepUpBlockedFrom && !live->smallOnScreen.since,
        "the step-down hold and the blocked step-up reset in a new live");
}

void PlanFailuresMatchTheAwaitedJob() {
  const PhaseChange stale = FailPlan(Awaiting(), kOtherJob);
  Check(AwaitedJob(stale.phase) == kJob && Commands(stale, 0),
        "a failure of another job leaves the awaited job");
  const PhaseChange failed = FailPlan(Awaiting(), kJob);
  Check(PhaseName(failed.phase) == "without_render" && Commands(failed, 0) &&
            !AppliedLevel(failed.phase),
        "a failure of the awaited job leaves the actor without a render");
  Check(Commands(FailPlan(LiveHeld(), kJob), 0),
        "a plan failure outside awaiting changes nothing");
}

void QueriesFollowThePhase() {
  Check(!TargetLevel(Awaiting()) && !AppliedLevel(Awaiting()),
        "an awaiting actor has no level yet");
  Check(!AppliedLevel(Bare()) && !MayRelease(Bare()) && BudgetBusy(Bare()) &&
            Commands(WantLevel(kActor, Bare(), kHalf), 0) &&
            Commands(SeeActor(kActor, Bare(), OutOfView(100000)), 0),
        "an actor without a render has no level and takes no decision");
  Check(TargetLevel(Running()) == kHalf && AppliedLevel(Running()) == kFull,
        "a resize targets its new level and applies its old one");
  Check(UnpublishedDrawOf(Drawing()) && UnpublishedDrawOf(Running()) &&
            !UnpublishedDrawOf(Queued()) && !UnpublishedDrawOf(LiveHeld()),
        "first draws and running resizes have an unpublished draw");
  const PhaseChange small =
      SeeActor(kActor, LiveHeld(), {true, true, kFull, 1000, 5000, 0});
  Check(!StepDownDue(small.phase, 2999, 2000) &&
            StepDownDue(small.phase, 3000, 2000),
        "the step-down is due after the hold");
}
}

int main() {
  EveryEventHasItsNextPhase();
  InstallsSetTheAppliedLevel();
  DrawsCountUnitsAndTicks();
  FailedResizeKeepsTheAppliedLevel();
  OnlyHeldLiveMayRelease();
  SwapEndsHeldAtTheNewLevel();
  OutOfViewReleaseWaitsForTheHold();
  FirstDrawQueuesALevel();
  ALevelChangeAbandonsARunningResize();
  LiveStateResetsWithANewLive();
  PlanFailuresMatchTheAwaitedJob();
  QueriesFollowThePhase();
  return test::Finish("orchestration_phasechanges");
}
