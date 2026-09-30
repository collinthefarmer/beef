// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/FieldInlining.h"

#include <algorithm>

namespace BetterEnchantmentEffects {
namespace {
constexpr std::size_t kMaxInliningRounds = 4096;

bool Eligible(const RenderPlan &plan, RenderStepId producer,
              const TextureRequirements &consumer,
              const std::vector<bool> &changing,
              const std::vector<std::size_t> &consumers) {
  const auto program = AsProgramLike(plan, plan.steps[producer].kind);
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
  const auto program = AsProgramLike(plan, plan.steps[producer].kind);
  if (!program || program->requirements.format != TextureFormat::kRgba8)
    return std::nullopt;
  auto stored = FieldProgram::Inline(FieldProgram::Sample(ValueType::kVec3), 0,
                                     program->program);
  if (!stored)
    return std::nullopt;
  return StoredField{LayerField{StepOutputRef{producer, 0}, std::move(*stored),
                                program->inputs, program->lookups},
                     program->requirements.size};
}

bool Reads(RenderValueRef operand, RenderStepId producer) {
  const auto *output = Get<StepOutputRef>(operand);
  return output && output->step == producer;
}

bool ReadsProducer(const LayerRead &read, RenderStepId producer) {
  const auto *value = Get<RenderValueRef>(read);
  return value && Reads(*value, producer);
}

bool FieldsFit(const CompositeStackStep &stack) {
  std::vector<const LayerField *> fields;
  for (const auto &field : stack.fields)
    fields.push_back(&field);
  return PackLayerFields(fields).has_value();
}

std::size_t AttachField(CompositeStackStep &stack, RenderStepId producer,
                        const LayerField &field) {
  const LayerFieldRef ref{stack.fields.size()};
  std::size_t attached = 0;
  for (auto &layer : stack.layers) {
    if (ReadsProducer(layer.source, producer)) {
      layer.source = ref;
      ++attached;
    }
    if (layer.mask && ReadsProducer(*layer.mask, producer)) {
      layer.mask = ref;
      ++attached;
    }
  }
  if (attached > 0)
    stack.fields.push_back(field);
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
        std::ranges::none_of(
            StepDependencies(plan, step),
            [&](RenderValueRef operand) { return Reads(operand, producer); }))
      continue;
    const auto *stack = Get<CompositeStackStep>(plan.steps[step].kind);
    if (!stack || stack->requirements.size != stored->size)
      return 0;
    auto candidate = *stack;
    const auto reads = AttachField(candidate, producer, stored->field);
    attached += reads;
    if (reads == 0 || !FieldsFit(candidate) ||
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
  std::size_t reads = 0;
  for (std::size_t producer = 0; producer < plan.steps.size(); ++producer) {
    const auto consumers = LiveConsumers(plan);
    if (changing[producer] && consumers[producer] > 0)
      reads += InlineField(plan, producer, consumers);
  }
  return reads;
}
}

std::optional<ProgramLikeStep> AsProgramLike(const RenderPlan &plan,
                                             const RenderStepKind &step) {
  return Match(
      step,
      [](const EvaluateProgramStep &k) -> std::optional<ProgramLikeStep> {
        return ProgramLikeStep{k.program, k.inputs, k.lookups, k.requirements};
      },
      [](const MapFieldStep &k) -> std::optional<ProgramLikeStep> {
        return ProgramLikeStep{
            FieldProgram::Map(), {k.value}, {k.lookup}, k.requirements};
      },
      [&](const ComposeVectorStep &k) -> std::optional<ProgramLikeStep> {
        std::array<bool, 3> textures{};
        if (k.components.size() < 2 || k.components.size() > textures.size())
          return std::nullopt;
        for (std::size_t i = 0; i < k.components.size(); ++i)
          textures[i] =
              TypeOf(plan, k.components[i]) ==
              std::optional<RenderValueType>{RenderResourceType::kTexture};
        auto program = FieldProgram::Compose(
            std::span{textures}.first(k.components.size()));
        if (!program)
          return std::nullopt;
        return ProgramLikeStep{
            std::move(*program), k.components, {}, k.requirements};
      },
      [](const auto &) -> std::optional<ProgramLikeStep> {
        return std::nullopt;
      });
}

InlinedPlan InlineFields(RenderPlan plan) {
  InlinedPlan inlined{std::move(plan), 0};
  auto &steps = inlined.plan.steps;
  for (std::size_t round = 0; round < kMaxInliningRounds; ++round) {
    const auto changing = ChangingSteps(inlined.plan);
    const auto consumers = LiveConsumers(inlined.plan);
    bool progressed = false;
    for (std::size_t step = 0; step < steps.size() && !progressed; ++step) {
      auto consumer = AsProgramLike(inlined.plan, steps[step].kind);
      if (!consumer || consumers[step] == 0)
        continue;
      for (std::size_t k = 0; k < consumer->inputs.size(); ++k) {
        const auto *producer = Get<StepOutputRef>(consumer->inputs[k]);
        if (!producer || producer->step >= step ||
            !Is<ProgramTextureInput>(consumer->program.Inputs()[k]) ||
            !Eligible(inlined.plan, producer->step, consumer->requirements,
                      changing, consumers))
          continue;
        const auto produced =
            AsProgramLike(inlined.plan, steps[producer->step].kind);
        auto program =
            FieldProgram::Inline(consumer->program, k, produced->program);
        if (!program)
          continue;
        std::vector<RenderValueRef> inputs;
        for (std::size_t i = 0; i < consumer->inputs.size(); ++i)
          if (i != k)
            inputs.push_back(consumer->inputs[i]);
        inputs.insert(inputs.end(), produced->inputs.begin(),
                      produced->inputs.end());
        auto lookups = consumer->lookups;
        lookups.insert(lookups.end(), produced->lookups.begin(),
                       produced->lookups.end());
        steps[step].kind =
            EvaluateProgramStep{std::move(*program), std::move(inputs),
                                std::move(lookups), consumer->requirements};
        ++inlined.programsInlined;
        progressed = true;
        break;
      }
    }
    if (!progressed)
      break;
  }
  inlined.layerFieldReads = InlineLayerFields(inlined.plan);
  return inlined;
}
}
