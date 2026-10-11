// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/Budget.h"
#include "orchestration/Tick.h"
#include "test_support.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Orchestration;
using test::Check;

namespace {
constexpr ActorId kFirst{0x100};
constexpr ActorId kSecond{0x200};
constexpr ActorId kThird{0x300};
constexpr ResolutionLevel kFull = ResolutionLevel::kFull;
constexpr ResolutionLevel kHalf = ResolutionLevel::kHalf;
constexpr ResolutionLevel kEighth = ResolutionLevel::kEighth;

constexpr std::uint64_t MiB(std::uint64_t a_count) noexcept {
  return a_count * kMebibyte;
}

using LevelCosts = PerLevel<std::optional<LevelCost>>;

LevelCosts Costs(std::uint64_t a_fullHeld = 400,
                 std::uint64_t a_fullReleased = 300) {
  LevelCosts costs;
  costs.At(kFull) = LevelCost{MiB(a_fullHeld), MiB(a_fullReleased)};
  costs.At(kHalf) = LevelCost{MiB(200), MiB(150)};
  costs.At(ResolutionLevel::kQuarter) = LevelCost{MiB(100), MiB(75)};
  costs.At(kEighth) = LevelCost{MiB(50), MiB(40)};
  return costs;
}

RenderPhase ShownAt(ResolutionLevel a_level,
                    Intermediates a_intermediates = Held{},
                    std::optional<FailedLevel> a_failed = std::nullopt) {
  return Shown{a_level, std::move(a_intermediates), {}, std::nullopt, a_failed};
}

RenderPhase BlockedAt(ResolutionLevel a_level) {
  return Shown{a_level, Held{}, {}, a_level, std::nullopt};
}

RenderPhase RunningResize(Level a_from, Level a_to) {
  return Running{Resize{a_from, a_to, IntermediateHolding::kHeld},
                 UnpublishedDraw{2, 0, 10, 0}};
}

RenderPhase QueuedResize(Level a_from, Level a_to) {
  return Queued{Resize{a_from, a_to, IntermediateHolding::kHeld}};
}

Sighting InView(float a_fraction, std::optional<float> a_distance = 500.0f) {
  return {ActorRole::kOther, true, true, a_fraction, a_distance};
}

Sighting OutOfView(std::optional<float> a_distance = 500.0f) {
  return {ActorRole::kOther, true, false, 0.1f, a_distance};
}

Sighting Player() {
  return {ActorRole::kPlayer, true, true, 0.6f, std::nullopt};
}

ActorRecord Rendered(RenderPhase a_phase, Sighting a_sight,
                     LevelCosts a_costs = Costs()) {
  ActorRecord record;
  record.sight = a_sight;
  record.render = InstalledRender{std::move(a_phase), a_costs, 1};
  return record;
}

ActorRecord Evicted(float a_distance,
                    std::optional<std::uint64_t> a_returnBytes) {
  ActorRecord record;
  record.sight = OutOfView(a_distance);
  record.syncState = Actors::ActorState{
      Actors::ActorPlan{{Actors::PlannedInstance{}}, {}, {}}, {}, {}, {}};
  record.returnBytes = a_returnBytes;
  return record;
}

BudgetSample SampleWithCap(std::uint64_t a_cap) {
  return {VideoMemory{MiB(16384), MiB(4096)}, 0, a_cap};
}

TickFacts FactsWithCap(std::uint64_t a_cap, Millis a_now = 1000,
                       float a_evictDistance = 0.0f) {
  TickFacts facts;
  facts.now = a_now;
  facts.memory = SampleWithCap(a_cap);
  facts.settings.evictDistance = a_evictDistance;
  return facts;
}

const RenderPhase *PhaseOf(const World &a_world, ActorId a_actor) {
  const auto found = a_world.actors.find(a_actor);
  if (found == a_world.actors.end() || !found->second.render) {
    return nullptr;
  }
  return &found->second.render->phase;
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

std::vector<TraceRecord> ActionsOf(const TickOutput &a_output,
                                   std::string_view a_action) {
  std::vector<TraceRecord> found;
  for (const TraceRecord &record : a_output.trace) {
    if (FieldOf(record, "action") == std::optional<std::string>{a_action}) {
      found.push_back(record);
    }
  }
  return found;
}

bool HasLog(const TickOutput &a_output, LogLevel a_level,
            const std::string &a_text) {
  for (const LogRecord &log : a_output.logs) {
    if (log.level == a_level && log.text == a_text) {
      return true;
    }
  }
  return false;
}

template <class T> std::vector<T> CommandsOf(const TickOutput &a_output) {
  std::vector<T> found;
  for (const Command &command : a_output.commands) {
    if (const T *typed = Get<T>(command)) {
      found.push_back(*typed);
    }
  }
  return found;
}

bool FieldsAre(const TraceRecord &a_record,
               const std::vector<std::pair<std::string, std::string>> &a_want) {
  if (a_record.fields.size() != a_want.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a_want.size(); ++i) {
    if (a_record.fields[i].name != a_want[i].first ||
        a_record.fields[i].value != a_want[i].second) {
      return false;
    }
  }
  return true;
}

void EstimateCountsEachRenderAtItsLevelAndHolding() {
  Check(PlannedBytesOf(Rendered(ShownAt(kFull), InView(0.6f))) == MiB(400),
        "a live held render counts its level with intermediates");
  Check(PlannedBytesOf(Rendered(ShownAt(kFull, Freed{}), OutOfView())) ==
            MiB(300),
        "a live released render counts its level without intermediates");
  Check(PlannedBytesOf(Rendered(RunningResize(kFull, kHalf), InView(0.3f))) ==
            MiB(600),
        "a running resize adds its target level in full");
  Check(PlannedBytesOf(Rendered(QueuedResize(kFull, kHalf), InView(0.3f))) ==
            MiB(400),
        "a queued resize adds nothing yet");
  Check(PlannedBytesOf(Rendered(Planning{}, InView(0.6f))) == 0,
        "an awaited plan holds nothing");
  ActorRecord unrendered = Rendered(ShownAt(kFull), InView(0.6f));
  unrendered.render->renderedFrame.reset();
  Check(PlannedBytesOf(unrendered) == 0,
        "a render not yet drawn counts nothing, as the old estimate did");
  World world;
  world.actors[kFirst] = Rendered(ShownAt(kFull), InView(0.6f));
  world.actors[kSecond] = Rendered(ShownAt(kHalf), InView(0.3f));
  world.actors[kThird] = Evicted(900.0f, MiB(10));
  Check(PlannedBytesOf(world) == MiB(600), "the world sums its records");
}

void TheMeasuredHeldBytesSetThePlannedBytes() {
  World world;
  world.actors[kFirst] = Rendered(ShownAt(kFull), InView(0.6f));
  world.actors[kSecond] = Rendered(ShownAt(kFull), InView(0.6f));
  TickFacts facts = FactsWithCap(MiB(4000));
  facts.memory->heldBytes = MiB(500);
  Check(RunBudget(world, facts).world.budget.planned == MiB(500),
        "the engine's held bytes, a shared target counted once, set the "
        "planned bytes");
  facts.memory->heldBytes.reset();
  Check(RunBudget(world, facts).world.budget.planned == MiB(800),
        "without held bytes the planned bytes are the sum of the records");
}

void NoSampleRunsNothing() {
  World world;
  world.actors[kFirst] = Rendered(ShownAt(kFull), OutOfView());
  world.radius = BudgetRadius{1000.0f, 0, false};
  TickFacts facts = FactsWithCap(MiB(100), 60000);
  facts.memory.reset();
  const TickOutput output = RunBudget(world, facts);
  Check(output.commands.empty() && output.trace.empty() &&
            output.logs.empty() && output.world.budget == BudgetState{} &&
            output.world.radius == world.radius,
        "a tick without a memory sample leaves the budget alone");
}

void PressureStartsAndEndsWithItsRecords() {
  World world;
  world.actors[kFirst] = Rendered(ShownAt(kFull), InView(0.6f));
  const TickFacts over = FactsWithCap(MiB(350));
  const TickOutput started = RunBudget(world, over);
  const BudgetState expected{MiB(350), MiB(400), Pressure::kOver};
  Check(started.world.budget.planned == MiB(400) &&
            started.world.budget.allowance == MiB(350),
        "the sample sets the allowance and the estimate");
  Check(IsOver(started.world.budget.pressure),
        "planned bytes over the allowance start pressure");
  Check(HasLog(started, LogLevel::kInfo,
               PressureStartLine(*over.memory, expected)),
        "the pressure start writes its info line");
  const std::vector<TraceRecord> start = ActionsOf(started, "budget_pressure");
  Check(start.size() == 1 &&
            FieldsAre(start.front(),
                      {{"action", "budget_pressure"},
                       {"pressure", "true"},
                       {"planned_bytes", std::to_string(MiB(400))},
                       {"allowance_bytes", std::to_string(MiB(350))}}),
        "the pressure start writes its trace record");

  World still = started.world;
  const TickOutput held = RunBudget(still, over);
  Check(ActionsOf(held, "budget_pressure").empty(),
        "pressure that stays writes no second record");

  World relieved = started.world;
  relieved.actors[kFirst] = Rendered(ShownAt(kHalf), InView(0.6f));
  const TickOutput ended = RunBudget(relieved, FactsWithCap(MiB(1000)));
  Check(!IsOver(ended.world.budget.pressure),
        "planned bytes under the goal end pressure");
  Check(HasLog(ended, LogLevel::kInfo, PressureEndLine(ended.world.budget)),
        "the pressure end writes its info line");
  const std::vector<TraceRecord> end = ActionsOf(ended, "budget_pressure");
  Check(end.size() == 1 && FieldOf(end.front(), "pressure") == "false",
        "the pressure end writes its trace record");
}

void ReleasesLargestOutOfViewFirstUntilEnough() {
  World world;
  world.actors[kFirst] = Rendered(ShownAt(kFull), OutOfView(), Costs(400, 300));
  world.actors[kSecond] =
      Rendered(ShownAt(kFull), OutOfView(), Costs(600, 300));
  const TickOutput output = RunBudget(world, FactsWithCap(MiB(900)));
  const std::vector<ReleaseIntermediates> releases =
      CommandsOf<ReleaseIntermediates>(output);
  Check(releases.size() == 2, "both releases are needed to reach the goal");
  Check(releases.size() == 2 && releases[0].actor == kSecond &&
            releases[0].bytes == MiB(300) && releases[1].actor == kFirst &&
            releases[1].bytes == MiB(100),
        "the largest release comes first");
  Check(releases.size() == 2 && releases[0].reason == ReleaseReason::kBudget &&
            releases[1].reason == ReleaseReason::kBudget,
        "a pressure release names the budget as its reason");
  const RenderPhase *first = PhaseOf(output.world, kFirst);
  const RenderPhase *second = PhaseOf(output.world, kSecond);
  Check(first && second && !MayRelease(*first) && !MayRelease(*second),
        "a released actor holds no intermediates");
  Check(ActionsOf(output, "budget_release").empty(),
        "the engine, not the core, traces a release with its steps");

  World enough;
  enough.actors[kFirst] =
      Rendered(ShownAt(kFull), OutOfView(), Costs(400, 300));
  enough.actors[kSecond] =
      Rendered(ShownAt(kFull), OutOfView(), Costs(600, 100));
  const std::vector<ReleaseIntermediates> one =
      CommandsOf<ReleaseIntermediates>(
          RunBudget(enough, FactsWithCap(MiB(900))));
  Check(one.size() == 1 && one.front().actor == kSecond,
        "the releases stop once the planned bytes reach the goal");
}

void StepsDownOneActorUnderPressure() {
  World world;
  world.actors[kFirst] = Rendered(ShownAt(kFull), InView(0.6f));
  const TickOutput output = RunBudget(world, FactsWithCap(MiB(300)));
  const RenderPhase *phase = PhaseOf(output.world, kFirst);
  const Queued *queued = phase ? Get<Queued>(*phase) : nullptr;
  Check(queued && queued->resize.from == kFull && queued->resize.to == kHalf,
        "a step down queues a resize one level coarser");
  Check(CommandsOf<ReleaseIntermediates>(output).empty(),
        "an actor in view releases nothing");
  const std::vector<TraceRecord> steps = ActionsOf(output, "budget_step_down");
  Check(steps.size() == 1 &&
            FieldsAre(steps.front(), {{"action", "budget_step_down"},
                                      {"actor", std::to_string(FormOf(kFirst))},
                                      {"from", "full"},
                                      {"to", "half"},
                                      {"freed_bytes", std::to_string(MiB(200))},
                                      {"reason", "render target budget"}}),
        "the step down writes its trace record");
  Check(HasLog(output, LogLevel::kInfo,
               StepDownLine({FormOf(kFirst),
                             {},
                             kFull,
                             StepDownOption{kHalf, MiB(200)},
                             std::nullopt})),
        "the step down writes its info line");

  World busy = world;
  busy.actors[kSecond] = Rendered(QueuedResize(kHalf, kEighth), InView(0.6f));
  const TickOutput waited = RunBudget(busy, FactsWithCap(MiB(300)));
  const RenderPhase *untouched = PhaseOf(waited.world, kFirst);
  Check(untouched && Is<Shown>(*untouched) &&
            ActionsOf(waited, "budget_step_down").empty(),
        "no step down starts while a resize is queued or running");
}

void NoStepDownToAFailedLevel() {
  World world;
  world.actors[kFirst] =
      Rendered(ShownAt(kFull, Held{}, FailedLevel{kHalf, kFull}), InView(0.6f));
  const std::optional<BudgetActor> actor =
      BudgetActorOf(kFirst, world.actors[kFirst]);
  Check(actor && !actor->stepDown,
        "the budget offers no step down to the failed level");
  const TickOutput output = RunBudget(world, FactsWithCap(MiB(300)));
  const RenderPhase *phase = PhaseOf(output.world, kFirst);
  Check(phase && Is<Shown>(*phase) &&
            ActionsOf(output, "budget_step_down").empty(),
        "an actor whose next level failed keeps its level");
}

void BudgetActorFollowsThePhase() {
  const std::optional<BudgetActor> held =
      BudgetActorOf(kFirst, Rendered(ShownAt(kFull), OutOfView(700.0f)));
  Check(held && held->actor == FormOf(kFirst) && held->level == kFull &&
            held->releasable == MiB(100) && held->plannedBytes == MiB(400) &&
            held->stepDown == StepDownOption{kHalf, MiB(200)} &&
            !held->resizing && held->distance == 700.0f,
        "a live held actor offers its release and its step down");
  Check(held && held->surplusBytes == MiB(350),
        "an actor out of view has a surplus down to eighth");
  const std::optional<BudgetActor> released =
      BudgetActorOf(kFirst, Rendered(ShownAt(kFull, Freed{}), OutOfView()));
  Check(released && released->releasable == 0 &&
            released->plannedBytes == MiB(300),
        "a released actor has nothing more to release");
  const std::optional<BudgetActor> resizing =
      BudgetActorOf(kFirst, Rendered(RunningResize(kFull, kHalf), OutOfView()));
  Check(resizing && resizing->resizing && resizing->releasable == 0 &&
            !resizing->stepDown && resizing->level == kFull,
        "a resizing actor may not be evicted, released or stepped down");
  const std::optional<BudgetActor> drawing = BudgetActorOf(
      kFirst,
      Rendered(Drawing{kHalf, UnpublishedDraw{3, 1, 1, 1}, std::nullopt},
               OutOfView()));
  Check(drawing && !drawing->resizing && drawing->releasable == 0 &&
            !drawing->stepDown && drawing->level == kHalf,
        "a first-drawing actor may be evicted but not released or stepped "
        "down");
  ActorRecord unrendered = Rendered(ShownAt(kFull), InView(0.6f));
  unrendered.render->renderedFrame.reset();
  Check(!BudgetActorOf(kFirst, unrendered),
        "an actor not yet drawn takes no budget decision");
  Check(!BudgetActorOf(kFirst, Rendered(Empty{}, InView(0.6f))),
        "an install without a render takes no budget decision");
}

void EvictedActorsHaveAPlanAndNothingInstalled() {
  World world;
  world.actors[kFirst] = Evicted(900.0f, MiB(10));
  world.actors[kSecond] = Evicted(1500.0f, std::nullopt);
  world.actors[kSecond].render = InstalledRender{};
  world.actors[kThird] = Rendered(ShownAt(kFull), InView(0.6f, 5000.0f));
  const std::vector<EvictedActor> evicted = EvictedActorsOf(world);
  Check(evicted.size() == 1 && evicted.front().distance == 900.0f &&
            evicted.front().returnBytes == MiB(10),
        "only a planned actor with no render counts as evicted");
  Check(LargestReturnBytes(world) == MiB(10),
        "the largest return bytes price an unpriced actor");
  Check(FarthestTrackedDistance(world) == 1500.0f,
        "the farthest distance counts only actors with a sync state");
}

void ExhaustedShrinksTheRadius() {
  World world;
  world.actors[kFirst] = Rendered(ShownAt(kEighth), InView(0.6f, 1000.0f));
  world.actors[kSecond] = Rendered(ShownAt(kEighth), InView(0.6f, 400.0f));
  const TickOutput output = RunBudget(world, FactsWithCap(MiB(60), 5000));
  Check(output.world.radius && output.world.radius->distance == 990.0f &&
            output.world.radius->sinceMs == 5000,
        "the radius shrinks to just inside the farthest actor");
  const std::vector<Observe> observed = CommandsOf<Observe>(output);
  Check(observed.size() == 1 && observed.front().actor == kFirst,
        "the evicted actor is observed next tick");
  const std::vector<TraceRecord> radius = ActionsOf(output, "budget_radius");
  Check(
      radius.size() == 1 &&
          FieldsAre(radius.front(), {{"action", "budget_radius"},
                                     {"change", "shrink"},
                                     {"radius", "990"},
                                     {"actor", std::to_string(FormOf(kFirst))},
                                     {"freed_bytes", std::to_string(MiB(50))}}),
      "the shrink writes its trace record");
  Check(HasLog(output, LogLevel::kInfo,
               RadiusShrinkLine(990.0f, FormOf(kFirst), {}, MiB(50))),
        "the shrink writes its info line");
  Check(output.world.budget.pressure == Pressure::kOver,
        "a shrink is a response, so no exhaustion warning");
}

void ExhaustedWithoutCandidateWarnsOnce() {
  World world;
  world.actors[kFirst] = Rendered(ShownAt(kFull), Player());
  const TickOutput first = RunBudget(world, FactsWithCap(MiB(300)));
  Check(first.world.budget.pressure == Pressure::kOverWarned,
        "nothing left to do marks the episode warned");
  Check(first.logs.size() == 2 && first.logs.back().level == LogLevel::kWarning,
        "nothing left to do writes one warning");
  Check(first.logs.size() == 2 &&
            first.logs.back().text.find("no release or step down is left") !=
                std::string::npos,
        "the warning names the exhaustion");
  const TickOutput second = RunBudget(first.world, FactsWithCap(MiB(300)));
  Check(second.logs.empty(), "the warning is written once per episode");
}

World WithRingAt(float a_distance, std::uint64_t a_returnBytes) {
  World world;
  world.radius = BudgetRadius{1000.0f, 0, false};
  world.actors[kFirst] = Evicted(a_distance, a_returnBytes);
  return world;
}

void GrowsTheRadiusWithoutPressure() {
  const TickOutput grown = RunBudget(WithRingAt(900.0f, MiB(10)),
                                     FactsWithCap(MiB(1000), 20000, 3000.0f));
  Check(grown.world.radius && grown.world.radius->distance == 1250.0f &&
            grown.world.radius->sinceMs == 20000,
        "the radius grows by one step when its ring fits");
  const std::vector<TraceRecord> grow = ActionsOf(grown, "budget_radius");
  Check(grow.size() == 1 &&
            FieldsAre(grow.front(),
                      {{"action", "budget_radius"},
                       {"change", "grow"},
                       {"radius", "1250"},
                       {"returning_bytes", std::to_string(MiB(10))}}),
        "the growth writes its trace record");
  Check(HasLog(grown, LogLevel::kInfo, RadiusGrowLine(1250.0f)),
        "the growth writes its info line");

  const TickOutput early = RunBudget(WithRingAt(900.0f, MiB(10)),
                                     FactsWithCap(MiB(1000), 5000, 3000.0f));
  Check(early.world.radius && early.world.radius->distance == 1000.0f &&
            early.trace.empty(),
        "the radius waits out its dwell");

  const TickOutput cleared =
      RunBudget(WithRingAt(900.0f, MiB(10)), FactsWithCap(MiB(1000), 20000));
  Check(!cleared.world.radius, "a growth past every tracked actor clears");
  Check(HasLog(cleared, LogLevel::kInfo, RadiusClearLine()) &&
            ActionsOf(cleared, "budget_radius").size() == 1 &&
            FieldOf(ActionsOf(cleared, "budget_radius").front(), "change") ==
                "clear",
        "the clear writes its records");

  const TickOutput blocked = RunBudget(WithRingAt(900.0f, MiB(2000)),
                                       FactsWithCap(MiB(1000), 20000, 3000.0f));
  Check(blocked.world.radius && blocked.world.radius->distance == 1000.0f &&
            blocked.world.radius->blockReported,
        "a ring that does not fit blocks the growth");
  const std::vector<TraceRecord> block = ActionsOf(blocked, "budget_radius");
  Check(block.size() == 1 && FieldOf(block.front(), "change") == "blocked" &&
            FieldOf(block.front(), "reason") == "ring" &&
            FieldOf(block.front(), "ring_bytes") == std::to_string(MiB(2000)),
        "the first block writes its trace record");
  Check(blocked.logs.size() == 1, "the first block writes its info line");
  const TickOutput again =
      RunBudget(blocked.world, FactsWithCap(MiB(1000), 40000, 3000.0f));
  Check(again.trace.empty() && again.logs.empty(),
        "a block already reported writes nothing");
}

void PreemptsForABlockedStepUp() {
  World world;
  world.actors[kFirst] = Rendered(BlockedAt(kEighth), InView(0.6f));
  world.actors[kSecond] = Rendered(ShownAt(kFull), OutOfView());
  const TickOutput output = RunBudget(world, FactsWithCap(MiB(600)));
  Check(!IsOver(output.world.budget.pressure),
        "preemption runs without pressure");
  const RenderPhase *victim = PhaseOf(output.world, kSecond);
  const Resize *resize = victim ? ResizeIn(*victim) : nullptr;
  Check(resize && resize->to == kHalf,
        "the actor with a surplus steps down for the requester");
  const std::vector<TraceRecord> steps = ActionsOf(output, "budget_step_down");
  Check(steps.size() == 1 && FieldOf(steps.front(), "reason") == "preempted" &&
            FieldOf(steps.front(), "for_actor") ==
                std::to_string(FormOf(kFirst)),
        "a preemption names its requester");
  Check(HasLog(output, LogLevel::kInfo,
               StepDownLine({FormOf(kSecond),
                             {},
                             kFull,
                             StepDownOption{kHalf, MiB(200)},
                             FormOf(kFirst)})),
        "a preemption writes the step-down line with its requester");

  const TickOutput roomy = RunBudget(world, FactsWithCap(MiB(1000)));
  Check(ActionsOf(roomy, "budget_step_down").empty(),
        "no preemption when the step up fits");

  World busy = world;
  busy.actors[kThird] = Rendered(QueuedResize(kHalf, kEighth), OutOfView());
  Check(ActionsOf(RunBudget(busy, FactsWithCap(MiB(700))), "budget_step_down")
            .empty(),
        "no preemption while a resize is queued or running");
}
}

int main() {
  TheMeasuredHeldBytesSetThePlannedBytes();
  EstimateCountsEachRenderAtItsLevelAndHolding();
  NoSampleRunsNothing();
  PressureStartsAndEndsWithItsRecords();
  ReleasesLargestOutOfViewFirstUntilEnough();
  StepsDownOneActorUnderPressure();
  NoStepDownToAFailedLevel();
  BudgetActorFollowsThePhase();
  EvictedActorsHaveAPlanAndNothingInstalled();
  ExhaustedShrinksTheRadius();
  ExhaustedWithoutCandidateWarnsOnce();
  GrowsTheRadiusWithoutPressure();
  PreemptsForABlockedStepUp();
  return test::Finish("orchestration_budget");
}
