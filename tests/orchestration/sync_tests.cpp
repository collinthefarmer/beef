// GPL-3.0-only with the additional permission in COPYING.md.
#include "actors/Lifecycle.h"
#include "actors/fixtures.h"
#include "orchestration/Budget.h"
#include "orchestration/Sync.h"
#include "orchestration/Tick.h"
#include "test_support.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Orchestration;
using actorfixtures::Geo;
using actorfixtures::Piece;
using actorfixtures::PlanBuilder;
using test::Check;
using test::Equal;

namespace {
constexpr ActorId kActor = ActorOf(0x14);
constexpr ActorId kOther = ActorOf(0x20);
constexpr Millis kNow = 50000;
constexpr PlanJobId kJob{7};

Actors::ActorPlan CircuitPlan() {
  return PlanBuilder{}
      .With(actorfixtures::Instance("circuit"), Geo(0x1, "Body"))
      .plan;
}

Actors::ActorPlan FrostPlan() {
  return PlanBuilder{}
      .With(actorfixtures::Instance("frost"), Geo(0x1, "Body"))
      .plan;
}

Actors::ActorFacts LoadedFacts(ActorId a_actor,
                               std::optional<float> a_distance = std::nullopt) {
  Actors::PieceFacts piece;
  piece.ref = Piece(0x1);
  piece.model = Actors::ModelToken{1};
  Actors::ActorFacts facts;
  facts.actor = a_actor;
  facts.loaded = true;
  facts.distance = a_distance;
  facts.pieces = {piece};
  return facts;
}

StaleActor StaleWith(ActorId a_actor, Actors::ActorPlan a_plan,
                     std::optional<float> a_distance = std::nullopt) {
  return {a_actor, LoadedFacts(a_actor, a_distance), {std::move(a_plan), {}}};
}

Sighting Seen(bool a_shown = true, std::optional<float> a_fraction = 1.0f) {
  return {ActorRole::kOther, true, a_shown, a_fraction, std::nullopt};
}

Actors::ActorState InstalledState(const Actors::ActorPlan &a_plan) {
  return {a_plan, a_plan, {}, {}};
}

LevelCost Cost(std::uint64_t a_held, std::uint64_t a_released) {
  return {a_held, a_released};
}

InstalledRender ShownRender(ResolutionLevel a_level) {
  InstalledRender render;
  render.phase = Shown{a_level, Held{}, {}, std::nullopt, std::nullopt};
  render.levelCosts.At(ResolutionLevel::kFull) = Cost(400, 300);
  render.levelCosts.At(ResolutionLevel::kHalf) = Cost(100, 80);
  render.renderedFrame = 1;
  return render;
}

InstalledRender PlanningRender(PlanJobId a_job) {
  InstalledRender render;
  render.phase = Planning{a_job};
  return render;
}

TickFacts FactsAt(Millis a_now) {
  TickFacts facts;
  facts.now = a_now;
  return facts;
}

template <class T> std::size_t CountOf(const std::vector<Command> &a_commands) {
  std::size_t count = 0;
  for (const Command &command : a_commands) {
    count += Is<T>(command) ? 1 : 0;
  }
  return count;
}

template <class T> const T *FirstOf(const std::vector<Command> &a_commands) {
  for (const Command &command : a_commands) {
    if (const T *found = Get<T>(command)) {
      return found;
    }
  }
  return nullptr;
}

template <class T>
const T *FirstFor(const std::vector<Command> &a_commands, ActorId a_actor) {
  for (const Command &command : a_commands) {
    if (const T *found = Get<T>(command); found && found->actor == a_actor) {
      return found;
    }
  }
  return nullptr;
}

template <class T>
std::optional<std::size_t> IndexOf(const std::vector<Command> &a_commands) {
  for (std::size_t i = 0; i < a_commands.size(); ++i) {
    if (Is<T>(a_commands[i])) {
      return i;
    }
  }
  return std::nullopt;
}

bool Observes(const std::vector<Command> &a_commands, ActorId a_actor) {
  for (const Command &command : a_commands) {
    if (const Observe *observe = Get<Observe>(command);
        observe && observe->actor == a_actor) {
      return true;
    }
  }
  return false;
}

World WorldWith(ActorId a_actor, TrackedActor a_record) {
  World world;
  world.actors.emplace(a_actor, std::move(a_record));
  return world;
}

void AnInstalledRenderChargesTheBudget() {
  TickFacts facts = FactsAt(kNow);
  facts.results.push_back(
      ActorEvent{kActor, Installed{ResolutionLevel::kFull, 3, {}, 250}});
  World world;
  world.budget.planned = 100;
  const TickOutput out = RecordSyncResults(std::move(world), facts);
  Equal(out.world.budget.planned, std::uint64_t{350},
        "an installed render charges its bytes to the budget");
  Check(out.commands.empty(), "recording results issues no command");
}

void ARetiredRenderLeavesItsLevelAndReturnBytes() {
  TrackedActor record;
  record.render = ShownRender(ResolutionLevel::kHalf);
  TickFacts facts = FactsAt(kNow);
  facts.results.push_back(Retired{kActor});
  const TickOutput out =
      RecordSyncResults(WorldWith(kActor, std::move(record)), facts);
  const TrackedActor &after = out.world.actors.at(kActor);
  Check(!after.render, "a retired actor has no render");
  Check(after.reinstallLevel == ResolutionLevel::kHalf,
        "a retired actor keeps its target level for the reinstall");
  Check(after.returnBytes == std::uint64_t{100},
        "a retired actor keeps the full cost of its target level");
}

void ARetireWithoutARenderKeepsTheOldLevel() {
  TrackedActor record;
  record.reinstallLevel = ResolutionLevel::kQuarter;
  record.returnBytes = 77;
  TickFacts facts = FactsAt(kNow);
  facts.results.push_back(Retired{kActor});
  const TickOutput out =
      RecordSyncResults(WorldWith(kActor, std::move(record)), facts);
  const TrackedActor &after = out.world.actors.at(kActor);
  Check(after.reinstallLevel == ResolutionLevel::kQuarter &&
            after.returnBytes == std::uint64_t{77},
        "a retire of an actor without a render changes nothing");
}

void AFailedInstallIsForgotten() {
  TrackedActor record;
  record.syncState = InstalledState(CircuitPlan());
  World world = WorldWith(kActor, std::move(record));
  world.waitingInstalls.push_back({.actor = FormOf(kActor)});
  TickFacts facts = FactsAt(kNow);
  facts.results.push_back(InstallFailed{kActor, "no geometry"});
  TickOutput recorded = RecordSyncResults(std::move(world), facts);
  Check(!recorded.world.actors.at(kActor).syncState,
        "a failed install drops the sync state");
  Check(recorded.world.waitingInstalls.empty(),
        "a failed install leaves the waiting installs");
  const TickOutput out = RunSync(std::move(recorded.world), facts);
  Check(!out.world.actors.contains(kActor), "a failed install is forgotten");
  Equal(CountOf<Forget>(out.commands), std::size_t{1},
        "a failed install issues one forget");
}

void AClaimIsRecordedAndObserved() {
  TrackedActor record;
  record.syncState = InstalledState(CircuitPlan());
  const Actors::Claim claim{Geo(0x1, "Body"), Actors::ModelToken{1}};
  TickFacts facts = FactsAt(kNow);
  facts.claims.push_back({kActor, claim});
  TickOutput recorded =
      RecordSyncResults(WorldWith(kActor, std::move(record)), facts);
  const std::optional<Actors::ActorState> &state =
      recorded.world.actors.at(kActor).syncState;
  Check(state && state->claims.size() == 1 && state->claims.front() == claim,
        "a claim joins the sync state");
  const TickOutput out = RunSync(std::move(recorded.world), facts);
  Check(Observes(out.commands, kActor), "a claimed actor is observed again");
}

void ANewActorInstalls() {
  TickFacts facts = FactsAt(kNow);
  facts.stale.push_back(StaleWith(kActor, CircuitPlan()));
  facts.sightings[kActor] = Seen();
  const TickOutput out = RunSync(World{}, facts);
  const Install *install = FirstOf<Install>(out.commands);
  Check(install && install->kind == InstallKind::kInstall &&
            install->replacedBytes == 0,
        "a new actor installs with nothing replaced");
  Equal(CountOf<Retire>(out.commands), std::size_t{0},
        "a new actor retires nothing");
  const SettleApplications *settle = FirstOf<SettleApplications>(out.commands);
  Check(settle && settle->reinstall && !settle->equip,
        "an install settles the reinstall request only");
  const std::optional<Actors::ActorState> &state =
      out.world.actors.at(kActor).syncState;
  Check(state && state->installed == CircuitPlan(),
        "the install is recorded in the sync state");
  Check(out.world.syncLog.size() == 1 &&
            out.world.syncLog.front().kind == Actors::SyncKind::kInstalled &&
            out.world.syncLog.front().name == "00000014",
        "the install enters the sync log");
}

void AChangedActorReinstallsAfterARetire() {
  TrackedActor record;
  record.syncState = InstalledState(CircuitPlan());
  record.render = ShownRender(ResolutionLevel::kHalf);
  World world = WorldWith(kActor, std::move(record));
  world.budget.allowance = 1000;
  world.budget.planned = 600;
  TickFacts facts = FactsAt(kNow);
  facts.stale.push_back(StaleWith(kActor, FrostPlan()));
  facts.sightings[kActor] = Seen();
  const TickOutput out = RunSync(std::move(world), facts);
  const std::optional<std::size_t> retire = IndexOf<Retire>(out.commands);
  const std::optional<std::size_t> install = IndexOf<Install>(out.commands);
  Check(retire && install && *retire < *install,
        "a reinstall retires before it installs");
  const Install *command = FirstOf<Install>(out.commands);
  Check(command && command->kind == InstallKind::kReinstall,
        "a replacing install is a reinstall");
  Check(command && command->replacedBytes == 100,
        "the replaced bytes are the render's held bytes");
  Check(command && command->bounds.finestLevel == ResolutionLevel::kHalf &&
            command->bounds.headroom == std::uint64_t{500},
        "the reinstall keeps the render's level with the headroom");
  Equal(out.world.budget.planned, std::uint64_t{500},
        "the reinstall releases the replaced bytes");
}

void AReinstallWaitsBehindAnotherInstall() {
  TrackedActor record;
  record.syncState = InstalledState(CircuitPlan());
  World world = WorldWith(kActor, std::move(record));
  TickFacts facts = FactsAt(kNow);
  facts.stale.push_back(StaleWith(kOther, CircuitPlan()));
  facts.stale.push_back(StaleWith(kActor, FrostPlan()));
  facts.sightings[kOther] = Seen();
  facts.sightings[kActor] = Seen();
  const TickOutput out = RunSync(std::move(world), facts);
  Equal(CountOf<Install>(out.commands), std::size_t{1},
        "the tick starts only the first install");
  const KeepModels *keep = FirstFor<KeepModels>(out.commands, kActor);
  Check(keep && keep->plan == CircuitPlan(),
        "a deferred install keeps the installed models");
  Check(out.world.waitingInstalls.size() == 1 &&
            out.world.waitingInstalls.front().actor == FormOf(kActor) &&
            out.world.waitingInstalls.front().sinceMS == PlannerMs(kNow),
        "a deferred install waits");
  Check(out.world.actors.at(kActor).syncState == InstalledState(CircuitPlan()),
        "a deferred install leaves the sync state");
  Check(std::ranges::count(out.trace, Trace::Event::kQueue,
                           &TraceRecord::event) == 1,
        "a deferred install is traced");
}

void OneInstallStartsPerTick() {
  TickFacts facts = FactsAt(kNow);
  facts.stale.push_back(StaleWith(kActor, CircuitPlan()));
  facts.stale.push_back(StaleWith(kOther, CircuitPlan()));
  facts.sightings[kActor] = Seen();
  facts.sightings[kOther] = Seen();
  const TickOutput out = RunSync(World{}, facts);
  Equal(CountOf<Install>(out.commands), std::size_t{1},
        "a second install in a tick waits");
  Check(out.world.waitingInstalls.size() == 1 &&
            out.world.waitingInstalls.front().actor == FormOf(kOther),
        "the later actor waits");
  Check(Observes(out.commands, kOther), "the waiting actor is observed again");
}

void AWaitingInstallGoesFirst() {
  World world;
  world.actors.try_emplace(kOther);
  world.waitingInstalls.push_back(
      {.actor = FormOf(kOther), .sinceMS = PlannerMs(kNow) - 10});
  TickFacts facts = FactsAt(kNow);
  facts.stale.push_back(StaleWith(kActor, CircuitPlan()));
  facts.stale.push_back(StaleWith(kOther, CircuitPlan()));
  facts.sightings[kActor] = Seen();
  facts.sightings[kOther] = Seen();
  const TickOutput out = RunSync(std::move(world), facts);
  const Install *install = FirstOf<Install>(out.commands);
  Check(install && install->actor == kOther,
        "the waiting actor installs before a new one");
  Check(out.world.waitingInstalls.size() == 1 &&
            out.world.waitingInstalls.front().actor == FormOf(kActor),
        "the new actor waits behind it");
}

void AnUnchangedActorSettles() {
  TrackedActor record;
  record.syncState = InstalledState(CircuitPlan());
  TickFacts facts = FactsAt(kNow);
  facts.stale.push_back(StaleWith(kActor, CircuitPlan()));
  facts.sightings[kActor] = Seen();
  const TickOutput out = RunSync(WorldWith(kActor, std::move(record)), facts);
  Equal(CountOf<Install>(out.commands) + CountOf<Retire>(out.commands),
        std::size_t{0}, "an unchanged actor installs nothing");
  const SettleApplications *settle = FirstOf<SettleApplications>(out.commands);
  Check(settle && settle->equip && settle->reinstall,
        "an unchanged actor settles its equip application");
  Check(FirstOf<KeepModels>(out.commands) != nullptr,
        "an unchanged actor keeps its models");
  Check(out.world.syncLog.empty(), "an unchanged actor is not logged");
}

void AnActorWaitingForAModelKeepsItsEquip() {
  TrackedActor record;
  record.syncState = InstalledState(CircuitPlan());
  StaleActor stale = StaleWith(kActor, CircuitPlan());
  Actors::PieceFacts pending;
  pending.ref = Piece(0x2);
  stale.facts.pieces.push_back(pending);
  TickFacts facts = FactsAt(kNow);
  facts.stale.push_back(stale);
  facts.sightings[kActor] = Seen();
  const TickOutput out = RunSync(WorldWith(kActor, std::move(record)), facts);
  const SettleApplications *settle = FirstOf<SettleApplications>(out.commands);
  Check(settle && !settle->equip,
        "an actor waiting for a model keeps its equip application");
  const std::optional<Actors::ActorState> &state =
      out.world.actors.at(kActor).syncState;
  Check(state && state->pending.size() == 1, "the waiting model is recorded");
}

void AnUnloadedStaleActorIsForgotten() {
  TrackedActor record;
  record.syncState = InstalledState(CircuitPlan());
  StaleActor stale = StaleWith(kActor, {});
  stale.facts.loaded = false;
  TickFacts facts = FactsAt(kNow);
  facts.stale.push_back(stale);
  facts.sightings[kActor] = Seen();
  const TickOutput out = RunSync(WorldWith(kActor, std::move(record)), facts);
  Check(!out.world.actors.contains(kActor), "the forgotten actor is dropped");
  Equal(CountOf<Forget>(out.commands), std::size_t{1},
        "the forgotten actor gets one forget");
  const SettleApplications *settle = FirstOf<SettleApplications>(out.commands);
  Check(settle && settle->equip && settle->reinstall,
        "a forgotten actor settles every application");
  Check(out.world.syncLog.size() == 1 &&
            out.world.syncLog.front().kind == Actors::SyncKind::kForgotten,
        "the forget enters the sync log");
}

void AnActorWithout3DIsForgotten() {
  TrackedActor record;
  record.syncState = InstalledState(CircuitPlan());
  record.render = ShownRender(ResolutionLevel::kFull);
  World world = WorldWith(kActor, std::move(record));
  world.waitingInstalls.push_back({.actor = FormOf(kActor)});
  TickFacts facts = FactsAt(kNow);
  facts.sightings[kActor] = Sighting{};
  const TickOutput out = RunSync(std::move(world), facts);
  Check(!out.world.actors.contains(kActor) && out.world.waitingInstalls.empty(),
        "an actor without 3D is dropped");
  Equal(CountOf<Forget>(out.commands), std::size_t{1},
        "an actor without 3D is forgotten");
}

void AForgetRequestForgetsLikeAnUnload() {
  TrackedActor record;
  record.syncState = InstalledState(CircuitPlan());
  record.render = ShownRender(ResolutionLevel::kFull);
  World world = WorldWith(kActor, record);
  world.waitingInstalls.push_back({.actor = FormOf(kActor)});
  TickFacts requested = FactsAt(kNow);
  requested.sightings[kActor] = Seen();
  requested.forgets.push_back(kActor);
  TickFacts unloaded = FactsAt(kNow);
  unloaded.sightings[kActor] = Sighting{};
  const TickOutput forgotten = RunSync(world, requested);
  const TickOutput gone = RunSync(world, unloaded);
  Check(!forgotten.world.actors.contains(kActor) &&
            forgotten.world.waitingInstalls.empty(),
        "a forget request drops the rendered actor");
  Equal(CountOf<Forget>(forgotten.commands), std::size_t{1},
        "a forget request issues one forget");
  Equal(CountOf<Retire>(forgotten.commands), CountOf<Retire>(gone.commands),
        "a forget request retires the same way an unload does");
  Equal(forgotten.commands.size(), gone.commands.size(),
        "a forget request issues the commands of an unload");
}

void AForgetRequestWithoutARecordDoesNothing() {
  TrackedActor record;
  record.syncState = InstalledState(CircuitPlan());
  const World world = WorldWith(kActor, std::move(record));
  TickFacts plain = FactsAt(kNow);
  plain.sightings[kActor] = Seen();
  TickFacts facts = plain;
  facts.forgets.push_back(kOther);
  const TickOutput out = RunSync(world, facts);
  const TickOutput baseline = RunSync(world, plain);
  Equal(CountOf<Forget>(out.commands), std::size_t{0},
        "a forget request for an unknown actor issues no forget");
  Equal(out.commands.size(), baseline.commands.size(),
        "a forget request for an unknown actor adds no command");
  Check(out.world.actors.contains(kActor) && !out.world.actors.contains(kOther),
        "a forget request for an unknown actor changes no record");
}

void AnActorOutOfRangeIsEvicted() {
  TrackedActor record;
  record.syncState = InstalledState(CircuitPlan());
  record.render = ShownRender(ResolutionLevel::kFull);
  World world = WorldWith(kActor, std::move(record));
  world.radius = BudgetRadius{1000.0f, 0, false};
  TickFacts facts = FactsAt(kNow);
  facts.stale.push_back(StaleWith(kActor, CircuitPlan(), 2000.0f));
  facts.sightings[kActor] = Seen();
  const TickOutput out = RunSync(std::move(world), facts);
  Equal(CountOf<Retire>(out.commands), std::size_t{1},
        "an actor beyond the budget radius is retired");
  Equal(CountOf<Install>(out.commands), std::size_t{0},
        "an evicted actor installs nothing");
  const std::optional<Actors::ActorState> &state =
      out.world.actors.at(kActor).syncState;
  Check(state && state->installed == Actors::ActorPlan{} &&
            state->plan == CircuitPlan(),
        "an evicted actor keeps its plan and nothing installed");
  Check(out.world.syncLog.size() == 1 &&
            out.world.syncLog.front().kind == Actors::SyncKind::kEvicted,
        "the eviction enters the sync log");
}

void AWaitingModelIsRecheckedSoon() {
  TrackedActor record;
  Actors::ActorState state = InstalledState(CircuitPlan());
  state.pending.push_back({Piece(0x2), PlannerMs(kNow) - 10});
  record.syncState = state;
  World world = WorldWith(kActor, std::move(record));
  world.timers = {kNow - Actors::kLoadingCheckMs, kNow};
  const TickOutput due = RunSync(world, FactsAt(kNow));
  Check(Observes(due.commands, kActor),
        "an actor waiting for a model is observed after the short delay");
  Equal(due.world.timers.lastLoadingCheck, kNow, "the short timer restarts");
  world.timers = {kNow - 1, kNow};
  const TickOutput early = RunSync(world, FactsAt(kNow));
  Check(!Observes(early.commands, kActor),
        "an actor waiting for a model is not observed before the delay");
}

void ARangeChangeIsRechecked() {
  TrackedActor record;
  record.syncState = InstalledState(CircuitPlan());
  World world = WorldWith(kActor, std::move(record));
  world.timers = {kNow, kNow - Actors::kSlowCheckMs};
  TickFacts facts = FactsAt(kNow);
  facts.settings.evictDistance = 1000.0f;
  facts.sightings[kActor] =
      Sighting{ActorRole::kOther, true, true, 1.0f, 2000.0f};
  const TickOutput out = RunSync(world, facts);
  Check(Observes(out.commands, kActor),
        "an installed actor beyond the evict distance is observed");
  Equal(out.world.timers.lastSlowCheck, kNow, "the long timer restarts");
  facts.sightings[kActor].distance = 10.0f;
  const TickOutput near = RunSync(world, facts);
  Check(!Observes(near.commands, kActor),
        "an installed actor in range is not observed");
}

void TheNextWaitingInstallIsObserved() {
  World world;
  world.actors.try_emplace(kActor);
  world.actors.try_emplace(kOther);
  world.waitingInstalls.push_back(
      {.actor = FormOf(kActor), .sinceMS = PlannerMs(kNow) - 10});
  world.waitingInstalls.push_back(
      {.actor = FormOf(kOther), .sinceMS = PlannerMs(kNow) - 10});
  TickFacts facts = FactsAt(kNow);
  facts.sightings[kActor] = Seen(false);
  facts.sightings[kOther] = Seen(true);
  const TickOutput out = RunSync(std::move(world), facts);
  Check(Observes(out.commands, kOther) && !Observes(out.commands, kActor),
        "the in-view waiting actor is observed first");
  Check(out.world.waitingInstalls.size() == 2 &&
            out.world.waitingInstalls.back().shown,
        "the waiting installs take this tick's sightings");
}

void ThePlayerInstallsWithoutHeadroom() {
  TrackedActor record;
  BudgetState budget;
  budget.allowance = 1000;
  const Sighting player{ActorRole::kPlayer, true, true, 0.01f, std::nullopt};
  const InstallBounds bounds =
      InstallBoundsOf(record, player, budget, 0, TickSettings{});
  Check(bounds.finestLevel == ResolutionLevel::kFull && !bounds.headroom,
        "the player installs at full resolution without a headroom");
}

void ANewActorInstallsAtItsWantedLevel() {
  const TrackedActor record;
  BudgetState budget;
  budget.allowance = 1000;
  budget.planned = 200;
  const InstallBounds away =
      InstallBoundsOf(record, Seen(false), budget, 0, TickSettings{});
  Check(away.finestLevel == ResolutionLevel::kEighth &&
            away.headroom == std::uint64_t{800},
        "an actor out of view installs at the coarsest level");
  const InstallBounds unmeasured = InstallBoundsOf(
      record, Seen(true, std::nullopt), budget, 0, TickSettings{});
  Check(unmeasured == InstallBounds{ResolutionLevel::kFull, 800},
        "an unmeasured actor installs at the finest level within the headroom");
  TrackedActor evicted;
  evicted.reinstallLevel = ResolutionLevel::kQuarter;
  const InstallBounds back =
      InstallBoundsOf(evicted, Seen(), budget, 0, TickSettings{});
  Check(back.finestLevel == ResolutionLevel::kQuarter,
        "a returning actor installs at its earlier level");
}

void ReturnBytesFollowTheTarget() {
  TrackedActor record;
  record.render = ShownRender(ResolutionLevel::kFull);
  Check(ReturnBytesOf(record) == std::uint64_t{400},
        "a shown actor returns at its level's full cost");
  record.render->phase =
      Queued{Resize{Level::kFull, Level::kHalf, IntermediateHolding::kHeld}};
  Check(ReturnBytesOf(record) == std::uint64_t{100},
        "a resizing actor returns at its target level's full cost");
  Check(!ReturnBytesOf(TrackedActor{}),
        "an actor without a render returns none");
}

void ScreenSizingOffStartsAtFullWithinTheHeadroom() {
  const TrackedActor record;
  BudgetState budget;
  budget.allowance = 1000;
  TickSettings off;
  off.sizeByScreen = false;
  Check(InstallBoundsOf(record, Seen(false), budget, 0, off) ==
            InstallBounds{ResolutionLevel::kFull, 1000},
        "with screen sizing off a new actor installs at the finest level "
        "within the headroom");
  TrackedActor returning;
  returning.reinstallLevel = ResolutionLevel::kHalf;
  Check(InstallBoundsOf(returning, Seen(false), budget, 0, off).finestLevel ==
            ResolutionLevel::kHalf,
        "a returning actor keeps its earlier level with screen sizing off");
}
}

void AReinstallCancelsItsAwaitedJob() {
  TrackedActor record;
  record.syncState = InstalledState(CircuitPlan());
  record.render = PlanningRender(kJob);
  TickFacts facts = FactsAt(kNow);
  facts.stale.push_back(StaleWith(kActor, FrostPlan()));
  facts.sightings[kActor] = Seen();
  const TickOutput out = RunSync(WorldWith(kActor, std::move(record)), facts);
  const std::optional<std::size_t> retire = IndexOf<Retire>(out.commands);
  const std::optional<std::size_t> cancel =
      IndexOf<CancelPlanJob>(out.commands);
  const std::optional<std::size_t> install = IndexOf<Install>(out.commands);
  Check(retire && cancel && install && *retire < *cancel && *cancel < *install,
        "a reinstall cancels its awaited job after the retire, before the "
        "install");
  const CancelPlanJob *command = FirstOf<CancelPlanJob>(out.commands);
  Check(command && command->job == kJob, "the cancel names the awaited job");
}

void ASharedJobIsNotCancelled() {
  TrackedActor record;
  record.syncState = InstalledState(CircuitPlan());
  record.render = PlanningRender(kJob);
  World world = WorldWith(kActor, record);
  world.actors.emplace(kOther, record);
  TickFacts facts = FactsAt(kNow);
  facts.stale.push_back(StaleWith(kActor, FrostPlan()));
  facts.sightings[kActor] = Seen();
  facts.sightings[kOther] = Seen();
  const TickOutput out = RunSync(std::move(world), facts);
  Equal(CountOf<CancelPlanJob>(out.commands), std::size_t{0},
        "a job another actor still awaits is not cancelled");
}

void AJobDoneThisTickIsNotCancelled() {
  TrackedActor record;
  record.syncState = InstalledState(CircuitPlan());
  record.render = PlanningRender(kJob);
  TickFacts facts = FactsAt(kNow);
  facts.jobs.push_back(PlanJobBuilt{kJob});
  facts.stale.push_back(StaleWith(kActor, FrostPlan()));
  facts.sightings[kActor] = Seen();
  const TickOutput out = RunSync(WorldWith(kActor, std::move(record)), facts);
  Equal(CountOf<CancelPlanJob>(out.commands), std::size_t{0},
        "a job reported done in the tick is not cancelled");
}

void AForgottenActorCancelsItsJob() {
  TrackedActor record;
  record.syncState = InstalledState(CircuitPlan());
  record.render = PlanningRender(kJob);
  TickFacts facts = FactsAt(kNow);
  facts.sightings[kActor] = Sighting{};
  const TickOutput out = RunSync(WorldWith(kActor, std::move(record)), facts);
  const std::optional<std::size_t> forget = IndexOf<Forget>(out.commands);
  const std::optional<std::size_t> cancel =
      IndexOf<CancelPlanJob>(out.commands);
  Check(forget && cancel && *forget < *cancel,
        "an actor forgotten while it plans cancels its job");
}

void AFullPlanQueueDefersTheInstall() {
  TickFacts facts = FactsAt(kNow);
  facts.planSlots = 0;
  facts.stale.push_back(StaleWith(kActor, CircuitPlan()));
  facts.sightings[kActor] = Seen();
  const TickOutput out = RunSync(World{}, facts);
  Equal(CountOf<Install>(out.commands), std::size_t{0},
        "no install starts while the plan queue is full");
  Check(out.world.waitingInstalls.size() == 1 &&
            out.world.waitingInstalls.front().actor == FormOf(kActor),
        "the install waits for a plan slot");
  facts.planSlots = 1;
  const TickOutput next = RunSync(World{}, facts);
  Equal(CountOf<Install>(next.commands), std::size_t{1},
        "the install starts once a plan slot is free");
}

int main() {
  AnInstalledRenderChargesTheBudget();
  ARetiredRenderLeavesItsLevelAndReturnBytes();
  ARetireWithoutARenderKeepsTheOldLevel();
  AFailedInstallIsForgotten();
  AClaimIsRecordedAndObserved();
  ANewActorInstalls();
  AChangedActorReinstallsAfterARetire();
  AReinstallWaitsBehindAnotherInstall();
  OneInstallStartsPerTick();
  AWaitingInstallGoesFirst();
  AnUnchangedActorSettles();
  AnActorWaitingForAModelKeepsItsEquip();
  AnUnloadedStaleActorIsForgotten();
  AnActorWithout3DIsForgotten();
  AForgetRequestForgetsLikeAnUnload();
  AForgetRequestWithoutARecordDoesNothing();
  AnActorOutOfRangeIsEvicted();
  AWaitingModelIsRecheckedSoon();
  ARangeChangeIsRechecked();
  TheNextWaitingInstallIsObserved();
  ThePlayerInstallsWithoutHeadroom();
  ANewActorInstallsAtItsWantedLevel();
  ReturnBytesFollowTheTarget();
  ScreenSizingOffStartsAtFullWithinTheHeadroom();
  AReinstallCancelsItsAwaitedJob();
  ASharedJobIsNotCancelled();
  AJobDoneThisTickIsNotCancelled();
  AForgottenActorCancelsItsJob();
  AFullPlanQueueDefersTheInstall();
  return test::Finish("orchestration_sync");
}
