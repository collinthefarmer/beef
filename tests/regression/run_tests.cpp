// GPL-3.0-only with the additional permission in COPYING.md.
#include "Core.h"
#include "regression/Run.h"
#include "test_support.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Regression;

namespace {
constexpr std::int64_t kNow = 1'800'000'000;

std::string RunFile(std::string_view a_fields) {
  return std::string{R"({"format":1,"run":"r1","save":"BEEFRegression",)"} +
         std::string{a_fields} + "}";
}

std::string ValidRunFile() {
  return RunFile(R"("suite":["lifecycle"],"notAfter":1800000600)");
}

void ParsesValidRunFile() {
  const auto parsed = ParseRunRequest(ValidRunFile(), kNow);
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
      {ValidRunFile(), "expired"},
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

struct World {
  bool equipped = false;
  bool carried = false;
  bool bodyArmorWorn = false;
  bool playerReady = true;
  RequestState request = RequestState::kNone;
  int requestFrames = -1;
  RequestState requestOutcome = RequestState::kPass;
  RequestState retireOutcome = RequestState::kPass;
  std::vector<std::string> commands;
  std::vector<ResultLine> lines;

  Observation Seen() const {
    return {playerReady, true, equipped, carried, bodyArmorWorn, request};
  }

  void Run(const Command &a_command) {
    std::visit(
        Overloaded{
            [&](const SoloRecipe &) { commands.emplace_back("solo"); },
            [&](const RestoreSolo &) { commands.emplace_back("restore"); },
            [&](const AddAndEquipFixture &) {
              commands.emplace_back("equip");
              carried = true;
              equipped = true;
            },
            [&](const UnequipAndRemoveFixture &) {
              commands.emplace_back("remove");
              carried = false;
              equipped = false;
            },
            [&](const SubmitApply &) {
              commands.emplace_back("apply");
              request = RequestState::kWaiting;
              requestFrames = 3;
            },
            [&](const SubmitRetire &) {
              commands.emplace_back("retire");
              request = RequestState::kWaiting;
              requestFrames = 1;
              requestOutcome = retireOutcome;
            },
            [&](const AbortRequest &) {
              commands.emplace_back("abort");
              request = RequestState::kAborted;
            },
            [&](const Quit &) { commands.emplace_back("quit"); },
        },
        a_command);
  }

  void Tick() {
    if (requestFrames > 0 && --requestFrames == 0) {
      request = requestOutcome;
    }
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

RunState Lifecycle() {
  const auto parsed = ParseRunRequest(ValidRunFile(), kNow);
  return parsed ? BeginRun(*parsed) : RunState{};
}

std::vector<Outcome> StepOutcomes(const World &a_world) {
  std::vector<Outcome> outcomes;
  for (const ResultLine &line : a_world.lines) {
    if (const auto *step = std::get_if<StepResult>(&line)) {
      outcomes.push_back(step->outcome);
    }
  }
  return outcomes;
}

const RunEnd *EndOf(const World &a_world) {
  return a_world.lines.empty() ? nullptr
                               : std::get_if<RunEnd>(&a_world.lines.back());
}

void LifecyclePasses() {
  World world;
  const RunState state = Drive(world, Lifecycle(), 1000);
  test::Check(Done(state), "run finishes");
  test::Equal(world.commands,
              std::vector<std::string>{"solo", "equip", "apply", "retire",
                                       "apply", "remove", "restore", "quit"},
              "commands follow the case");
  test::Equal(StepOutcomes(world).size(), std::size_t{7},
              "one result per step, cleanup included");
  const RunEnd *end = EndOf(world);
  test::Check(end && end->outcome == Outcome::kPass, "run passes");
}

void SettlingWaitsForAReadyPlayer() {
  World world;
  world.playerReady = false;
  RunState state = Drive(world, Lifecycle(), 500);
  test::Check(world.commands.empty(),
              "nothing runs before the player is ready");
  world.playerReady = true;
  state = Drive(world, std::move(state), kSettleFrames - 1);
  test::Check(world.commands.empty(), "settling counts ready frames");
  Drive(world, std::move(state), 2);
  test::Equal(world.commands, std::vector<std::string>{"solo"},
              "first step starts after settling");
}

void FailureRunsCleanup() {
  World world;
  world.retireOutcome = RequestState::kFail;
  Drive(world, Lifecycle(), 1000);
  test::Equal(world.commands,
              std::vector<std::string>{"solo", "equip", "apply", "retire",
                                       "remove", "restore", "quit"},
              "a failed step skips to cleanup");
  const RunEnd *end = EndOf(world);
  test::Check(end && end->outcome == Outcome::kFail, "failure decides the run");
}

void BlockedEquipLeavesForeignArmor() {
  World world;
  world.bodyArmorWorn = true;
  Drive(world, Lifecycle(), 1000);
  test::Equal(world.commands,
              std::vector<std::string>{"solo", "restore", "quit"},
              "cleanup removes only armor the run added");
  const RunEnd *end = EndOf(world);
  test::Check(end && end->outcome == Outcome::kBlocked, "worn armor blocks");
}

void SilentRequestTimesOut() {
  World world;
  RunState state = Drive(world, Lifecycle(), kSettleFrames + 4);
  world.requestFrames = -1;
  Drive(world, std::move(state), 5000);
  test::Check(std::ranges::find(world.commands, std::string{"abort"}) !=
                  world.commands.end(),
              "a silent request is aborted");
  const RunEnd *end = EndOf(world);
  test::Check(end && end->outcome == Outcome::kFail, "a timeout fails");
}

void RefusedRequestBlocks() {
  World world;
  RunState state = Drive(world, Lifecycle(), kSettleFrames + 4);
  test::Equal(world.commands,
              std::vector<std::string>{"solo", "equip", "apply"},
              "apply submitted");
  world.request = RequestState::kNone;
  world.requestFrames = -1;
  Drive(world, std::move(state), 1000);
  const RunEnd *end = EndOf(world);
  test::Check(end && end->outcome == Outcome::kBlocked,
              "a refused request blocks");
}

void EveryObservationIsSafe() {
  for (int bits = 0; bits < 16; ++bits) {
    for (int request = 0; request <= 5; ++request) {
      const Observation seen{true,
                             (bits & 1) != 0,
                             (bits & 2) != 0,
                             (bits & 4) != 0,
                             (bits & 8) != 0,
                             static_cast<RequestState>(request)};
      RunState state = Lifecycle();
      for (int frame = 0; frame < 5000 && !Done(state); ++frame) {
        state = Advance(std::move(state), seen).state;
      }
      test::Check(Done(state), "every fixed observation ends the run");
    }
  }
}

void ResultLinesAreJson() {
  const std::string step = ResultLineJson(StepResult{
      "lifecycle", 2, "apply", Outcome::kBlocked, 7, "quote \" and \\"});
  test::Equal(step,
              std::string{R"({"kind":"step","case":"lifecycle","step":2,)"
                          R"("action":"apply","outcome":"BLOCKED","frames":7,)"
                          R"("reason":"quote \" and \\"})"},
              "step line escapes its reason");
  test::Equal(ResultLineJson(RunEnd{Outcome::kPass, {}}),
              std::string{R"({"kind":"end","outcome":"PASS","reason":""})"},
              "end line");
}
}

int main() {
  ParsesValidRunFile();
  RefusesBadRunFiles();
  LifecyclePasses();
  SettlingWaitsForAReadyPlayer();
  FailureRunsCleanup();
  BlockedEquipLeavesForeignArmor();
  SilentRequestTimesOut();
  RefusedRequestBlocks();
  EveryObservationIsSafe();
  ResultLinesAreJson();
  return test::Finish("regression runs");
}
