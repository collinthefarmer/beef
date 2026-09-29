// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/RenderPlan.h"

#include <algorithm>
#include <map>
#include <tuple>

namespace BetterEnchantmentEffects {
namespace {
struct PlanBuilder {
  const RenderBindingResolver &bindings;
  RenderPlan plan;
  std::map<TextureKey, RenderValueRef> values;
  std::string problem;
  std::size_t visits = 0;
  std::size_t identityBytes = 0;

  void Remember(const TextureKey &key, RenderValueRef result,
                TextureValue value) {
    if (identityBytes + key.identity.canonical.size() > 8 * 1024 * 1024) {
      problem = "render identities exceed their size limit";
      return;
    }
    if (values.emplace(key, result).second)
      identityBytes += key.identity.canonical.size();
    plan.values.push_back({value, result});
  }
  RenderValueRef Step(RenderStepKind kind, std::string label) {
    for (std::size_t i = 0; i < plan.steps.size(); ++i)
      if (plan.steps[i].kind == kind)
        return StepOutputRef{i, 0};
    if (plan.steps.size() >= 4096) {
      problem = "render plan exceeds step limit";
      return StepOutputRef{4096, 0};
    }
    const auto id = plan.steps.size();
    plan.steps.push_back({std::move(kind), std::move(label)});
    return StepOutputRef{id, 0};
  }
  RenderValueRef Input(TextureValue value, RenderValueType type) {
    for (std::size_t i = 0; i < plan.inputs.size(); ++i)
      if (const auto *existing = Get<TextureValue>(plan.inputs[i].binding);
          existing && *existing == value && plan.inputs[i].type == type)
        return RenderInputRef{i};
    const auto id = plan.inputs.size();
    plan.inputs.push_back({type, value});
    return RenderInputRef{id};
  }
  RenderValueRef Readback(StepOutputRef submission, RenderValueType type) {
    const ReadbackBinding binding{submission.step};
    for (std::size_t i = 0; i < plan.inputs.size(); ++i)
      if (const auto *existing = Get<ReadbackBinding>(plan.inputs[i].binding);
          existing && *existing == binding)
        return RenderInputRef{i};
    plan.inputs.push_back({type, binding});
    return RenderInputRef{plan.inputs.size() - 1};
  }
  RenderValueRef Lookup(const RecipeGraph &graph, FunctionId function,
                        std::vector<LookupArgument> arguments,
                        const std::string &name,
                        std::size_t sampledParameter = 0) {
    return Step(BuildLookupStep{&graph, function, sampledParameter,
                                std::move(arguments), 256},
                name);
  }
  RenderValueRef Field(RenderValueRef value, TextureRequirements requirements,
                       const std::string &label) {
    const auto type = TypeOf(plan, value);
    const auto *numeric = type ? Get<ValueType>(*type) : nullptr;
    return numeric ? Step(EvaluateProgramStep{InterpreterProgram::Sample(
                                                  *numeric, false),
                                              {value},
                                              {},
                                              requirements},
                          label)
                   : value;
  }
  RenderValueRef TryBuild(TextureValue value, TextureRequirements requirements,
                          RenderValueType fallback, const std::string &label) {
    const auto oldSteps = plan.steps.size(), oldInputs = plan.inputs.size(),
               oldBindings = plan.values.size();
    const auto oldValues = values;
    const auto oldBytes = identityBytes;
    const auto result = Build(value, requirements);
    if (problem.empty())
      return result;
    auto failure = std::move(problem);
    problem.clear();
    plan.steps.resize(oldSteps);
    plan.inputs.resize(oldInputs);
    plan.values.resize(oldBindings);
    values = oldValues;
    identityBytes = oldBytes;
    return Step(UnavailableStep{fallback, std::move(failure)}, label);
  }
  RenderValueRef Build(TextureValue value, TextureRequirements requirements,
                       std::size_t depth = 0) {
    if (!problem.empty())
      return RenderInputRef{0};
    if (++visits > 65536 || depth > 64 || !value.graph) {
      problem = "render lowering exceeds its traversal limit or has no graph";
      return RenderInputRef{0};
    }
    const auto &graph = *value.graph;
    const auto *node = graph.NodeAt(value.output.node);
    const auto type = graph.OutputType(value.output);
    if (!node || !type || graph.IsDisabled(value.output.node)) {
      problem = "render lowering encountered an invalid graph output";
      return RenderInputRef{0};
    }
    auto identity = IdentifyValue(graph, value.output, bindings(value));
    if (!identity) {
      problem = identity.error();
      return RenderInputRef{0};
    }
    auto keyRequirements = requirements;
    if (!graph.SampleDependent(value.output) && Is<ValueType>(*type))
      keyRequirements.format = TextureFormat::kRgba8;
    const TextureKey key{std::move(*identity), keyRequirements};
    if (const auto found = values.find(key); found != values.end()) {
      plan.values.push_back({value, found->second});
      return found->second;
    }
    const auto build = [&](OutputRef ref) {
      return Build({&graph, ref, value.instance}, requirements, depth + 1);
    };
    const auto numeric = Get<ValueType>(*type);
    if (numeric && !graph.SampleDependent(value.output) &&
        std::ranges::find(graph.TickOrder(), value.output.node) !=
            graph.TickOrder().end()) {
      const auto input = Input(value, *numeric);
      Remember(key, input, value);
      return input;
    }
    if (numeric && !graph.SampleDependent(value.output) &&
        (Is<ExpressionOperation>(node->kind) || Is<CallOperation>(node->kind) ||
         Is<MapFunctionOperation>(node->kind) ||
         Is<VectorOperation>(node->kind))) {
      std::vector<RenderValueRef> inputs;
      for (auto ref : InputsOf(node->kind))
        inputs.push_back(build(ref));
      const auto result =
          Step(EvaluateValueStep{value, std::move(inputs), *numeric},
               node->displayName);
      Remember(key, result, value);
      return result;
    }
    const auto label = node->displayName;
    RenderValueRef result = Match(
        node->kind,
        [&](const ConstantOperation &k) -> RenderValueRef {
          return Step(ConstantRenderStep{k.value}, label);
        },
        [&](const ExternalInput &k) -> RenderValueRef {
          if (numeric)
            return Input(value, *numeric);
          if (Is<GeometryInput>(k.source))
            return Input(value, RenderResourceType::kMesh);
          if (Is<MaterialInput>(k.source))
            return Input(value, RenderResourceType::kMaterial);
          if (Is<TextureInput>(k.source))
            return Input(value, RenderResourceType::kTexture);
          if (Is<RootTransformInput>(k.source))
            return Input(value, RenderResourceType::kTransform);
          problem = label + ": unsupported render input";
          return RenderInputRef{0};
        },
        [&](const ReductionOperation &k) -> RenderValueRef {
          auto measured = requirements;
          measured.format = TextureFormat::kRgba32Float;
          const auto field = Field(
              Build({&graph, k.value, value.instance}, measured, depth + 1),
              measured, label + " field");
          const auto fieldType = graph.OutputType(k.value);
          const auto *sampleType =
              fieldType ? Get<ValueType>(*fieldType) : nullptr;
          if (!sampleType) {
            problem = label + ": reduction requires a numeric field";
            return RenderInputRef{0};
          }
          const auto submission =
              Step(SubmitReductionStep{k.kind,
                                       field,
                                       *sampleType,
                                       {requirements.size.Pixels(),
                                        requirements.size.Pixels()}},
                   label + " submission");
          const auto *submitted = Get<StepOutputRef>(submission);
          if (!submitted)
            return submission;
          return Readback(*submitted, *sampleType);
        },
        [&](const MapFunctionOperation &k) -> RenderValueRef {
          if (k.arguments.empty()) {
            problem = label + ": mapped function has no arguments";
            return RenderInputRef{0};
          }
          std::optional<std::size_t> sampled;
          for (std::size_t i = 0; i < k.arguments.size(); ++i)
            if (graph.SampleDependent(k.arguments[i])) {
              if (sampled) {
                problem =
                    label + ": lookup cannot bind another sampled argument";
                return RenderInputRef{0};
              }
              sampled = i;
            }
          if (!sampled) {
            problem = label + ": mapped field has no sampled argument";
            return RenderInputRef{0};
          }
          std::vector<LookupArgument> arguments;
          for (std::size_t i = 0; i < k.arguments.size(); ++i)
            if (i != *sampled)
              arguments.push_back({i, build(k.arguments[i])});
          const auto source = build(k.arguments[*sampled]);
          const auto lookup = Lookup(graph, k.function, std::move(arguments),
                                     label + " lookup", *sampled);
          return Step(MapFieldStep{source, lookup, requirements}, label);
        },
        [&](const ExpressionOperation &k) -> RenderValueRef {
          auto program = InterpreterProgram::Compile(graph, value.output);
          if (!program) {
            problem = program.error();
            return RenderInputRef{0};
          }
          std::vector<RenderValueRef> inputs, lookups;
          for (const auto ref : k.expression.valueBindings)
            inputs.push_back(build(ref));
          for (const auto &lookup : program->FunctionLookups()) {
            std::vector<LookupArgument> arguments;
            for (const auto &argument : lookup.arguments)
              arguments.push_back({argument.parameter, build(argument.value)});
            lookups.push_back(Lookup(graph, lookup.function,
                                     std::move(arguments), label + " lookup",
                                     lookup.sampledParameter));
          }
          return Step(EvaluateProgramStep{std::move(*program),
                                          std::move(inputs), std::move(lookups),
                                          requirements},
                      label);
        },
        [&](const VectorOperation &k) -> RenderValueRef {
          std::vector<RenderValueRef> components;
          for (auto component : k.components)
            components.push_back(build(component));
          return Step(ComposeVectorStep{std::move(components), requirements},
                      label);
        },
        [&](const ImageOperation &k) -> RenderValueRef {
          std::vector<RenderValueRef> coordinates;
          const auto *coord = graph.NodeAt(k.coordinates.node);
          const auto *transform =
              coord ? Get<TextureCoordinatesOperation>(coord->kind) : nullptr;
          if (transform) {
            if (transform->scroll)
              coordinates.push_back(build(*transform->scroll));
            if (transform->tile)
              coordinates.push_back(build(*transform->tile));
          }
          return Step(SampleFieldStep{build(k.texture), value,
                                      std::move(coordinates), requirements},
                      label);
        },
        [&](const MaterialOperation &k) -> RenderValueRef {
          const auto material = build(k.material);
          if (k.channel == MaterialChannel::kNormalSlope)
            return Step(NormalSlopeStep{material, requirements}, label);
          return Step(SampleFieldStep{material, value, {}, requirements},
                      label);
        },
        [&](const BakeOperation &k) -> RenderValueRef {
          const auto buffers =
              Step(BuildBakeBuffersStep{build(k.geometry), k.bake},
                   label + " buffers");
          return Step(BakeMeshStep{buffers, requirements,
                                   Is<ComponentIdBake>(k.bake) ||
                                       Is<ChartIdBake>(k.bake)},
                      label);
        },
        [&](const DistanceOperation &k) -> RenderValueRef {
          const auto buffers =
              Step(BuildBakeBuffersStep{build(k.geometry), build(k.origin)},
                   label + " buffers");
          return Step(BakeMeshStep{buffers, requirements}, label);
        },
        [&](const MaterialClustersOperation &k) -> RenderValueRef {
          const auto material = build(k.material);
          const auto submission =
              Step(SubmitMaterialSampleStep{material}, label + " samples");
          const auto *submitted = Get<StepOutputRef>(submission);
          if (!submitted)
            return submission;
          const auto sample =
              Readback(*submitted, RenderResourceType::kMaterialSample);
          const auto analysis = Step(ClusterMaterialStep{sample, k.settings},
                                     label + " analysis");
          return Step(DrawClustersStep{material, analysis, requirements},
                      label);
        },
        [&](const RippleOperation &k) -> RenderValueRef {
          const auto mesh = build(k.geometry);
          const auto buffers =
              Step(BuildBakeBuffersStep{mesh, BakeKind{PositionBake{}}},
                   label + " position buffers");
          const auto positions =
              Step(BakeMeshStep{buffers, requirements}, label + " positions");
          std::vector<RenderValueRef> inputs;
          for (auto ref : InputsOf(node->kind)) {
            if (ref == k.coordinates)
              continue;
            inputs.push_back(build(ref));
          }
          return Step(
              DrawRippleStep{positions, value, std::move(inputs), requirements},
              label);
        },
        [&](const auto &) -> RenderValueRef {
          if (const auto *resource = Get<ResourceType>(*type);
              resource && *resource == ResourceType::kFirings)
            return Input(value, RenderResourceType::kFirings);
          problem = label + ": no render lowering for operation";
          return RenderInputRef{0};
        });
    Remember(key, result, value);
    return result;
  }
  std::optional<OutputRef> Bound(const RecipeGraph &graph,
                                 const std::string &property) {
    for (const auto &binding : graph.OutputBindings())
      if (binding.property == property)
        return binding.value;
    return std::nullopt;
  }
  void Stack(const RenderStackRequest &request) {
    if (!request.graph || !request.surface) {
      problem = "stack request is incomplete";
      return;
    }
    const StackInputBinding binding{request.placement, request.output};
    const RenderInputRef base{plan.inputs.size()};
    plan.inputs.push_back({RenderResourceType::kStack, binding});
    const RenderInputRef visibility{plan.inputs.size()};
    plan.inputs.push_back({RenderResourceType::kVisibility, binding});
    CompositeStackStep stack{
        base, visibility, {}, request.requirements, request.surface->slot};
    for (std::size_t i = 0; i < request.surface->stack.size(); ++i) {
      const auto &layer = request.surface->stack[i];
      const auto location = LayerWhere(request.output, i);
      const auto get = [&](const std::string &property) {
        const auto ref = Bound(*request.graph, location + property);
        if (!ref) {
          problem = location + property + ": graph binding is missing";
          return RenderValueRef{RenderInputRef{0}};
        }
        const auto expected =
            property == " opacity" ? RenderValueType{ValueType::kScalar}
            : property == " color"
                ? RenderValueType{ValueType::kVec3}
                : RenderValueType{RenderResourceType::kTexture};
        auto value =
            TryBuild({request.graph, *ref, request.instance},
                     request.requirements, expected, location + property);
        const auto type = TypeOf(plan, value);
        if (property == " mask" ||
            (property == " source" &&
             type == std::optional<RenderValueType>{ValueType::kScalar}))
          value = Field(value, request.requirements, location + property);
        return value;
      };
      PlannedLayer planned{get(" source"), get(" opacity"), {}, {},
                           layer.blend,    layer.channels};
      if (layer.mask)
        planned.mask = get(" mask");
      if (layer.color)
        planned.color = get(" color");
      stack.layers.push_back(std::move(planned));
    }
    const auto result = Step(std::move(stack), OutputWhere(request.output));
    if (const auto *ref = Get<StepOutputRef>(result))
      plan.stackOutputs.push_back({request.placement, request.output, *ref});
  }
};
}
std::expected<RenderPlan, std::string>
BuildRenderPlan(std::span<const TextureDemand> demands,
                std::span<const RenderStackRequest> stacks,
                const RenderBindingResolver &bindings) {
  if (!bindings)
    return std::unexpected("render plan has no input identity resolver");
  PlanBuilder builder{bindings, {}, {}, {}};
  for (const auto &demand : demands) {
    const auto value = builder.Field(
        builder.TryBuild(demand.value, demand.key.requirements,
                         RenderResourceType::kTexture, "texture demand"),
        demand.key.requirements, "uniform field");
    const auto id = static_cast<TextureDemandId>(&demand - demands.data());
    for (const auto &use : demand.dependents) {
      const bool prerequisite =
          std::ranges::any_of(demands, [&](const TextureDemand &parent) {
            return std::ranges::find(parent.dependencies, id) !=
                       parent.dependencies.end() &&
                   std::ranges::find(parent.dependents, use) !=
                       parent.dependents.end();
          });
      if (!prerequisite)
        builder.plan.textureUses.push_back({use, value});
    }
    if (!builder.problem.empty())
      return std::unexpected(builder.problem);
  }
  for (const auto &stack : stacks) {
    builder.Stack(stack);
    if (!builder.problem.empty())
      return std::unexpected(builder.problem);
  }
  if (auto valid = ValidateRenderPlan(builder.plan); !valid)
    return std::unexpected(valid.error());
  return std::move(builder.plan);
}
}
