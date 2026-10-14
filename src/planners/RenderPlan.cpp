// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/RenderPlan.h"

#include "planners/StackShader.h"

#include <algorithm>
#include <array>
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
enum class StackControls : std::uint8_t { kChanging, kStable };

bool InputChanging(const RenderPlan &plan, RenderInputId id,
                   const std::vector<bool> &steps, StackControls controls) {
  const auto &input = plan.inputs[id];
  return Match(
      input.binding,
      [&](const TextureValue &value) {
        return input.type == RenderValueType{RenderResourceType::kFirings} ||
               (value.graph && value.graph->Changing(value.output.node));
      },
      [&](const StackInputBinding &) {
        return controls == StackControls::kChanging;
      },
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
      [](const SubmitMaterialSampleStep &k) {
        return std::vector{k.material, k.mesh};
      },
      [](const DrawClustersStep &k) {
        std::vector inputs{k.material, k.groups};
        if (k.pick) {
          inputs.push_back(*k.pick);
        }
        if (k.areaBias) {
          inputs.push_back(*k.areaBias);
        }
        if (k.area) {
          inputs.insert(inputs.end(),
                        {k.area->seed, k.area->geometries, k.area->areaBias,
                         k.area->geometryIndex, k.area->geometryCount});
          if (k.area->charts) {
            inputs.insert(inputs.end(),
                          {k.area->charts->chartIds, k.area->charts->count});
          }
        }
        return inputs;
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

std::vector<std::vector<RenderStepId>>
DownstreamStepsOf(const RenderPlan &plan) {
  std::vector<std::vector<RenderStepId>> readers(plan.inputs.size());
  std::vector<std::vector<RenderStepId>> consumers(plan.steps.size());
  for (RenderStepId step = 0; step < plan.steps.size(); ++step)
    for (const RenderValueRef &ref : InputsOf(plan.steps[step].kind))
      Match(
          ref,
          [&](const RenderInputRef &input) {
            if (input.input < readers.size())
              readers[input.input].push_back(step);
          },
          [&](const StepOutputRef &output) {
            if (output.step < consumers.size())
              consumers[output.step].push_back(step);
          });
  for (RenderInputId input = 0; input < plan.inputs.size(); ++input)
    if (const auto *readback = Get<ReadbackBinding>(plan.inputs[input].binding);
        readback && readback->submission < consumers.size())
      std::ranges::copy(readers[input],
                        std::back_inserter(consumers[readback->submission]));
  std::vector<std::vector<RenderStepId>> downstream(plan.inputs.size());
  for (RenderInputId input = 0; input < plan.inputs.size(); ++input) {
    std::vector<bool> seen(plan.steps.size(), false);
    std::vector<RenderStepId> pending = readers[input];
    while (!pending.empty()) {
      const RenderStepId step = pending.back();
      pending.pop_back();
      if (seen[step])
        continue;
      seen[step] = true;
      downstream[input].push_back(step);
      for (const RenderStepId consumer : consumers[step])
        if (!seen[consumer])
          pending.push_back(consumer);
    }
  }
  return downstream;
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
namespace {
inline constexpr std::array<std::string_view, kRenderStepKindCount>
    kStepKindNames{"Unavailable",   "Constant",      "BuildBakeBuffers",
                   "BakeMesh",      "NormalSlope",   "SubmitMaterialSample",
                   "DrawClusters",  "SampleField",   "SubmitReduction",
                   "BuildLookup",   "EvaluateValue", "EvaluateProgram",
                   "MapField",      "ComposeVector", "DrawRipple",
                   "CompositeStack"};
}
static_assert(std::ranges::none_of(kStepKindNames, &std::string_view::empty));
std::string_view StepKindNameAt(std::size_t index) {
  return index < kStepKindNames.size() ? kStepKindNames[index]
                                       : std::string_view{"Unknown"};
}
std::string_view StepKindName(const RenderStepKind &step) {
  return StepKindNameAt(step.index());
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
std::expected<void, std::string> ValidateAreaPick(const RenderPlan &plan,
                                                  const AreaPickInputs &area) {
  if (area.charts) {
    if (std::expected<void, std::string> charts = RequireType(
            plan, area.charts->chartIds, RenderResourceType::kTexture);
        !charts)
      return charts;
    if (std::expected<void, std::string> count =
            RequireType(plan, area.charts->count, ValueType::kScalar);
        !count)
      return count;
  }
  for (const RenderValueRef scalar : {area.seed, area.geometries, area.areaBias,
                                      area.geometryIndex, area.geometryCount}) {
    if (std::expected<void, std::string> checked =
            RequireType(plan, scalar, ValueType::kScalar);
        !checked)
      return checked;
  }
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
        return RequireType(plan, k.material, RenderResourceType::kMaterial)
            .and_then([&] {
              return RequireType(plan, k.mesh, RenderResourceType::kMesh);
            });
      },
      [&](const DrawClustersStep &k) -> Validation {
        return RequireType(plan, k.material, RenderResourceType::kMaterial)
            .and_then([&] {
              return RequireType(plan, k.groups,
                                 RenderResourceType::kMaterialGroups);
            })
            .and_then([&]() -> Validation {
              if (!k.pick)
                return {};
              return RequireType(plan, *k.pick, ValueType::kScalar);
            })
            .and_then([&]() -> Validation {
              if (!k.areaBias)
                return {};
              return RequireType(plan, *k.areaBias, ValueType::kScalar);
            })
            .and_then([&]() -> Validation {
              if (!k.area)
                return {};
              return ValidateAreaPick(plan, *k.area);
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
         input.type == RenderValueType{RenderResourceType::kMaterialGroups});
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

namespace {
std::string RequirementsText(const TextureRequirements &requirements) {
  return std::format("{}|{}|{}", requirements.side.index,
                     static_cast<int>(requirements.format),
                     static_cast<int>(requirements.mipPolicy));
}

std::optional<std::string>
ContentKeyOf(const TextureKey &produced, const RenderStepKind &kind,
             const TextureRequirements &requirements) {
  std::string key =
      std::format("{}|{}|{}|{}", produced.identity.canonical,
                  RequirementsText(produced.requirements), StepKindName(kind),
                  RequirementsText(requirements));
  if (key.size() > kMaxCacheKeyBytes)
    return std::nullopt;
  return key;
}

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

}

std::optional<std::string> CacheKeyFor(std::string_view contentKey,
                                       std::span<const Value> operands) {
  std::string key{contentKey};
  for (const Value &operand : operands) {
    key += '|';
    key += OperandBits(operand);
  }
  if (key.size() > kMaxCacheKeyBytes) {
    return std::nullopt;
  }
  return key;
}

namespace {
using ReadCounts = std::vector<std::size_t>;

void CountRead(ReadCounts &counts, RenderValueRef ref) {
  if (const auto *output = Get<StepOutputRef>(ref);
      output && output->step < counts.size())
    ++counts[output->step];
}

void CountReads(ReadCounts &counts, std::span<const RenderValueRef> refs) {
  for (const RenderValueRef ref : refs)
    CountRead(counts, ref);
}

void CountSampledLayerRead(ReadCounts &counts, const CompositeStackStep &stack,
                           const LayerRead &read, bool direct) {
  if (const auto *value = Get<RenderValueRef>(read)) {
    if (direct)
      CountRead(counts, *value);
  } else if (const auto *field = ReadField(stack, read)) {
    CountReads(counts, field->inputs);
  }
}

void CountSampledReads(ReadCounts &counts, const RenderStepKind &kind) {
  if (const auto *program = Get<EvaluateProgramStep>(kind))
    CountReads(counts, program->inputs);
  else if (const auto *map = Get<MapFieldStep>(kind))
    CountRead(counts, map->value);
  else if (const auto *vector = Get<ComposeVectorStep>(kind))
    CountReads(counts, vector->components);
  else if (const auto *stack = Get<CompositeStackStep>(kind))
    for (const PlannedLayer &layer : stack->layers) {
      CountSampledLayerRead(counts, *stack, layer.source, true);
      if (layer.mask)
        CountSampledLayerRead(counts, *stack, *layer.mask, false);
    }
}

bool ReadsTexture(const SampleFieldStep &step) {
  const RecipeNode *node =
      step.field.graph ? step.field.graph->NodeAt(step.field.output.node)
                       : nullptr;
  return node &&
         (Is<ImageOperation>(node->kind) || Is<MaterialOperation>(node->kind));
}
}

RenderPlan MarkDirectSamples(RenderPlan plan) {
  ReadCounts reads(plan.steps.size(), 0);
  ReadCounts sampled(plan.steps.size(), 0);
  for (const RenderStep &step : plan.steps) {
    CountReads(reads, InputsOf(step.kind));
    CountSampledReads(sampled, step.kind);
  }
  for (std::size_t i = 0; i < plan.steps.size(); ++i) {
    auto *sample = Get<SampleFieldStep>(plan.steps[i].kind);
    if (!sample || !ReadsTexture(*sample) || reads[i] == 0 ||
        reads[i] != sampled[i])
      continue;
    sample->direct = true;
    plan.steps[i].contentKey.reset();
  }
  return plan;
}

namespace {
std::optional<RenderStepId> StackStepOf(const RenderPlan &plan,
                                        GeometryId geometry, ChainLink link) {
  for (const StackOutputBinding &binding : plan.stackOutputs)
    if (binding.geometry == geometry && binding.placement == link.placement &&
        binding.output == link.output &&
        binding.result.step < plan.steps.size() &&
        Is<CompositeStackStep>(plan.steps[binding.result.step].kind))
      return binding.result.step;
  return std::nullopt;
}

LayerRead ShiftedRead(const LayerRead &read, std::size_t fieldOffset) {
  if (const auto *field = Get<LayerFieldRef>(read))
    return LayerFieldRef{field->field + fieldOffset};
  return read;
}

void AppendLink(CompositeStackStep &merged, const CompositeStackStep &link) {
  const std::size_t fieldOffset = merged.fields.size();
  merged.fields.insert(merged.fields.end(), link.fields.begin(),
                       link.fields.end());
  for (PlannedLayer layer : link.layers) {
    layer.source = ShiftedRead(layer.source, fieldOffset);
    if (layer.mask)
      layer.mask = ShiftedRead(*layer.mask, fieldOffset);
    merged.layers.push_back(layer);
  }
}

bool FitsOnePass(const CompositeStackStep &stack) {
  if (stack.layers.size() > kMaxStackLayers)
    return false;
  std::vector<const LayerField *> fields;
  fields.reserve(stack.fields.size());
  for (const LayerField &field : stack.fields)
    fields.push_back(&field);
  return PackLayerFields(fields).has_value();
}

struct MergedChain {
  CompositeStackStep stack;
  std::vector<std::size_t> layerStarts;
};

std::optional<MergedChain> MergedStackOf(RenderPlan &plan,
                                         const SlotChainRequest &chain) {
  if (chain.links.size() < 2)
    return std::nullopt;
  std::optional<MergedChain> merged;
  for (const ChainLink link : chain.links) {
    const std::optional<RenderStepId> step =
        StackStepOf(plan, chain.geometry, link);
    if (!step)
      return std::nullopt;
    const auto &stack = *Get<CompositeStackStep>(plan.steps[*step].kind);
    if (!merged) {
      const RenderInputRef visibility{plan.inputs.size()};
      plan.inputs.push_back(
          {RenderResourceType::kVisibility,
           StackInputBinding{link.placement, link.output, chain.geometry},
           chain.geometry});
      merged = MergedChain{
          CompositeStackStep{
              stack.base, visibility, {}, stack.requirements, stack.slot, {}},
          {}};
    } else if (stack.requirements != merged->stack.requirements ||
               stack.slot != merged->stack.slot) {
      return std::nullopt;
    }
    merged->layerStarts.push_back(merged->stack.layers.size());
    AppendLink(merged->stack, stack);
  }
  if (!merged || !FitsOnePass(merged->stack))
    return std::nullopt;
  return merged;
}
}

RenderPlan MergeSlotChains(RenderPlan plan,
                           std::span<const SlotChainRequest> chains) {
  for (const SlotChainRequest &chain : chains) {
    const std::size_t inputs = plan.inputs.size();
    std::optional<MergedChain> merged = MergedStackOf(plan, chain);
    if (!merged) {
      plan.inputs.resize(inputs);
      continue;
    }
    const std::size_t layerCount = merged->stack.layers.size();
    const StepOutputRef result{plan.steps.size(), 0};
    plan.steps.push_back(
        {std::move(merged->stack),
         "merged " + std::string{SlotName(chain.slot)} + " chain",
         {},
         {}});
    plan.mergedChains.push_back({chain.geometry, chain.surface, chain.slot,
                                 chain.links, std::move(merged->layerStarts),
                                 layerCount, result});
  }
  return plan;
}

RenderPlan MarkContentKeyedSteps(RenderPlan plan) {
  const std::vector<bool> changing = ChangingSteps(plan);
  for (std::size_t i = 0; i < plan.steps.size() && i < changing.size(); ++i) {
    RenderStep &step = plan.steps[i];
    const std::optional<TextureRequirements> requirements =
        RequirementsOf(step.kind);
    const bool texture =
        OutputType(step.kind) == RenderValueType{RenderResourceType::kTexture};
    const bool shareable = !changing[i] && texture && requirements &&
                           step.producedKey &&
                           !Is<CompositeStackStep>(step.kind);
    step.contentKey =
        shareable ? ContentKeyOf(*step.producedKey, step.kind, *requirements)
                  : std::nullopt;
  }
  return plan;
}

namespace {
std::vector<bool> StepsChangingWith(const RenderPlan &plan,
                                    StackControls controls) {
  std::vector<bool> changing(plan.steps.size(), false);
  for (std::size_t step = 0; step < plan.steps.size(); ++step)
    for (const auto &operand : StepDependencies(plan, step))
      changing[step] =
          changing[step] ||
          Match(
              operand,
              [&](RenderInputRef input) {
                return input.input < plan.inputs.size() &&
                       InputChanging(plan, input.input, changing, controls);
              },
              [&](StepOutputRef output) {
                return output.step < step && changing[output.step];
              });
  return changing;
}
}

std::vector<bool> ChangingSteps(const RenderPlan &plan) {
  return StepsChangingWith(plan, StackControls::kChanging);
}

std::vector<bool> AnimatedSteps(const RenderPlan &plan) {
  return StepsChangingWith(plan, StackControls::kStable);
}

namespace {
bool CoveredByMerge(const RenderPlan &plan, const StackOutputBinding &output) {
  return std::ranges::any_of(
      plan.mergedChains, [&](const MergedChainBinding &merged) {
        return merged.geometry == output.geometry &&
               std::ranges::contains(
                   merged.links, ChainLink{output.placement, output.output});
      });
}
}

std::vector<RenderStepId> RenderedRoots(const RenderPlan &plan) {
  std::vector<RenderStepId> roots;
  roots.reserve(plan.mergedChains.size() + plan.stackOutputs.size());
  for (const MergedChainBinding &merged : plan.mergedChains)
    roots.push_back(merged.result.step);
  for (const StackOutputBinding &output : plan.stackOutputs)
    if (!CoveredByMerge(plan, output))
      roots.push_back(output.result.step);
  return roots;
}

bool Draws(const RenderStepKind &kind) {
  if (const auto *sample = Get<SampleFieldStep>(kind))
    return !sample->direct;
  return OutputType(kind) == RenderValueType{RenderResourceType::kTexture} ||
         OutputType(kind) == RenderValueType{RenderResourceType::kStack};
}

std::size_t AnimatedPasses(const RenderPlan &plan) {
  const std::vector<bool> animated = AnimatedSteps(plan);
  std::vector<bool> reached(plan.steps.size(), false);
  std::vector<RenderStepId> pending = RenderedRoots(plan);
  std::size_t passes = 0;
  while (!pending.empty()) {
    const RenderStepId step = pending.back();
    pending.pop_back();
    if (step >= plan.steps.size() || reached[step])
      continue;
    reached[step] = true;
    if (animated[step] && Draws(plan.steps[step].kind))
      ++passes;
    for (const RenderValueRef operand : StepDependencies(plan, step))
      if (const auto *output = Get<StepOutputRef>(operand))
        pending.push_back(output->step);
  }
  return passes;
}

std::vector<std::size_t> LiveConsumers(const RenderPlan &plan) {
  std::vector<std::size_t> consumers(plan.steps.size(), 0);
  std::vector<bool> live(plan.steps.size(), false);
  std::vector<RenderStepId> pending;
  const auto root = [&](StepOutputRef result) {
    if (result.step < plan.steps.size() && !live[result.step]) {
      live[result.step] = true;
      pending.push_back(result.step);
    }
  };
  for (const auto &output : plan.stackOutputs)
    root(output.result);
  for (const auto &merged : plan.mergedChains)
    root(merged.result);
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

namespace {
template <class Entry, class Same>
bool SameEach(const std::vector<Entry> &a_left,
              const std::vector<Entry> &a_right, Same a_same) {
  return a_left.size() == a_right.size() &&
         std::ranges::equal(a_left, a_right, a_same);
}
}

bool SameStructure(const RenderPlan &a_left, const RenderPlan &a_right) {
  return SameEach(
             a_left.values, a_right.values,
             [](const RenderValueBinding &a_l, const RenderValueBinding &a_r) {
               return a_l.value == a_r.value && a_l.result == a_r.result &&
                      a_l.geometry == a_r.geometry;
             }) &&
         SameEach(a_left.inputs, a_right.inputs,
                  [](const RenderInput &a_l, const RenderInput &a_r) {
                    return a_l.type == a_r.type && a_l.binding == a_r.binding &&
                           a_l.geometry == a_r.geometry;
                  }) &&
         SameEach(a_left.steps, a_right.steps,
                  [](const RenderStep &a_l, const RenderStep &a_r) {
                    return a_l.kind == a_r.kind &&
                           a_l.producedKey.has_value() ==
                               a_r.producedKey.has_value() &&
                           a_l.contentKey.has_value() ==
                               a_r.contentKey.has_value();
                  }) &&
         SameEach(
             a_left.textureUses, a_right.textureUses,
             [](const TextureUseBinding &a_l, const TextureUseBinding &a_r) {
               return a_l.use == a_r.use && a_l.texture == a_r.texture;
             }) &&
         SameEach(
             a_left.mergedChains, a_right.mergedChains,
             [](const MergedChainBinding &a_l, const MergedChainBinding &a_r) {
               return a_l.geometry == a_r.geometry &&
                      a_l.surface == a_r.surface && a_l.slot == a_r.slot &&
                      a_l.links == a_r.links &&
                      a_l.layerStarts == a_r.layerStarts &&
                      a_l.layerCount == a_r.layerCount &&
                      a_l.result == a_r.result;
             }) &&
         SameEach(
             a_left.stackOutputs, a_right.stackOutputs,
             [](const StackOutputBinding &a_l, const StackOutputBinding &a_r) {
               return a_l.placement == a_r.placement &&
                      a_l.output == a_r.output &&
                      a_l.geometry == a_r.geometry && a_l.result == a_r.result;
             });
}
}
