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

struct StoredField {
  LayerField field;
  TextureSize size;
};

std::optional<StoredField> AsStoredField(const RenderPlan &plan,
                                         RenderStepId producer) {
  const auto program = AsProgram(plan, plan.steps[producer].kind);
  if (!program || program->requirements.format != TextureFormat::kRgba8)
    return std::nullopt;
  auto stored = InterpreterProgram::Inline(
      InterpreterProgram::Sample(ValueType::kVec3), 0, program->program);
  if (!stored)
    return std::nullopt;
  return StoredField{
      LayerField{std::move(*stored), program->inputs, program->lookups},
      program->requirements.size};
}

bool Reads(RenderValueRef operand, RenderStepId producer) {
  const auto *output = Get<StepOutputRef>(operand);
  return output && output->step == producer;
}

bool FieldsFit(const CompositeStackStep &stack) {
  std::vector<const LayerField *> fields;
  for (const auto &layer : stack.layers)
    for (const auto *field : {&layer.sourceField, &layer.maskField})
      if (*field)
        fields.push_back(&**field);
  return PackFields(fields).has_value();
}

std::size_t AttachField(CompositeStackStep &stack, RenderStepId producer,
                        const LayerField &field) {
  std::size_t attached = 0;
  for (auto &layer : stack.layers) {
    if (!layer.sourceField && Reads(layer.source, producer)) {
      layer.sourceField = field;
      ++attached;
    }
    if (!layer.maskField && layer.mask && Reads(*layer.mask, producer)) {
      layer.maskField = field;
      ++attached;
    }
  }
  return attached;
}

bool Live(const RenderPlan &plan, RenderStepId step,
          const std::vector<std::size_t> &consumers) {
  return consumers[step] > 0 ||
         std::ranges::any_of(plan.stackOutputs, [&](const auto &output) {
           return output.result.step == step;
         });
}

std::size_t InlineField(RenderPlan &plan, RenderStepId producer,
                        const std::vector<std::size_t> &consumers) {
  const auto stored = AsStoredField(plan, producer);
  if (!stored)
    return 0;
  std::vector<std::pair<RenderStepId, CompositeStackStep>> updated;
  std::size_t attached = 0;
  for (std::size_t step = producer + 1; step < plan.steps.size(); ++step) {
    if (!Live(plan, step, consumers) ||
        std::ranges::none_of(Operands(plan, step), [&](RenderValueRef operand) {
          return Reads(operand, producer);
        }))
      continue;
    const auto *stack = Get<CompositeStackStep>(plan.steps[step].kind);
    if (!stack || stack->requirements.size != stored->size)
      return 0;
    auto candidate = *stack;
    attached += AttachField(candidate, producer, stored->field);
    if (!FieldsFit(candidate) ||
        std::ranges::any_of(InputsOf(candidate), [&](RenderValueRef operand) {
          return Reads(operand, producer);
        }))
      return 0;
    updated.emplace_back(step, std::move(candidate));
  }
  if (updated.size() != consumers[producer])
    return 0;
  for (auto &[step, stack] : updated)
    plan.steps[step].kind = std::move(stack);
  return attached;
}

std::size_t InlineLayerFields(RenderPlan &plan) {
  const auto changing = ChangingSteps(plan);
  std::size_t inlined = 0;
  for (std::size_t producer = 0; producer < plan.steps.size(); ++producer) {
    const auto consumers = LiveConsumers(plan);
    if (changing[producer] && consumers[producer] > 0)
      inlined += InlineField(plan, producer, consumers);
  }
  return inlined;
}
}

std::expected<PackedFields, std::string>
PackFields(std::span<const LayerField *const> fields) {
  std::vector<const InterpreterProgram *> programs;
  PackedFields packed;
  for (const auto *field : fields) {
    if (!field)
      return std::unexpected("packed layer field is missing");
    programs.push_back(&field->program);
    packed.inputs.insert(packed.inputs.end(), field->inputs.begin(),
                         field->inputs.end());
    packed.lookups.insert(packed.lookups.end(), field->lookups.begin(),
                          field->lookups.end());
  }
  auto pack = PackInterpreters(programs);
  if (!pack)
    return std::unexpected(pack.error());
  packed.pack = std::move(*pack);
  return packed;
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
  fused.layerFields = InlineLayerFields(fused.plan);
  return fused;
}
}
