// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/OutputState.h"
#include "test_support.h"

#include <string>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
const OutputState kInactive = OutputInactive{"selector did not match"};

void ActivationChecksTheProblem() {
  Check(Activated("") == OutputState{OutputPending{}},
        "an output without a problem waits for its first render");
  Check(Activated("shell could not be created") ==
            OutputState{OutputFailed{"shell could not be created"}},
        "an output with a surface problem fails at once");
  Check(!IsActive(kInactive) &&
            ProblemOf(kInactive) == "selector did not match",
        "an inactive output keeps its placement problem");
}

void ASkippedOutputIsNeitherActiveNorFailed() {
  const OutputState skipped = OutputSkipped{"the material has a coat model"};
  Check(!IsActive(skipped) && ProblemOf(skipped).empty() &&
            SkipReasonOf(skipped) == "the material has a coat model",
        "a skipped output draws nothing and reports a reason, not a problem");
  Check(AfterRender(skipped, OutputRender::kFailed) == skipped,
        "a render never touches a skipped output");
}

void RendersMoveAnActiveOutput() {
  const OutputState rendered =
      AfterRender(OutputPending{}, OutputRender::kRendered);
  Check(rendered == OutputState{OutputRendered{}},
        "a render completes the output");
  Check(AfterRender(rendered, OutputRender::kPending) ==
            OutputState{OutputPending{}},
        "a pending render waits again");
  const OutputState failed = AfterRender(rendered, OutputRender::kFailed);
  Check(ProblemOf(failed) == kRenderFailedProblem,
        "a failed render names the retry");
  Check(AfterRender(failed, OutputRender::kRendered) ==
            OutputState{OutputRendered{}},
        "a later render recovers a failed output");
  Check(AfterRender(kInactive, OutputRender::kRendered) == kInactive,
        "a render does not activate an inactive output");
}

void ProblemsKeepTheActivation() {
  Check(WithProblem(kInactive, "replaced by recipe b") ==
            OutputState{OutputInactive{"replaced by recipe b"}},
        "a problem on an inactive output keeps it inactive");
  Check(WithProblem(OutputPending{}, "replaced by recipe b") ==
            OutputState{OutputFailed{"replaced by recipe b"}},
        "a problem on an active output fails it");
}

void LostAndRetiredGeometriesFail() {
  Check(ProblemOf(AfterLostGeometry(OutputRendered{})) == kRenderFailedProblem,
        "a lost geometry fails a rendered output");
  Check(AfterLostGeometry(kInactive) == kInactive,
        "a lost geometry leaves an inactive output alone");
  Check(AfterRetire(OutputRendered{}) ==
            OutputState{OutputFailed{std::string{kRetiredProblem}}},
        "a retire fails an active output");
  Check(AfterRetire(OutputFailed{"material binding failed"}) ==
            OutputState{OutputFailed{"material binding failed"}},
        "a retire keeps an earlier problem");
  Check(AfterRetire(OutputInactive{}) ==
            OutputState{OutputInactive{std::string{kRetiredProblem}}},
        "a retire names itself on an inactive output without a problem");
}
}

int main() {
  ActivationChecksTheProblem();
  ASkippedOutputIsNeitherActiveNorFailed();
  RendersMoveAnActiveOutput();
  ProblemsKeepTheActivation();
  LostAndRetiredGeometriesFail();
  return test::Finish("planners_outputstate");
}
