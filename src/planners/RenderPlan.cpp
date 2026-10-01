// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/RenderPlan.h"

#include <algorithm>
#include <bit>
#include <format>
#include <iterator>

namespace BetterEnchantmentEffects {
const LayerField *ReadField(const CompositeStackStep &stack,
                            const LayerRead &read) {
  const auto *ref = Get<LayerFieldRef>(read);
  return ref && ref->field < stack.fields.size() ? &stack.fields[ref->field]
                                                 : nullptr;
}
std::optional<RenderValueRef> ReadValue(const CompositeStackStep &stack,
                                        const LayerRead &read) {
  if (const auto *value = Get<RenderValueRef>(read))
    return *value;
  if (const auto *field = ReadField(stack, read))
    return field->value;
  return std::nullopt;
}
std::vector<RenderValueRef> LayerOperands(const CompositeStackStep &stack,
                                          const PlannedLayer &layer) {
  std::vector<RenderValueRef> operands;
  const auto read = [&](const LayerRead &r) {
    if (const auto *value = Get<RenderValueRef>(r))
      operands.push_back(*value);
    else if (const auto *field = ReadField(stack, r)) {
      operands.insert(operands.end(), field->inputs.begin(),
                      field->inputs.end());
      operands.insert(operands.end(), field->lookups.begin(),
                      field->lookups.end());
    }
  };
  read(layer.source);
  operands.push_back(layer.opacity);
  if (layer.mask)
    read(*layer.mask);
  if (layer.color)
    operands.push_back(*layer.color);
  return operands;
}
std::expected<PackedLayerFields, std::string>
PackLayerFields(std::span<const LayerField *const> fields) {
  std::vector<const FieldProgram *> programs;
  PackedLayerFields packed;
  for (const auto *field : fields) {
    if (!field)
      return std::unexpected("packed layer field is missing");
    programs.push_back(&field->program);
    packed.inputs.insert(packed.inputs.end(), field->inputs.begin(),
                         field->inputs.end());
    packed.lookups.insert(packed.lookups.end(), field->lookups.begin(),
                          field->lookups.end());
  }
  auto pack = PackPrograms(programs);
  if (!pack)
    return std::unexpected(pack.error());
  packed.pack = std::move(*pack);
  return packed;
}
std::vector<std::size_t>
VisibleLayerFields(std::span<const PlannedLayer *const> layers) {
  std::vector<std::size_t> fields;
  const auto note = [&](const LayerRead &read) {
    if (const auto *ref = Get<LayerFieldRef>(read);
        ref && std::ranges::find(fields, ref->field) == fields.end())
      fields.push_back(ref->field);
  };
  for (const auto *layer : layers) {
    if (!layer)
      continue;
    note(layer->source);
    if (layer->mask)
      note(*layer->mask);
  }
  return fields;
}
std::optional<std::uint32_t> SegmentIndexOf(std::span<const std::size_t> fields,
                                            const LayerRead &read) {
  const auto *ref = Get<LayerFieldRef>(read);
  if (!ref)
    return std::nullopt;
  const auto found = std::ranges::find(fields, ref->field);
  if (found == fields.end())
    return std::nullopt;
  return static_cast<std::uint32_t>(found - fields.begin());
}
namespace {
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

}
std::vector<RenderValueRef> InputsOf(const RenderStepKind &step) {
  return Match(
      step,
      [](const UnavailableStep &) { return std::vector<RenderValueRef>{}; },
      [](const ConstantRenderStep &) { return std::vector<RenderValueRef>{}; },
      [](const BuildBakeBuffersStep &k) {
        std::vector<RenderValueRef> inputs{k.mesh};
        if (const auto *origin = Get<RenderValueRef>(k.operation))
          inputs.push_back(*origin);
        return inputs;
      },
      [](const BakeMeshStep &k) { return std::vector{k.buffers}; },
      [](const NormalSlopeStep &k) { return std::vector{k.material}; },
      [](const SubmitMaterialSampleStep &k) { return std::vector{k.material}; },
      [](const ClusterMaterialStep &k) { return std::vector{k.sample}; },
      [](const DrawClustersStep &k) {
        return std::vector{k.material, k.analysis};
      },
      [](const SampleFieldStep &k) {
        auto inputs = k.coordinates;
        inputs.insert(inputs.begin(), k.texture);
        return inputs;
      },
      [](const SubmitReductionStep &k) { return std::vector{k.value}; },
      [](const BuildLookupStep &k) {
        std::vector<RenderValueRef> inputs;
        inputs.reserve(k.boundArguments.size());
        for (const auto &a : k.boundArguments)
          inputs.push_back(a.value);
        return inputs;
      },
      [](const EvaluateValueStep &k) { return k.inputs; },
      [](const EvaluateProgramStep &k) {
        auto inputs = k.inputs;
        inputs.insert(inputs.end(), k.lookups.begin(), k.lookups.end());
        return inputs;
      },
      [](const MapFieldStep &k) { return std::vector{k.value, k.lookup}; },
      [](const ComposeVectorStep &k) { return k.components; },
      [](const DrawRippleStep &k) {
        auto inputs = k.inputs;
        inputs.insert(inputs.begin(), k.positions);
        return inputs;
      },
      [](const CompositeStackStep &k) {
        std::vector<RenderValueRef> inputs{k.base, k.visibility};
        for (const auto &layer : k.layers)
          std::ranges::copy(LayerOperands(k, layer),
                            std::back_inserter(inputs));
        return inputs;
      });
}
RenderValueType OutputType(const RenderStepKind &step) {
  return Match(
      step, [](const UnavailableStep &k) -> RenderValueType { return k.type; },
      [](const EvaluateValueStep &k) -> RenderValueType { return k.type; },
      [](const ConstantRenderStep &k) -> RenderValueType {
        return TypeOf(k.value);
      },
      [](const BuildBakeBuffersStep &) -> RenderValueType {
        return RenderResourceType::kBakeBuffers;
      },
      [](const SubmitMaterialSampleStep &) -> RenderValueType {
        return RenderResourceType::kSubmission;
      },
      [](const ClusterMaterialStep &) -> RenderValueType {
        return RenderResourceType::kMaterialAnalysis;
      },
      [](const SubmitReductionStep &) -> RenderValueType {
        return RenderResourceType::kSubmission;
      },
      [](const BuildLookupStep &) -> RenderValueType {
        return RenderResourceType::kLookup;
      },
      [](const CompositeStackStep &) -> RenderValueType {
        return RenderResourceType::kStack;
      },
      [](const BakeMeshStep &) -> RenderValueType {
        return RenderResourceType::kTexture;
      },
      [](const NormalSlopeStep &) -> RenderValueType {
        return RenderResourceType::kTexture;
      },
      [](const DrawClustersStep &) -> RenderValueType {
        return RenderResourceType::kTexture;
      },
      [](const SampleFieldStep &) -> RenderValueType {
        return RenderResourceType::kTexture;
      },
      [](const EvaluateProgramStep &) -> RenderValueType {
        return RenderResourceType::kTexture;
      },
      [](const MapFieldStep &) -> RenderValueType {
        return RenderResourceType::kTexture;
      },
      [](const ComposeVectorStep &) -> RenderValueType {
        return RenderResourceType::kTexture;
      },
      [](const DrawRippleStep &) -> RenderValueType {
        return RenderResourceType::kTexture;
      });
}
std::string_view StepKindName(const RenderStepKind &step) {
  return Match(
      step,
      [](const UnavailableStep &) { return std::string_view{"Unavailable"}; },
      [](const ConstantRenderStep &) { return std::string_view{"Constant"}; },
      [](const BuildBakeBuffersStep &) {
        return std::string_view{"BuildBakeBuffers"};
      },
      [](const BakeMeshStep &) { return std::string_view{"BakeMesh"}; },
      [](const NormalSlopeStep &) { return std::string_view{"NormalSlope"}; },
      [](const SubmitMaterialSampleStep &) {
        return std::string_view{"SubmitMaterialSample"};
      },
      [](const ClusterMaterialStep &) {
        return std::string_view{"ClusterMaterial"};
      },
      [](const DrawClustersStep &) { return std::string_view{"DrawClusters"}; },
      [](const SampleFieldStep &) { return std::string_view{"SampleField"}; },
      [](const SubmitReductionStep &) {
        return std::string_view{"SubmitReduction"};
      },
      [](const BuildLookupStep &) { return std::string_view{"BuildLookup"}; },
      [](const EvaluateValueStep &) {
        return std::string_view{"EvaluateValue"};
      },
      [](const EvaluateProgramStep &) {
        return std::string_view{"EvaluateProgram"};
      },
      [](const MapFieldStep &) { return std::string_view{"MapField"}; },
      [](const ComposeVectorStep &) {
        return std::string_view{"ComposeVector"};
      },
      [](const DrawRippleStep &) { return std::string_view{"DrawRipple"}; },
      [](const CompositeStackStep &) {
        return std::string_view{"CompositeStack"};
      });
}
std::optional<TextureRequirements> RequirementsOf(const RenderStepKind &step) {
  return Match(step,
               [](const auto &kind) -> std::optional<TextureRequirements> {
                 if constexpr (requires { kind.requirements; })
                   return kind.requirements;
                 else
                   return std::nullopt;
               });
}
std::optional<RenderValueType> TypeOf(const RenderPlan &plan,
                                      RenderValueRef value) {
  return Match(
      value,
      [&](RenderInputRef ref) -> std::optional<RenderValueType> {
        return ref.input < plan.inputs.size()
                   ? std::optional{plan.inputs[ref.input].type}
                   : std::nullopt;
      },
      [&](StepOutputRef ref) -> std::optional<RenderValueType> {
        return ref.output == 0 && ref.step < plan.steps.size()
                   ? std::optional{OutputType(plan.steps[ref.step].kind)}
                   : std::nullopt;
      });
}
namespace {
std::expected<void, std::string> RequireType(const RenderPlan &plan,
                                             RenderValueRef value,
                                             RenderValueType type) {
  const std::optional<RenderValueType> actual = TypeOf(plan, value);
  if (!actual || *actual != type)
    return std::unexpected("incompatible operand type");
  return {};
}
bool HasEitherType(const RenderPlan &plan, RenderValueRef value,
                   RenderValueType first, RenderValueType second) {
  const std::optional<RenderValueType> actual = TypeOf(plan, value);
  return actual && (*actual == first || *actual == second);
}
bool BindingsMatch(const FieldProgram &program,
                   std::span<const RenderValueRef> inputs,
                   std::span<const RenderValueRef> lookups) {
  return inputs.size() == program.Inputs().size() &&
         lookups.size() == program.FunctionLookups().size();
}
std::expected<void, std::string>
ValidateBuildBakeBuffersStep(const RenderPlan &plan,
                             const BuildBakeBuffersStep &step) {
  if (std::expected<void, std::string> mesh =
          RequireType(plan, step.mesh, RenderResourceType::kMesh);
      !mesh)
    return mesh;
  if (const RenderValueRef *origin = Get<RenderValueRef>(step.operation))
    return RequireType(plan, *origin, ValueType::kVec3);
  return {};
}
std::expected<void, std::string>
ValidateSampleFieldStep(const RenderPlan &plan, const SampleFieldStep &step) {
  if (!HasEitherType(plan, step.texture, RenderResourceType::kTexture,
                     RenderResourceType::kMaterial))
    return std::unexpected("field sampling requires a texture or material");
  for (const RenderValueRef coordinate : step.coordinates)
    if (std::expected<void, std::string> typed =
            RequireType(plan, coordinate, ValueType::kVec2);
        !typed)
      return typed;
  if (!step.field.graph || !step.field.graph->NodeAt(step.field.output.node))
    return std::unexpected("sample field has no valid graph output");
  return {};
}
std::expected<void, std::string>
ValidateSubmitReductionStep(const RenderPlan &plan,
                            const SubmitReductionStep &step) {
  if (std::expected<void, std::string> value =
          RequireType(plan, step.value, RenderResourceType::kTexture);
      !value)
    return value;
  if (static_cast<unsigned>(step.kind) >
          static_cast<unsigned>(ReductionKind::kMaximum) ||
      static_cast<unsigned>(step.type) >
          static_cast<unsigned>(ValueType::kVec3))
    return std::unexpected("invalid reduction operation");
  if (!step.domain.width || !step.domain.height || step.domain.width > 4096 ||
      step.domain.height > 4096)
    return std::unexpected("invalid reduction domain");
  return {};
}
std::expected<void, std::string>
ValidateLookupArguments(const RenderPlan &plan, const BuildLookupStep &step,
                        const FunctionDefinition &function) {
  std::vector<bool> bound(function.parameters.size());
  if (step.sampledParameter >= bound.size())
    return std::unexpected("invalid lookup function or domain");
  bound[step.sampledParameter] = true;
  for (const LookupArgument &argument : step.boundArguments) {
    if (argument.parameter >= bound.size() || bound[argument.parameter])
      return std::unexpected("duplicate or invalid lookup argument");
    bound[argument.parameter] = true;
    if (std::expected<void, std::string> typed = RequireType(
            plan, argument.value, function.parameters[argument.parameter].type);
        !typed)
      return typed;
  }
  if (std::ranges::find(bound, false) != bound.end())
    return std::unexpected("lookup argument is missing");
  return {};
}
std::expected<void, std::string>
ValidateBuildLookupStep(const RenderPlan &plan, const BuildLookupStep &step) {
  const FunctionDefinition *function =
      step.graph ? step.graph->FunctionAt(step.function) : nullptr;
  if (!function || function->isDisabled || step.samples != 256 ||
      step.sampledParameter >= function->parameters.size())
    return std::unexpected("invalid lookup function or domain");
  if (SamplesAsLookup(*function, step.sampledParameter))
    return ValidateLookupArguments(plan, step, *function);
  if (FunctionResultTypeOf(*function) !=
      std::optional<GraphValueType>{ValueType::kScalar})
    return std::unexpected("lookup result must be scalar");
  return std::unexpected("lookup sampled parameter must be scalar");
}
std::expected<void, std::string>
ValidateEvaluateValueStep(const RenderPlan &plan,
                          const EvaluateValueStep &step) {
  const RecipeGraph *graph = step.value.graph;
  const RecipeNode *node =
      graph ? graph->NodeAt(step.value.output.node) : nullptr;
  if (!node || graph->SampleDependent(step.value.output))
    return std::unexpected("invalid uniform operation");
  const std::vector<OutputRef> operands = InputsOf(node->kind);
  if (operands.size() != step.inputs.size())
    return std::unexpected("uniform bindings do not match");
  for (std::size_t n = 0; n < operands.size(); ++n) {
    const std::optional<GraphValueType> type = graph->OutputType(operands[n]);
    const ValueType *numeric = type ? Get<ValueType>(*type) : nullptr;
    if (!numeric)
      return std::unexpected("uniform operand is not numeric");
    if (std::expected<void, std::string> typed =
            RequireType(plan, step.inputs[n], *numeric);
        !typed)
      return typed;
  }
  return {};
}
std::expected<void, std::string>
ValidateEvaluateProgramStep(const RenderPlan &plan,
                            const EvaluateProgramStep &step) {
  if (!BindingsMatch(step.program, step.inputs, step.lookups))
    return std::unexpected("program bindings do not match");
  const std::span<const ProgramInput> programInputs = step.program.Inputs();
  for (std::size_t n = 0; n < step.inputs.size(); ++n) {
    if (Is<ProgramTextureInput>(programInputs[n])) {
      if (std::expected<void, std::string> typed =
              RequireType(plan, step.inputs[n], RenderResourceType::kTexture);
          !typed)
        return typed;
      continue;
    }
    const std::optional<RenderValueType> actual = TypeOf(plan, step.inputs[n]);
    if (!actual || !Is<ValueType>(*actual))
      return std::unexpected("program value input is not numeric");
  }
  for (const RenderValueRef lookup : step.lookups)
    if (std::expected<void, std::string> typed =
            RequireType(plan, lookup, RenderResourceType::kLookup);
        !typed)
      return typed;
  return {};
}
std::expected<void, std::string>
ValidateComposeVectorStep(const RenderPlan &plan,
                          const ComposeVectorStep &step) {
  if (step.components.size() < 2 || step.components.size() > 3)
    return std::unexpected("invalid vector component count");
  for (const RenderValueRef component : step.components)
    if (!HasEitherType(plan, component, RenderResourceType::kTexture,
                       ValueType::kScalar))
      return std::unexpected(
          "vector component must be a scalar field or scalar value");
  return {};
}
std::expected<void, std::string>
ValidateDrawRippleStep(const RenderPlan &plan, const DrawRippleStep &step) {
  if (std::expected<void, std::string> positions =
          RequireType(plan, step.positions, RenderResourceType::kTexture);
      !positions)
    return positions;
  if (!step.field.graph || !step.field.graph->NodeAt(step.field.output.node))
    return std::unexpected("invalid ripple field");
  return {};
}
std::expected<void, std::string>
ValidateStackLayer(const RenderPlan &plan, const CompositeStackStep &stack,
                   const PlannedLayer &layer) {
  const std::optional<RenderValueRef> source = ReadValue(stack, layer.source);
  if (!source || !HasEitherType(plan, *source, RenderResourceType::kTexture,
                                ValueType::kVec3))
    return std::unexpected("invalid stack source");
  if (std::expected<void, std::string> opacity =
          RequireType(plan, layer.opacity, ValueType::kScalar);
      !opacity)
    return opacity;
  if (layer.mask) {
    const std::optional<RenderValueRef> mask = ReadValue(stack, *layer.mask);
    if (!mask)
      return std::unexpected("invalid stack mask");
    if (std::expected<void, std::string> typed =
            RequireType(plan, *mask, RenderResourceType::kTexture);
        !typed)
      return typed;
  }
  if (layer.color)
    return RequireType(plan, *layer.color, ValueType::kVec3);
  return {};
}
std::expected<void, std::string>
ValidateCompositeStackStep(const RenderPlan &plan,
                           const CompositeStackStep &step) {
  if (std::expected<void, std::string> base =
          RequireType(plan, step.base, RenderResourceType::kStack);
      !base)
    return base;
  if (std::expected<void, std::string> visibility =
          RequireType(plan, step.visibility, RenderResourceType::kVisibility);
      !visibility)
    return visibility;
  for (const LayerField &field : step.fields)
    if (!BindingsMatch(field.program, field.inputs, field.lookups))
      return std::unexpected("layer field binding count mismatch");
  for (const PlannedLayer &layer : step.layers)
    if (std::expected<void, std::string> valid =
            ValidateStackLayer(plan, step, layer);
        !valid)
      return valid;
  return {};
}
std::expected<void, std::string> ValidateStepKind(const RenderPlan &plan,
                                                  const RenderStepKind &kind) {
  using Validation = std::expected<void, std::string>;
  return Match(
      kind, [](const UnavailableStep &) -> Validation { return {}; },
      [](const ConstantRenderStep &) -> Validation { return {}; },
      [&](const BuildBakeBuffersStep &k) -> Validation {
        return ValidateBuildBakeBuffersStep(plan, k);
      },
      [&](const BakeMeshStep &k) -> Validation {
        return RequireType(plan, k.buffers, RenderResourceType::kBakeBuffers);
      },
      [&](const NormalSlopeStep &k) -> Validation {
        return RequireType(plan, k.material, RenderResourceType::kMaterial);
      },
      [&](const SubmitMaterialSampleStep &k) -> Validation {
        return RequireType(plan, k.material, RenderResourceType::kMaterial);
      },
      [&](const ClusterMaterialStep &k) -> Validation {
        return RequireType(plan, k.sample, RenderResourceType::kMaterialSample);
      },
      [&](const DrawClustersStep &k) -> Validation {
        return RequireType(plan, k.material, RenderResourceType::kMaterial)
            .and_then([&] {
              return RequireType(plan, k.analysis,
                                 RenderResourceType::kMaterialAnalysis);
            });
      },
      [&](const SampleFieldStep &k) -> Validation {
        return ValidateSampleFieldStep(plan, k);
      },
      [&](const SubmitReductionStep &k) -> Validation {
        return ValidateSubmitReductionStep(plan, k);
      },
      [&](const BuildLookupStep &k) -> Validation {
        return ValidateBuildLookupStep(plan, k);
      },
      [&](const EvaluateValueStep &k) -> Validation {
        return ValidateEvaluateValueStep(plan, k);
      },
      [&](const EvaluateProgramStep &k) -> Validation {
        return ValidateEvaluateProgramStep(plan, k);
      },
      [&](const MapFieldStep &k) -> Validation {
        return RequireType(plan, k.value, RenderResourceType::kTexture)
            .and_then([&] {
              return RequireType(plan, k.lookup, RenderResourceType::kLookup);
            });
      },
      [&](const ComposeVectorStep &k) -> Validation {
        return ValidateComposeVectorStep(plan, k);
      },
      [&](const DrawRippleStep &k) -> Validation {
        return ValidateDrawRippleStep(plan, k);
      },
      [&](const CompositeStackStep &k) -> Validation {
        return ValidateCompositeStackStep(plan, k);
      });
}
std::expected<void, std::string> ValidateStepInputs(const RenderPlan &plan,
                                                    RenderStepId step) {
  if (step >= plan.steps.size())
    return std::unexpected("render step is missing");
  for (const RenderValueRef input : InputsOf(plan.steps[step].kind)) {
    if (!TypeOf(plan, input))
      return std::unexpected("invalid input reference");
    if (const StepOutputRef *producer = Get<StepOutputRef>(input);
        producer && producer->step >= step)
      return std::unexpected(
          "step dependencies are not in acyclic producer order");
  }
  return {};
}
std::expected<void, std::string> ValidateStep(const RenderPlan &plan,
                                              RenderStepId step) {
  if (step >= plan.steps.size())
    return std::unexpected("render step is missing");
  const RenderStep &validated = plan.steps[step];
  std::expected<void, std::string> valid = ValidateStepInputs(plan, step);
  if (valid)
    valid = ValidateStepKind(plan, validated.kind);
  if (!valid)
    return std::unexpected(validated.displayName + ": " + valid.error());
  return {};
}
std::expected<void, std::string>
ValidateReadbackInputs(const RenderPlan &plan) {
  for (const RenderInput &input : plan.inputs) {
    const ReadbackBinding *readback = Get<ReadbackBinding>(input.binding);
    if (!readback)
      continue;
    const RenderStepKind *submission =
        readback->submission < plan.steps.size()
            ? &plan.steps[readback->submission].kind
            : nullptr;
    const SubmitReductionStep *reduction =
        submission ? Get<SubmitReductionStep>(*submission) : nullptr;
    const bool valid =
        (reduction && input.type == RenderValueType{reduction->type}) ||
        (submission && Is<SubmitMaterialSampleStep>(*submission) &&
         input.type == RenderValueType{RenderResourceType::kMaterialSample});
    if (!valid)
      return std::unexpected("invalid readback input");
  }
  return {};
}
std::expected<void, std::string>
ValidateOutputBindings(const RenderPlan &plan) {
  for (const StackOutputBinding &output : plan.stackOutputs)
    if (TypeOf(plan, output.result) !=
        std::optional<RenderValueType>{RenderResourceType::kStack})
      return std::unexpected("invalid stack output binding");
  for (const TextureUseBinding &use : plan.textureUses)
    if (TypeOf(plan, use.texture) !=
        std::optional<RenderValueType>{RenderResourceType::kTexture})
      return std::unexpected("invalid texture use binding");
  return {};
}
}
std::expected<void, std::string> ValidateRenderPlan(const RenderPlan &plan) {
  if (plan.steps.size() > 4096 || plan.inputs.size() > 8192)
    return std::unexpected("render plan exceeds its size limit");
  for (RenderStepId step = 0; step < plan.steps.size(); ++step)
    if (std::expected<void, std::string> valid = ValidateStep(plan, step);
        !valid)
      return valid;
  if (std::expected<void, std::string> readback = ValidateReadbackInputs(plan);
      !readback)
    return readback;
  return ValidateOutputBindings(plan);
}
std::vector<RenderValueRef> StepDependencies(const RenderPlan &plan,
                                             RenderStepId step) {
  auto operands = InputsOf(plan.steps[step].kind);
  for (const auto &operand : std::vector(operands))
    if (const auto *input = Get<RenderInputRef>(operand);
        input && input->input < plan.inputs.size())
      if (const auto *readback =
              Get<ReadbackBinding>(plan.inputs[input->input].binding))
        operands.emplace_back(StepOutputRef{readback->submission, 0});
  return operands;
}

std::string ShareKeyOf(const RenderStep &step,
                       const TextureRequirements &requirements) {
  return std::format("{}|{}|{}|{}|{}", step.valueKey, StepKindName(step.kind),
                     requirements.size.Pixels(),
                     static_cast<int>(requirements.format),
                     static_cast<int>(requirements.mipPolicy));
}

constexpr std::size_t kMaxShareKeyBytes = 4096;

std::string OperandBits(const Value &value) {
  const auto bits = [](float component) {
    return std::bit_cast<std::uint32_t>(component);
  };
  return Match(
      value, [&](float v) { return std::format("{:08x}", bits(v)); },
      [&](Vec2 v) { return std::format("{:08x}{:08x}", bits(v.x), bits(v.y)); },
      [&](Vec3 v) {
        return std::format("{:08x}{:08x}{:08x}", bits(v.x), bits(v.y),
                           bits(v.z));
      });
}

std::string ShareKeyFor(std::string_view shareKey,
                        std::span<const Value> operands) {
  std::string key{shareKey};
  for (const Value &operand : operands) {
    key += '|';
    key += OperandBits(operand);
  }
  return key;
}

RenderPlan MarkShareableSteps(RenderPlan plan) {
  const std::vector<bool> changing = ChangingSteps(plan);
  for (std::size_t i = 0; i < plan.steps.size() && i < changing.size(); ++i) {
    RenderStep &step = plan.steps[i];
    const std::optional<TextureRequirements> requirements =
        RequirementsOf(step.kind);
    const bool texture =
        OutputType(step.kind) == RenderValueType{RenderResourceType::kTexture};
    step.shareKey = !changing[i] && texture && requirements &&
                            !step.valueKey.empty() &&
                            step.valueKey.size() <= kMaxShareKeyBytes &&
                            !Is<CompositeStackStep>(step.kind)
                        ? ShareKeyOf(step, *requirements)
                        : std::string{};
  }
  return plan;
}

std::vector<bool> ChangingSteps(const RenderPlan &plan) {
  std::vector<bool> changing(plan.steps.size(), false);
  for (std::size_t step = 0; step < plan.steps.size(); ++step)
    for (const auto &operand : StepDependencies(plan, step))
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
    for (const auto &operand : StepDependencies(plan, step)) {
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

}
