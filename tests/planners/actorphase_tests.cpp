// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/ActorPhase.h"
#include "test_support.h"

#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
enum class Job : int {};
struct Draw {
  int slots = 0;
};
struct Work {
  int stacks = 0;
};

using Phase = ActorPhase<Job, Draw, Work>;
using Event = PhaseEvent<Job, Draw, Work>;
using Result = PhaseStep<Job, Draw, Work>;

constexpr Job kJob{7};
constexpr Job kOtherJob{8};
constexpr ResolutionLevel kFull = ResolutionLevel::kFull;
constexpr ResolutionLevel kHalf = ResolutionLevel::kHalf;
constexpr ResolutionLevel kQuarter = ResolutionLevel::kQuarter;

Phase LiveHeld() { return LiveAt(kFull, HeldIntermediates{}); }
Phase LiveReleased() { return LiveAt(kFull, ReleasedIntermediates{}); }
Phase Awaiting() { return AwaitingPlan<Job>{kJob}; }
Phase Drawing() { return FirstDrawing<Draw>{kFull, Draw{3}, std::nullopt}; }
Phase Queued() {
  return Resizing<Work>{kFull, kHalf, IntermediateHolding::kHeld,
                        ResizeQueued{}};
}
Phase Running() {
  return Resizing<Work>{kFull, kHalf, IntermediateHolding::kReleased,
                        ResizeRunning<Work>{10, Work{2}}};
}

struct Row {
  std::string_view phaseName;
  std::function<Phase()> phase;
  std::string_view eventName;
  std::function<Event()> event;
  std::string_view next;
  std::vector<PhaseEffect> effects;
};

Event Hidden(Millis a_now) {
  return Observed{false, false, kFull, a_now, 5000};
}

void EveryPairHasItsNextPhase() {
  const std::array<std::pair<std::string_view, std::function<Phase()>>, 6>
      phases{{{"live held", LiveHeld},
              {"live released", LiveReleased},
              {"awaiting", Awaiting},
              {"drawing", Drawing},
              {"queued", Queued},
              {"running", Running}}};
  const std::vector<std::pair<std::string_view, std::function<Event()>>> events{
      {"plan awaited", [] { return Event{JobAwaited<Job>{kOtherJob}}; }},
      {"plan failed", [] { return Event{JobFailed<Job>{kJob, kHalf}}; }},
      {"stale plan failed",
       [] { return Event{JobFailed<Job>{kOtherJob, kHalf}}; }},
      {"installed", [] { return Event{Installed<Draw>{kHalf, Draw{1}}}; }},
      {"installed bare",
       [] { return Event{Installed<Draw>{kHalf, std::nullopt}}; }},
      {"first draw done", [] { return Event{FirstDrawEnded{}}; }},
      {"level wanted", [] { return Event{LevelWanted{kQuarter}}; }},
      {"resize started",
       [] { return Event{ResizeStarted<Work>{20, Work{4}}}; }},
      {"resize failed", [] { return Event{ResizeFailed{}}; }},
      {"swapped", [] { return Event{Swapped{}}; }},
      {"swap mismatch", [] { return Event{SwapMismatch{}}; }},
      {"budget release", [] { return Event{ReleaseForBudget{}}; }},
  };
  const std::array<std::array<std::string_view, 12>, 6> expected{{
      {"awaiting_plan", "live", "live", "first_drawing", "live", "live",
       "resize_queued", "live", "live", "live", "live", "live"},
      {"awaiting_plan", "live", "live", "first_drawing", "live", "live",
       "resize_queued", "live", "live", "live", "live", "live"},
      {"awaiting_plan", "live", "awaiting_plan", "first_drawing", "live",
       "awaiting_plan", "awaiting_plan", "awaiting_plan", "awaiting_plan",
       "awaiting_plan", "awaiting_plan", "awaiting_plan"},
      {"awaiting_plan", "first_drawing", "first_drawing", "first_drawing",
       "live", "live", "first_drawing", "first_drawing", "first_drawing",
       "first_drawing", "first_drawing", "first_drawing"},
      {"awaiting_plan", "resize_queued", "resize_queued", "first_drawing",
       "live", "resize_queued", "resize_queued", "resizing", "live",
       "resize_queued", "resize_queued", "resize_queued"},
      {"awaiting_plan", "resizing", "resizing", "first_drawing", "live",
       "resizing", "resize_queued", "resizing", "resizing", "live",
       "resize_queued", "resizing"},
  }};
  for (std::size_t p = 0; p < phases.size(); ++p) {
    for (std::size_t e = 0; e < events.size(); ++e) {
      const Result next = StepPhase(phases[p].second(), events[e].second());
      Check(PhaseName(next.phase) == expected[p][e],
            std::string{phases[p].first} + " + " +
                std::string{events[e].first} + " gives " +
                std::string{expected[p][e]} + ", not " +
                std::string{PhaseName(next.phase)});
    }
  }
}

void FailedResizeKeepsTheAppliedLevel() {
  const Result failed = StepPhase(Queued(), Event{ResizeFailed{}});
  Check(TargetLevel(failed.phase) == AppliedLevel(failed.phase) &&
            AppliedLevel(failed.phase) == kFull,
        "after a failed resize the target level is the applied level");
  const Result again =
      StepPhase(Phase{failed.phase}, Event{LevelWanted{kHalf}});
  Check(PhaseName(again.phase) == "live",
        "the failed level is not asked for again while it stays wanted");
  const Result moved = StepPhase(
      Phase{failed.phase}, Event{Observed{true, false, kQuarter, 0, 5000}});
  const Result retried =
      StepPhase(Phase{moved.phase}, Event{LevelWanted{kHalf}});
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
  for (const auto &phase : {Awaiting(), Drawing(), Queued(), Running()}) {
    const Result released = StepPhase(Phase{phase}, Event{ReleaseForBudget{}});
    Check(released.effects.empty(),
          "a budget release outside live releases nothing");
  }
  const Result drawingHidden = StepPhase(Drawing(), Hidden(100000));
  Check(drawingHidden.effects.empty(),
        "a first-drawing actor out of view releases nothing");
}

void SwapEndsHeld() {
  const Result swapped = StepPhase(Running(), Event{Swapped{}});
  Check(HoldingOf(swapped.phase) == IntermediateHolding::kHeld &&
            AppliedLevel(swapped.phase) == kHalf &&
            swapped.effects == std::vector<PhaseEffect>{SetAppliedLevel{kHalf}},
        "a swap from released intermediates ends held at the new level");
  Check(HoldingOf(Running()) == IntermediateHolding::kReleased,
        "a resize keeps the attached instance's holding until the swap");
}

void HiddenReleaseWaitsForTheHold() {
  Result step = StepPhase(LiveHeld(), Hidden(1000));
  Check(step.effects.empty(), "leaving the view starts the hold");
  step = StepPhase(Phase{step.phase}, Hidden(5999));
  Check(step.effects.empty(), "nothing is released before the hold ends");
  step = StepPhase(Phase{step.phase}, Hidden(6000));
  Check(step.effects == std::vector<PhaseEffect>{ReleaseHeldIntermediates{}} &&
            HoldingOf(step.phase) == IntermediateHolding::kReleased,
        "the hold releases the intermediates once");
  step = StepPhase(Phase{step.phase}, Hidden(9000));
  Check(step.effects.empty(), "a released actor is not released again");
  step = StepPhase(Phase{step.phase},
                   Event{Observed{true, false, kFull, 9100, 5000}});
  Check(HoldingOf(step.phase) == IntermediateHolding::kHeld,
        "coming into view holds the intermediates again");
}

void FirstDrawQueuesALevel() {
  const Result wanted = StepPhase(Drawing(), Event{LevelWanted{kQuarter}});
  Check(TargetLevel(wanted.phase) == kQuarter &&
            AppliedLevel(wanted.phase) == kFull,
        "a level wanted during the first draw becomes the target");
  const Result done = StepPhase(Phase{wanted.phase}, Event{FirstDrawEnded{}});
  Check(PhaseName(done.phase) == "resize_queued" &&
            TargetLevel(done.phase) == kQuarter,
        "the first draw's end starts the queued resize");
}

void LiveStateResetsWithANewLive() {
  Phase phase = LiveHeld();
  if (Live *live = Get<Live>(phase)) {
    live->stepUpBlockedFrom = kFull;
    live->smallOnScreen = Hold{}.Update(true, 100);
  }
  const Result resized = StepPhase(std::move(phase), Event{LevelWanted{kHalf}});
  const Result swapped = StepPhase(
      StepPhase(Phase{resized.phase}, Event{ResizeStarted<Work>{1, Work{}}})
          .phase,
      Event{Swapped{}});
  const Live *live = Get<Live>(swapped.phase);
  Check(live && !live->stepUpBlockedFrom && !live->smallOnScreen.since,
        "the step-down hold and the blocked step-up reset in a new live");
}

void StaleArrivalsChangeNothing() {
  const Result stale =
      StepPhase(Awaiting(), Event{JobFailed<Job>{kOtherJob, kHalf}});
  Check(AwaitedJob(stale.phase) == kJob && stale.effects.empty(),
        "a failure of another job leaves the awaited job");
  const Result live = StepPhase(LiveHeld(), Event{JobFailed<Job>{kJob, kHalf}});
  Check(AppliedLevel(live.phase) == kFull,
        "a plan failure outside awaiting changes nothing");
}

void QueriesFollowThePhase() {
  Check(!TargetLevel(Awaiting()) && !AppliedLevel(Awaiting()),
        "an awaiting actor has no level yet");
  Check(TargetLevel(Running()) == kHalf && AppliedLevel(Running()) == kFull,
        "a resize targets its new level and applies its old one");
  Check(DrawsHidden(Drawing()) && DrawsHidden(Running()) &&
            !DrawsHidden(Queued()) && !DrawsHidden(LiveHeld()),
        "first draws and running resizes draw hidden");
  Phase small = LiveHeld();
  small = StepPhase(std::move(small),
                    Event{Observed{true, true, kFull, 1000, 5000}})
              .phase;
  Check(!StepDownDue(small, 2999, 2000) && StepDownDue(small, 3000, 2000),
        "the step-down is due after the hold");
}
}

int main() {
  EveryPairHasItsNextPhase();
  FailedResizeKeepsTheAppliedLevel();
  OnlyHeldLiveMayRelease();
  SwapEndsHeld();
  HiddenReleaseWaitsForTheHold();
  FirstDrawQueuesALevel();
  LiveStateResetsWithANewLive();
  StaleArrivalsChangeNothing();
  QueriesFollowThePhase();
  return test::Finish("planners_actorphase");
}
