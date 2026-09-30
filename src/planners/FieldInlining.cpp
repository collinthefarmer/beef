// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/FieldInlining.h"

#include <algorithm>

namespace BetterEnchantmentEffects {
namespace {
constexpr std::size_t kMaxInliningRounds = 4096;

struct StepUses {
  std::vector<bool> changing;
  std::vector<std::size_t> consumers;
};

StepUses StepUsesOf(const RenderPlan &plan) {
  return StepUses{ChangingSteps(plan), LiveConsumers(plan)};
}

bool CanInlineProducer(const RenderPlan &plan, RenderStepId producer,
                       const TextureRequirements &consumer,
                       const StepUses &uses) {
  if (producer >= plan.steps.size() || producer >= uses.changing.size() ||
      producer >= uses.consumers.size())
    return false;
  const std::optional<ProgramLikeStep> program =
      AsProgramLike(plan, plan.steps[producer].kind);
  return program && program->requirements == consumer &&
         consumer.format == TextureFormat::kRgba8 &&
         uses.consumers[producer] == 1 && uses.changing[producer];
}

bool ReadsTexture(const FieldProgram &program, std::size_t input) {
  const std::span<const ProgramInput> inputs = program.Inputs();
  return input < inputs.size() && Is<ProgramTextureInput>(inputs[input]);
}

std::vector<RenderValueRef>
InputsWithout(std::span<const RenderValueRef> inputs, std::size_t removed) {
  std::vector<RenderValueRef> kept;
  for (std::size_t i = 0; i < inputs.size(); ++i)
    if (i != removed)
      kept.push_back(inputs[i]);
  return kept;
}

std::optional<EvaluateProgramStep>
InlinedStep(const ProgramLikeStep &consumer, std::size_t input,
            const ProgramLikeStep &producer) {
  auto program =
      FieldProgram::Inline(consumer.program, input, producer.program);
  if (!program)
    return std::nullopt;
  std::vector<RenderValueRef> inputs = InputsWithout(consumer.inputs, input);
  inputs.insert(inputs.end(), producer.inputs.begin(), producer.inputs.end());
  std::vector<RenderValueRef> lookups = consumer.lookups;
  lookups.insert(lookups.end(), producer.lookups.begin(),
                 producer.lookups.end());
  return EvaluateProgramStep{std::move(*program), std::move(inputs),
                             std::move(lookups), consumer.requirements};
}

bool InlineIntoStep(RenderPlan &plan, RenderStepId step, const StepUses &uses) {
  if (step >= plan.steps.size() || step >= uses.consumers.size())
    return false;
  const std::optional<ProgramLikeStep> consumer =
      AsProgramLike(plan, plan.steps[step].kind);
  if (!consumer || uses.consumers[step] == 0)
    return false;
  for (std::size_t input = 0; input < consumer->inputs.size(); ++input) {
    const auto *producer = Get<StepOutputRef>(consumer->inputs[input]);
    if (!producer || producer->step >= step ||
        !ReadsTexture(consumer->program, input) ||
        !CanInlineProducer(plan, producer->step, consumer->requirements, uses))
      continue;
    const std::optional<ProgramLikeStep> produced =
        AsProgramLike(plan, plan.steps[producer->step].kind);
    if (!produced)
      continue;
    std::optional<EvaluateProgramStep> inlined =
        InlinedStep(*consumer, input, *produced);
    if (!inlined)
      continue;
    plan.steps[step].kind = std::move(*inlined);
    return true;
  }
  return false;
}

bool InlineOneProgram(RenderPlan &plan) {
  const StepUses uses = StepUsesOf(plan);
  for (RenderStepId step = 0; step < plan.steps.size(); ++step)
    if (InlineIntoStep(plan, step, uses))
      return true;
  return false;
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
  fields.reserve(stack.fields.size());
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

bool Live(const RenderPlan &plan, RenderStepId step, const StepUses &uses) {
  return (step < uses.consumers.size() && uses.consumers[step] > 0) ||
         std::ranges::any_of(plan.stackOutputs, [&](const auto &output) {
           return output.result.step == step;
         });
}

std::size_t InlineField(RenderPlan &plan, RenderStepId producer,
                        const StepUses &uses) {
  const auto stored = AsStoredField(plan, producer);
  if (!stored)
    return 0;
  std::vector<std::pair<RenderStepId, CompositeStackStep>> updated;
  std::size_t attached = 0;
  for (std::size_t step = producer + 1; step < plan.steps.size(); ++step) {
    if (!Live(plan, step, uses) ||
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
  if (producer >= uses.consumers.size() ||
      updated.size() != uses.consumers[producer])
    return 0;
  for (auto &[step, stack] : updated)
    plan.steps[step].kind = std::move(stack);
  return attached;
}

std::size_t InlineLayerFields(RenderPlan &plan) {
  StepUses uses = StepUsesOf(plan);
  std::size_t reads = 0;
  for (RenderStepId producer = 0; producer < plan.steps.size(); ++producer) {
    uses.consumers = LiveConsumers(plan);
    if (producer < uses.changing.size() && producer < uses.consumers.size() &&
        uses.changing[producer] && uses.consumers[producer] > 0)
      reads += InlineField(plan, producer, uses);
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
  for (std::size_t round = 0; round < kMaxInliningRounds; ++round) {
    if (!InlineOneProgram(inlined.plan))
      break;
    ++inlined.programsInlined;
  }
  inlined.layerFieldReads = InlineLayerFields(inlined.plan);
  return inlined;
}
}
