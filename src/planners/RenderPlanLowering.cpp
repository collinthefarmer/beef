// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/FieldInlining.h"
#include "planners/RenderPlan.h"

#include <algorithm>
#include <map>
#include <tuple>

namespace BetterEnchantmentEffects {
namespace {
constexpr std::size_t kMaxValueBindings = 1 << 18;
constexpr std::size_t kMaxLoweringVisits = 65536;
constexpr std::size_t kMaxLoweringDepth = 64;
bool GeometryBound(RenderValueType type) {
  return type == RenderValueType{RenderResourceType::kMesh} ||
         type == RenderValueType{RenderResourceType::kFirings};
}
struct LoweringRequest {
  TextureValue value;
  TextureRequirements requirements;
  GeometryId geometry{};
};
struct LoweringMark {
  std::size_t steps = 0;
  std::size_t inputs = 0;
  std::size_t bindings = 0;
  std::map<TextureKey, RenderValueRef> values;
  std::map<TextureKey, std::vector<RenderValueBinding>> subtrees;
  std::size_t identityBytes = 0;
};
struct LoweringSite {
  const RecipeGraph &graph;
  const RecipeNode &node;
  GraphValueType type;
  TextureKey key;
};
LoweringRequest WithoutMips(const LoweringRequest &request) {
  LoweringRequest stripped = request;
  stripped.requirements.mipPolicy = MipPolicy::kNone;
  return stripped;
}
LoweringRequest OperandRequest(const LoweringRequest &request,
                               OutputRef operand) {
  return LoweringRequest{
      TextureValue{request.value.graph, operand, request.value.instance},
      request.requirements, request.geometry};
}
TextureRequirements KeyRequirementsOf(const RecipeGraph &graph,
                                      const LoweringRequest &request,
                                      const GraphValueType &type) {
  TextureRequirements requirements = request.requirements;
  if (!graph.SampleDependent(request.value.output) && Is<ValueType>(type))
    requirements.format = TextureFormat::kRgba8;
  return requirements;
}
bool IsUniformNumeric(const LoweringSite &site,
                      const LoweringRequest &request) {
  return Is<ValueType>(site.type) &&
         !site.graph.SampleDependent(request.value.output);
}
bool IsTickedUniform(const LoweringSite &site, const LoweringRequest &request) {
  return IsUniformNumeric(site, request) &&
         std::ranges::find(site.graph.TickOrder(), request.value.output.node) !=
             site.graph.TickOrder().end();
}
bool IsComputedUniform(const LoweringSite &site,
                       const LoweringRequest &request) {
  const NodeKind &kind = site.node.kind;
  return IsUniformNumeric(site, request) &&
         (Is<ExpressionOperation>(kind) || Is<CallOperation>(kind) ||
          Is<MapFunctionOperation>(kind) || Is<VectorOperation>(kind));
}
std::optional<RenderValueType> ExternalInputType(const ExternalInput &operation,
                                                 const GraphValueType &type) {
  if (const auto *numeric = Get<ValueType>(type))
    return RenderValueType{*numeric};
  if (Is<GeometryInput>(operation.source))
    return RenderValueType{RenderResourceType::kMesh};
  if (Is<MaterialInput>(operation.source))
    return RenderValueType{RenderResourceType::kMaterial};
  if (Is<TextureInput>(operation.source))
    return RenderValueType{RenderResourceType::kTexture};
  if (Is<RootTransformInput>(operation.source))
    return RenderValueType{RenderResourceType::kTransform};
  return std::nullopt;
}
struct PlanBuilder {
  const RenderBindingResolver &bindings;
  RenderPlan plan;
  std::map<TextureKey, RenderValueRef> values;
  std::map<TextureKey, std::vector<RenderValueBinding>> subtrees;
  std::string problem;
  std::size_t visits = 0;
  std::size_t identityBytes = 0;
  std::vector<std::string> inputIdentities;

  void Remember(const TextureKey &key, const RenderValueBinding &binding,
                std::size_t subtreeStart) {
    subtrees[key].assign(plan.values.begin() +
                             static_cast<std::ptrdiff_t>(
                                 std::min(subtreeStart, plan.values.size())),
                         plan.values.end());
    if (identityBytes + key.identity.canonical.size() > kMaxIdentityBytes) {
      problem = "render identities exceed their size limit";
      return;
    }
    if (values.emplace(key, binding.result).second)
      identityBytes += key.identity.canonical.size();
    plan.values.push_back(binding);
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
  RenderValueRef Input(const TextureKey &key, TextureValue value,
                       RenderValueType type, GeometryId geometry) {
    const bool bound = GeometryBound(type);
    inputIdentities.resize(plan.inputs.size());
    for (std::size_t i = 0; i < plan.inputs.size(); ++i)
      if (Is<TextureValue>(plan.inputs[i].binding) &&
          plan.inputs[i].type == type &&
          inputIdentities[i] == key.identity.canonical &&
          (!bound || plan.inputs[i].geometry == geometry))
        return RenderInputRef{i};
    plan.inputs.push_back({type, value, geometry});
    inputIdentities.push_back(key.identity.canonical);
    return RenderInputRef{plan.inputs.size() - 1};
  }
  RenderValueRef Readback(StepOutputRef submission, RenderValueType type) {
    const ReadbackBinding binding{submission.step};
    for (std::size_t i = 0; i < plan.inputs.size(); ++i)
      if (const auto *existing = Get<ReadbackBinding>(plan.inputs[i].binding);
          existing && *existing == binding)
        return RenderInputRef{i};
    plan.inputs.push_back({type, binding});
    inputIdentities.resize(plan.inputs.size());
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
    return numeric
               ? Step(EvaluateProgramStep{FieldProgram::Sample(*numeric, false),
                                          {value},
                                          {},
                                          requirements},
                      label)
               : value;
  }
  [[nodiscard]] LoweringMark Mark() const {
    return LoweringMark{plan.steps.size(),  plan.inputs.size(),
                        plan.values.size(), values,
                        subtrees,           identityBytes};
  }
  void RewindTo(const LoweringMark &mark) {
    plan.steps.resize(mark.steps);
    plan.inputs.resize(mark.inputs);
    inputIdentities.resize(std::min(inputIdentities.size(), mark.inputs));
    plan.values.resize(mark.bindings);
    values = mark.values;
    subtrees = mark.subtrees;
    identityBytes = mark.identityBytes;
  }
  RenderValueRef TryBuild(const LoweringRequest &request,
                          RenderValueType fallback, const std::string &label) {
    const LoweringMark mark = Mark();
    const RenderValueRef result = Build(request);
    if (problem.empty())
      return result;
    std::string failure = std::move(problem);
    problem.clear();
    RewindTo(mark);
    return Step(UnavailableStep{fallback, std::move(failure)}, label);
  }
  RenderValueRef Fail(std::string message) {
    problem = std::move(message);
    return RenderInputRef{0};
  }
  std::optional<LoweringSite> SiteFor(const LoweringRequest &request) {
    if (!request.value.graph) {
      problem = "render lowering exceeds its traversal limit or has no graph";
      return std::nullopt;
    }
    const RecipeGraph &graph = *request.value.graph;
    const RecipeNode *node = graph.NodeAt(request.value.output.node);
    const std::optional<GraphValueType> type =
        graph.OutputType(request.value.output);
    if (!node || !type || graph.IsDisabled(request.value.output.node)) {
      problem = "render lowering encountered an invalid graph output";
      return std::nullopt;
    }
    auto identity = IdentifyValue(graph, request.value.output,
                                  bindings(request.value, request.geometry));
    if (!identity) {
      problem = identity.error();
      return std::nullopt;
    }
    return LoweringSite{graph, *node, *type,
                        TextureKey{std::move(*identity),
                                   KeyRequirementsOf(graph, request, *type)}};
  }
  std::optional<RenderValueRef> ReuseLowered(const TextureKey &key,
                                             const LoweringRequest &request) {
    const auto found = values.find(key);
    if (found == values.end())
      return std::nullopt;
    plan.values.push_back({request.value, found->second, request.geometry});
    if (const auto subtree = subtrees.find(key); subtree != subtrees.end()) {
      if (plan.values.size() + subtree->second.size() > kMaxValueBindings)
        return Fail("render value bindings exceed their limit");
      ReplaySubtree(subtree->second, request.geometry);
    }
    return found->second;
  }
  void ReplaySubtree(std::span<const RenderValueBinding> subtree,
                     GeometryId geometry) {
    for (const RenderValueBinding &binding : subtree)
      if (binding.geometry != geometry)
        plan.values.push_back({binding.value, binding.result, geometry});
  }
  RenderValueRef Build(const LoweringRequest &request, std::size_t depth = 0) {
    if (!problem.empty())
      return RenderInputRef{0};
    const LoweringRequest lowered = WithoutMips(request);
    if (++visits > kMaxLoweringVisits || depth > kMaxLoweringDepth ||
        !lowered.value.graph)
      return Fail(
          "render lowering exceeds its traversal limit or has no graph");
    const std::optional<LoweringSite> site = SiteFor(lowered);
    if (!site)
      return RenderInputRef{0};
    if (const std::optional<RenderValueRef> reused =
            ReuseLowered(site->key, lowered))
      return *reused;
    const std::size_t subtreeStart = plan.values.size();
    const RenderValueRef result = LowerNode(*site, lowered, depth);
    Remember(site->key, {lowered.value, result, lowered.geometry},
             subtreeStart);
    return result;
  }
  RenderValueRef BuildOperand(OutputRef operand, const LoweringRequest &request,
                              std::size_t depth) {
    return Build(OperandRequest(request, operand), depth + 1);
  }
  RenderValueRef LowerNode(const LoweringSite &site,
                           const LoweringRequest &request, std::size_t depth) {
    if (IsTickedUniform(site, request))
      return LowerTickedUniform(site, request);
    if (IsComputedUniform(site, request))
      return LowerComputedUniform(site, request, depth);
    return Match(
        site.node.kind,
        [&](const ConstantOperation &operation) -> RenderValueRef {
          return LowerConstantOperation(operation, site);
        },
        [&](const ExternalInput &operation) -> RenderValueRef {
          return LowerExternalInput(operation, site, request);
        },
        [&](const ReductionOperation &operation) -> RenderValueRef {
          return LowerReductionOperation(operation, site, request, depth);
        },
        [&](const MapFunctionOperation &operation) -> RenderValueRef {
          return LowerMapFunctionOperation(operation, site, request, depth);
        },
        [&](const ExpressionOperation &operation) -> RenderValueRef {
          return LowerExpressionOperation(operation, site, request, depth);
        },
        [&](const VectorOperation &operation) -> RenderValueRef {
          return LowerVectorOperation(operation, site, request, depth);
        },
        [&](const ImageOperation &operation) -> RenderValueRef {
          return LowerImageOperation(operation, site, request, depth);
        },
        [&](const MaterialOperation &operation) -> RenderValueRef {
          return LowerMaterialOperation(operation, site, request, depth);
        },
        [&](const BakeOperation &operation) -> RenderValueRef {
          return LowerBakeOperation(operation, site, request, depth);
        },
        [&](const DistanceOperation &operation) -> RenderValueRef {
          return LowerDistanceOperation(operation, site, request, depth);
        },
        [&](const MaterialClustersOperation &operation) -> RenderValueRef {
          return LowerMaterialClustersOperation(operation, site, request,
                                                depth);
        },
        [&](const RippleOperation &operation) -> RenderValueRef {
          return LowerRippleOperation(operation, site, request, depth);
        },
        [&](const auto &) -> RenderValueRef {
          return LowerUnsupported(site, request);
        });
  }
  RenderValueRef LowerTickedUniform(const LoweringSite &site,
                                    const LoweringRequest &request) {
    const ValueType *numeric = Get<ValueType>(site.type);
    if (!numeric)
      return Fail(site.node.displayName + ": ticked value is not numeric");
    return Input(site.key, request.value, *numeric, request.geometry);
  }
  RenderValueRef LowerComputedUniform(const LoweringSite &site,
                                      const LoweringRequest &request,
                                      std::size_t depth) {
    const ValueType *numeric = Get<ValueType>(site.type);
    if (!numeric)
      return Fail(site.node.displayName + ": computed value is not numeric");
    std::vector<RenderValueRef> inputs;
    for (const OutputRef operand : InputsOf(site.node.kind))
      inputs.push_back(BuildOperand(operand, request, depth));
    return Step(EvaluateValueStep{request.value, std::move(inputs), *numeric},
                site.node.displayName);
  }
  RenderValueRef LowerConstantOperation(const ConstantOperation &operation,
                                        const LoweringSite &site) {
    return Step(ConstantRenderStep{operation.value}, site.node.displayName);
  }
  RenderValueRef LowerExternalInput(const ExternalInput &operation,
                                    const LoweringSite &site,
                                    const LoweringRequest &request) {
    const std::optional<RenderValueType> type =
        ExternalInputType(operation, site.type);
    if (!type)
      return Fail(site.node.displayName + ": unsupported render input");
    return Input(site.key, request.value, *type, request.geometry);
  }
  RenderValueRef LowerReductionOperation(const ReductionOperation &operation,
                                         const LoweringSite &site,
                                         const LoweringRequest &request,
                                         std::size_t depth) {
    const std::string &label = site.node.displayName;
    LoweringRequest measured = OperandRequest(request, operation.value);
    measured.requirements.format = TextureFormat::kRgba32Float;
    const RenderValueRef field = Field(Build(measured, depth + 1),
                                       measured.requirements, label + " field");
    const std::optional<GraphValueType> fieldType =
        site.graph.OutputType(operation.value);
    const ValueType *sampleType =
        fieldType ? Get<ValueType>(*fieldType) : nullptr;
    if (!sampleType)
      return Fail(label + ": reduction requires a numeric field");
    const RenderValueRef submission =
        Step(SubmitReductionStep{operation.kind,
                                 field,
                                 *sampleType,
                                 {request.requirements.size.Pixels(),
                                  request.requirements.size.Pixels()}},
             label + " submission");
    const auto *submitted = Get<StepOutputRef>(submission);
    if (!submitted)
      return submission;
    return Readback(*submitted, *sampleType);
  }
  RenderValueRef
  LowerMapFunctionOperation(const MapFunctionOperation &operation,
                            const LoweringSite &site,
                            const LoweringRequest &request, std::size_t depth) {
    const std::string &label = site.node.displayName;
    if (operation.arguments.empty())
      return Fail(label + ": mapped function has no arguments");
    std::optional<std::size_t> sampled;
    for (std::size_t i = 0; i < operation.arguments.size(); ++i)
      if (site.graph.SampleDependent(operation.arguments[i])) {
        if (sampled)
          return Fail(label + ": lookup cannot bind another sampled argument");
        sampled = i;
      }
    if (!sampled)
      return Fail(label + ": mapped field has no sampled argument");
    std::vector<LookupArgument> arguments;
    for (std::size_t i = 0; i < operation.arguments.size(); ++i)
      if (i != *sampled)
        arguments.push_back(
            {i, BuildOperand(operation.arguments[i], request, depth)});
    const RenderValueRef source =
        BuildOperand(operation.arguments[*sampled], request, depth);
    const RenderValueRef lookup =
        Lookup(site.graph, operation.function, std::move(arguments),
               label + " lookup", *sampled);
    return Step(MapFieldStep{source, lookup, request.requirements}, label);
  }
  RenderValueRef LowerExpressionOperation(const ExpressionOperation &operation,
                                          const LoweringSite &site,
                                          const LoweringRequest &request,
                                          std::size_t depth) {
    const std::string &label = site.node.displayName;
    auto program = FieldProgram::Compile(site.graph, request.value.output);
    if (!program)
      return Fail(program.error());
    std::vector<RenderValueRef> inputs, lookups;
    inputs.reserve(operation.expression.valueBindings.size());
    lookups.reserve(program->FunctionLookups().size());
    for (const OutputRef operand : operation.expression.valueBindings)
      inputs.push_back(BuildOperand(operand, request, depth));
    for (const FunctionLookup &lookup : program->FunctionLookups()) {
      std::vector<LookupArgument> arguments;
      arguments.reserve(lookup.arguments.size());
      for (const BoundFunctionArgument &argument : lookup.arguments)
        arguments.push_back(
            {argument.parameter, BuildOperand(argument.value, request, depth)});
      lookups.push_back(Lookup(site.graph, lookup.function,
                               std::move(arguments), label + " lookup",
                               lookup.sampledParameter));
    }
    return Step(EvaluateProgramStep{std::move(*program), std::move(inputs),
                                    std::move(lookups), request.requirements},
                label);
  }
  RenderValueRef LowerVectorOperation(const VectorOperation &operation,
                                      const LoweringSite &site,
                                      const LoweringRequest &request,
                                      std::size_t depth) {
    std::vector<RenderValueRef> components;
    components.reserve(operation.components.size());
    for (const OutputRef component : operation.components)
      components.push_back(BuildOperand(component, request, depth));
    return Step(ComposeVectorStep{std::move(components), request.requirements},
                site.node.displayName);
  }
  RenderValueRef LowerImageOperation(const ImageOperation &operation,
                                     const LoweringSite &site,
                                     const LoweringRequest &request,
                                     std::size_t depth) {
    std::vector<RenderValueRef> coordinates;
    const RecipeNode *coordinateNode =
        site.graph.NodeAt(operation.coordinates.node);
    const auto *transform =
        coordinateNode ? Get<TextureCoordinatesOperation>(coordinateNode->kind)
                       : nullptr;
    if (transform) {
      if (transform->scroll)
        coordinates.push_back(BuildOperand(*transform->scroll, request, depth));
      if (transform->tile)
        coordinates.push_back(BuildOperand(*transform->tile, request, depth));
    }
    return Step(SampleFieldStep{BuildOperand(operation.texture, request, depth),
                                request.value, std::move(coordinates),
                                request.requirements},
                site.node.displayName);
  }
  RenderValueRef LowerMaterialOperation(const MaterialOperation &operation,
                                        const LoweringSite &site,
                                        const LoweringRequest &request,
                                        std::size_t depth) {
    const RenderValueRef material =
        BuildOperand(operation.material, request, depth);
    if (operation.channel == MaterialChannel::kNormalSlope)
      return Step(NormalSlopeStep{material, request.requirements},
                  site.node.displayName);
    return Step(
        SampleFieldStep{material, request.value, {}, request.requirements},
        site.node.displayName);
  }
  RenderValueRef LowerBakeOperation(const BakeOperation &operation,
                                    const LoweringSite &site,
                                    const LoweringRequest &request,
                                    std::size_t depth) {
    const std::string &label = site.node.displayName;
    const RenderValueRef buffers = Step(
        BuildBakeBuffersStep{BuildOperand(operation.geometry, request, depth),
                             operation.bake},
        label + " buffers");
    return Step(BakeMeshStep{buffers, request.requirements,
                             Is<ComponentIdBake>(operation.bake) ||
                                 Is<ChartIdBake>(operation.bake)},
                label);
  }
  RenderValueRef LowerDistanceOperation(const DistanceOperation &operation,
                                        const LoweringSite &site,
                                        const LoweringRequest &request,
                                        std::size_t depth) {
    const std::string &label = site.node.displayName;
    const RenderValueRef buffers = Step(
        BuildBakeBuffersStep{BuildOperand(operation.geometry, request, depth),
                             BuildOperand(operation.origin, request, depth)},
        label + " buffers");
    return Step(BakeMeshStep{buffers, request.requirements}, label);
  }
  RenderValueRef LowerMaterialClustersOperation(
      const MaterialClustersOperation &operation, const LoweringSite &site,
      const LoweringRequest &request, std::size_t depth) {
    const std::string &label = site.node.displayName;
    const RenderValueRef material =
        BuildOperand(operation.material, request, depth);
    const RenderValueRef submission =
        Step(SubmitMaterialSampleStep{material}, label + " samples");
    const auto *submitted = Get<StepOutputRef>(submission);
    if (!submitted)
      return submission;
    const RenderValueRef sample =
        Readback(*submitted, RenderResourceType::kMaterialSample);
    const RenderValueRef analysis = Step(
        ClusterMaterialStep{sample, operation.settings}, label + " analysis");
    return Step(DrawClustersStep{material, analysis, request.requirements},
                label);
  }
  RenderValueRef LowerRippleOperation(const RippleOperation &operation,
                                      const LoweringSite &site,
                                      const LoweringRequest &request,
                                      std::size_t depth) {
    const std::string &label = site.node.displayName;
    const RenderValueRef mesh =
        BuildOperand(operation.geometry, request, depth);
    const RenderValueRef buffers =
        Step(BuildBakeBuffersStep{mesh, BakeKind{PositionBake{}}},
             label + " position buffers");
    const RenderValueRef positions =
        Step(BakeMeshStep{buffers, request.requirements}, label + " positions");
    std::vector<RenderValueRef> inputs;
    for (const OutputRef operand : InputsOf(site.node.kind)) {
      if (operand == operation.coordinates)
        continue;
      inputs.push_back(BuildOperand(operand, request, depth));
    }
    return Step(DrawRippleStep{positions, request.value, std::move(inputs),
                               request.requirements},
                label);
  }
  RenderValueRef LowerUnsupported(const LoweringSite &site,
                                  const LoweringRequest &request) {
    if (const auto *resource = Get<ResourceType>(site.type);
        resource && *resource == ResourceType::kFirings)
      return Input(site.key, request.value, RenderResourceType::kFirings,
                   request.geometry);
    return Fail(site.node.displayName + ": no render lowering for operation");
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
    const StackInputBinding binding{request.placement, request.output,
                                    request.geometry};
    const RenderInputRef base{plan.inputs.size()};
    plan.inputs.push_back(
        {RenderResourceType::kStack, binding, request.geometry});
    const RenderInputRef visibility{plan.inputs.size()};
    plan.inputs.push_back(
        {RenderResourceType::kVisibility, binding, request.geometry});
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
        auto value = TryBuild({{request.graph, *ref, request.instance},
                               request.requirements,
                               request.geometry},
                              expected, location + property);
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
      stack.layers.push_back(planned);
    }
    const auto result = Step(std::move(stack), OutputWhere(request.output));
    if (const auto *ref = Get<StepOutputRef>(result))
      plan.stackOutputs.push_back(
          {request.placement, request.output, request.geometry, *ref});
  }
};
}
std::expected<RenderPlan, std::string>
LowerRenderPlan(std::span<const TextureDemand> demands,
                std::span<const RenderStackRequest> stacks,
                const RenderBindingResolver &bindings) {
  if (!bindings)
    return std::unexpected("render plan has no input identity resolver");
  PlanBuilder builder{bindings, {}, {}, {}};
  for (const auto &demand : demands) {
    const auto value = builder.Field(
        builder.TryBuild(
            {demand.value, demand.key.requirements, demand.geometry},
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
std::expected<RenderPlan, std::string>
BuildRenderPlan(std::span<const TextureDemand> demands,
                std::span<const RenderStackRequest> stacks,
                const RenderBindingResolver &bindings) {
  auto lowered = LowerRenderPlan(demands, stacks, bindings);
  if (!lowered)
    return lowered;
  auto inlined = InlineFields(std::move(*lowered)).plan;
  if (auto valid = ValidateRenderPlan(inlined); !valid)
    return std::unexpected(valid.error());
  return inlined;
}
}
