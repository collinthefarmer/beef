// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/Lifecycle.h"
#include "orchestration/PhaseChanges.h"
#include "orchestration/Tick.h"
#include "test_support.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Orchestration;
using test::Check;

namespace {
constexpr ActorId kActor{40};
constexpr ActorId kOther{41};
constexpr ActorId kPlayer{0x14};
constexpr PlanJobId kJob{7};
constexpr ResolutionLevel kFull = ResolutionLevel::kFull;
constexpr ResolutionLevel kHalf = ResolutionLevel::kHalf;
constexpr ResolutionLevel kQuarter = ResolutionLevel::kQuarter;
constexpr Millis kNow = 100000;
constexpr std::uint64_t kFrame = 500;

Sighting InView(float a_fraction) {
  return {ActorRole::kOther, true, true, a_fraction, 100.0f};
}

Sighting OutOfView(float a_fraction) {
  return {ActorRole::kOther, true, false, a_fraction, 100.0f};
}

Sighting PlayerSight() { return {ActorRole::kPlayer, true, true, 0.05f, 0.0f}; }

PerLevel<std::optional<LevelCost>> Costs() {
  PerLevel<std::optional<LevelCost>> costs;
  costs.At(kFull) = LevelCost{1000, 400};
  costs.At(kHalf) = LevelCost{300, 100};
  costs.At(kQuarter) = LevelCost{80, 30};
  return costs;
}

Shown ShownAt(ResolutionLevel a_level) {
  return Shown{a_level, Held{}, {}, std::nullopt, std::nullopt};
}

RenderPhase DrawingAt(std::size_t a_total, std::size_t a_drawn,
                      std::uint64_t a_ticks) {
  return Drawing{kHalf, UnpublishedDraw{a_total, a_drawn, 400, a_ticks},
                 std::nullopt};
}

RenderPhase QueuedResize(Level a_from, Level a_to) {
  return Queued{Resize{a_from, a_to, IntermediateHolding::kHeld}};
}

RenderPhase RunningAt(std::size_t a_total, std::size_t a_drawn) {
  return Running{Resize{kQuarter, kHalf, IntermediateHolding::kHeld},
                 UnpublishedDraw{a_total, a_drawn, kFrame, 1}};
}

TrackedActor Tracked(RenderPhase a_phase, Sighting a_sight) {
  TrackedActor record;
  record.sight = a_sight;
  record.render = InstalledRender{std::move(a_phase), Costs(), std::nullopt};
  return record;
}

World WorldOf(std::vector<std::pair<ActorId, TrackedActor>> a_records) {
  World world;
  for (auto &[actor, record] : a_records) {
    world.actors[actor] = std::move(record);
  }
  return world;
}

TickFacts FactsAt(Millis a_now) {
  TickFacts facts;
  facts.now = a_now;
  facts.frame = kFrame;
  return facts;
}

const RenderPhase *PhaseOf(const TickOutput &a_out, ActorId a_actor) {
  const auto found = a_out.world.actors.find(a_actor);
  if (found == a_out.world.actors.end() || !found->second.render) {
    return nullptr;
  }
  return &found->second.render->phase;
}

std::string_view PhaseNameOf(const TickOutput &a_out, ActorId a_actor) {
  const RenderPhase *phase = PhaseOf(a_out, a_actor);
  return phase ? PhaseName(*phase) : std::string_view{"none"};
}

template <class T> std::vector<T> CommandsOf(const TickOutput &a_out) {
  std::vector<T> found;
  for (const Command &command : a_out.commands) {
    if (const T *typed = Get<T>(command)) {
      found.push_back(*typed);
    }
  }
  return found;
}

std::optional<std::string> FieldOf(const TraceRecord &a_record,
                                   std::string_view a_name) {
  for (const Trace::Field &field : a_record.fields) {
    if (field.name == a_name) {
      return field.value;
    }
  }
  return std::nullopt;
}

std::vector<TraceRecord> TracesOf(const TickOutput &a_out,
                                  std::string_view a_action) {
  std::vector<TraceRecord> found;
  for (const TraceRecord &record : a_out.trace) {
    if (FieldOf(record, "action") == a_action) {
      found.push_back(record);
    }
  }
  return found;
}

bool HasField(const TraceRecord &a_record, std::string_view a_name,
              std::string_view a_value) {
  return FieldOf(a_record, a_name) == a_value;
}

std::optional<TickActor> TickOf(const TickOutput &a_out, ActorId a_actor) {
  for (const TickActor &tick : CommandsOf<TickActor>(a_out)) {
    if (tick.actor == a_actor) {
      return tick;
    }
  }
  return std::nullopt;
}

TickOutput Results(World a_world, std::vector<CommandResult> a_results,
                   std::map<ActorId, Sighting> a_sightings = {}) {
  TickFacts facts = FactsAt(kNow);
  facts.results = std::move(a_results);
  facts.sightings = std::move(a_sightings);
  return RecordRenderResults(std::move(a_world), facts);
}

void SightingsUpdateKnownActorsOnly() {
  const TickOutput out =
      Results(WorldOf({{kActor, Tracked(ShownAt(kFull), InView(0.6f))}}), {},
              {{kActor, OutOfView(0.2f)}, {kOther, InView(0.5f)}});
  Check(out.world.actors.at(kActor).sight == OutOfView(0.2f),
        "a sighting replaces the actor's last one");
  Check(!out.world.actors.contains(kOther),
        "a sighting alone does not create an actor record");
}

void InstalledCreatesAFirstDraw() {
  const TickOutput out =
      Results(World{}, {ActorEvent{kActor, Installed{kHalf, 4, Costs(), 300}}});
  const RenderPhase *phase = PhaseOf(out, kActor);
  const UnpublishedDraw *draw = phase ? UnpublishedDrawOf(*phase) : nullptr;
  Check(phase && Is<Drawing>(*phase) && draw && draw->total == 4 &&
            draw->beganFrame == kFrame,
        "an install creates a first draw of its slots from this frame");
  Check(out.world.actors.at(kActor).render->levelCosts == Costs(),
        "an install stores its level costs");
  const std::vector<SetAppliedLevel> applied = CommandsOf<SetAppliedLevel>(out);
  Check(applied.size() == 1 && applied.front().level == kHalf,
        "an install applies its level");
  const std::vector<TraceRecord> phases = TracesOf(out, "actor_phase");
  Check(phases.size() == 1 && HasField(phases.front(), "from", "none") &&
            HasField(phases.front(), "to", "drawing") &&
            HasField(phases.front(), "event", "installed") &&
            HasField(phases.front(), "actor", "40"),
        "an install traces the phase it creates");
}

void OtherCreatingEventsCreateTheirPhases() {
  const TickOutput awaited =
      Results(World{}, {ActorEvent{kActor, PlanAwaited{kJob}}});
  Check(PhaseNameOf(awaited, kActor) == "planning",
        "an install that awaits a plan creates an awaiting phase");
  const TickOutput bare =
      Results(World{}, {ActorEvent{kActor, InstalledWithoutRender{}}});
  Check(PhaseNameOf(bare, kActor) == "empty" && bare.commands.empty(),
        "an install without a render creates a phase without a render");
}

void AnEventWithoutARenderIsIgnored() {
  const TickOutput out =
      Results(World{}, {ActorEvent{kActor, UnpublishedDrawn{2, true}}});
  Check(!out.world.actors.contains(kActor),
        "an event for an actor without a render creates nothing");
  const std::vector<TraceRecord> ignored = TracesOf(out, "actor_phase_ignored");
  Check(ignored.size() == 1 && HasField(ignored.front(), "phase", "none") &&
            HasField(ignored.front(), "event", "unpublished_drawn"),
        "the ignored event is traced");
}

void AnEventThePhaseCannotTakeIsIgnored() {
  const TickOutput out =
      Results(WorldOf({{kActor, Tracked(ShownAt(kFull), InView(0.6f))}}),
              {ActorEvent{kActor, ResizeSwapped{}}});
  Check(*PhaseOf(out, kActor) == RenderPhase{ShownAt(kFull)},
        "an ignored event leaves the phase as it was");
  const std::vector<TraceRecord> ignored = TracesOf(out, "actor_phase_ignored");
  Check(ignored.size() == 1 && HasField(ignored.front(), "phase", "shown") &&
            HasField(ignored.front(), "event", "resize_swapped") &&
            TracesOf(out, "actor_phase").empty(),
        "an ignored event is traced and changes no phase");
}

void AFailedResizeRemembersTheWantedLevel() {
  const TickOutput out = Results(
      WorldOf({{kActor, Tracked(QueuedResize(kFull, kHalf), InView(0.6f))}}),
      {ActorEvent{kActor, ResizeNotStarted{"no stack"}}},
      {{kActor, InView(0.3f)}});
  const Shown *shown = Get<Shown>(*PhaseOf(out, kActor));
  Check(shown && shown->level == kFull && shown->failed &&
            shown->failed->level == kHalf && shown->failed->wantedThen == kHalf,
        "a resize that does not start records the level wanted at this "
        "tick's sighting");
  const std::vector<TraceRecord> phases = TracesOf(out, "actor_phase");
  Check(phases.size() == 1 &&
            HasField(phases.front(), "from", "resize_queued") &&
            HasField(phases.front(), "to", "shown") &&
            HasField(phases.front(), "event", "resize_not_started"),
        "the return to live is traced");
}

void AnInstallAfterAPlanReplacesTheCosts() {
  TrackedActor record = Tracked(Planning{kJob}, InView(0.6f));
  record.render->levelCosts = {};
  const TickOutput out =
      Results(WorldOf({{kActor, std::move(record)}}),
              {ActorEvent{kActor, Installed{kFull, 2, Costs(), 1000}}});
  Check(PhaseNameOf(out, kActor) == "drawing" &&
            out.world.actors.at(kActor).render->levelCosts == Costs(),
        "the build after a plan stores the new level costs");
}

TickOutput Jobs(World a_world, std::vector<PlanJobDone> a_jobs) {
  TickFacts facts = FactsAt(kNow);
  facts.jobs = std::move(a_jobs);
  return RecordRenderResults(std::move(a_world), facts);
}

World PlanningWorld(Sighting a_sight,
                    std::optional<ResolutionLevel> a_reinstall) {
  TrackedActor record = Tracked(Planning{kJob}, a_sight);
  record.reinstallLevel = a_reinstall;
  World world = WorldOf({{kActor, std::move(record)}});
  world.budget = BudgetState{5000, 1200, Pressure::kNone};
  return world;
}

void ABuiltPlanBuildsTheAwaitedRender() {
  const TickOutput reinstall =
      Jobs(PlanningWorld(InView(0.6f), kQuarter), {PlanJobBuilt{kJob}});
  const std::vector<BuildAwaitedRender> builds =
      CommandsOf<BuildAwaitedRender>(reinstall);
  Check(builds.size() == 1 && builds.front().actor == kActor &&
            builds.front().bounds == InstallBounds{kQuarter, 3800},
        "a reinstall builds at its old level within the budget's headroom");
  const TickOutput fresh =
      Jobs(PlanningWorld(InView(0.3f), std::nullopt), {PlanJobBuilt{kJob}});
  const std::vector<BuildAwaitedRender> freshBuilds =
      CommandsOf<BuildAwaitedRender>(fresh);
  Check(freshBuilds.size() == 1 &&
            freshBuilds.front().bounds == InstallBounds{kHalf, 3800},
        "a first install builds at the wanted level within the headroom");
  const TickOutput other = Jobs(PlanningWorld(InView(0.6f), std::nullopt),
                                {PlanJobBuilt{PlanJobId{9}}});
  Check(other.commands.empty() && PhaseNameOf(other, kActor) == "planning",
        "another job's plan builds nothing");
}

void ThePlayerBuildsWithoutHeadroom() {
  TrackedActor record = Tracked(Planning{kJob}, PlayerSight());
  World world = WorldOf({{kPlayer, std::move(record)}});
  world.budget = BudgetState{5000, 1200, Pressure::kNone};
  const std::vector<BuildAwaitedRender> builds = CommandsOf<BuildAwaitedRender>(
      Jobs(std::move(world), {PlanJobBuilt{kJob}}));
  Check(builds.size() == 1 &&
            builds.front().bounds == InstallBounds{kFull, std::nullopt},
        "the player's render builds at full with no headroom");
}

void AFailedPlanEndsWithoutARender() {
  const TickOutput out = Jobs(PlanningWorld(InView(0.6f), std::nullopt),
                              {PlanJobFailed{kJob, "bad recipe"}});
  const std::vector<FailAwaitedRender> failed =
      CommandsOf<FailAwaitedRender>(out);
  Check(PhaseNameOf(out, kActor) == "empty" && failed.size() == 1 &&
            failed.front().problem == "bad recipe",
        "a failed plan fails the awaited render with its problem");
  const std::vector<TraceRecord> phases = TracesOf(out, "actor_phase");
  Check(phases.size() == 1 && HasField(phases.front(), "event", "plan_failed"),
        "the failed plan's phase change is traced");
}

void ARenderInViewRecordsItsFrame() {
  const TickOutput out =
      Results(WorldOf({{kActor, Tracked(ShownAt(kFull), InView(0.6f))},
                       {kOther, Tracked(ShownAt(kFull), OutOfView(0.6f))}}),
              {});
  Check(out.world.actors.at(kActor).render->renderedFrame == kFrame,
        "an actor in view records this frame as rendered");
  Check(!out.world.actors.at(kOther).render->renderedFrame,
        "an actor out of view records no rendered frame");
}

TickOutput Lifecycle(World a_world, Millis a_now = kNow,
                     TickSettings a_settings = {}) {
  TickFacts facts = FactsAt(a_now);
  facts.settings = a_settings;
  return RunLifecycle(std::move(a_world), facts);
}

void OutOfViewReleasesAfterTheDelay() {
  World world = WorldOf({{kActor, Tracked(ShownAt(kHalf), OutOfView(0.3f))}});
  TickOutput first = Lifecycle(std::move(world), kNow);
  Check(CommandsOf<ReleaseIntermediates>(first).empty(),
        "an actor that just left the view keeps its intermediates");
  const TickOutput later = Lifecycle(std::move(first.world), kNow + 5000);
  const std::vector<ReleaseIntermediates> released =
      CommandsOf<ReleaseIntermediates>(later);
  Check(released.size() == 1 && released.front().bytes == 200 &&
            released.front().reason == ReleaseReason::kOutOfView,
        "after the delay the actor releases its level's intermediate bytes");
}

World StepUpWorld(BudgetState a_budget) {
  World world = WorldOf({{kActor, Tracked(ShownAt(kHalf), InView(0.6f))}});
  world.budget = a_budget;
  return world;
}

void AnAdmittedStepUpChargesAndResizes() {
  const TickOutput out =
      Lifecycle(StepUpWorld(BudgetState{5000, 1000, Pressure::kNone}));
  Check(out.world.budget.planned == 2000,
        "an admitted step-up charges the target level's full cost");
  Check(PhaseNameOf(out, kActor) == "resize_queued",
        "an admitted step-up queues a resize");
  const std::vector<TraceRecord> levels = TracesOf(out, "resolution_level");
  Check(levels.size() == 1 && HasField(levels.front(), "from", "half") &&
            HasField(levels.front(), "to", "full") &&
            HasField(levels.front(), "reason", "screen") &&
            HasField(levels.front(), "screen_fraction", "0.600"),
        "the step-up is traced with today's fields");
  Check(CommandsOf<StartResize>(out).empty(),
        "a resize queued this tick starts no earlier than the next tick");
  const TickOutput next = Lifecycle(out.world);
  const std::vector<StartResize> started = CommandsOf<StartResize>(next);
  Check(started.size() == 1 && started.front().to == kFull,
        "the queued resize starts in the next tick");
}

void ABlockedStepUpIsRecordedOnce() {
  TickOutput first =
      Lifecycle(StepUpWorld(BudgetState{5000, 4500, Pressure::kNone}));
  const Shown *shown = Get<Shown>(*PhaseOf(first, kActor));
  Check(shown && shown->waitingForRoomFrom == kHalf &&
            first.world.budget.planned == 4500,
        "a blocked step-up keeps the level, charges nothing and records "
        "where it was blocked");
  const std::vector<TraceRecord> blocked =
      TracesOf(first, "resolution_blocked");
  Check(blocked.size() == 1 &&
            HasField(blocked.front(), "reason", "over allowance") &&
            HasField(blocked.front(), "added_bytes", "1000") &&
            HasField(blocked.front(), "planned_bytes", "4500") &&
            HasField(blocked.front(), "allowance_bytes", "5000"),
        "the block is traced with today's fields");
  const TickOutput second = Lifecycle(std::move(first.world));
  Check(TracesOf(second, "resolution_blocked").empty(),
        "a block from the same level is traced once");
}

void ThePlayerStepsUpWithoutAdmission() {
  World world = WorldOf({{kPlayer, Tracked(ShownAt(kHalf), PlayerSight())}});
  world.budget = BudgetState{5000, 4500, Pressure::kOver};
  const TickOutput out = Lifecycle(std::move(world));
  Check(PhaseNameOf(out, kPlayer) == "resize_queued" &&
            out.world.budget.planned == 4500,
        "the player steps up without admission or charge");
}

void ASmallActorStepsDownAfterTheHold() {
  World world = WorldOf({{kActor, Tracked(ShownAt(kFull), InView(0.3f))}});
  TickOutput first = Lifecycle(std::move(world), kNow);
  Check(PhaseNameOf(first, kActor) == "shown" && first.commands.size() == 1,
        "a small actor keeps its level while the hold runs");
  const TickOutput later = Lifecycle(std::move(first.world), kNow + 2000);
  const std::vector<TraceRecord> levels = TracesOf(later, "resolution_level");
  Check(PhaseNameOf(later, kActor) == "resize_queued" && levels.size() == 1 &&
            HasField(levels.front(), "to", "half"),
        "after the hold the actor steps down one level");
}

void AFailedLevelIsNotAskedAgain() {
  Shown live = ShownAt(kHalf);
  live.failed = FailedLevel{kFull, kFull};
  World world = WorldOf({{kActor, Tracked(live, InView(0.6f))}});
  world.budget = BudgetState{5000, 1000, Pressure::kNone};
  const TickOutput out = Lifecycle(std::move(world));
  Check(PhaseNameOf(out, kActor) == "shown" &&
            TracesOf(out, "resolution_level").empty() &&
            out.world.budget.planned == 1000,
        "a level that failed while still wanted is not asked for again");
}

void OneResizeRunsAtATime() {
  const TickOutput busy = Lifecycle(WorldOf(
      {{kActor, Tracked(RunningAt(3, 1), InView(0.3f))},
       {kOther, Tracked(QueuedResize(kHalf, kQuarter), InView(0.15f))}}));
  Check(CommandsOf<StartResize>(busy).empty(),
        "no resize starts while another runs");
  const TickOutput idle = Lifecycle(WorldOf(
      {{kActor, Tracked(QueuedResize(kHalf, kQuarter), OutOfView(0.6f))},
       {kOther, Tracked(QueuedResize(kHalf, kQuarter), InView(0.15f))}}));
  const std::vector<StartResize> started = CommandsOf<StartResize>(idle);
  Check(started.size() == 1 && started.front().actor == kOther &&
            started.front().to == kQuarter,
        "an actor in view resizes before one out of view");
}

void ADrawnResizeSwapsAndFreesItsSlot() {
  const TickOutput out = Lifecycle(WorldOf(
      {{kActor, Tracked(RunningAt(3, 3), InView(0.3f))},
       {kOther, Tracked(QueuedResize(kHalf, kQuarter), InView(0.15f))}}));
  const std::vector<SwapResize> swapped = CommandsOf<SwapResize>(out);
  Check(swapped.size() == 1 && swapped.front().actor == kActor,
        "a fully drawn resize swaps");
  Check(CommandsOf<StartResize>(out).size() == 1,
        "the swapped resize no longer blocks the next start");
  Check(Is<SwapResize>(out.commands.front()) &&
            Is<StartResize>(out.commands.at(1)),
        "the swap comes before the start");
  Check(CommandsOf<DrawUnpublished>(out).empty(),
        "a swapping resize draws no more");
}

void AResizeOutOfViewSwapsAtOnce() {
  const TickOutput out =
      Lifecycle(WorldOf({{kActor, Tracked(RunningAt(3, 0), OutOfView(0.6f))}}));
  Check(CommandsOf<SwapResize>(out).size() == 1,
        "a resize out of view swaps without drawing");
  const TickOutput drawing =
      Lifecycle(WorldOf({{kActor, Tracked(RunningAt(3, 0), InView(0.3f))}}));
  Check(CommandsOf<SwapResize>(drawing).empty(),
        "a resize in view that is still drawing does not swap");
}

void ADrawnFirstDrawEnds() {
  const TickOutput out =
      Lifecycle(WorldOf({{kActor, Tracked(DrawingAt(3, 3, 2), InView(0.3f))}}));
  Check(PhaseNameOf(out, kActor) == "shown", "a drawn first draw goes live");
  const std::vector<TraceRecord> ends = TracesOf(out, "first_draw");
  Check(ends.size() == 1 && HasField(ends.front(), "end", "drawn") &&
            HasField(ends.front(), "frames", "100") &&
            HasField(ends.front(), "draw_ticks", "2") &&
            HasField(ends.front(), "stacks", "3") &&
            HasField(ends.front(), "drawn", "3"),
        "the end of the first draw is traced with today's fields");
  const std::vector<TraceRecord> phases = TracesOf(out, "actor_phase");
  Check(phases.size() == 1 &&
            HasField(phases.front(), "event", "first_draw_ended"),
        "the phase change is traced");
  const std::optional<TickActor> tick = TickOf(out, kActor);
  Check(tick && !tick->concealSlots && !tick->hideShells && !tick->hideLights,
        "a live actor renders in the same tick its first draw ends");
}

void AFirstDrawEndsAtTheTickLimit() {
  const TickOutput out = Lifecycle(
      WorldOf({{kActor, Tracked(DrawingAt(3, 1, 60), InView(0.3f))}}));
  const std::vector<TraceRecord> ends = TracesOf(out, "first_draw");
  Check(ends.size() == 1 && HasField(ends.front(), "end", "tick_limit"),
        "a first draw ends after its tick limit");
  const TickOutput still = Lifecycle(
      WorldOf({{kActor, Tracked(DrawingAt(3, 1, 59), InView(0.3f))}}));
  Check(PhaseNameOf(still, kActor) == "drawing",
        "a first draw under the limit keeps drawing");
}

void AFirstDrawWithANewLevelQueuesAResize() {
  Drawing drawing{kHalf, UnpublishedDraw{2, 2, 400, 1}, kFull};
  const TickOutput out =
      Lifecycle(WorldOf({{kActor, Tracked(drawing, InView(0.6f))}}));
  const Queued *queued = Get<Queued>(*PhaseOf(out, kActor));
  Check(queued && queued->resize.from == kHalf && queued->resize.to == kFull &&
            CommandsOf<StartResize>(out).empty(),
        "a level wanted during the first draw queues a resize that starts "
        "next tick");
}

void TheTickFlagsFollowThePhase() {
  const std::optional<TickActor> drawing = TickOf(
      Lifecycle(WorldOf({{kActor, Tracked(DrawingAt(3, 0, 0), InView(0.3f))}})),
      kActor);
  Check(drawing && drawing->drawStacks && drawing->concealSlots &&
            drawing->hideShells && drawing->hideLights &&
            !drawing->finishApplications,
        "a first draw in view conceals and hides and finishes nothing");
  const std::optional<TickActor> away =
      TickOf(Lifecycle(WorldOf(
                 {{kActor, Tracked(DrawingAt(3, 0, 0), OutOfView(0.3f))}})),
             kActor);
  Check(away && !away->drawStacks && away->finishApplications,
        "a first draw out of view draws nothing and finishes applications");
  const std::optional<TickActor> awaiting = TickOf(
      Lifecycle(WorldOf({{kActor, Tracked(Planning{kJob}, InView(0.3f))}})),
      kActor);
  Check(awaiting && awaiting->hideShells && awaiting->concealSlots &&
            !awaiting->finishApplications,
        "an actor awaiting a plan in view hides its shells, conceals the "
        "slots of a render built from the plan this tick and finishes "
        "nothing");
  TickSettings unpublished;
  unpublished.publishEffects = false;
  const std::optional<TickActor> live = TickOf(
      Lifecycle(WorldOf({{kActor, Tracked(ShownAt(kHalf), InView(0.3f))}}),
                kNow, unpublished),
      kActor);
  Check(live && live->hideShells && !live->concealSlots && !live->hideLights,
        "unpublished effects hide the shells of every actor");
}

void AnActorWithout3DDecidesNothing() {
  Sighting sight = InView(0.6f);
  sight.loaded3D = false;
  const TickOutput out =
      Lifecycle(WorldOf({{kActor, Tracked(DrawingAt(3, 0, 0), sight)}}));
  Check(out.commands.empty() && out.trace.empty(),
        "an actor without 3D gets no command");
}

void UnpublishedDrawsComeLastInPacingOrder() {
  World world =
      WorldOf({{kActor, Tracked(DrawingAt(3, 0, 0), InView(0.2f))},
               {kOther, Tracked(DrawingAt(3, 0, 0), InView(0.4f))},
               {kPlayer, Tracked(DrawingAt(3, 0, 0), PlayerSight())},
               {ActorId{30}, Tracked(DrawingAt(3, 0, 0), OutOfView(0.9f))}});
  TickSettings settings;
  settings.unpublishedDrawBudgetMs = 6;
  const TickOutput out = Lifecycle(std::move(world), kNow, settings);
  Check(!out.commands.empty() && Is<DrawUnpublished>(out.commands.back()) &&
            CommandsOf<DrawUnpublished>(out).size() == 1,
        "one unpublished draw comes last");
  const std::vector<DrawUnpublished> draws = CommandsOf<DrawUnpublished>(out);
  Check(!draws.empty() &&
            draws.front().order ==
                std::vector<ActorId>{kPlayer, kOther, kActor} &&
            draws.front().budgetUs == 6000,
        "the player draws first, then larger actors, within the budget; "
        "actors out of view wait");
  const TickOutput none =
      Lifecycle(WorldOf({{kActor, Tracked(ShownAt(kHalf), InView(0.3f))}}));
  Check(CommandsOf<DrawUnpublished>(none).empty(),
        "nothing unpublished draws nothing");
}

void TheCommandsFollowTheStepOrder() {
  World world = WorldOf({{kActor, Tracked(ShownAt(kHalf), OutOfView(0.3f))},
                         {kOther, Tracked(RunningAt(3, 1), InView(0.3f))}});
  TickOutput first = Lifecycle(std::move(world), kNow);
  const TickOutput out = Lifecycle(std::move(first.world), kNow + 5000);
  std::vector<std::size_t> kinds;
  for (const Command &command : out.commands) {
    kinds.push_back(command.index());
  }
  const std::vector<std::size_t> expected{
      Command{ReleaseIntermediates{}}.index(), Command{TickActor{}}.index(),
      Command{TickActor{}}.index(), Command{DrawUnpublished{}}.index()};
  Check(kinds == expected,
        "releases come before the tick commands and the draw comes last");
}

void EventsBeforeARetireAreDropped() {
  World world;
  world.actors[kActor].sight = InView(0.3f);
  const TickOutput out = Results(
      std::move(world),
      {ActorEvent{kActor, Installed{kFull, 18, Costs(), 0}}, Retired{kActor},
       ActorEvent{kActor, Installed{kFull, 12, Costs(), 0}}});
  const RenderPhase *phase = PhaseOf(out, kActor);
  const UnpublishedDraw *draw = phase ? UnpublishedDrawOf(*phase) : nullptr;
  Check(draw && draw->total == 12,
        "an install after a retire creates the render, and the install the "
        "retire replaced is dropped");
  Check(std::ranges::none_of(
            out.trace,
            [](const TraceRecord &a_record) {
              return std::ranges::any_of(
                  a_record.fields, [](const Trace::Field &a_field) {
                    return a_field.value == "actor_phase_ignored";
                  });
            }),
        "a dropped event is not reported as ignored");
}

void ATickCarriesAnInstallToItsFirstTick() {
  World world;
  world.actors[kActor].sight = InView(0.3f);
  TickFacts facts = FactsAt(kNow);
  facts.sightings = {{kActor, InView(0.3f)}};
  facts.results = {ActorEvent{kActor, Installed{kHalf, 0, Costs(), 300}}};
  const TickOutput out = Tick(std::move(world), facts);
  Check(PhaseNameOf(out, kActor) == "shown",
        "an install with nothing to draw goes live in its first tick");
  Check(!out.commands.empty() && Is<SetAppliedLevel>(out.commands.front()) &&
            Is<TickActor>(out.commands.back()),
        "the install's level comes first and the actor ticks last");
}
}

int main() {
  SightingsUpdateKnownActorsOnly();
  InstalledCreatesAFirstDraw();
  OtherCreatingEventsCreateTheirPhases();
  AnEventWithoutARenderIsIgnored();
  AnEventThePhaseCannotTakeIsIgnored();
  AFailedResizeRemembersTheWantedLevel();
  AnInstallAfterAPlanReplacesTheCosts();
  ABuiltPlanBuildsTheAwaitedRender();
  ThePlayerBuildsWithoutHeadroom();
  AFailedPlanEndsWithoutARender();
  ARenderInViewRecordsItsFrame();
  OutOfViewReleasesAfterTheDelay();
  AnAdmittedStepUpChargesAndResizes();
  ABlockedStepUpIsRecordedOnce();
  ThePlayerStepsUpWithoutAdmission();
  ASmallActorStepsDownAfterTheHold();
  AFailedLevelIsNotAskedAgain();
  OneResizeRunsAtATime();
  ADrawnResizeSwapsAndFreesItsSlot();
  AResizeOutOfViewSwapsAtOnce();
  ADrawnFirstDrawEnds();
  AFirstDrawEndsAtTheTickLimit();
  AFirstDrawWithANewLevelQueuesAResize();
  TheTickFlagsFollowThePhase();
  EventsBeforeARetireAreDropped();
  AnActorWithout3DDecidesNothing();
  UnpublishedDrawsComeLastInPacingOrder();
  TheCommandsFollowTheStepOrder();
  ATickCarriesAnInstallToItsFirstTick();
  return test::Finish("orchestration_lifecycle");
}
