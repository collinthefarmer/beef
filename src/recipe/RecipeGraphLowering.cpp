// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/RecipeGraph.h"

#include <algorithm>
#include <format>
#include <limits>

namespace BetterEnchantmentEffects {
using detail::DeclarationCategory;
using detail::DeclarationExpression;
using detail::RecipeDeclaration;
struct RecipeGraphLowering {
  RecipeGraph &graph;
  const Recipe &recipe;
  std::vector<OutputRef> results;
  std::vector<std::optional<FunctionId>> functions;
  std::vector<bool> rejected;
  std::unordered_map<std::string, OutputRef> sharedInputs;
  static constexpr std::size_t kNodeLimit = 16 * kMaxRecipeRows;

  OutputRef Add(NodeKind kind, GraphValueType type, std::string label) {
    if (graph.nodes_.size() >= kNodeLimit) {
      if (graph.nodes_.size() == rejected.size()) {
        Reporter{graph.diagnostics_, "recipe"}.Error(
            "operation graph exceeds node budget");
        rejected.push_back(true);
      }
      return {};
    }
    const OutputRef result{graph.nodes_.size(), 0};
    graph.nodes_.push_back(
        {std::move(label), std::move(kind), {{"value", type}}});
    rejected.push_back(false);
    return result;
  }
  OutputRef Constant(Value value) {
    const auto type = BetterEnchantmentEffects::TypeOf(value);
    return Add(ConstantOperation{std::move(value)}, type, "constant");
  }
  OutputRef Input(ExternalSource source, GraphValueType type,
                  std::string label) {
    return Add(ExternalInput{std::move(source)}, type, std::move(label));
  }
  OutputRef Shared(ExternalSource source, GraphValueType type,
                   std::string name) {
    if (const auto it = sharedInputs.find(name); it != sharedInputs.end())
      return it->second;
    const auto result = Input(std::move(source), type, name);
    sharedInputs.emplace(std::move(name), result);
    return result;
  }
  OutputRef Time() { return Shared(TimeInput{}, ValueType::kScalar, "time"); }
  OutputRef Delta() {
    return Shared(DeltaTimeInput{}, ValueType::kScalar, "delta time");
  }
  OutputRef Uv() {
    return Shared(SampleUvInput{}, ValueType::kVec2, "sample UV");
  }
  OutputRef Geometry() {
    return Shared(GeometryInput{}, ResourceType::kGeometry, "geometry");
  }
  OutputRef Material() {
    return Shared(MaterialInput{}, ResourceType::kMaterial, "material");
  }
  OutputRef Transform() {
    return Shared(RootTransformInput{}, ResourceType::kTransform,
                  "world to root");
  }
  OutputRef Reference(const Ref &ref) const {
    const auto it = graph.nodeIndicesByName_.find(ref.name);
    return it != graph.nodeIndicesByName_.end() && it->second < results.size()
               ? results[it->second]
               : OutputRef{};
  }
  OutputRef Parameter(const Param &param) {
    return Match(
        param, [&](float value) { return Constant(value); },
        [&](const Ref &ref) { return Reference(ref); });
  }
  template <std::size_t N>
  OutputRef Parameter(const std::variant<std::array<Param, N>, Ref> &param) {
    return Match(
        param, [&](const Ref &ref) { return Reference(ref); },
        [&](const std::array<Param, N> &parts) {
          VectorOperation vector;
          for (const auto &part : parts)
            vector.components.push_back(Parameter(part));
          return Add(std::move(vector),
                     N == 2 ? ValueType::kVec2 : ValueType::kVec3, "vector");
        });
  }
  OutputRef TriggerPort(const Ref &ref, std::size_t port) const {
    const auto it = graph.triggersByName_.find(ref.name);
    return it == graph.triggersByName_.end() ? OutputRef{}
                                             : OutputRef{it->second, port};
  }
  std::optional<OutputRef> Optional(const std::optional<Param> &param) {
    return param ? std::optional{Parameter(*param)} : std::nullopt;
  }
  BoundExpression Expression(const RecipeDeclaration &declaration) {
    BoundExpression bound;
    if (!declaration.expression)
      return bound;
    const auto &original = *declaration.expression;
    for (const auto index : original.valueBindings)
      bound.valueBindings.push_back(index < results.size() ? results[index]
                                                           : OutputRef{});
    for (const auto index : original.functionBindings)
      bound.functionBindings.push_back(
          {index < functions.size() && functions[index]
               ? *functions[index]
               : std::numeric_limits<FunctionId>::max(),
           0,
           {{1, Constant(0.5f)}}});
    std::optional<std::uint32_t> time;
    if (original.program.UsesTime()) {
      time = static_cast<std::uint32_t>(bound.valueBindings.size());
      bound.valueBindings.push_back(Time());
    }
    bound.program =
        original.program.BindContext(time, std::nullopt, std::nullopt);
    return bound;
  }
  NodeKind SignalKindOf(const Signal &signal,
                        const RecipeDeclaration &declaration) {
    return Match(
        signal.kind,
        [&](const ConstantSignal &k) -> NodeKind {
          return ConstantOperation{k.value};
        },
        [&](const WaveSignal &k) -> NodeKind {
          return WaveOperation{Parameter(k.base),
                               Parameter(k.amplitude),
                               Parameter(k.period),
                               Parameter(k.phase),
                               Delta(),
                               k.waveform};
        },
        [&](const RampSignal &k) -> NodeKind {
          return RampOperation{Parameter(k.from), Parameter(k.to),
                               Parameter(k.seconds), Time()};
        },
        [&](const EfshSignal &k) -> NodeKind {
          return EffectShaderOperation{Input(EffectShaderInput{k.record},
                                             ResourceType::kEffectShader,
                                             "effect shader record"),
                                       Time(), k.field};
        },
        [&](const ActorValueSignal &k) -> NodeKind { return ExternalInput{k}; },
        [&](const ActorStateSignal &k) -> NodeKind { return ExternalInput{k}; },
        [&](const EnchantmentSignal &k) -> NodeKind {
          return ExternalInput{k};
        },
        [&](const NoiseSignal &k) -> NodeKind {
          return NoiseOperation{Parameter(k.frequency), Parameter(k.amplitude),
                                Time(), k.seed};
        },
        [&](const GradientSignal &k) -> NodeKind {
          GradientOperation op{Parameter(k.t), {}};
          for (const auto &stop : k.stops)
            op.stops.push_back({stop.at, Parameter(stop.color)});
          return op;
        },
        [&](const ToRootSignal &k) -> NodeKind {
          return ToRootOperation{Reference(k.of), Transform()};
        },
        [&](const RateSignal &k) -> NodeKind {
          return RateOperation{Reference(k.of), Delta()};
        },
        [&](const SmoothSignal &k) -> NodeKind {
          return SmoothOperation{Reference(k.of), Parameter(k.seconds),
                                 Delta()};
        },
        [&](const ExprSignal &) -> NodeKind {
          return ExpressionOperation{Expression(declaration)};
        },
        [&](const TriggerSignal &k) -> NodeKind {
          TriggerOperation op;
          op.time = Time();
          op.lifetime = Parameter(k.lifetime);
          op.max = k.max;
          op.payloadType = k.payload;
          op.anchor = k.anchor;
          op.origin = Match(
              k.origin,
              [&](const WhenOrigin &origin) -> decltype(op.origin) {
                return ConditionTriggerInput{
                    Reference(origin.when),
                    origin.value ? std::optional{Reference(*origin.value)}
                                 : std::nullopt};
              },
              [&](const EventOrigin &origin) -> decltype(op.origin) {
                return EventTriggerInput{Input(EventInput{origin},
                                               ResourceType::kEvents,
                                               "events " + origin.event)};
              },
              [&](const PluginOrigin &origin) -> decltype(op.origin) {
                return EventTriggerInput{Input(EventInput{origin},
                                               ResourceType::kEvents,
                                               "plugin events " + origin.id)};
              });
          return op;
        },
        [&](const PayloadSignal &k) -> NodeKind {
          return HoldOperation{TriggerPort(k.trigger, 1)};
        },
        [&](const CounterSignal &k) -> NodeKind {
          return CounterOperation{
              TriggerPort(k.trigger, 2),
              k.reset ? std::optional{TriggerPort(*k.reset, 2)} : std::nullopt,
              Optional(k.cap)};
        },
        [&](const AccumulateSignal &k) -> NodeKind {
          return AccumulateOperation{TriggerPort(k.trigger, 2),
                                     Parameter(k.decay), Delta()};
        });
  }
  NodeKind SourceKindOf(const Source &source) {
    return Match(
        source.kind,
        [&](const ImageSource &k) -> NodeKind {
          TextureCoordinatesOperation coordinates;
          coordinates.uv = Uv();
          coordinates.mirror = k.mirror;
          coordinates.transpose = k.transpose;
          if (k.scroll) {
            coordinates.scroll = Parameter(*k.scroll);
          }
          if (k.tile)
            coordinates.tile = Parameter(*k.tile);
          const auto uv = Add(std::move(coordinates), ValueType::kVec2,
                              "image coordinates");
          ImageOperation image{Input(TextureInput{k.path},
                                     ResourceType::kTexture,
                                     "texture " + k.path),
                               uv, k.channel, k.space, k.mip};
          if (k.channel != ImageChannel::kRgb)
            return image;
          const auto sampled =
              Add(image, ValueType::kVec3, source.name + " sampled image");
          const auto luma = Project(sampled, ValueType::kScalar,
                                    "dot(@value,[0.299,0.587,0.114])",
                                    source.name + " image luminance");
          const auto mean =
              Add(ReductionOperation{ReductionKind::kMean, luma},
                  ValueType::kScalar, source.name + " image mean");
          auto program = Program::Parse("@sample * (0.5 / max(@center,0.05))");
          if (!program)
            return image;
          return ExpressionOperation{
              BoundExpression{std::move(*program), {sampled, mean}, {}}};
        },
        [&](const MaterialSource &k) -> NodeKind {
          return MaterialOperation{Material(), Uv(), k.channel};
        },
        [&](const BakeSource &k) -> NodeKind {
          return BakeOperation{Geometry(), Uv(), k.bake};
        },
        [&](const DistanceSource &k) -> NodeKind {
          return DistanceOperation{Geometry(), Uv(),
                                   Input(NodePositionInput{k.from},
                                         ValueType::kVec3,
                                         "node position " + k.from)};
        },
        [&](const RippleSource &k) -> NodeKind {
          return RippleOperation{TriggerPort(k.trigger, 1),
                                 Geometry(),
                                 Uv(),
                                 Transform(),
                                 Time(),
                                 Parameter(k.speed),
                                 Parameter(k.width),
                                 Parameter(k.decay),
                                 Parameter(k.direction),
                                 k.shape};
        },
        [&](const MaterialClustersSource &k) -> NodeKind {
          return MaterialClustersOperation{Material(), Geometry(), Uv(),
                                           k.settings};
        });
  }
  void Functions() {
    functions.resize(graph.declarations_.size());
    for (std::size_t i = 0; i < graph.declarations_.size(); ++i) {
      const auto &declaration = graph.declarations_[i];
      if (!Is<Curve>(declaration.definition))
        continue;
      functions[i] = graph.functions_.size();
      if (const auto *curve = Get<Curve>(declaration.definition);
          curve && !curve->name.empty())
        graph.functionIndicesByName_.emplace(curve->name, *functions[i]);
      FunctionDefinition function;
      function.displayName = declaration.displayName;
      function.parameters = {{"x", ValueType::kScalar},
                             {"mean", ValueType::kScalar}};
      function.nodes.push_back(
          {"x", ParameterOperation{0}, {{"value", ValueType::kScalar}}});
      function.nodes.push_back(
          {"mean", ParameterOperation{1}, {{"value", ValueType::kScalar}}});
      BoundExpression expression;
      if (declaration.expression)
        expression.program =
            declaration.expression->program.BindContext(std::nullopt, 0, 1);
      expression.valueBindings = {{0, 0}, {1, 0}};
      function.nodes.push_back({function.displayName,
                                ExpressionOperation{std::move(expression)},
                                {{"value", ValueType::kScalar}}});
      function.result = {2, 0};
      function.isDisabled = declaration.isDisabled || !declaration.expression;
      graph.functions_.push_back(std::move(function));
    }
    for (const auto &[location, index] : graph.resultTransformsByLocation_)
      if (index < functions.size() && functions[index])
        graph.transformsByLocation_[location] = *functions[index];
  }
  void Nodes() {
    results.resize(graph.declarations_.size());
    std::vector<OutputRef> bases(results.size());
    for (std::size_t i = 0; i < results.size(); ++i) {
      const auto &declaration = graph.declarations_[i];
      if (Is<Curve>(declaration.definition))
        continue;
      results[i] = Add(ConstantOperation{}, declaration.valueType,
                       declaration.displayName);
      bases[i] = results[i];
      if (declaration.resultTransform)
        bases[i] = Add(ConstantOperation{}, declaration.valueType,
                       declaration.displayName + " before transform");
      if (results[i].node >= graph.nodes_.size() ||
          bases[i].node >= graph.nodes_.size())
        continue;
      rejected[results[i].node] = rejected[bases[i].node] =
          declaration.isDisabled;
      if (const auto *signal = Get<Signal>(declaration.definition)) {
        graph.signalDeclarations_[results[i].node] = i;
        graph.signalDeclarations_[bases[i].node] = i;
        if (Is<TriggerSignal>(signal->kind))
          graph.triggersByName_[signal->name] = bases[i].node;
      } else if (Is<Source>(declaration.definition))
        graph.sourceDeclarations_[results[i].node] = i;
      else
        graph.maskDeclarations_[results[i].node] = i;
    }
    for (std::size_t i = 0; i < results.size(); ++i) {
      const auto &declaration = graph.declarations_[i];
      const auto node = bases[i].node;
      if (node >= graph.nodes_.size())
        continue;
      NodeKind kind = Match(
          declaration.definition,
          [&](const Signal &s) { return SignalKindOf(s, declaration); },
          [&](const Source &s) { return SourceKindOf(s); },
          [&](const Mask &) -> NodeKind {
            return ExpressionOperation{Expression(declaration)};
          },
          [](const Curve &) -> NodeKind { return ConstantOperation{}; });
      graph.nodes_[node].kind = std::move(kind);
      if (Is<TriggerOperation>(graph.nodes_[node].kind))
        graph.nodes_[node].outputs = {{"progress", ValueType::kScalar},
                                      {"activeFirings", ResourceType::kFirings},
                                      {"acceptedCount", ResourceType::kCount}};
      if (declaration.resultTransform) {
        const auto f = *declaration.resultTransform;
        const auto mean = Constant(0.5f);
        if (f < functions.size() && functions[f]) {
          if (declaration.valueType == ValueType::kScalar)
            graph.nodes_[results[i].node].kind =
                CallOperation{*functions[f], {bases[i], mean}};
          else {
            const auto mapped = MapComponents(
                *functions[f], bases[i], mean, declaration.valueType,
                declaration.displayName + " transformed value");
            if (mapped.node < graph.nodes_.size())
              graph.nodes_[results[i].node].kind =
                  graph.nodes_[mapped.node].kind;
            else
              rejected[results[i].node] = true;
          }
        } else
          rejected[results[i].node] = true;
      }
    }
    BindOutputs();
    auto names = std::move(graph.nodeIndicesByName_);
    graph.nodeIndicesByName_.clear();
    for (const auto &[name, index] : names)
      if (index < results.size() && results[index].node < graph.nodes_.size())
        graph.nodeIndicesByName_[name] = results[index].node;
  }
  OutputRef Project(OutputRef value, ValueType type, std::string expression,
                    const std::string &label) {
    auto program = Program::Parse(expression);
    if (!program)
      return {};
    return Add(
        ExpressionOperation{BoundExpression{std::move(*program), {value}, {}}},
        type, label);
  }
  OutputRef MapComponents(FunctionId function, OutputRef source, OutputRef mean,
                          GraphValueType type, const std::string &label) {
    const auto *numeric = Get<ValueType>(type);
    if (!numeric || *numeric == ValueType::kScalar)
      return Add(MapFunctionOperation{function, {source, mean}}, type, label);
    VectorOperation result;
    const std::array<std::string, 3> axes =
        *numeric == ValueType::kVec2
            ? std::array<std::string, 3>{"dot(@value,[1,0])",
                                         "dot(@value,[0,1])", ""}
            : std::array<std::string, 3>{"dot(@value,[1,0,0])",
                                         "dot(@value,[0,1,0])",
                                         "dot(@value,[0,0,1])"};
    const std::size_t count = *numeric == ValueType::kVec2 ? 2 : 3;
    for (std::size_t i = 0; i < count; ++i) {
      const auto component =
          Project(source, ValueType::kScalar, axes[i], label + " component");
      result.components.push_back(
          Add(MapFunctionOperation{function, {component, mean}},
              ValueType::kScalar, label + " mapped component"));
    }
    return Add(std::move(result), type, label);
  }
  void BindOutput(std::string property, OutputRef value) {
    graph.outputBindings_.push_back({std::move(property), value});
  }
  void BindOutputs() {
    for (std::size_t i = 0; i < recipe.outputs.size(); ++i) {
      const auto label = OutputWhere(i);
      Match(
          recipe.outputs[i],
          [&](const SurfaceOutput &surface) {
            for (std::size_t j = 0; j < surface.stack.size(); ++j) {
              const auto &layer = surface.stack[j];
              const auto location = LayerWhere(i, j);
              auto source = Match(
                  layer.source, [&](const Ref &ref) { return Reference(ref); },
                  [&](const Vec3 &value) { return Constant(value); });
              if (const auto f = graph.transformsByLocation_.find(location);
                  f != graph.transformsByLocation_.end()) {
                const auto original =
                    graph.resultTransformsByLocation_.find(location);
                const auto *program =
                    original != graph.resultTransformsByLocation_.end() &&
                            graph.declarations_[original->second].expression
                        ? &graph.declarations_[original->second]
                               .expression->program
                        : nullptr;
                const bool textureSource =
                    Get<Ref>(layer.source) &&
                    !Get<Ref>(layer.source)->name.empty();
                auto type = source.node < graph.nodes_.size()
                                ? graph.nodes_[source.node].outputs.front().type
                                : GraphValueType{ValueType::kScalar};
                auto measured = source;
                if (const auto *numeric = Get<ValueType>(type);
                    numeric && *numeric != ValueType::kScalar) {
                  measured = Project(source, ValueType::kScalar,
                                     *numeric == ValueType::kVec3
                                         ? "dot(@value,[0.299,0.587,0.114])"
                                         : "dot(@value,[1,0])",
                                     location + " mean projection");
                }
                const auto mean =
                    program && program->UsesMean() && textureSource
                        ? Add(ReductionOperation{ReductionKind::kMean,
                                                 measured},
                              ValueType::kScalar, location + " source mean")
                        : Constant(0.5f);
                if (layer.color) {
                  auto tint = Program::Parse("@value * @color");
                  if (tint)
                    source = Add(ExpressionOperation{BoundExpression{
                                     std::move(*tint),
                                     {source, Parameter(*layer.color)},
                                     {}}},
                                 ValueType::kVec3, location + " tinted source");
                  type = ValueType::kVec3;
                }
                source = MapComponents(f->second, source, mean, type,
                                       location + " transformed source");
              }
              BindOutput(location + " source", source);
              BindOutput(location + " opacity", Parameter(layer.opacity));
              if (layer.color)
                BindOutput(location + " color", layer.curve
                                                    ? Constant(Vec3{1, 1, 1})
                                                    : Parameter(*layer.color));
              if (layer.mask)
                BindOutput(location + " mask", Reference(*layer.mask));
            }
            const auto scalar = [&](std::string name,
                                    const std::optional<Param> &value) {
              if (value)
                BindOutput(label + " " + name, Parameter(*value));
            };
            scalar("strength", surface.scalars.strength);
            scalar("scale", surface.scalars.scale);
            scalar("weight", surface.scalars.weight);
            scalar("screenSpaceScale", surface.scalars.screenSpaceScale);
            scalar("logMicrofacetDensity",
                   surface.scalars.logMicrofacetDensity);
            scalar("microfacetRoughness", surface.scalars.microfacetRoughness);
            scalar("densityRandomization",
                   surface.scalars.densityRandomization);
            scalar("roughness", surface.scalars.roughness);
            scalar("level", surface.scalars.level);
            scalar("thickness", surface.scalars.thickness);
            if (surface.scalars.color)
              BindOutput(label + " color", Parameter(*surface.scalars.color));
          },
          [&](const LightOutput &light) {
            BindOutput(label + " offset", Parameter(light.offset));
            BindOutput(label + " color", Parameter(light.color));
            BindOutput(label + " intensity", Parameter(light.intensity));
            BindOutput(label + " size", Parameter(light.size));
            BindOutput(label + " cutoff", Parameter(light.cutoff));
          });
    }
    BindOutput("shell opacity", Parameter(recipe.shell.opacity));
    BindOutput("shell rimPower", Parameter(recipe.shell.rimPower));
    BindOutput("shell emissive", Parameter(recipe.shell.emissive));
    BindOutput("shell inflate", Parameter(recipe.shell.pose.inflate));
    BindOutput("shell offset", Parameter(recipe.shell.pose.offset));
    BindOutput("shell scale", Parameter(recipe.shell.pose.scale));
    BindOutput("shell spin", Parameter(recipe.shell.pose.spin));
  }
  void Analyze() {
    const auto count = graph.nodes_.size();
    graph.disabled_.assign(rejected.begin(), rejected.begin() + count);
    graph.sampleDependent_.assign(count, false);
    graph.changing_.assign(count, false);
    graph.dependencyOrder_.clear();
    graph.signalEvaluationOrder_.clear();
    enum class Mark { kNew, kOpen, kDone };
    std::vector<Mark> marks(count, Mark::kNew);
    std::vector<bool> backendRequired(count, false);
    struct Frame {
      NodeId node;
      std::vector<OutputRef> inputs;
      std::size_t next = 0;
    };
    std::vector<Frame> stack;
    for (NodeId root = 0; root < count; ++root) {
      if (marks[root] != Mark::kNew)
        continue;
      marks[root] = Mark::kOpen;
      stack.push_back({root, InputsOf(graph.nodes_[root].kind)});
      while (!stack.empty()) {
        auto &frame = stack.back();
        if (frame.next < frame.inputs.size()) {
          const auto input = frame.inputs[frame.next++];
          if (input.node >= count ||
              input.output >= graph.nodes_[input.node].outputs.size()) {
            graph.disabled_[frame.node] = true;
            continue;
          }
          if (marks[input.node] == Mark::kOpen) {
            graph.disabled_[frame.node] = true;
            graph.disabled_[input.node] = true;
            continue;
          }
          if (marks[input.node] == Mark::kNew) {
            marks[input.node] = Mark::kOpen;
            stack.push_back(
                {input.node, InputsOf(graph.nodes_[input.node].kind)});
          }
          continue;
        }
        const auto id = frame.node;
        const auto &kind = graph.nodes_[id].kind;
        bool changing = Stateful(kind), sample = false;
        backendRequired[id] = Is<ReductionOperation>(kind);
        if (const auto *external = Get<ExternalInput>(kind)) {
          sample = Is<SampleUvInput>(external->source);
          backendRequired[id] = Is<NodePositionInput>(external->source);
          changing = Is<TimeInput>(external->source) ||
                     Is<DeltaTimeInput>(external->source) ||
                     Is<ActorValueSignal>(external->source) ||
                     Is<ActorStateSignal>(external->source) ||
                     Is<EnchantmentSignal>(external->source) ||
                     Is<RootTransformInput>(external->source) ||
                     Is<EventInput>(external->source);
        }
        for (const auto input : frame.inputs) {
          if (input.node >= count)
            continue;
          backendRequired[id] =
              backendRequired[id] || backendRequired[input.node];
          changing = changing || graph.changing_[input.node];
          sample = sample || graph.sampleDependent_[input.node];
          graph.disabled_[id] =
              graph.disabled_[id] || graph.disabled_[input.node];
        }
        const auto invalidFunction = [&](FunctionId function) {
          return function >= graph.functions_.size() ||
                 graph.functions_[function].isDisabled;
        };
        const auto invalidCall = [&](FunctionId function,
                                     std::span<const OutputRef> arguments) {
          if (invalidFunction(function))
            return true;
          const auto &definition = graph.functions_[function];
          if (arguments.size() != definition.parameters.size())
            return true;
          for (std::size_t i = 0; i < arguments.size(); ++i)
            if (graph.OutputType(arguments[i]) !=
                std::optional<GraphValueType>{definition.parameters[i].type})
              return true;
          if (definition.result.node >= definition.nodes.size())
            return true;
          const auto &outputs =
              definition.nodes[definition.result.node].outputs;
          return definition.result.output >= outputs.size() ||
                 graph.nodes_[id].outputs.empty() ||
                 graph.nodes_[id].outputs.front().type !=
                     outputs[definition.result.output].type;
        };
        if (const auto *call = Get<CallOperation>(kind))
          graph.disabled_[id] = graph.disabled_[id] ||
                                invalidCall(call->function, call->arguments);
        if (const auto *map = Get<MapFunctionOperation>(kind))
          graph.disabled_[id] =
              graph.disabled_[id] || invalidCall(map->function, map->arguments);
        if (const auto *expression = Get<ExpressionOperation>(kind)) {
          for (const auto &binding : expression->expression.functionBindings) {
            if (invalidFunction(binding.function)) {
              graph.disabled_[id] = true;
              continue;
            }
            const auto &function = graph.functions_[binding.function];
            std::vector<bool> bound(function.parameters.size());
            if (binding.sampledParameter >= bound.size() ||
                function.parameters[binding.sampledParameter].type !=
                    ValueType::kScalar) {
              graph.disabled_[id] = true;
              continue;
            }
            bound[binding.sampledParameter] = true;
            for (const auto &argument : binding.arguments) {
              if (argument.parameter >= bound.size() ||
                  bound[argument.parameter]) {
                graph.disabled_[id] = true;
                continue;
              }
              bound[argument.parameter] = true;
              if (graph.OutputType(argument.value) !=
                  std::optional<GraphValueType>{
                      function.parameters[argument.parameter].type})
                graph.disabled_[id] = true;
            }
            if (std::ranges::find(bound, false) != bound.end())
              graph.disabled_[id] = true;
          }
        }
        if (const auto *reduction = Get<ReductionOperation>(kind)) {
          const auto inputType = graph.OutputType(reduction->value);
          if (!inputType || !Is<ValueType>(*inputType) ||
              graph.nodes_[id].outputs.empty() ||
              graph.nodes_[id].outputs.front().type != *inputType)
            graph.disabled_[id] = true;
          if (static_cast<unsigned>(reduction->kind) >
              static_cast<unsigned>(ReductionKind::kMaximum))
            graph.disabled_[id] = true;
        }
        if (Stateful(kind) && backendRequired[id]) {
          graph.disabled_[id] = true;
          graph.diagnostics_.push_back(
              {Severity::kError, graph.nodes_[id].displayName,
               "render results cannot feed tick signal state"});
        }
        graph.changing_[id] = changing;
        graph.sampleDependent_[id] = sample && !Is<ReductionOperation>(kind);
        graph.dependencyOrder_.push_back(id);
        if (!graph.sampleDependent_[id] && !backendRequired[id])
          graph.tickOrder_.push_back(id);
        marks[id] = Mark::kDone;
        stack.pop_back();
      }
    }
    graph.lowered_ = true;
    for (const auto id : graph.tickOrder_)
      if (const auto *signal = graph.SignalAt(id);
          signal && graph.FindSignalIndex(signal->name) == id)
        graph.signalEvaluationOrder_.push_back(id);
  }
  void Run() {
    Functions();
    Nodes();
    Analyze();
  }
};
void LowerRecipeGraph(RecipeGraph &graph, const Recipe &recipe) {
  RecipeGraphLowering{graph, recipe, {}, {}, {}, {}}.Run();
}
}
