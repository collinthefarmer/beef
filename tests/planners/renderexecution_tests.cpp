// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/RenderExecution.h"
#include "test_support.h"
using namespace BetterEnchantmentEffects;
using test::Check;
int main() {
  RenderPlan plan;
  plan.inputs.push_back({RenderResourceType::kTexture, TextureValue{}});
  plan.steps.push_back({SubmitReductionStep{ReductionKind::kMean,
                                            RenderInputRef{0},
                                            ValueType::kScalar,
                                            {64, 64}},
                        "mean"});
  plan.steps.push_back({ConstantRenderStep{1.0f}, "independent"});
  RenderExecution<int> execution{plan};
  int measured = 0, constants = 0;
  bool fail = false;
  const auto execute =
      [&](const RenderStep &step,
          std::span<const ResolvedRenderInput<int>> inputs,
          std::monostate &) -> std::expected<int, std::string> {
    if (Is<ConstantRenderStep>(step.kind)) {
      ++constants;
      return 42;
    }
    ++measured;
    if (fail)
      return std::unexpected("measurement failed");
    return inputs.front().value / 2;
  };
  const auto same = [](int a, int b) { return a == b; };
  const auto select = [](const CompositeStackStep &, const int &)
      -> std::expected<std::vector<RenderValueRef>, std::string> {
    return std::vector<RenderValueRef>{};
  };
  Check(execution.SetInput(0, 8, true).has_value(), "bind imported texture");
  auto first = execution.Evaluate(StepOutputRef{0, 0}, execute, same, select);
  auto cached = execution.Evaluate(StepOutputRef{0, 0}, execute, same, select);
  Check(first && cached && measured == 1,
        "unchanged input reuses the measurement");
  Check(execution.SetInput(0, 9, true).has_value(),
        "report updated texture contents behind the same input");
  auto unchanged =
      execution.Evaluate(StepOutputRef{0, 0}, execute, same, select);
  Check(first && unchanged && measured == 2 &&
            first->changeVersion == unchanged->changeVersion,
        "recomputation with unchanged mean preserves its output version");
  Check(execution.Steps()[0].inputs.front().input ==
                RenderValueRef{RenderInputRef{0}} &&
            execution.Steps()[0].inputs.front().changeVersion == 2,
        "observations pair reference and version");
  Check(execution.SetInput(0, 10, true).has_value(), "advance source");
  fail = true;
  Check(!execution.Evaluate(StepOutputRef{0, 0}, execute, same, select) &&
            !execution.Steps()[0].outputs[0].value,
        "failure makes the current output unavailable rather than reusing "
        "stale data");
  Check(execution.Steps()[0].inputs.front().changeVersion == 2,
        "failed execution does not commit observations");
  fail = false;
  auto recovered =
      execution.Evaluate(StepOutputRef{0, 0}, execute, same, select);
  Check(recovered && recovered->value == 5 &&
            recovered->changeVersion > first->changeVersion,
        "recovery publishes a new version");
  execution.UnsetInput(0);
  Check(!execution.Evaluate(StepOutputRef{0, 0}, execute, same, select),
        "missing external input blocks cached result");
  Check(execution.Evaluate(StepOutputRef{1, 0}, execute, same, select)
                .has_value() &&
            execution.Evaluate(StepOutputRef{1, 0}, execute, same, select)
                .has_value() &&
            constants == 1,
        "zero-input producers execute once rather than treating empty "
        "observations as initial success");
  Check(!execution.Evaluate(StepOutputRef{0, 1}, execute, same, select),
        "invalid output handle is rejected");
  RenderPlan cycle;
  cycle.steps.push_back({SubmitReductionStep{ReductionKind::kMean,
                                             StepOutputRef{0, 0},
                                             ValueType::kScalar,
                                             {64, 64}},
                         "cycle"});
  Check(!ValidateRenderPlan(cycle), "cyclic producer reference is rejected");
  RenderPlan hidden;
  hidden.inputs = {{RenderResourceType::kStack, StackInputBinding{}},
                   {RenderResourceType::kVisibility, StackInputBinding{}},
                   {RenderResourceType::kTexture, TextureValue{}},
                   {ValueType::kScalar, TextureValue{}}};
  CompositeStackStep stack{
      RenderInputRef{0},
      RenderInputRef{1},
      {{RenderInputRef{2}, RenderInputRef{3}, {}, {}, Blend{}, ChannelSet{}}},
      {TextureSize{64}},
      Slot::kEmissive};
  hidden.steps.push_back({stack, "stack"});
  RenderExecution<int> visible{hidden};
  Check(visible.SetInput(1, 0, true).has_value(), "hide all layers");
  const auto choose = [](const CompositeStackStep &s, const int &shown)
      -> std::expected<std::vector<RenderValueRef>, std::string> {
    return shown ? InputsOf(RenderStepKind{s})
                 : std::vector<RenderValueRef>{s.visibility};
  };
  int rendered = 0;
  const auto draw = [&](const RenderStep &,
                        std::span<const ResolvedRenderInput<int>>,
                        std::monostate &) -> std::expected<int, std::string> {
    ++rendered;
    return 0;
  };
  Check(visible.Evaluate(StepOutputRef{0, 0}, draw, same, choose).has_value() &&
            rendered == 1,
        "hidden missing branch does not block successful empty stack");
  Check(visible.SetInput(1, 1, true).has_value(), "show layer");
  Check(!visible.Evaluate(StepOutputRef{0, 0}, draw, same, choose),
        "control change requires newly visible dependencies");
  Check(visible.SetInput(1, 0, true).has_value(), "hide failed layer");
  Check(visible.Evaluate(StepOutputRef{0, 0}, draw, same, choose).has_value(),
        "hiding failed branch restores empty stack success");
  RenderPlan diamond;
  diamond.inputs.push_back({RenderResourceType::kTexture, TextureValue{}});
  RenderValueRef previous = RenderInputRef{0};
  for (std::size_t i = 0; i < 24; ++i) {
    const StepOutputRef left{diamond.steps.size(), 0};
    diamond.steps.push_back(
        {EvaluateProgramStep{InterpreterProgram::Sample(ValueType::kScalar),
                             {previous},
                             {},
                             {TextureSize{64}}},
         "left"});
    const StepOutputRef right{diamond.steps.size(), 0};
    diamond.steps.push_back(
        {EvaluateProgramStep{InterpreterProgram::Sample(ValueType::kScalar),
                             {previous},
                             {},
                             {TextureSize{64}}},
         "right"});
    previous = StepOutputRef{diamond.steps.size(), 0};
    diamond.steps.push_back(
        {ComposeVectorStep{{left, right}, {TextureSize{64}}}, "join"});
  }
  RenderExecution<int> shared{diamond};
  Check(shared.SetInput(0, 1, true).has_value(), "bind shared diamond graph");
  int executed = 0;
  const auto produce =
      [&](const RenderStep &, std::span<const ResolvedRenderInput<int>>,
          std::monostate &) -> std::expected<int, std::string> {
    ++executed;
    return 1;
  };
  Check(shared.Evaluate(previous, produce, same, select).has_value() &&
            executed == 72,
        "shared DAG prerequisites refresh once per request without exponential "
        "traversal");
  Check(shared.Evaluate(previous, produce, same, select).has_value() &&
            executed == 72,
        "shared DAG observations still reuse prior successful outputs");
  return test::Finish("render cache execution");
}
