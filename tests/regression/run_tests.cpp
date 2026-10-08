// GPL-3.0-only with the additional permission in COPYING.md.
#include "Core.h"
#include "regression/Run.h"
#include "test_support.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Regression;

namespace {
constexpr std::int64_t kNow = 1'800'000'000;
constexpr std::uint64_t kSimulatedFrameMs = 1000;

std::string RunFileText(std::string_view a_fields) {
  return std::string{R"({"format":1,"run":"r1","save":"BEEFRegression",)"} +
         std::string{a_fields} + "}";
}

std::string RunFileTextFor(std::string_view a_case) {
  return RunFileText(std::string{R"("suite":[")"} + std::string{a_case} +
                     R"("],"notAfter":1800000600)");
}

void ParsesValidRunFile() {
  const auto parsed = ParseRunFile(RunFileTextFor("lifecycle"), kNow);
  test::Check(parsed.has_value(), "valid run file parses");
  if (!parsed) {
    return;
  }
  test::Equal(parsed->run, std::string{"r1"}, "run identifier kept");
  test::Equal(parsed->save, std::string{"BEEFRegression"}, "save name kept");
  test::Equal(parsed->suite.size(), std::size_t{1}, "suite resolved");
  test::Check(!parsed->verifyRenderPlans,
              "render plans are not verified unless the run asks");
  const auto verifying = ParseRunFile(
      RunFileText(
          R"("suite":["lifecycle"],"notAfter":1800000600,"verifyRenderPlans":true)"),
      kNow);
  test::Check(verifying && verifying->verifyRenderPlans,
              "a run can ask to verify render plans");
  test::Check(!parsed->measuredSeconds,
              "a run holds the full windows unless it asks");
  const auto quick = ParseRunFile(
      RunFileText(
          R"("suite":["soak"],"notAfter":1800000600,"measuredSeconds":60)"),
      kNow);
  test::Check(quick && quick->measuredSeconds == 60u,
              "a run can ask for quick measured windows");
  test::Check(!parsed->screenResolution,
              "a run keeps the screen resolution setting unless it asks");
  const auto unscaled = ParseRunFile(
      RunFileText(
          R"("suite":["soak"],"notAfter":1800000600,"screenResolution":false)"),
      kNow);
  test::Check(unscaled && unscaled->screenResolution == false,
              "a run can turn screen resolution off");
}

struct BadRunFile {
  std::string text;
  std::string what;
  std::int64_t now = kNow;
};

void RefusesBadRunFiles() {
  const std::vector<BadRunFile> files{
      {RunFileTextFor("lifecycle"), "expired", kNow + 601},
      {RunFileText(R"("suite":["nothing"],"notAfter":1800000600)"),
       "unknown case"},
      {RunFileText(R"("suite":[],"notAfter":1800000600)"), "empty suite"},
      {RunFileText(R"("suite":[1],"notAfter":1800000600)"),
       "case not a string"},
      {RunFileText(R"("suite":["lifecycle"],"notAfter":"soon")"),
       "notAfter not an integer"},
      {RunFileText(R"("suite":["lifecycle"],"notAfter":1800000600,"x":1)"),
       "unknown key"},
      {RunFileText(
           R"("suite":["lifecycle"],"notAfter":1,"notAfter":1800000600)"),
       "duplicate key"},
      {R"({"format":2,"run":"r1","save":"s","suite":["lifecycle"],"notAfter":1800000600})",
       "wrong format"},
      {R"({"format":1,"run":"r 1","save":"s","suite":["lifecycle"],"notAfter":1800000600})",
       "run with a space"},
      {R"({"format":1,"run":"r1","save":"..\\x","suite":["lifecycle"],"notAfter":1800000600})",
       "save as a path"},
      {R"({"format":1,"run":"","save":"s","suite":["lifecycle"],"notAfter":1800000600})",
       "empty run"},
      {R"({"format":1,"save":"s","suite":["lifecycle"],"notAfter":1800000600})",
       "missing run"},
      {RunFileText(
           R"("suite":["lifecycle"],"notAfter":1800000600,"verifyRenderPlans":1)"),
       "verifyRenderPlans that is not a boolean"},
      {RunFileText(
           R"("suite":["soak"],"notAfter":1800000600,"measuredSeconds":9)"),
       "measuredSeconds below the minimum"},
      {RunFileText(
           R"("suite":["soak"],"notAfter":1800000600,"measuredSeconds":3601)"),
       "measuredSeconds above the maximum"},
      {RunFileText(
           R"("suite":["soak"],"notAfter":1800000600,"measuredSeconds":"60")"),
       "measuredSeconds that is not a number"},
      {RunFileText(
           R"("suite":["soak"],"notAfter":1800000600,"screenResolution":"off")"),
       "screenResolution that is not a boolean"},
      {"[1,2,3]", "not an object"},
      {"{\"format\":1", "truncated"},
      {std::string(200, '[') + std::string(200, ']'), "deep nesting"},
      {std::string(70000, ' '), "oversized"},
  };
  for (const BadRunFile &file : files) {
    test::Check(!ParseRunFile(file.text, file.now).has_value(), file.what);
  }
}

struct Faults {
  bool controlGainsState = false;
  bool unloadKeepsState = false;
  bool retireFails = false;
  bool requestsHang = false;
  bool loadKeepsWork = false;
  bool loadCompletesRequest = false;
  bool editAppliesFirst = false;
  bool saveDropsEdit = false;
  bool crowdKeepsEffect = false;
  bool crowdNeverHurt = false;
  bool cameraFlipsBack = false;
  bool firstPersonMissing = false;
  bool unequipLeavesStray = false;
  bool thirdPersonMissing = false;
  bool idleCameraNeverStarts = false;
  bool culledCrowdNeverRenders = false;
  std::uint32_t crowdBehindCamera = 0;
  bool crowdMemberStuck = false;
};

struct ScratchFile {
  std::optional<float> firstOpacity;
};

enum class Acquire { kAddToInventory, kFromInventory };
enum class Destination { kAway, kStart };

struct SimActor {
  RoleFacts view;
  bool spawned = false;
  bool disabled = false;
  int renderIn = -1;
  std::uint32_t strays = 0;
};

struct World {
  std::array<SimActor, kRoleCount> actors{};
  Camera camera = Camera::kThirdPerson;
  bool away = false;
  RequestState request = NoRequest{};
  int requestIn = -1;
  Outcome requestOutcome = Outcome::kPass;
  std::uint64_t revision = 0;
  Activity activity;
  std::uint32_t loads = 0;
  int loadIn = -1;
  RecipeFacts scratch;
  std::optional<ScratchFile> disk;
  CrowdFacts crowd;
  int crowdIn = -1;
  int fightIn = -1;
  int cameraFlipIn = -1;
  int idleOrbitIn = -1;
  bool idleOrbit = false;
  std::uint32_t crowdSize = 0;
  bool crowdBehind = false;
  std::uint64_t nowMs = 0;
  Faults faults;
  std::vector<std::string> commands;
  std::vector<ResultLine> lines;

  World() { actors[IndexOf(Role::kPlayer)].view.present = true; }

  explicit World(std::optional<ScratchFile> a_disk) : World() {
    disk = a_disk;
    if (disk) {
      scratch = {
          .loaded = true, .dirty = false, .firstOpacity = disk->firstOpacity};
    }
  }

  Observation Seen() const {
    Observation seen;
    for (std::size_t role = 0; role < kRoleCount; ++role) {
      seen.actors[role] = actors[role].view;
    }
    seen.itemsLoaded = {true, true, true};
    seen.npcEffects = true;
    seen.camera = idleOrbit ? std::nullopt : std::optional{camera};
    seen.idleOrbit = idleOrbit;
    seen.awayFromStart = away;
    seen.request = request;
    seen.activity = activity;
    seen.loads = loads;
    seen.scratch = scratch;
    seen.crowd = crowd;
    seen.nowMs = nowMs;
    return seen;
  }

  SimActor &ActorOf(Role a_role) { return actors[IndexOf(a_role)]; }

  bool WearsFixture(const SimActor &a_actor) const {
    return a_actor.view.armor[IndexOf(Item::kFixture)].equipped;
  }

  void Clear(SimActor &a_actor) {
    a_actor.view.live = a_actor.view.live && faults.unloadKeepsState;
    a_actor.view.residue = 0;
    a_actor.view.thirdPersonResidue = 0;
    a_actor.view.firstPersonResidue = 0;
    a_actor.view.present = false;
    a_actor.renderIn = -1;
  }

  void Render(SimActor &a_actor) {
    a_actor.view.live = true;
    a_actor.view.renderedAttempt = ++revision;
    a_actor.view.residue = 2;
    a_actor.view.thirdPersonResidue = faults.thirdPersonMissing ? 0 : 1;
    a_actor.view.firstPersonResidue = faults.firstPersonMissing ? 0 : 1;
  }

  void Spawn(Role a_role) {
    SimActor &actor = ActorOf(a_role);
    actor = SimActor{};
    actor.spawned = true;
    actor.view.present = !away;
    actor.view.armor[IndexOf(Item::kFixture)].slotTaken = true;
    actor.view.armor[IndexOf(Item::kPlainCuirass)].slotTaken = true;
    if (a_role == Role::kControl && faults.controlGainsState) {
      actor.view.live = true;
    }
  }

  void Equip(Role a_role, Item a_item, Acquire a_acquire) {
    ItemFacts &armor = ActorOf(a_role).view.armor[IndexOf(a_item)];
    armor.equipped = true;
    armor.carried = armor.carried || a_acquire == Acquire::kAddToInventory;
    if (a_item == Item::kFixture) {
      ActorOf(a_role).renderIn = 2;
    }
  }

  static bool Enchanted(Item a_item) { return a_item != Item::kPlainCuirass; }

  static std::uint32_t EnchantedWorn(const SimActor &a_actor) {
    std::uint32_t worn = 0;
    for (const Named<Item> &item : kItems) {
      if (Enchanted(item.value) &&
          a_actor.view.armor[IndexOf(item.value)].equipped) {
        ++worn;
      }
    }
    return worn;
  }

  void TakeOff(Role a_role, Item a_item, ItemFacts a_after) {
    SimActor &actor = ActorOf(a_role);
    const bool wasWorn = actor.view.armor[IndexOf(a_item)].equipped;
    actor.view.armor[IndexOf(a_item)] = a_after;
    if (wasWorn && actor.view.live && faults.unequipLeavesStray &&
        a_item == Item::kPairedGauntlets) {
      actor.strays = 1;
    }
    if (EnchantedWorn(actor) == 0) {
      actor.view.residue = 0;
      actor.view.thirdPersonResidue = 0;
      actor.view.firstPersonResidue = 0;
    }
  }

  void Submit(Outcome a_outcome, int a_frames) {
    request = RequestPending{};
    requestIn = faults.requestsHang ? -1 : a_frames;
    requestOutcome = a_outcome;
  }

  void Travel(Destination a_destination) {
    away = a_destination == Destination::kAway;
    for (Role role : {Role::kWearer, Role::kControl}) {
      SimActor &actor = ActorOf(role);
      if (!actor.spawned || actor.disabled) {
        continue;
      }
      if (away) {
        Clear(actor);
      } else {
        actor.view.present = true;
        actor.renderIn = WearsFixture(actor) ? 2 : -1;
      }
    }
  }

  void Run(const Command &a_command) {
    std::visit(
        Overloaded{
            [&](const SoloRecipe &) { commands.emplace_back("solo"); },
            [&](const EndSolo &) { commands.emplace_back("restore"); },
            [&](const ShowRecipeSet &) {
              commands.emplace_back("show-recipes");
            },
            [&](const SpawnActor &a_c) {
              commands.emplace_back("spawn");
              Spawn(a_c.role);
            },
            [&](const DespawnActor &a_c) {
              commands.emplace_back("despawn");
              ActorOf(a_c.role) = SimActor{};
            },
            [&](const DisableActor &a_c) {
              commands.emplace_back("disable");
              ActorOf(a_c.role).disabled = true;
              Clear(ActorOf(a_c.role));
            },
            [&](const EnableActor &a_c) {
              commands.emplace_back("enable");
              SimActor &actor = ActorOf(a_c.role);
              actor.disabled = false;
              actor.view.present = !away;
              actor.renderIn = WearsFixture(actor) ? 2 : -1;
            },
            [&](const AddAndEquip &a_c) {
              commands.emplace_back("equip");
              Equip(a_c.role, a_c.item, Acquire::kAddToInventory);
            },
            [&](const EquipCarried &a_c) {
              commands.emplace_back("equip");
              Equip(a_c.role, a_c.item, Acquire::kFromInventory);
            },
            [&](const UnequipArmor &a_c) {
              commands.emplace_back("unequip");
              ItemFacts after = ActorOf(a_c.role).view.armor[IndexOf(a_c.item)];
              after.equipped = false;
              TakeOff(a_c.role, a_c.item, after);
            },
            [&](const RemoveArmor &a_c) {
              commands.emplace_back("remove");
              TakeOff(a_c.role, a_c.item, ItemFacts{});
            },
            [&](const SubmitApply &a_c) {
              commands.emplace_back("apply");
              ActorOf(a_c.role).renderIn = 3;
              Submit(Outcome::kPass, 3);
            },
            [&](const SubmitRetire &a_c) {
              commands.emplace_back("retire");
              SimActor &actor = ActorOf(a_c.role);
              actor.view.live = false;
              actor.view.residue = 0;
              actor.view.thirdPersonResidue = 0;
              actor.view.firstPersonResidue = 0;
              Submit(faults.retireFails ? Outcome::kFail : Outcome::kPass, 1);
            },
            [&](const AbortRequest &) {
              commands.emplace_back("abort");
              request = Outcome::kAborted;
            },
            [&](const TravelFromStart &) {
              commands.emplace_back("leave");
              Travel(Destination::kAway);
            },
            [&](const TravelToStart &) {
              commands.emplace_back("return");
              Travel(Destination::kStart);
            },
            [&](const StartIdleCameraAfter &) {
              commands.emplace_back("idle-camera");
              idleOrbitIn = faults.idleCameraNeverStarts ? -1 : 5;
            },
            [&](const StopIdleCamera &) {
              commands.emplace_back("hold-idle-camera");
              idleOrbitIn = -1;
            },
            [&](const SwitchCamera &a_c) {
              commands.emplace_back("camera");
              camera = a_c.camera;
              idleOrbit = false;
              cameraFlipIn =
                  faults.cameraFlipsBack && a_c.camera == Camera::kFirstPerson
                      ? 20
                      : -1;
            },
            [&](const StartDuplicate &) {
              commands.emplace_back("copy");
              scratch = {
                  .loaded = true, .dirty = true, .firstOpacity = std::nullopt};
              activity.editOutcome = WorkOutcome::kApplied;
            },
            [&](const StartOpacityEdit &a_c) {
              commands.emplace_back("edit");
              scratch.firstOpacity = a_c.value;
              scratch.dirty = true;
              activity.editOutcome = WorkOutcome::kApplied;
            },
            [&](const StartSave &) {
              commands.emplace_back("save");
              disk = faults.saveDropsEdit
                         ? std::nullopt
                         : std::optional{ScratchFile{scratch.firstOpacity}};
              scratch.dirty = false;
              activity.fileOutcome = WorkOutcome::kApplied;
            },
            [&](const StartDelete &) {
              commands.emplace_back("delete");
              scratch = {};
              disk.reset();
              activity.editOutcome = WorkOutcome::kApplied;
            },
            [&](const SpawnCrowdActors &a_c) {
              commands.emplace_back("crowd");
              crowdSize = a_c.count;
              crowd.present = a_c.count;
              crowdBehind = a_c.spot == Spot::kBehind;
              crowdIn = crowdBehind ? -1 : 5;
              crowd.awaitingView = crowdBehind ? a_c.count : 0;
            },
            [&](const TurnPlayer &) {
              commands.emplace_back("turn");
              crowdBehind = !crowdBehind;
              if (crowdBehind) {
                crowd.outOfView = crowdSize;
                return;
              }
              crowd.outOfView = 0;
              crowd.awaitingView = 0;
              crowdIn = faults.culledCrowdNeverRenders ? -1 : 5;
            },
            [&](const DespawnCrowdActors &) {
              commands.emplace_back("uncrowd");
              crowd = {};
              crowdIn = -1;
              fightIn = -1;
            },
            [&](const SetCrowdHostile &) {
              commands.emplace_back("hostile");
              fightIn = 5;
            },
            [&](const UnequipCrowdArmor &) {
              commands.emplace_back("unequip-crowd");
              if (!faults.crowdKeepsEffect) {
                crowd.rendered = 0;
              }
            },
            [&](const EquipCrowdArmor &) {
              commands.emplace_back("equip-crowd");
              crowdIn = 5;
            },
            [&](const OpenSession &a_c) {
              commands.emplace_back("begin");
              OpenSessionFor(a_c.session);
            },
            [&](const LoadSaveWith &a_c) {
              commands.emplace_back("reload");
              QueueWork(a_c.work);
              ActorOf(Role::kPlayer).view.present = false;
              loadIn = 3;
            },
            [&](const Quit &) { commands.emplace_back("quit"); },
        },
        a_command);
  }

  void QueueWork(QueuedWork a_work) {
    switch (a_work) {
    case QueuedWork::kNothing:
      break;
    case QueuedWork::kApply:
      Submit(Outcome::kPass, 5);
      activity.pendingApplications = 1;
      break;
    case QueuedWork::kEdit:
      activity.pendingApplications = 1;
      activity.editOutcome = faults.editAppliesFirst ? WorkOutcome::kApplied
                                                     : WorkOutcome::kPending;
      break;
    }
  }

  void OpenSessionFor(Session a_session) {
    if (a_session == Session::kGesture) {
      activity.gestureActive = true;
      activity.gestureOutcome = WorkOutcome::kPending;
    } else {
      activity.paintActive = true;
    }
  }

  void FinishLoad() {
    ++loads;
    away = false;
    for (SimActor &actor : actors) {
      actor = SimActor{};
    }
    ActorOf(Role::kPlayer).view.present = true;
    if (std::holds_alternative<RequestPending>(request)) {
      request =
          faults.loadCompletesRequest ? Outcome::kPass : Outcome::kAborted;
      requestIn = -1;
    }
    const auto cancel = [](WorkOutcome a_outcome) {
      return a_outcome == WorkOutcome::kPending ? WorkOutcome::kCancelledByLoad
                                                : a_outcome;
    };
    const WorkOutcome edit = cancel(activity.editOutcome);
    const WorkOutcome gesture = cancel(activity.gestureOutcome);
    if (!faults.loadKeepsWork) {
      activity = {};
    }
    activity.editOutcome = edit;
    activity.gestureOutcome = gesture;
  }

  void Tick() {
    nowMs += kSimulatedFrameMs;
    if (idleOrbitIn > 0 && --idleOrbitIn == 0) {
      idleOrbit = true;
    }
    if (cameraFlipIn > 0 && --cameraFlipIn == 0) {
      camera = Camera::kThirdPerson;
    }
    if (fightIn > 0 && --fightIn == 0) {
      crowd.fighting = crowdSize;
      crowd.engaged = crowdSize;
      crowd.hurt = faults.crowdNeverHurt ? 0 : 1;
    }
    if (crowdIn > 0 && --crowdIn == 0) {
      const std::uint32_t waiting =
          crowdBehind ? 0 : std::min(faults.crowdBehindCamera, crowdSize);
      const std::uint32_t stuck = faults.crowdMemberStuck ? 1 : 0;
      crowd.rendered =
          crowdSize - waiting - std::min(stuck, crowdSize - waiting);
      crowd.awaitingView = waiting;
    }
    if (loadIn > 0 && --loadIn == 0) {
      FinishLoad();
    }
    for (SimActor &actor : actors) {
      if (actor.renderIn > 0 && --actor.renderIn == 0) {
        Render(actor);
      }
      actor.view.strayItems = actor.view.live ? actor.strays : 0;
      actor.view.liveItems =
          actor.view.live ? EnchantedWorn(actor) + actor.view.strayItems : 0;
    }
    if (requestIn > 0 && --requestIn == 0) {
      request = requestOutcome;
    }
  }

  bool Clean() const {
    const SimActor &player = actors[IndexOf(Role::kPlayer)];
    return !away && !idleOrbit && camera == Camera::kThirdPerson &&
           !actors[IndexOf(Role::kWearer)].spawned &&
           !actors[IndexOf(Role::kControl)].spawned &&
           !player.view.armor[IndexOf(Item::kFixture)].carried &&
           !player.view.armor[IndexOf(Item::kPairedGauntlets)].carried;
  }
};

RunState Drive(World &a_world, RunState a_state, int a_frames) {
  for (int frame = 0; frame < a_frames && !Done(a_state); ++frame) {
    Advanced next = Advance(std::move(a_state), a_world.Seen());
    a_state = std::move(next.state);
    for (ResultLine &line : next.lines) {
      a_world.lines.push_back(std::move(line));
    }
    if (next.command) {
      a_world.Run(*next.command);
    }
    a_world.Tick();
  }
  return a_state;
}

RunState RunOf(std::string_view a_case) {
  const auto parsed = ParseRunFile(RunFileTextFor(a_case), kNow);
  return parsed ? BeginRun(*parsed) : RunState{};
}

RunState QuickRunOf(std::string_view a_case, std::uint32_t a_seconds) {
  const auto parsed = ParseRunFile(
      RunFileText(std::format(R"("suite":["{}"],"notAfter":1800000600,)"
                              R"("measuredSeconds":{})",
                              a_case, a_seconds)),
      kNow);
  return parsed ? BeginRun(*parsed) : RunState{};
}

const RunEnd *EndOf(const World &a_world) {
  return a_world.lines.empty() ? nullptr
                               : std::get_if<RunEnd>(&a_world.lines.back());
}

Outcome EndOutcome(const World &a_world) {
  const RunEnd *end = EndOf(a_world);
  return end ? end->outcome : Outcome::kAborted;
}

std::string FirstFailure(const World &a_world) {
  for (const ResultLine &line : a_world.lines) {
    if (const auto *step = std::get_if<StepResult>(&line);
        step && step->outcome != Outcome::kPass) {
      return step->action + ": " + step->reason;
    }
  }
  return {};
}

bool Issued(const World &a_world, std::string_view a_command) {
  return std::ranges::find(a_world.commands, a_command) !=
         a_world.commands.end();
}

void EveryCasePassesInAHealthyWorld() {
  for (const Case &entry : Catalog()) {
    World world{entry.name == "studio-reload"
                    ? std::optional{ScratchFile{0.25f}}
                    : std::nullopt};
    const RunState state = Drive(world, RunOf(entry.name), 20000);
    const std::string name{entry.name};
    test::Check(Done(state), name + " finishes");
    test::Equal(FirstFailure(world), std::string{}, name + " has no failure");
    test::Check(EndOutcome(world) == Outcome::kPass, name + " passes");
    test::Check(world.Clean(), name + " cleans up after itself");
    test::Check(!world.commands.empty() && world.commands.back() == "quit",
                name + " quits");
  }
}

void LifecycleCommandsFollowTheCase() {
  World world;
  Drive(world, RunOf("lifecycle"), 1000);
  test::Equal(world.commands,
              std::vector<std::string>{"solo", "equip", "apply", "retire",
                                       "apply", "remove", "restore", "quit"},
              "lifecycle commands");
}

void SettlingWaitsForAReadyPlayer() {
  World world;
  world.ActorOf(Role::kPlayer).view.present = false;
  RunState state = Drive(world, RunOf("lifecycle"), 500);
  test::Check(world.commands.empty(),
              "nothing runs before the player is ready");
  world.ActorOf(Role::kPlayer).view.present = true;
  state = Drive(world, std::move(state), kSettleFrames - 1);
  test::Check(world.commands.empty(), "settling counts ready frames");
  Drive(world, std::move(state), 2);
  test::Equal(world.commands, std::vector<std::string>{"solo"},
              "first step starts after settling");
}

void FailureRunsCleanup() {
  World world;
  world.faults.retireFails = true;
  Drive(world, RunOf("lifecycle"), 1000);
  test::Equal(world.commands,
              std::vector<std::string>{"solo", "equip", "apply", "retire",
                                       "remove", "restore", "quit"},
              "a failed step skips to cleanup");
  test::Check(EndOutcome(world) == Outcome::kFail, "failure decides the run");
}

void BlockedEquipLeavesForeignArmor() {
  World world;
  world.ActorOf(Role::kPlayer).view.armor[IndexOf(Item::kFixture)].slotTaken =
      true;
  Drive(world, RunOf("lifecycle"), 1000);
  test::Check(!Issued(world, "remove"),
              "cleanup removes only armor the run added");
  test::Check(EndOutcome(world) == Outcome::kBlocked, "worn armor blocks");
}

void AStrayItemFailsUnequipOne() {
  World world;
  world.faults.unequipLeavesStray = true;
  Drive(world, RunOf("unequip-one"), 20000);
  test::Check(EndOutcome(world) == Outcome::kFail,
              "effects left on an unequipped item fail the case");
  test::Check(FirstFailure(world).starts_with("await-live-items player 1"),
              "the wait for one live item names the failure");
  test::Check(world.Clean(), "unequip-one cleans up after a failure");
}

void AnIdleCameraThatNeverStartsFailsTheOrbit() {
  World world;
  world.faults.idleCameraNeverStarts = true;
  Drive(world, RunOf("soak-orbit"), 200000);
  test::Check(FirstFailure(world).starts_with("await-idle-orbit"),
              "an idle camera that never starts fails the orbit wait");
  test::Check(world.Clean(), "soak-orbit cleans up after a failure");
}

void ACrowdThatNeverReturnsFailsCull() {
  World healthy;
  Drive(healthy, RunOf("cull"), 20000);
  test::Equal(FirstFailure(healthy), std::string{}, "cull passes");
  test::Check(Issued(healthy, "turn"), "cull turns the player");

  World stuck;
  stuck.faults.culledCrowdNeverRenders = true;
  Drive(stuck, RunOf("cull"), 200000);
  test::Check(FirstFailure(stuck).starts_with("await-crowd-rendered"),
              "a crowd that never renders after the turn fails cull");
  test::Check(stuck.Clean(), "cull cleans up after a failure");
}

void SilentRequestTimesOut() {
  World world;
  world.faults.requestsHang = true;
  Drive(world, RunOf("lifecycle"), 5000);
  test::Check(Issued(world, "abort"), "a silent request is aborted");
  test::Check(EndOutcome(world) == Outcome::kFail, "a timeout fails");
}

void ControlStateFailsIsolation() {
  World world;
  world.faults.controlGainsState = true;
  Drive(world, RunOf("isolation"), 20000);
  test::Check(EndOutcome(world) == Outcome::kFail,
              "state on the control fails the case");
  test::Check(FirstFailure(world).starts_with("hold-untouched control"),
              "the hold names the control");
  test::Check(world.Clean(), "isolation cleans up after a failure");
}

void CameraFaultsFailTheCameraCase() {
  World flips;
  flips.faults.cameraFlipsBack = true;
  Drive(flips, RunOf("camera"), 20000);
  test::Check(FirstFailure(flips).starts_with("hold-camera first-person"),
              "a camera that leaves first person fails the hold");
  test::Check(flips.Clean(), "the camera case cleans up after a flip");

  World missing;
  missing.faults.firstPersonMissing = true;
  Drive(missing, RunOf("camera"), 20000);
  test::Check(FirstFailure(missing).starts_with("expect-first-person-effect"),
              "a missing first-person effect fails the case");

  World firstPersonOnly;
  firstPersonOnly.faults.thirdPersonMissing = true;
  Drive(firstPersonOnly, RunOf("camera"), 20000);
  test::Check(FirstFailure(firstPersonOnly).starts_with("expect-effect player"),
              "an effect only on the first-person body fails third person");
}

void UnloadedStateFailsUnload() {
  World world;
  world.faults.unloadKeepsState = true;
  Drive(world, RunOf("unload"), 20000);
  test::Check(EndOutcome(world) == Outcome::kFail,
              "state kept after unload fails the case");
  test::Check(FirstFailure(world).starts_with("await-retired wearer"),
              "the retirement wait names the wearer");
  test::Check(world.Clean(), "unload returns and despawns after a failure");
}

void LoadCases(std::initializer_list<std::string_view> a_cases, Faults a_faults,
               std::string_view a_expected) {
  for (std::string_view name : a_cases) {
    World world;
    world.faults = a_faults;
    Drive(world, RunOf(name), 20000);
    const std::string failure = FirstFailure(world);
    if (a_expected.empty()) {
      test::Equal(failure, std::string{}, std::string{name} + " passes");
    } else {
      test::Check(failure.starts_with(a_expected),
                  std::string{name} + " fails at " + std::string{a_expected});
    }
    test::Check(world.loads == 1, std::string{name} + " loads once");
    test::Check(world.Clean(), std::string{name} + " cleans up");
  }
}

void LoadsCancelWork() {
  LoadCases(
      {"load-idle", "load-apply", "load-edit", "load-gesture", "load-paint"},
      {}, {});
  LoadCases({"load-apply", "load-edit", "load-gesture", "load-paint"},
            {.loadKeepsWork = true}, "await-idle");
  LoadCases({"load-idle"}, {.loadKeepsWork = true}, {});
  World world;
  world.faults.loadCompletesRequest = true;
  Drive(world, RunOf("load-apply"), 20000);
  test::Check(FirstFailure(world).starts_with("expect-aborted"),
              "a request that survives the load fails");
  World applied;
  applied.faults.editAppliesFirst = true;
  Drive(applied, RunOf("load-edit"), 20000);
  test::Equal(FirstFailure(applied), std::string{},
              "an edit that lands before the load settles the case");
}

void ARingBehindTheCameraStillSettles() {
  World partly;
  partly.faults.crowdBehindCamera = 4;
  Drive(partly, RunOf("soak"), 20000);
  test::Equal(FirstFailure(partly), std::string{},
              "members waiting out of view count as settled");

  World stuck;
  stuck.faults.crowdMemberStuck = true;
  Drive(stuck, RunOf("soak"), 200000);
  test::Check(FirstFailure(stuck).starts_with("await-crowd-rendered"),
              "a member that neither renders nor waits fails the soak");
}

void SoakHoldsAndCleansUp() {
  World world;
  Drive(world, RunOf("soak"), 20000);
  test::Equal(FirstFailure(world), std::string{}, "soak passes");
  test::Check(Issued(world, "crowd") && Issued(world, "uncrowd"),
              "soak spawns and removes its crowd");
  test::Check(world.crowd.present == 0, "no crowd remains");
  std::vector<std::string> windows;
  for (const ResultLine &line : world.lines) {
    if (const auto *step = std::get_if<StepResult>(&line);
        step && step->action.starts_with("hold ")) {
      windows.push_back(step->action);
    }
  }
  test::Equal(windows,
              std::vector<std::string>{"hold baseline 30s", "hold settle 30s",
                                       "hold steady 120s", "hold recovery 30s"},
              "soak names its measurement windows");
}

std::vector<std::string> ActionsStartingWith(const World &a_world,
                                             std::string_view a_prefix) {
  std::vector<std::string> actions;
  for (const ResultLine &line : a_world.lines) {
    if (const auto *step = std::get_if<StepResult>(&line);
        step && step->action.starts_with(a_prefix)) {
      actions.push_back(step->action);
    }
  }
  return actions;
}

void SoakVariantsHoldAndCleanUp() {
  World stack;
  Drive(stack, RunOf("soak-stack"), 20000);
  test::Equal(FirstFailure(stack), std::string{}, "soak-stack passes");
  test::Equal(ActionsStartingWith(stack, "solo"), std::vector<std::string>{},
              "soak-stack solos no single recipe");
  test::Equal(ActionsStartingWith(stack, "show-recipes"),
              std::vector<std::string>{
                  "show-recipes arcane-circuit resonant-ward winterglass"},
              "soak-stack shows the three demo recipes");
  test::Check(Issued(stack, "restore"),
              "the shared cleanup ends the recipe set");

  World large;
  Drive(large, RunOf("soak-large"), 20000);
  test::Equal(FirstFailure(large), std::string{}, "soak-large passes");
  test::Equal(ActionsStartingWith(large, "spawn-crowd"),
              std::vector<std::string>{"spawn-crowd 32"},
              "soak-large spawns 32 actors");

  World waves;
  Drive(waves, RunOf("soak-waves"), 20000);
  test::Equal(FirstFailure(waves), std::string{}, "soak-waves passes");
  test::Equal(std::ranges::count(waves.commands, "crowd"), std::ptrdiff_t{3},
              "soak-waves spawns three crowds");
  test::Equal(ActionsStartingWith(waves, "hold gap"),
              std::vector<std::string>{"hold gap-1 10s", "hold gap-2 10s",
                                       "hold gap-3 10s"},
              "soak-waves measures the gap after each wave");
  test::Check(waves.crowd.present == 0, "soak-waves leaves no crowd");

  World churn;
  Drive(churn, RunOf("soak-churn"), 20000);
  test::Equal(FirstFailure(churn), std::string{}, "soak-churn passes");
  test::Equal(std::ranges::count(churn.commands, "equip-crowd"),
              std::ptrdiff_t{5},
              "soak-churn dresses the crowd once and re-equips it four times");
  test::Check(churn.crowd.present == 0, "soak-churn leaves no crowd");

  World fight;
  Drive(fight, RunOf("soak-fight"), 20000);
  test::Equal(FirstFailure(fight), std::string{}, "soak-fight passes");
  test::Equal(ActionsStartingWith(fight, "spawn-crowd"),
              std::vector<std::string>{"spawn-crowd 12 fighter"},
              "soak-fight spawns fighters");
  test::Check(Issued(fight, "hostile"), "soak-fight sets the crowd hostile");

  World vanilla;
  Drive(vanilla, RunOf("soak-vanilla"), 20000);
  test::Equal(ActionsStartingWith(vanilla, "spawn-crowd"),
              std::vector<std::string>{"spawn-crowd 12 in vanilla-cuirasses"},
              "soak-vanilla dresses mannequins in vanilla cuirasses");

  World enchanted;
  Drive(enchanted, RunOf("soak-enchanted"), 20000);
  test::Equal(FirstFailure(enchanted), std::string{}, "soak-enchanted passes");
  test::Equal(ActionsStartingWith(enchanted, "spawn-crowd"),
              std::vector<std::string>{"spawn-crowd 12 in enchanted-cuirasses"},
              "soak-enchanted dresses mannequins in enchanted cuirasses");
  test::Equal(ActionsStartingWith(enchanted, "show-all-recipes"),
              std::vector<std::string>{"show-all-recipes"},
              "soak-enchanted shows every recipe");

  World stuck;
  stuck.faults.crowdKeepsEffect = true;
  Drive(stuck, RunOf("soak-churn"), 20000);
  test::Check(FirstFailure(stuck).starts_with("await-crowd-bare"),
              "a crowd that keeps the effect after unequip fails the churn");
  test::Check(Issued(stuck, "uncrowd"),
              "a failed churn still removes the crowd");

  World standoff;
  standoff.faults.crowdNeverHurt = true;
  Drive(standoff, RunOf("soak-fight"), 20000);
  test::Check(FirstFailure(standoff).starts_with("await-crowd-hurt"),
              "a crowd that targets but never lands a blow fails the fight");
}

void StudioRoundTrip() {
  World first;
  Drive(first, RunOf("studio-save"), 20000);
  test::Equal(FirstFailure(first), std::string{}, "studio-save passes");
  test::Check(first.disk && first.disk->firstOpacity == 0.25f,
              "studio-save leaves the edited scratch recipe on disk");
  World second{first.disk};
  Drive(second, RunOf("studio-reload"), 20000);
  test::Equal(FirstFailure(second), std::string{}, "studio-reload passes");
  test::Check(!second.disk, "studio-reload deletes the scratch recipe");

  World dropped;
  dropped.faults.saveDropsEdit = true;
  Drive(dropped, RunOf("studio-save"), 20000);
  World reloaded{dropped.disk};
  Drive(reloaded, RunOf("studio-reload"), 20000);
  test::Check(FirstFailure(reloaded).starts_with("expect-scratch"),
              "a save that drops the edit fails the reload");
  test::Check(!reloaded.disk, "a failed reload still deletes the scratch");
}

const std::array<RequestState, 6> kEveryRequestState{
    NoRequest{},    RequestPending{},  Outcome::kPass,
    Outcome::kFail, Outcome::kBlocked, Outcome::kAborted};

void EveryFixedObservationEndsTheRun() {
  for (const Case &entry : Catalog()) {
    for (int bits = 0; bits < 64; ++bits) {
      for (const RequestState &request : kEveryRequestState) {
        Observation seen;
        for (std::size_t role = 0; role < kRoleCount; ++role) {
          RoleFacts &actor = seen.actors[role];
          actor.present = (bits & 1) != 0 || role == 0;
          actor.live = (bits & 2) != 0;
          actor.residue = (bits & 4) != 0 ? 1 : 0;
          actor.thirdPersonResidue = actor.residue;
          actor.armor[0] = {(bits & 8) != 0, (bits & 8) != 0};
          actor.renderedAttempt = static_cast<std::uint64_t>(bits);
        }
        seen.itemsLoaded = {(bits & 16) != 0, true};
        seen.npcEffects = (bits & 32) != 0;
        seen.request = request;
        RunState state = RunOf(entry.name);
        for (int frame = 0; frame < 100000 && !Done(state); ++frame) {
          seen.nowMs = static_cast<std::uint64_t>(frame) * kSimulatedFrameMs;
          state = Advance(std::move(state), seen).state;
        }
        test::Check(Done(state), std::string{entry.name} +
                                     ": every fixed observation ends the run");
      }
    }
  }
}

void HoldSecondsFollowTheRole() {
  const HoldWindow measured{"steady", 120, 0, HoldRole::kMeasured};
  const HoldWindow edge{"baseline", 30, 0, HoldRole::kEdge};
  const HoldWindow shortEdge{"baseline", 5, 0, HoldRole::kEdge};
  const HoldWindow fixed{"settle", 30};
  test::Check(HoldSeconds(measured, std::nullopt) == 120 &&
                  HoldSeconds(edge, std::nullopt) == 30 &&
                  HoldSeconds(fixed, std::nullopt) == 30,
              "a full run holds every window for its catalog length");
  test::Check(HoldSeconds(measured, 45) == 45 &&
                  HoldSeconds(edge, 45) == kQuickEdgeSeconds &&
                  HoldSeconds(shortEdge, 45) == 5 &&
                  HoldSeconds(fixed, 45) == 30,
              "a quick run sets the measured window, caps the edges and keeps "
              "the rest");
}

std::vector<std::string_view> HeldWindows(std::span<const Step> a_steps,
                                          HoldRole a_role) {
  std::vector<std::string_view> names;
  for (const Step &step : a_steps) {
    if (const auto *hold = std::get_if<HoldWindow>(&step);
        hold && hold->role == a_role) {
      names.push_back(hold->window);
    }
  }
  return names;
}

void MeasuredWindowsStartSettled() {
  for (const Case &entry : Catalog()) {
    const std::span<const Step> body = entry.body;
    for (std::size_t i = 0; i < body.size(); ++i) {
      const auto *hold = std::get_if<HoldWindow>(&body[i]);
      if (!hold || hold->role != HoldRole::kMeasured) {
        continue;
      }
      const auto before = HeldWindows(body.first(i), HoldRole::kFixed);
      const auto after = HeldWindows(body.subspan(i + 1), HoldRole::kEdge);
      test::Check(
          std::ranges::contains(before, std::string_view{"settle"}) &&
              std::ranges::contains(after, std::string_view{"recovery"}),
          std::string{entry.name} + "'s " + std::string{hold->window} +
              " window follows a settle and precedes a recovery");
    }
  }
}

void QuickSoakShortensItsWindows() {
  World world;
  Drive(world, QuickRunOf("soak-fight", 20), 20000);
  test::Equal(FirstFailure(world), std::string{}, "a quick soak passes");
  std::vector<std::pair<std::string, std::uint32_t>> held;
  for (const ResultLine &line : world.lines) {
    if (const auto *step = std::get_if<StepResult>(&line);
        step && step->action.starts_with("hold ")) {
      held.emplace_back(step->action, step->frames);
    }
  }
  const auto framesOf = [&](std::string_view a_action) {
    const auto found = std::ranges::find(
        held, a_action, &std::pair<std::string, std::uint32_t>::first);
    return found == held.end() ? 0u : found->second;
  };
  test::Check(framesOf("hold fight 20s") > 0 &&
                  framesOf("hold fight 20s") <= 21 &&
                  framesOf("hold baseline 10s") > 0 &&
                  framesOf("hold baseline 10s") <= 11 &&
                  framesOf("hold recovery 10s") > 0 &&
                  framesOf("hold recovery 10s") <= 11 &&
                  framesOf("hold settle 30s") >= 30,
              "a quick soak holds and labels the measured window and the edges "
              "short and the settle in full");
}

void ResultLinesAreJson() {
  const std::string step = ResultLineJson(StepResult{
      "lifecycle", 2, "apply player", Outcome::kBlocked, 7, "quote \" and \\"});
  test::Equal(step,
              std::string{R"({"kind":"step","case":"lifecycle","step":2,)"
                          R"("action":"apply player","outcome":"BLOCKED",)"
                          R"("frames":7,"reason":"quote \" and \\"})"},
              "step line escapes its reason");
  test::Equal(ResultLineJson(WindowBegins{"soak-churn", "churn-2"}),
              std::string{R"({"kind":"window","case":"soak-churn",)"
                          R"("window":"churn-2","edge":"begin"})"},
              "window line names its case and cycle");
  test::Equal(
      StartLineJson({"r", "b", "s", "t.jsonl", 60, true}),
      std::string{R"({"kind":"start","run":"r","build":"b","source":"s",)"
                  R"("trace":"t.jsonl","measuredSeconds":60,)"
                  R"("screenResolution":true})"},
      "a quick run's start line records its measured seconds");
  test::Equal(
      StartLineJson({"r", "b", "s", "t.jsonl", std::nullopt, false}),
      std::string{R"({"kind":"start","run":"r","build":"b","source":"s",)"
                  R"("trace":"t.jsonl","screenResolution":false})"},
      "the start line records whether screen resolution was on");
  test::Equal(ResultLineJson(RunEnd{Outcome::kPass, {}}),
              std::string{R"({"kind":"end","outcome":"PASS","reason":""})"},
              "end line");
}
}

int main() {
  ParsesValidRunFile();
  RefusesBadRunFiles();
  EveryCasePassesInAHealthyWorld();
  LifecycleCommandsFollowTheCase();
  SettlingWaitsForAReadyPlayer();
  FailureRunsCleanup();
  BlockedEquipLeavesForeignArmor();
  AStrayItemFailsUnequipOne();
  AnIdleCameraThatNeverStartsFailsTheOrbit();
  ACrowdThatNeverReturnsFailsCull();
  SilentRequestTimesOut();
  ControlStateFailsIsolation();
  CameraFaultsFailTheCameraCase();
  UnloadedStateFailsUnload();
  LoadsCancelWork();
  StudioRoundTrip();
  SoakHoldsAndCleansUp();
  ARingBehindTheCameraStillSettles();
  SoakVariantsHoldAndCleanUp();
  EveryFixedObservationEndsTheRun();
  HoldSecondsFollowTheRole();
  MeasuredWindowsStartSettled();
  QuickSoakShortensItsWindows();
  ResultLinesAreJson();
  return test::Finish("regression runs");
}
