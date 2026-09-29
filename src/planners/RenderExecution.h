// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/RenderPlan.h"

#include <limits>

namespace BetterEnchantmentEffects {
using ChangeVersion = std::uint64_t;
struct StepInput {
  RenderValueRef input;
  ChangeVersion changeVersion = 0;
};
template <class T> struct StepOutput {
  std::optional<T> value;
  ChangeVersion changeVersion = 0;
};
template <class T> struct RenderInputState {
  std::optional<T> value;
  ChangeVersion changeVersion = 0;
};
template <class T, class Scratch> struct StepExecutionState {
  std::vector<StepInput> inputs;
  std::vector<StepOutput<T>> outputs{1};
  Scratch scratch;
  std::string diagnostic;
};
template <class T> struct ResolvedRenderInput {
  RenderValueRef input;
  T value;
  ChangeVersion changeVersion = 0;
};
template <class T, class Scratch = std::monostate> class RenderExecution {
public:
  using ExecuteStep = std::function<std::expected<T, std::string>(
      const RenderStep &, std::span<const ResolvedRenderInput<T>>, Scratch &)>;
  using SameValue = std::function<bool(const T &, const T &)>;
  using SelectInputs =
      std::function<std::expected<std::vector<RenderValueRef>, std::string>(
          const CompositeStackStep &, const T &)>;

  explicit RenderExecution(RenderPlan plan)
      : plan_(std::move(plan)), inputs_(plan_.inputs.size()),
        steps_(plan_.steps.size()) {
    if (auto valid = ValidateRenderPlan(plan_); !valid)
      problem_ = valid.error();
  }
  [[nodiscard]] const RenderPlan &Plan() const noexcept { return plan_; }
  [[nodiscard]] std::span<const StepExecutionState<T, Scratch>>
  Steps() const noexcept {
    return steps_;
  }
  [[nodiscard]] std::span<const RenderInputState<T>> Inputs() const noexcept {
    return inputs_;
  }
  [[nodiscard]] Scratch *ScratchOf(RenderStepId step) noexcept {
    return step < steps_.size() ? &steps_[step].scratch : nullptr;
  }
  [[nodiscard]] std::expected<void, std::string>
  SetInput(RenderInputId id, T value, bool changed) {
    if (id >= inputs_.size())
      return std::unexpected("invalid render input");
    auto &input = inputs_[id];
    if (!input.value || changed) {
      if (input.changeVersion == std::numeric_limits<ChangeVersion>::max())
        return std::unexpected("input change version exhausted");
      ++input.changeVersion;
    }
    input.value = std::move(value);
    return {};
  }
  void UnsetInput(RenderInputId id) {
    if (id < inputs_.size())
      inputs_[id].value.reset();
  }
  [[nodiscard]] std::expected<ResolvedRenderInput<T>, std::string>
  Evaluate(RenderValueRef result, const ExecuteStep &execute,
           const SameValue &same, const SelectInputs &select) {
    if (!problem_.empty())
      return std::unexpected(problem_);
    if (!execute || !same || !select)
      return std::unexpected("render execution callbacks are incomplete");
    std::size_t visits = 0;
    std::vector<bool> refreshed(steps_.size(), false);
    return EvaluateValue(result, execute, same, select, 0, visits, refreshed);
  }

private:
  RenderPlan plan_;
  std::vector<RenderInputState<T>> inputs_;
  std::vector<StepExecutionState<T, Scratch>> steps_;
  std::string problem_;

  [[nodiscard]] std::expected<ResolvedRenderInput<T>, std::string>
  EvaluateValue(RenderValueRef result, const ExecuteStep &execute,
                const SameValue &same, const SelectInputs &select,
                std::size_t depth, std::size_t &visits,
                std::vector<bool> &refreshed) {
    if (depth > 64 || ++visits > 65536)
      return std::unexpected("render execution exceeds traversal limit");
    if (const auto *input = Get<RenderInputRef>(result)) {
      if (input->input >= inputs_.size())
        return std::unexpected("render input is unavailable");
      if (const auto *readback =
              Get<ReadbackBinding>(plan_.inputs[input->input].binding)) {
        auto submitted =
            EvaluateValue(StepOutputRef{readback->submission, 0}, execute, same,
                          select, depth + 1, visits, refreshed);
        if (!submitted)
          return std::unexpected(submitted.error());
      }
      if (!inputs_[input->input].value)
        return std::unexpected("render input is unavailable");
      const auto &value = inputs_[input->input];
      return ResolvedRenderInput<T>{result, *value.value, value.changeVersion};
    }
    const auto *reference = Get<StepOutputRef>(result);
    if (!reference || reference->step >= steps_.size() ||
        reference->output != 0)
      return std::unexpected("invalid step output");
    const auto &step = plan_.steps[reference->step];
    auto &state = steps_[reference->step];
    auto &output = state.outputs.front();
    if (refreshed[reference->step] && output.value)
      return ResolvedRenderInput<T>{result, *output.value,
                                    output.changeVersion};
    const auto fail = [&](std::string message)
        -> std::expected<ResolvedRenderInput<T>, std::string> {
      state.diagnostic = step.displayName + ": " + message;
      output.value.reset();
      return std::unexpected(state.diagnostic);
    };
    auto required = InputsOf(step.kind);
    if (const auto *stack = Get<CompositeStackStep>(step.kind)) {
      auto control = EvaluateValue(stack->visibility, execute, same, select,
                                   depth + 1, visits, refreshed);
      if (!control)
        return fail(control.error());
      auto selected = select(*stack, control->value);
      if (!selected)
        return fail(selected.error());
      for (const auto &ref : *selected)
        if (std::ranges::find(required, ref) == required.end())
          return fail("selected input is not an operation operand");
      required = std::move(*selected);
      if (std::ranges::find(required, stack->visibility) == required.end())
        required.insert(required.begin(), stack->visibility);
    }
    std::vector<ResolvedRenderInput<T>> values;
    for (const auto &input : required) {
      if (std::ranges::find_if(values, [&](const ResolvedRenderInput<T> &v) {
            return v.input == input;
          }) != values.end())
        continue;
      auto resolved = EvaluateValue(input, execute, same, select, depth + 1,
                                    visits, refreshed);
      if (!resolved)
        return fail(resolved.error());
      values.push_back(std::move(*resolved));
    }
    bool reusable =
        output.value.has_value() && values.size() == state.inputs.size();
    for (const auto &value : values) {
      const auto observed =
          std::ranges::find_if(state.inputs, [&](const StepInput &input) {
            return input.input == value.input;
          });
      reusable = reusable && observed != state.inputs.end() &&
                 observed->changeVersion == value.changeVersion;
    }
    if (reusable) {
      refreshed[reference->step] = true;
      return ResolvedRenderInput<T>{result, *output.value,
                                    output.changeVersion};
    }
    auto produced = execute(step, values, state.scratch);
    if (!produced)
      return fail(produced.error());
    if (!output.value || !same(*output.value, *produced)) {
      if (output.changeVersion == std::numeric_limits<ChangeVersion>::max())
        return fail("output change version exhausted");
      ++output.changeVersion;
    }
    output.value = std::move(*produced);
    state.inputs.clear();
    for (const auto &value : values)
      state.inputs.push_back({value.input, value.changeVersion});
    state.diagnostic.clear();
    refreshed[reference->step] = true;
    return ResolvedRenderInput<T>{result, *output.value, output.changeVersion};
  }
};
}
