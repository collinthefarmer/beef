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
constexpr EventContext kAtFrame30{30, kFull};

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
  return Resizing{kFull, kHalf, IntermediateHolding::kHeld, ResizeQueued{}};
}
RenderPhase Running() {
  return Resizing{kFull, kHalf, IntermediateHolding::kReleased,
                  ResizeRunning{UnpublishedDraw{2, 0, 10, 0}}};
}

SeenThisTick OutOfView(Millis a_now) {
  return {false, false, kFull, a_now, 5000, 64};
}

SeenThisTick InView(Millis a_now, std::optional<ResolutionLevel> a_wanted) {
  return {true, false, a_wanted, a_now, 5000, 64};
}

bool Commands(const PhaseChange &a_change, std::size_t a_count) {
  return a_change.commands.size() == a_count;
}

template <class T> bool FirstCommandIs(const PhaseChange &a_change) {
  return !a_change.commands.empty() && Is<T>(a_change.commands.front());
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
  const std::array<std::array<std::string_view, 8>, 7> names{{
      {"live", "live", "live", "live", "live", "live", "live", "live"},
      {"live", "live", "live", "live", "live", "live", "live", "live"},
      {"without_render", "without_render", "without_render", "without_render",
       "without_render", "without_render", "without_render", "without_render"},
      {"awaiting_plan", "first_drawing", "without_render", "awaiting_plan",
       "awaiting_plan", "awaiting_plan", "awaiting_plan", "awaiting_plan"},
      {"first_drawing", "first_drawing", "first_drawing", "first_drawing",
       "first_drawing", "first_drawing", "first_drawing", "first_drawing"},
      {"resize_queued", "resize_queued", "resize_queued", "resize_queued",
       "resize_running", "live", "resize_queued", "resize_queued"},
      {"resize_running", "resize_running", "resize_running", "resize_running",
       "resize_running", "resize_running", "live", "live"},
  }};
  const std::array<std::array<bool, 8>, 7> ignored{{
      {true, true, true, true, true, true, true, true},
      {true, true, true, true, true, true, true, true},
      {true, true, true, true, true, true, true, true},
      {false, false, false, true, true, true, true, true},
      {true, true, true, false, true, true, true, true},
      {true, true, true, true, false, false, true, true},
      {true, true, true, false, true, true, false, false},
  }};
  const std::array<std::array<std::size_t, 8>, 7> commands{{
      {0, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0, 0, 0},
      {0, 1, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0, 1, 1},
  }};
  for (std::size_t p = 0; p < phases.size(); ++p) {
    for (std::size_t e = 0; e < events.size(); ++e) {
      const PhaseChange next =
          OnEvent(kActor, phases[p].second(), events[e].second, kAtFrame30);
      const std::string pair =
          std::string{phases[p].first} + " + " + std::string{events[e].first};
      Check(PhaseName(next.phase) == names[p][e],
            pair + " gives " + std::string{names[p][e]} + ", not " +
                std::string{PhaseName(next.phase)});
      Check(next.ignored == ignored[p][e],
            pair + (ignored[p][e] ? " is ignored" : " is applied"));
      Check(Commands(next, commands[p][e]),
            pair + " gives " + std::to_string(commands[p][e]) + " commands");
      if (next.ignored) {
        Check(next.phase == phases[p].second(),
              pair + " leaves the phase as it was");
      }
    }
  }
}

void TheFirstEventCreatesThePhase() {
  const std::optional<PhaseChange> awaited =
      PhaseFor(kActor, PlanAwaited{kJob}, 30);
  Check(awaited && AwaitedJob(awaited->phase) == kJob && Commands(*awaited, 0),
        "a plan awaited at install creates an awaiting phase");
  const std::optional<PhaseChange> installed =
      PhaseFor(kActor, Installed{kHalf, 5, {}, 0}, 30);
  const UnpublishedDraw *draw =
      installed ? UnpublishedDrawOf(installed->phase) : nullptr;
  Check(draw && draw->total == 5 && draw->beganFrame == 30 &&
            AppliedLevel(installed->phase) == kHalf &&
            FirstCommandIs<SetAppliedLevel>(*installed),
        "an install starts its first draw and sets the applied level");
  const std::optional<PhaseChange> bare =
      PhaseFor(kActor, InstalledWithoutRender{}, 30);
  Check(bare && Is<WithoutRender>(bare->phase) && Commands(*bare, 0),
        "an install without a render creates a phase without one");
  Check(!PhaseFor(kActor, ResizeSwapped{}, 30),
        "only an install or an awaited plan creates a phase");
}

void DrawsCountUnitsAndTicks() {
  PhaseChange step =
      OnEvent(kActor, Drawing(), UnpublishedDrawn{1, true}, kAtFrame30);
  step = OnEvent(kActor, std::move(step.phase), UnpublishedDrawn{1, false},
                 kAtFrame30);
  const UnpublishedDraw *draw = UnpublishedDrawOf(step.phase);
  Check(draw && draw->drawn == 1 && draw->drawTicks == 1,
        "a draw records its units and counts only ticks that drew");
  const PhaseChange over =
      OnEvent(kActor, Drawing(), UnpublishedDrawn{99, true}, kAtFrame30);
  const UnpublishedDraw *clamped = UnpublishedDrawOf(over.phase);
  Check(clamped && clamped->drawn == 3,
        "a draw never counts more units than it has");
  const PhaseChange started =
      OnEvent(kActor, Queued(), ResizeStarted{4}, {40, kHalf});
  const UnpublishedDraw *resize = UnpublishedDrawOf(started.phase);
  Check(resize && resize->total == 4 && resize->drawn == 0 &&
            resize->beganFrame == 40,
        "a started resize draws its units from the frame it began");
  const PhaseChange covered =
      OnEvent(kActor, Queued(), ResizeStarted{4, 3}, {40, kHalf});
  const UnpublishedDraw *partly = UnpublishedDrawOf(covered.phase);
  Check(partly && partly->drawn == 3,
        "a resize starts with the units the engine had drawn already");
  const PhaseChange overCovered =
      OnEvent(kActor, Queued(), ResizeStarted{4, 9}, {40, kHalf});
  const UnpublishedDraw *clampedStart = UnpublishedDrawOf(overCovered.phase);
  Check(clampedStart && clampedStart->drawn == 4,
        "a resize never starts with more units drawn than it has");
  for (const RenderPhase &phase : {LiveHeld(), Queued()}) {
    const PhaseChange drawn =
        OnEvent(kActor, phase, UnpublishedDrawn{2, true}, kAtFrame30);
    Check(drawn.ignored && drawn.phase == phase,
          "a draw on a phase without an unpublished draw changes nothing");
  }
}

void FailedResizesKeepTheAppliedLevel() {
  const PhaseChange failed =
      OnEvent(kActor, Queued(), ResizeNotStarted{"no stack"}, {30, kHalf});
  Check(TargetLevel(failed.phase) == kFull &&
            AppliedLevel(failed.phase) == kFull &&
            FailedLevelOf(failed.phase) == kHalf,
        "a resize that did not start returns live at its old level");
  Check(WantLevel(kActor, failed.phase, kHalf).phase == failed.phase,
        "the failed level is not asked for again while it stays wanted");
  const PhaseChange kept = SeeActor(kActor, failed.phase, InView(1, kHalf));
  Check(FailedLevelOf(kept.phase) == kHalf,
        "the failed level stays while the wanted level is unchanged");
  const PhaseChange moved = SeeActor(kActor, failed.phase, InView(1, kQuarter));
  Check(!FailedLevelOf(moved.phase) &&
            PhaseName(WantLevel(kActor, moved.phase, kHalf).phase) ==
                "resize_queued",
        "a change of the wanted level allows the failed level again");
  const PhaseChange swap =
      OnEvent(kActor, Running(), ResizeNotSwapped{}, {31, kHalf});
  Check(Is<Live>(swap.phase) && FailedLevelOf(swap.phase) == kHalf &&
            FirstCommandIs<AbandonResize>(swap),
        "a swap that did not match abandons the resize and records the level");
  Check(WantLevel(kActor, swap.phase, kHalf).phase == swap.phase,
        "a resize that did not swap is not retried at once");
}

void ABudgetFailureIsNotRetriedEveryTick() {
  const PhaseChange failed =
      OnEvent(kActor, Queued(), ResizeNotStarted{"no stack"}, {30, kFull});
  const PhaseChange seen = SeeActor(kActor, failed.phase, InView(1, kFull));
  Check(
      FailedLevelOf(seen.phase) == kHalf,
      "a failed budget step-down stays recorded while the screen level holds");
}

void OnlyHeldLiveMayRelease() {
  Check(MayRelease(LiveHeld()) && !MayRelease(LiveReleased()) &&
            !MayRelease(Bare()) && !MayRelease(Awaiting()) &&
            !MayRelease(Drawing()) && !MayRelease(Queued()) &&
            !MayRelease(Running()),
        "only a live actor that holds its intermediates may release them");
  Check(MayStepDown(LiveHeld()) && MayStepDown(LiveReleased()) &&
            !MayStepDown(Bare()) && !MayStepDown(Awaiting()) &&
            !MayStepDown(Drawing()) && !MayStepDown(Queued()) &&
            !MayStepDown(Running()),
        "only a live actor may step down for the budget");
  Check(MayEvict(LiveHeld()) && MayEvict(Bare()) && MayEvict(Awaiting()) &&
            MayEvict(Drawing()) && !MayEvict(Queued()) && !MayEvict(Running()),
        "every actor may be evicted except during a resize");
  for (const RenderPhase &phase :
       {LiveReleased(), Bare(), Awaiting(), Drawing(), Queued(), Running()}) {
    Check(Commands(ReleaseForBudget(kActor, phase, 64), 0),
          "a budget release releases only a held live actor");
  }
  const PhaseChange released = ReleaseForBudget(kActor, LiveHeld(), 64);
  const ReleaseIntermediates *command =
      FirstCommandIs<ReleaseIntermediates>(released)
          ? Get<ReleaseIntermediates>(released.commands.front())
          : nullptr;
  Check(command && command->reason == ReleaseReason::kBudget &&
            command->bytes == 64 && !MayRelease(released.phase),
        "a budget release of a held live actor releases once");
  Check(Commands(SeeActor(kActor, Drawing(), OutOfView(100000)), 0),
        "a first-drawing actor out of view releases nothing");
}

void SwapEndsHeldAtTheNewLevel() {
  const PhaseChange swapped =
      OnEvent(kActor, Running(), ResizeSwapped{}, kAtFrame30);
  Check(HoldingOf(swapped.phase) == IntermediateHolding::kHeld &&
            AppliedLevel(swapped.phase) == kHalf &&
            FirstCommandIs<SetAppliedLevel>(swapped),
        "a swap from released intermediates ends held at the new level");
  Check(HoldingOf(Running()) == IntermediateHolding::kReleased,
        "a resize keeps the attached render's holding until the swap");
}

void AResizeInViewHoldsItsIntermediates() {
  const PhaseChange seen = SeeActor(kActor, Running(), InView(1, kHalf));
  Check(HoldingOf(seen.phase) == IntermediateHolding::kHeld &&
            Commands(seen, 0),
        "an attached render drawn in view during a resize holds again");
  const PhaseChange hidden = SeeActor(kActor, Running(), OutOfView(100000));
  Check(HoldingOf(hidden.phase) == IntermediateHolding::kReleased &&
            Commands(hidden, 0),
        "a resize out of view releases nothing");
}

void OutOfViewReleaseWaitsForTheHold() {
  PhaseChange step = SeeActor(kActor, LiveHeld(), OutOfView(1000));
  Check(Commands(step, 0), "leaving the view starts the hold");
  step = SeeActor(kActor, std::move(step.phase), OutOfView(5999));
  Check(Commands(step, 0), "nothing is released before the hold ends");
  step = SeeActor(kActor, std::move(step.phase), OutOfView(6000));
  const ReleaseIntermediates *command =
      FirstCommandIs<ReleaseIntermediates>(step)
          ? Get<ReleaseIntermediates>(step.commands.front())
          : nullptr;
  Check(command && command->reason == ReleaseReason::kOutOfView &&
            HoldingOf(step.phase) == IntermediateHolding::kReleased,
        "the hold releases the intermediates once");
  step = SeeActor(kActor, std::move(step.phase), OutOfView(9000));
  Check(Commands(step, 0), "a released actor is not released again");
  step = SeeActor(kActor, std::move(step.phase), InView(9100, kFull));
  Check(HoldingOf(step.phase) == IntermediateHolding::kHeld,
        "coming into view holds the intermediates again");
}

void AFailedResizeStartsAFreshHold() {
  PhaseChange held = SeeActor(kActor, LiveHeld(), OutOfView(1000));
  const PhaseChange queued = WantLevel(kActor, std::move(held.phase), kHalf);
  const PhaseChange failed =
      OnEvent(kActor, queued.phase, ResizeNotStarted{"no stack"}, {30, kHalf});
  const PhaseChange later = SeeActor(kActor, failed.phase, OutOfView(100000));
  Check(Commands(later, 0),
        "a hold from before a resize does not release after it at once");
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
  Check(PhaseName(
            EndFirstDraw(WantLevel(kActor, Drawing(), kFull).phase).phase) ==
            "live",
        "a first draw that wants its own level ends live");
  for (const RenderPhase &phase : {LiveHeld(), Bare(), Awaiting(), Running()}) {
    Check(EndFirstDraw(phase).phase == phase,
          "ending a first draw outside one changes nothing");
  }
}

void LevelRequestsKeepOrAbandonWork() {
  RenderPhase small = LiveHeld();
  if (Live *live = Get<Live>(small)) {
    live->smallOnScreen = Hold{}.Update(true, 100);
    live->stepUpBlockedFrom = kHalf;
  }
  Check(WantLevel(kActor, small, kFull).phase == small,
        "asking for the current level keeps the live state");
  const PhaseChange changed = WantLevel(kActor, Running(), kQuarter);
  Check(PhaseName(changed.phase) == "resize_queued" &&
            TargetLevel(changed.phase) == kQuarter &&
            FirstCommandIs<AbandonResize>(changed),
        "a new level during a running resize abandons its work");
  const PhaseChange back = WantLevel(kActor, Running(), kFull);
  Check(Is<Live>(back.phase) && AppliedLevel(back.phase) == kFull &&
            FirstCommandIs<AbandonResize>(back),
        "asking for the old level during a running resize ends live");
  Check(Commands(WantLevel(kActor, Running(), kHalf), 0),
        "asking again for the running resize's level changes nothing");
  Check(Commands(WantLevel(kActor, Queued(), kQuarter), 0),
        "a queued resize has no work to abandon");
  Check(WantLevel(kActor, Bare(), kHalf).phase == Bare(),
        "an actor without a render takes no level");
}

void AFailedLevelSurvivesOtherResizes() {
  RenderPhase phase = LiveWith(IntermediatesHeld{});
  if (Live *live = Get<Live>(phase)) {
    live->failed = FailedLevel{kHalf, kHalf};
  }
  const PhaseChange quarter = WantLevel(kActor, phase, kQuarter);
  Check(FailedLevelOf(quarter.phase) == kHalf &&
            Commands(WantLevel(kActor, quarter.phase, kHalf), 0) &&
            TargetLevel(WantLevel(kActor, quarter.phase, kHalf).phase) ==
                kQuarter,
        "a queued resize keeps refusing the failed level");
  const PhaseChange back = WantLevel(kActor, quarter.phase, kFull);
  Check(Is<Live>(back.phase) && FailedLevelOf(back.phase) == kHalf &&
            WantLevel(kActor, back.phase, kHalf).phase == back.phase,
        "returning to the old level keeps the failed level");
  const PhaseChange running =
      OnEvent(kActor, quarter.phase, ResizeStarted{2}, {50, kHalf});
  const PhaseChange swapped =
      OnEvent(kActor, running.phase, ResizeSwapped{}, {51, kHalf});
  Check(FailedLevelOf(swapped.phase) == kHalf,
        "a swap to another level keeps the failed level");
  const PhaseChange seen = SeeActor(kActor, quarter.phase, InView(1, kQuarter));
  Check(!FailedLevelOf(seen.phase),
        "a resize forgets the failed level once the wanted level changes");
}

void AwaitingTakesANewJobAndAnInstall() {
  const PhaseChange rejob =
      OnEvent(kActor, Awaiting(), PlanAwaited{kOtherJob}, kAtFrame30);
  Check(AwaitedJob(rejob.phase) == kOtherJob,
        "a rebuild that awaits again waits for the new job");
  const PhaseChange installed =
      OnEvent(kActor, Awaiting(), Installed{kQuarter, 2, {}, 0}, kAtFrame30);
  const SetAppliedLevel *applied =
      FirstCommandIs<SetAppliedLevel>(installed)
          ? Get<SetAppliedLevel>(installed.commands.front())
          : nullptr;
  Check(applied && applied->level == kQuarter && applied->actor == kActor,
        "an install applies its own level");
}

void HoldsRestartAndReset() {
  PhaseChange step = SeeActor(kActor, LiveHeld(), OutOfView(1000));
  step = SeeActor(kActor, std::move(step.phase), InView(4000, kFull));
  step = SeeActor(kActor, std::move(step.phase), OutOfView(6000));
  Check(Commands(step, 0), "coming into view restarts the out-of-view hold");
  step = SeeActor(kActor, std::move(step.phase), OutOfView(11000));
  Check(Commands(step, 1), "the restarted hold releases after its delay");
  PhaseChange small =
      SeeActor(kActor, LiveHeld(), {true, true, kFull, 1000, 5000, 0});
  small = SeeActor(kActor, std::move(small.phase),
                   {true, false, kFull, 2000, 5000, 0});
  Check(!ScreenStepDownDue(small.phase, 9000, 2000),
        "an actor no longer small clears its step-down hold");
}

void LiveStateResetsWithANewLive() {
  RenderPhase phase = LiveHeld();
  if (Live *live = Get<Live>(phase)) {
    live->stepUpBlockedFrom = kFull;
    live->smallOnScreen = Hold{}.Update(true, 100);
  }
  const PhaseChange queued = WantLevel(kActor, std::move(phase), kHalf);
  const PhaseChange running =
      OnEvent(kActor, queued.phase, ResizeStarted{1}, {60, kHalf});
  const PhaseChange swapped =
      OnEvent(kActor, running.phase, ResizeSwapped{}, {61, kHalf});
  const Live *live = Get<Live>(swapped.phase);
  Check(live && !live->stepUpBlockedFrom && !live->smallOnScreen.since,
        "the step-down hold and the blocked step-up reset in a new live");
}

void PlanFailuresMatchTheAwaitedJob() {
  const PhaseChange stale =
      FailPlan(kActor, Awaiting(), PlanJobFailed{kOtherJob, "lowering"});
  Check(AwaitedJob(stale.phase) == kJob && Commands(stale, 0),
        "a failure of another job leaves the awaited job");
  const PhaseChange failed =
      FailPlan(kActor, Awaiting(), PlanJobFailed{kJob, "lowering"});
  const FailAwaitedRender *command =
      FirstCommandIs<FailAwaitedRender>(failed)
          ? Get<FailAwaitedRender>(failed.commands.front())
          : nullptr;
  Check(Is<WithoutRender>(failed.phase) && command &&
            command->problem == "lowering" && !AppliedLevel(failed.phase),
        "a failure of the awaited job fails its render and leaves it bare");
  for (const RenderPhase &phase : {LiveHeld(), Bare(), Drawing(), Running()}) {
    Check(FailPlan(kActor, phase, PlanJobFailed{kJob, "lowering"}).phase ==
              phase,
          "a plan failure outside awaiting changes nothing");
  }
  const PhaseChange built = PlanBuilt(kActor, Awaiting(), kJob, {kHalf, 64});
  const BuildAwaitedRender *build =
      FirstCommandIs<BuildAwaitedRender>(built)
          ? Get<BuildAwaitedRender>(built.commands.front())
          : nullptr;
  Check(built.phase == Awaiting() && build && build->bound.finest == kHalf,
        "a built plan job asks for the awaited render with its bound");
  Check(Commands(PlanBuilt(kActor, Awaiting(), kOtherJob, {}), 0) &&
            Commands(PlanBuilt(kActor, LiveHeld(), kJob, {}), 0),
        "only the awaited job builds the render");
}

void QueriesFollowThePhase() {
  Check(!TargetLevel(Awaiting()) && !AppliedLevel(Awaiting()),
        "an awaiting actor has no level yet");
  Check(!AppliedLevel(Bare()) &&
            Commands(SeeActor(kActor, Bare(), OutOfView(100000)), 0),
        "an actor without a render has no level and takes no decision");
  Check(TargetLevel(Running()) == kHalf && AppliedLevel(Running()) == kFull,
        "a resize targets its new level and applies its old one");
  const std::optional<ResizeProgress> progress = ResizeProgressOf(Running());
  Check(!ResizeProgressOf(Queued()) && progress && progress->beganFrame == 10,
        "only a running resize has progress");
  Check(UnpublishedDrawOf(Drawing()) && UnpublishedDrawOf(Running()) &&
            !UnpublishedDrawOf(Queued()) && !UnpublishedDrawOf(LiveHeld()),
        "first draws and running resizes have an unpublished draw");
  const PhaseChange small =
      SeeActor(kActor, LiveHeld(), {true, true, kFull, 1000, 5000, 0});
  Check(!ScreenStepDownDue(small.phase, 2999, 2000) &&
            ScreenStepDownDue(small.phase, 3000, 2000),
        "the screen step-down is due after the hold");
}
}

int main() {
  EveryEventHasItsNextPhase();
  TheFirstEventCreatesThePhase();
  DrawsCountUnitsAndTicks();
  FailedResizesKeepTheAppliedLevel();
  ABudgetFailureIsNotRetriedEveryTick();
  OnlyHeldLiveMayRelease();
  SwapEndsHeldAtTheNewLevel();
  AResizeInViewHoldsItsIntermediates();
  OutOfViewReleaseWaitsForTheHold();
  AFailedResizeStartsAFreshHold();
  FirstDrawQueuesALevel();
  LevelRequestsKeepOrAbandonWork();
  AFailedLevelSurvivesOtherResizes();
  AwaitingTakesANewJobAndAnInstall();
  HoldsRestartAndReset();
  LiveStateResetsWithANewLive();
  PlanFailuresMatchTheAwaitedJob();
  QueriesFollowThePhase();
  return test::Finish("orchestration_phasechanges");
}
