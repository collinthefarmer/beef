// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/RenderFusion.h"

#include <algorithm>

namespace BetterEnchantmentEffects {
namespace {
constexpr std::size_t kMaxFusionRounds = 4096;

std::vector<RenderValueRef> Operands(const RenderPlan &plan,
                                     RenderStepId step) {
  auto operands = InputsOf(plan.steps[step].kind);
  for (const auto &operand : std::vector(operands))
    if (const auto *input = Get<RenderInputRef>(operand);
        input && input->input < plan.inputs.size())
      if (const auto *readback =
              Get<ReadbackBinding>(plan.inputs[input->input].binding))
        operands.push_back(StepOutputRef{readback->submission, 0});
  return operands;
}

bool InputChanging(const RenderPlan &plan, RenderInputId id,
                   const std::vector<bool> &steps) {
  const auto &input = plan.inputs[id];
  return Match(
      input.binding,
      [&](const TextureValue &value) {
        return input.type == RenderValueType{RenderResourceType::kFirings} ||
               (value.graph && value.graph->Changing(value.output.node));
      },
      [](const StackInputBinding &) { return true; },
      [&](const ReadbackBinding &readback) {
        return readback.submission < steps.size() && steps[readback.submission];
      });
}

bool Eligible(const RenderPlan &plan, RenderStepId producer,
              const TextureRequirements &consumer,
              const std::vector<bool> &changing,
              const std::vector<std::size_t> &consumers) {
  const auto program = AsProgram(plan, plan.steps[producer].kind);
  return program && program->requirements == consumer &&
         consumer.format == TextureFormat::kRgba8 && consumers[producer] == 1 &&
         changing[producer];
}
}

std::optional<ProgramStep> AsProgram(const RenderPlan &plan,
                                     const RenderStepKind &step) {
  return Match(
      step,
      [](const EvaluateProgramStep &k) -> std::optional<ProgramStep> {
        return ProgramStep{k.program, k.inputs, k.lookups, k.requirements};
      },
      [](const MapFieldStep &k) -> std::optional<ProgramStep> {
        return ProgramStep{
            InterpreterProgram::Map(), {k.value}, {k.lookup}, k.requirements};
      },
      [&](const ComposeVectorStep &k) -> std::optional<ProgramStep> {
        std::array<bool, 3> textures{};
        if (k.components.size() < 2 || k.components.size() > textures.size())
          return std::nullopt;
        for (std::size_t i = 0; i < k.components.size(); ++i)
          textures[i] =
              TypeOf(plan, k.components[i]) ==
              std::optional<RenderValueType>{RenderResourceType::kTexture};
        auto program = InterpreterProgram::Compose(
            std::span{textures}.first(k.components.size()));
        if (!program)
          return std::nullopt;
        return ProgramStep{
            std::move(*program), k.components, {}, k.requirements};
      },
      [](const auto &) -> std::optional<ProgramStep> { return std::nullopt; });
}

std::vector<bool> ChangingSteps(const RenderPlan &plan) {
  std::vector<bool> changing(plan.steps.size(), false);
  for (std::size_t step = 0; step < plan.steps.size(); ++step)
    for (const auto &operand : Operands(plan, step))
      changing[step] = changing[step] ||
                       Match(
                           operand,
                           [&](RenderInputRef input) {
                             return input.input < plan.inputs.size() &&
                                    InputChanging(plan, input.input, changing);
                           },
                           [&](StepOutputRef output) {
                             return output.step < step && changing[output.step];
                           });
  return changing;
}

std::vector<std::size_t> LiveConsumers(const RenderPlan &plan) {
  std::vector<std::size_t> consumers(plan.steps.size(), 0);
  std::vector<bool> live(plan.steps.size(), false);
  std::vector<RenderStepId> pending;
  for (const auto &output : plan.stackOutputs)
    if (output.result.step < plan.steps.size() && !live[output.result.step]) {
      live[output.result.step] = true;
      pending.push_back(output.result.step);
    }
  while (!pending.empty()) {
    const auto step = pending.back();
    pending.pop_back();
    std::vector<RenderStepId> seen;
    for (const auto &operand : Operands(plan, step)) {
      const auto *output = Get<StepOutputRef>(operand);
      if (!output || output->step >= plan.steps.size() ||
          std::ranges::find(seen, output->step) != seen.end())
        continue;
      seen.push_back(output->step);
      ++consumers[output->step];
      if (!live[output->step]) {
        live[output->step] = true;
        pending.push_back(output->step);
      }
    }
  }
  return consumers;
}

FusedPlan FusePrograms(RenderPlan plan) {
  FusedPlan fused{std::move(plan), 0};
  auto &steps = fused.plan.steps;
  for (std::size_t round = 0; round < kMaxFusionRounds; ++round) {
    const auto changing = ChangingSteps(fused.plan);
    const auto consumers = LiveConsumers(fused.plan);
    bool progressed = false;
    for (std::size_t step = 0; step < steps.size() && !progressed; ++step) {
      auto consumer = AsProgram(fused.plan, steps[step].kind);
      if (!consumer || consumers[step] == 0)
        continue;
      for (std::size_t k = 0; k < consumer->inputs.size(); ++k) {
        const auto *producer = Get<StepOutputRef>(consumer->inputs[k]);
        if (!producer || producer->step >= step ||
            !Is<InterpreterTextureInput>(consumer->program.Inputs()[k]) ||
            !Eligible(fused.plan, producer->step, consumer->requirements,
                      changing, consumers))
          continue;
        const auto inlined = AsProgram(fused.plan, steps[producer->step].kind);
        auto program =
            InterpreterProgram::Inline(consumer->program, k, inlined->program);
        if (!program)
          continue;
        std::vector<RenderValueRef> inputs;
        for (std::size_t i = 0; i < consumer->inputs.size(); ++i)
          if (i != k)
            inputs.push_back(consumer->inputs[i]);
        inputs.insert(inputs.end(), inlined->inputs.begin(),
                      inlined->inputs.end());
        auto lookups = consumer->lookups;
        lookups.insert(lookups.end(), inlined->lookups.begin(),
                       inlined->lookups.end());
        steps[step].kind =
            EvaluateProgramStep{std::move(*program), std::move(inputs),
                                std::move(lookups), consumer->requirements};
        ++fused.inlined;
        progressed = true;
        break;
      }
    }
    if (!progressed)
      break;
  }
  return fused;
}
}
