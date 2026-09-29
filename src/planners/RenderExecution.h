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
  bool released = false;
  std::uint64_t lastUsed = 0;
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
    Walk walk{execute, same, select, 0, std::vector<bool>(steps_.size())};
    return Materialize(result, walk, 0);
  }
  void AdvanceEpoch() noexcept { ++epoch_; }
  void Touch(RenderStepId step) noexcept {
    if (step < steps_.size())
      steps_[step].lastUsed = epoch_;
  }
  [[nodiscard]] std::uint64_t Restores() const noexcept { return restores_; }
  template <class Releasable>
  std::size_t ReleaseIdle(std::uint64_t idleEpochs,
                          const Releasable &releasable) {
    std::size_t released = 0;
    for (std::size_t i = 0; i < steps_.size(); ++i) {
      auto &state = steps_[i];
      auto &output = state.outputs.front();
      if (!output.value || epoch_ - state.lastUsed <= idleEpochs ||
          !releasable(plan_.steps[i], *output.value))
        continue;
      output.value.reset();
      state.released = true;
      ++released;
    }
    return released;
  }

private:
  struct Walk {
    const ExecuteStep &execute;
    const SameValue &same;
    const SelectInputs &select;
    std::size_t visits = 0;
    std::vector<bool> refreshed;
  };
  using Version = std::expected<ChangeVersion, std::string>;
  using Resolved = std::expected<ResolvedRenderInput<T>, std::string>;

  RenderPlan plan_;
  std::vector<RenderInputState<T>> inputs_;
  std::vector<StepExecutionState<T, Scratch>> steps_;
  std::string problem_;
  std::uint64_t epoch_ = 1;
  std::uint64_t restores_ = 0;

  [[nodiscard]] static bool Within(Walk &walk, std::size_t depth) {
    return depth <= 64 && ++walk.visits <= 65536;
  }
  [[nodiscard]] std::unexpected<std::string> Fail(RenderStepId id,
                                                  std::string message) {
    auto &state = steps_[id];
    state.diagnostic = plan_.steps[id].displayName + ": " + message;
    state.outputs.front().value.reset();
    state.released = false;
    return std::unexpected(state.diagnostic);
  }
  [[nodiscard]] std::expected<std::vector<RenderValueRef>, std::string>
  Required(RenderStepId id, Walk &walk, std::size_t depth) {
    const auto &step = plan_.steps[id];
    auto required = InputsOf(step.kind);
    if (const auto *stack = Get<CompositeStackStep>(step.kind)) {
      auto control = Materialize(stack->visibility, walk, depth + 1);
      if (!control)
        return std::unexpected(control.error());
      auto selected = walk.select(*stack, control->value);
      if (!selected)
        return std::unexpected(selected.error());
      for (const auto &ref : *selected)
        if (std::ranges::find(required, ref) == required.end())
          return std::unexpected("selected input is not an operation operand");
      required = std::move(*selected);
      if (std::ranges::find(required, stack->visibility) == required.end())
        required.insert(required.begin(), stack->visibility);
    }
    std::vector<RenderValueRef> unique;
    for (const auto &ref : required)
      if (std::ranges::find(unique, ref) == unique.end())
        unique.push_back(ref);
    return unique;
  }
  [[nodiscard]] Version Refresh(RenderValueRef ref, Walk &walk,
                                std::size_t depth) {
    if (!Within(walk, depth))
      return std::unexpected("render execution exceeds traversal limit");
    if (const auto *input = Get<RenderInputRef>(ref)) {
      if (input->input >= inputs_.size())
        return std::unexpected("render input is unavailable");
      if (const auto *readback =
              Get<ReadbackBinding>(plan_.inputs[input->input].binding)) {
        auto submitted =
            Refresh(StepOutputRef{readback->submission, 0}, walk, depth + 1);
        if (!submitted)
          return std::unexpected(submitted.error());
      }
      if (!inputs_[input->input].value)
        return std::unexpected("render input is unavailable");
      return inputs_[input->input].changeVersion;
    }
    const auto *reference = Get<StepOutputRef>(ref);
    if (!reference || reference->step >= steps_.size() ||
        reference->output != 0)
      return std::unexpected("invalid step output");
    const auto id = reference->step;
    auto &state = steps_[id];
    auto &output = state.outputs.front();
    const bool current = output.value.has_value() || state.released;
    if (walk.refreshed[id] && current)
      return output.changeVersion;
    auto required = Required(id, walk, depth);
    if (!required)
      return Fail(id, required.error());
    bool observed = current && required->size() == state.inputs.size();
    for (const auto &input : *required) {
      auto version = Refresh(input, walk, depth + 1);
      if (!version)
        return Fail(id, version.error());
      const auto seen =
          std::ranges::find_if(state.inputs, [&](const StepInput &step) {
            return step.input == input;
          });
      observed = observed && seen != state.inputs.end() &&
                 seen->changeVersion == *version;
    }
    if (observed) {
      walk.refreshed[id] = true;
      return output.changeVersion;
    }
    return Execute(id, *required, walk, depth, false);
  }
  [[nodiscard]] Resolved Materialize(RenderValueRef ref, Walk &walk,
                                     std::size_t depth) {
    auto version = Refresh(ref, walk, depth);
    if (!version)
      return std::unexpected(version.error());
    if (const auto *input = Get<RenderInputRef>(ref))
      return ResolvedRenderInput<T>{ref, *inputs_[input->input].value,
                                    *version};
    const auto id = Get<StepOutputRef>(ref)->step;
    auto &state = steps_[id];
    state.lastUsed = epoch_;
    if (!state.outputs.front().value) {
      auto required = Required(id, walk, depth);
      if (!required)
        return Fail(id, required.error());
      auto restored = Execute(id, *required, walk, depth, true);
      if (!restored)
        return std::unexpected(restored.error());
    }
    const auto &output = state.outputs.front();
    return ResolvedRenderInput<T>{ref, *output.value, output.changeVersion};
  }
  [[nodiscard]] Version Execute(RenderStepId id,
                                std::span<const RenderValueRef> required,
                                Walk &walk, std::size_t depth, bool restoring) {
    std::vector<ResolvedRenderInput<T>> values;
    for (const auto &input : required) {
      auto resolved = Materialize(input, walk, depth + 1);
      if (!resolved)
        return Fail(id, resolved.error());
      values.push_back(std::move(*resolved));
    }
    auto &state = steps_[id];
    auto &output = state.outputs.front();
    auto produced = walk.execute(plan_.steps[id], values, state.scratch);
    if (!produced)
      return Fail(id, produced.error());
    if (restoring)
      ++restores_;
    else if (!output.value || !walk.same(*output.value, *produced)) {
      if (output.changeVersion == std::numeric_limits<ChangeVersion>::max())
        return Fail(id, "output change version exhausted");
      ++output.changeVersion;
    }
    output.value = std::move(*produced);
    state.inputs.clear();
    for (const auto &value : values)
      state.inputs.push_back({value.input, value.changeVersion});
    state.diagnostic.clear();
    state.released = false;
    state.lastUsed = epoch_;
    walk.refreshed[id] = true;
    return output.changeVersion;
  }
};
}
