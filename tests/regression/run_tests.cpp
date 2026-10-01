// GPL-3.0-only with the additional permission in COPYING.md.
#include "Core.h"
#include "regression/Run.h"
#include "test_support.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Regression;

namespace {
constexpr std::int64_t kNow = 1'800'000'000;

std::size_t Index(Role a_role) { return static_cast<std::size_t>(a_role); }
std::size_t Index(Item a_item) { return static_cast<std::size_t>(a_item); }

std::string RunFile(std::string_view a_fields) {
  return std::string{R"({"format":1,"run":"r1","save":"BEEFRegression",)"} +
         std::string{a_fields} + "}";
}

std::string RunFileFor(std::string_view a_case) {
  return RunFile(std::string{R"("suite":[")"} + std::string{a_case} +
                 R"("],"notAfter":1800000600)");
}

void ParsesValidRunFile() {
  const auto parsed = ParseRunRequest(RunFileFor("lifecycle"), kNow);
  test::Check(parsed.has_value(), "valid run file parses");
  if (!parsed) {
    return;
  }
  test::Equal(parsed->run, std::string{"r1"}, "run identifier kept");
  test::Equal(parsed->save, std::string{"BEEFRegression"}, "save name kept");
  test::Equal(parsed->suite.size(), std::size_t{1}, "suite resolved");
}

void RefusesBadRunFiles() {
  const std::vector<std::pair<std::string, std::string>> cases{
      {RunFileFor("lifecycle"), "expired"},
      {RunFile(R"("suite":["nothing"],"notAfter":1800000600)"), "unknown case"},
      {RunFile(R"("suite":[],"notAfter":1800000600)"), "empty suite"},
      {RunFile(R"("suite":[1],"notAfter":1800000600)"), "case not a string"},
      {RunFile(R"("suite":["lifecycle"],"notAfter":"soon")"),
       "notAfter not an integer"},
      {RunFile(R"("suite":["lifecycle"],"notAfter":1800000600,"x":1)"),
       "unknown key"},
      {R"({"format":2,"run":"r1","save":"s","suite":["lifecycle"],"notAfter":1800000600})",
       "wrong format"},
      {R"({"format":1,"run":"r 1","save":"s","suite":["lifecycle"],"notAfter":1800000600})",
       "run with a space"},
      {R"({"format":1,"run":"r1","save":"..\\x","suite":["lifecycle"],"notAfter":1800000600})",
       "save as a path"},
      {R"({"format":1,"run":"","save":"s","suite":["lifecycle"],"notAfter":1800000600})",
       "empty run"},
      {"[1,2,3]", "not an object"},
      {"{\"format\":1", "truncated"},
      {std::string(200, '[') + std::string(200, ']'), "deep nesting"},
      {std::string(70000, ' '), "oversized"},
  };
  for (std::size_t index = 0; index < cases.size(); ++index) {
    const std::int64_t now = index == 0 ? kNow + 601 : kNow;
    test::Check(!ParseRunRequest(cases[index].first, now).has_value(),
                cases[index].second);
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
};

struct SimActor {
  ActorView view;
  bool spawned = false;
  bool disabled = false;
  int renderIn = -1;
};

struct World {
  std::array<SimActor, kRoleCount> actors{};
  bool firstPerson = false;
  bool away = false;
  RequestState request = RequestState::kNone;
  int requestIn = -1;
  RequestState requestOutcome = RequestState::kPass;
  std::uint64_t revision = 0;
  Activity activity;
  std::uint32_t loads = 0;
  int loadIn = -1;
  RecipeView scratch;
  std::optional<std::optional<float>> disk;
  Faults faults;
  std::vector<std::string> commands;
  std::vector<ResultLine> lines;

  World() { actors[Index(Role::kPlayer)].view.present = true; }

  explicit World(std::optional<std::optional<float>> a_disk) : World() {
    disk = a_disk;
    if (disk) {
      scratch = {true, false, *disk};
    }
  }

  Observation Seen() const {
    Observation seen;
    for (std::size_t role = 0; role < kRoleCount; ++role) {
      seen.actors[role] = actors[role].view;
    }
    seen.itemsLoaded = {true, true};
    seen.npcEffects = true;
    seen.firstPerson = firstPerson;
    seen.awayFromStart = away;
    seen.request = request;
    seen.activity = activity;
    seen.loads = loads;
    seen.scratch = scratch;
    return seen;
  }

  SimActor &ActorOf(Role a_role) { return actors[Index(a_role)]; }

  bool WearsFixture(const SimActor &a_actor) const {
    return a_actor.view.armor[Index(Item::kFixture)].equipped;
  }

  void Clear(SimActor &a_actor) {
    a_actor.view.live = a_actor.view.live && faults.unloadKeepsState;
    a_actor.view.traces = 0;
    a_actor.view.present = false;
    a_actor.renderIn = -1;
  }

  void Render(SimActor &a_actor) {
    a_actor.view.live = true;
    a_actor.view.renderedAttempt = ++revision;
    a_actor.view.traces = 2;
  }

  void Spawn(Role a_role) {
    SimActor &actor = ActorOf(a_role);
    actor = SimActor{};
    actor.spawned = true;
    actor.view.present = !away;
    actor.view.bodyArmorWorn = true;
    if (a_role == Role::kControl && faults.controlGainsState) {
      actor.view.live = true;
    }
  }

  void Equip(Role a_role, Item a_item, bool a_add) {
    ArmorView &armor = ActorOf(a_role).view.armor[Index(a_item)];
    armor.equipped = true;
    armor.carried = armor.carried || a_add;
    if (a_item == Item::kFixture) {
      ActorOf(a_role).renderIn = 2;
    }
  }

  void Submit(RequestState a_outcome, int a_frames) {
    request = RequestState::kWaiting;
    requestIn = faults.requestsHang ? -1 : a_frames;
    requestOutcome = a_outcome;
  }

  void Travel(bool a_away) {
    away = a_away;
    for (Role role : {Role::kWearer, Role::kControl}) {
      SimActor &actor = ActorOf(role);
      if (!actor.spawned || actor.disabled) {
        continue;
      }
      if (a_away) {
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
            [&](const RestoreSolo &) { commands.emplace_back("restore"); },
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
              Equip(a_c.role, a_c.item, true);
            },
            [&](const EquipCarried &a_c) {
              commands.emplace_back("equip");
              Equip(a_c.role, a_c.item, false);
            },
            [&](const UnequipArmor &a_c) {
              commands.emplace_back("unequip");
              ActorOf(a_c.role).view.armor[Index(a_c.item)].equipped = false;
              ActorOf(a_c.role).view.traces = 0;
            },
            [&](const RemoveArmor &a_c) {
              commands.emplace_back("remove");
              ActorOf(a_c.role).view.armor[Index(a_c.item)] = {};
              ActorOf(a_c.role).view.traces = 0;
            },
            [&](const SubmitApply &a_c) {
              commands.emplace_back("apply");
              ActorOf(a_c.role).renderIn = 3;
              Submit(RequestState::kPass, 3);
            },
            [&](const SubmitRetire &a_c) {
              commands.emplace_back("retire");
              SimActor &actor = ActorOf(a_c.role);
              actor.view.live = false;
              actor.view.traces = 0;
              Submit(faults.retireFails ? RequestState::kFail
                                        : RequestState::kPass,
                     1);
            },
            [&](const AbortRequest &) {
              commands.emplace_back("abort");
              request = RequestState::kAborted;
            },
            [&](const TravelAway &) {
              commands.emplace_back("leave");
              Travel(true);
            },
            [&](const TravelBack &) {
              commands.emplace_back("return");
              Travel(false);
            },
            [&](const SetCamera &a_c) {
              commands.emplace_back("camera");
              firstPerson = a_c.view == View::kFirstPerson;
            },
            [&](const CopyRecipe &) {
              commands.emplace_back("copy");
              scratch = {true, true, std::nullopt};
              activity.edit = WorkOutcome::kApplied;
            },
            [&](const EditOpacity &a_c) {
              commands.emplace_back("edit");
              scratch.firstOpacity = a_c.value;
              scratch.dirty = true;
              activity.edit = WorkOutcome::kApplied;
            },
            [&](const WriteRecipe &) {
              commands.emplace_back("save");
              disk = faults.saveDropsEdit ? std::nullopt : scratch.firstOpacity;
              scratch.dirty = false;
              activity.file = WorkOutcome::kApplied;
            },
            [&](const RemoveRecipe &) {
              commands.emplace_back("delete");
              scratch = {};
              disk.reset();
              activity.edit = WorkOutcome::kApplied;
            },
            [&](const BeginWork &a_c) {
              commands.emplace_back("begin");
              StartWork(a_c.work);
            },
            [&](const ReloadDuring &a_c) {
              commands.emplace_back("reload");
              StartWork(a_c.work);
              ActorOf(Role::kPlayer).view.present = false;
              loadIn = 3;
            },
            [&](const Quit &) { commands.emplace_back("quit"); },
        },
        a_command);
  }

  void StartWork(Work a_work) {
    switch (a_work) {
    case Work::kNothing:
      break;
    case Work::kApply:
      Submit(RequestState::kPass, 5);
      activity.applications = 1;
      break;
    case Work::kEdit:
      activity.applications = 1;
      activity.edit = faults.editAppliesFirst ? WorkOutcome::kApplied
                                              : WorkOutcome::kPending;
      break;
    case Work::kGesture:
      activity.gesture = true;
      activity.tuning = WorkOutcome::kPending;
      break;
    case Work::kPaint:
      activity.paint = true;
      break;
    }
  }

  void FinishLoad() {
    ++loads;
    away = false;
    for (SimActor &actor : actors) {
      actor = SimActor{};
    }
    ActorOf(Role::kPlayer).view.present = true;
    if (request == RequestState::kWaiting) {
      request = faults.loadCompletesRequest ? RequestState::kPass
                                            : RequestState::kAborted;
      requestIn = -1;
    }
    const auto cancel = [](WorkOutcome a_outcome) {
      return a_outcome == WorkOutcome::kPending ? WorkOutcome::kCancelledByLoad
                                                : a_outcome;
    };
    const WorkOutcome edit = cancel(activity.edit);
    const WorkOutcome tuning = cancel(activity.tuning);
    if (!faults.loadKeepsWork) {
      activity = {};
    }
    activity.edit = edit;
    activity.tuning = tuning;
  }

  void Tick() {
    if (loadIn > 0 && --loadIn == 0) {
      FinishLoad();
    }
    for (SimActor &actor : actors) {
      if (actor.renderIn > 0 && --actor.renderIn == 0) {
        Render(actor);
      }
    }
    if (requestIn > 0 && --requestIn == 0) {
      request = requestOutcome;
    }
  }

  bool Clean() const {
    const SimActor &player = actors[Index(Role::kPlayer)];
    return !away && !firstPerson && !actors[Index(Role::kWearer)].spawned &&
           !actors[Index(Role::kControl)].spawned &&
           !player.view.armor[Index(Item::kFixture)].carried;
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
  const auto parsed = ParseRunRequest(RunFileFor(a_case), kNow);
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
                    ? std::optional<std::optional<float>>{0.25f}
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
  world.ActorOf(Role::kPlayer).view.bodyArmorWorn = true;
  Drive(world, RunOf("lifecycle"), 1000);
  test::Check(!Issued(world, "remove"),
              "cleanup removes only armor the run added");
  test::Check(EndOutcome(world) == Outcome::kBlocked, "worn armor blocks");
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

void LoadCases(Faults a_faults, std::string_view a_expected) {
  for (std::string_view name :
       {"load-idle", "load-apply", "load-edit", "load-gesture", "load-paint"}) {
    World world;
    world.faults = a_faults;
    Drive(world, RunOf(name), 20000);
    const std::string failure = FirstFailure(world);
    if (a_expected.empty()) {
      test::Equal(failure, std::string{}, std::string{name} + " passes");
    } else if (name != "load-idle") {
      test::Check(failure.starts_with(a_expected),
                  std::string{name} + " fails at " + std::string{a_expected});
    }
    test::Check(world.loads == 1, std::string{name} + " loads once");
    test::Check(world.Clean(), std::string{name} + " cleans up");
  }
}

void LoadsCancelWork() {
  LoadCases({}, {});
  LoadCases({.loadKeepsWork = true}, "await-idle");
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

void StudioRoundTrip() {
  World first;
  Drive(first, RunOf("studio-save"), 20000);
  test::Equal(FirstFailure(first), std::string{}, "studio-save passes");
  test::Check(first.disk && *first.disk == 0.25f,
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

void EveryFixedObservationEndsTheRun() {
  for (const Case &entry : Catalog()) {
    for (int bits = 0; bits < 64; ++bits) {
      for (int request = 0; request <= 5; ++request) {
        Observation seen;
        for (std::size_t role = 0; role < kRoleCount; ++role) {
          ActorView &actor = seen.actors[role];
          actor.present = (bits & 1) != 0 || role == 0;
          actor.live = (bits & 2) != 0;
          actor.traces = (bits & 4) != 0 ? 1 : 0;
          actor.armor[0] = {(bits & 8) != 0, (bits & 8) != 0};
          actor.renderedAttempt = static_cast<std::uint64_t>(bits);
        }
        seen.itemsLoaded = {(bits & 16) != 0, true};
        seen.npcEffects = (bits & 32) != 0;
        seen.request = static_cast<RequestState>(request);
        RunState state = RunOf(entry.name);
        for (int frame = 0; frame < 100000 && !Done(state); ++frame) {
          state = Advance(std::move(state), seen).state;
        }
        test::Check(Done(state), std::string{entry.name} +
                                     ": every fixed observation ends the run");
      }
    }
  }
}

void ResultLinesAreJson() {
  const std::string step = ResultLineJson(StepResult{
      "lifecycle", 2, "apply player", Outcome::kBlocked, 7, "quote \" and \\"});
  test::Equal(step,
              std::string{R"({"kind":"step","case":"lifecycle","step":2,)"
                          R"("action":"apply player","outcome":"BLOCKED",)"
                          R"("frames":7,"reason":"quote \" and \\"})"},
              "step line escapes its reason");
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
  SilentRequestTimesOut();
  ControlStateFailsIsolation();
  UnloadedStateFailsUnload();
  LoadsCancelWork();
  StudioRoundTrip();
  EveryFixedObservationEndsTheRun();
  ResultLinesAreJson();
  return test::Finish("regression runs");
}
