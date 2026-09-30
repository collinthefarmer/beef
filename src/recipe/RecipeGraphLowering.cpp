// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/RecipeGraph.h"

#include <algorithm>
#include <format>
#include <limits>
#include <span>

namespace BetterEnchantmentEffects {
using detail::RecipeDeclaration;
struct RecipeGraphLowering {
  enum class VisitMark : std::uint8_t { kNew, kOpen, kDone };
  struct PostOrderFrame {
    NodeId node;
    std::vector<OutputRef> inputs;
    std::size_t next = 0;
  };
  struct AnalysisWalk {
    std::vector<VisitMark> marks;
    std::vector<PostOrderFrame> stack;
    std::vector<bool> backendRequired;
  };
  struct NodeTraits {
    bool changing = false;
    bool sampleDependent = false;
    bool backendRequired = false;
  };
  struct AppliedCurve {
    FunctionId function = 0;
    OutputRef mean;
  };

  RecipeGraph &graph;
  const Recipe &recipe;
  std::vector<OutputRef> results;
  std::vector<std::optional<FunctionId>> functions;
  std::vector<bool> rejected;
  std::unordered_map<std::string, OutputRef> sharedInputs;
  std::unordered_map<std::string, OutputRef> unscrolledSources;
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
    return Add(ConstantOperation{value}, type, "constant");
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
  [[nodiscard]] OutputRef Reference(const Ref &ref) const {
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
  [[nodiscard]] OutputRef TriggerPort(const Ref &ref, std::size_t port) const {
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
  TriggerOperation TriggerOperationFor(const TriggerSignal &signal) {
    TriggerOperation op;
    op.time = Time();
    op.lifetime = Parameter(signal.lifetime);
    op.max = signal.max;
    op.payloadType = signal.payload;
    op.anchor = signal.anchor;
    op.origin = Match(
        signal.origin,
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
          return TriggerOperationFor(k);
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
  ImageOperation ImageAt(const ImageSource &image, OutputRef texture,
                         bool scrolled) {
    TextureCoordinatesOperation coordinates;
    coordinates.uv = Uv();
    coordinates.mirror = image.mirror;
    coordinates.transpose = image.transpose;
    if (image.tile)
      coordinates.tile = Parameter(*image.tile);
    if (scrolled && image.scroll)
      coordinates.scroll = Parameter(*image.scroll);
    const auto uv =
        Add(coordinates, ValueType::kVec2,
            scrolled ? "image coordinates" : "unscrolled image coordinates");
    return ImageOperation{texture, uv, image.channel, image.space, image.mip};
  }
  std::optional<ExpressionOperation> Normalized(OutputRef sample,
                                                OutputRef mean) {
    auto program = Program::Parse("@sample * (0.5 / max(@center,0.05))");
    if (!program)
      return std::nullopt;
    return ExpressionOperation{
        BoundExpression{std::move(*program), {sample, mean}, {}}};
  }
  NodeKind ImageKindOf(const ImageSource &image, const std::string &name,
                       ValueType type) {
    const auto texture = Input(TextureInput{image.path}, ResourceType::kTexture,
                               "texture " + image.path);
    const auto shown = ImageAt(image, texture, true);
    if (image.channel != ImageChannel::kRgb) {
      if (image.scroll)
        unscrolledSources.emplace(name, Add(ImageAt(image, texture, false),
                                            type, name + " unscrolled image"));
      return shown;
    }
    const auto sampled = Add(shown, ValueType::kVec3, name + " sampled image");
    const auto still = image.scroll
                           ? Add(ImageAt(image, texture, false),
                                 ValueType::kVec3, name + " unscrolled image")
                           : sampled;
    const auto luma =
        Project(still, ValueType::kScalar, "dot(@value,[0.299,0.587,0.114])",
                name + " image luminance");
    const auto mean = Add(ReductionOperation{ReductionKind::kMean, luma},
                          ValueType::kScalar, name + " image mean");
    auto normalized = Normalized(sampled, mean);
    if (!normalized)
      return shown;
    if (image.scroll)
      if (auto coverage = Normalized(still, mean))
        unscrolledSources.emplace(name,
                                  Add(std::move(*coverage), ValueType::kVec3,
                                      name + " unscrolled coverage"));
    return std::move(*normalized);
  }
  OutputRef SourceMean(const Layer &layer, OutputRef source,
                       GraphValueType type, const std::string &location) {
    const auto *ref = Get<Ref>(layer.source);
    if (!ref || ref->name.empty())
      return Constant(0.5f);
    auto measured = source;
    if (const auto still = unscrolledSources.find(ref->name);
        still != unscrolledSources.end())
      measured = still->second;
    if (const auto *numeric = Get<ValueType>(type);
        numeric && *numeric != ValueType::kScalar)
      measured = Project(measured, ValueType::kScalar,
                         *numeric == ValueType::kVec3
                             ? "dot(@value,[0.299,0.587,0.114])"
                             : "dot(@value,[1,0])",
                         location + " mean projection");
    return Add(ReductionOperation{ReductionKind::kMean, measured},
               ValueType::kScalar, location + " source mean");
  }
  NodeKind SourceKindOf(const Source &source, ValueType type) {
    return Match(
        source.kind,
        [&](const ImageSource &k) -> NodeKind {
          return ImageKindOf(k, source.name, type);
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
  OutputRef AddDeclarationNode(std::size_t declaration) {
    if (declaration >= graph.declarations_.size() ||
        declaration >= results.size())
      return {};
    const RecipeDeclaration &definition = graph.declarations_[declaration];
    if (Is<Curve>(definition.definition))
      return {};
    results[declaration] =
        Add(ConstantOperation{}, definition.valueType, definition.displayName);
    OutputRef base = results[declaration];
    if (definition.resultTransform)
      base = Add(ConstantOperation{}, definition.valueType,
                 definition.displayName + " before transform");
    if (results[declaration].node < graph.nodes_.size() &&
        base.node < graph.nodes_.size())
      IndexDeclaration(declaration, base);
    return base;
  }
  void IndexDeclaration(std::size_t declaration, OutputRef base) {
    if (declaration >= graph.declarations_.size() ||
        declaration >= results.size())
      return;
    const RecipeDeclaration &definition = graph.declarations_[declaration];
    const NodeId result = results[declaration].node;
    if (result >= rejected.size() || base.node >= rejected.size())
      return;
    rejected[result] = rejected[base.node] = definition.isDisabled;
    if (const auto *signal = Get<Signal>(definition.definition)) {
      graph.signalDeclarations_[result] = declaration;
      graph.signalDeclarations_[base.node] = declaration;
      if (Is<TriggerSignal>(signal->kind))
        graph.triggersByName_[signal->name] = base.node;
    } else if (Is<Source>(definition.definition))
      graph.sourceDeclarations_[result] = declaration;
    else
      graph.maskDeclarations_[result] = declaration;
  }
  std::vector<OutputRef> AddDeclarationNodes() {
    results.resize(graph.declarations_.size());
    std::vector<OutputRef> bases(results.size());
    for (std::size_t i = 0; i < bases.size(); ++i)
      bases[i] = AddDeclarationNode(i);
    return bases;
  }
  NodeKind DeclarationKindOf(const RecipeDeclaration &declaration) {
    return Match(
        declaration.definition,
        [&](const Signal &s) { return SignalKindOf(s, declaration); },
        [&](const Source &s) {
          NodeKind kind = SourceKindOf(s, declaration.valueType);
          if (declaration.resultTransform)
            unscrolledSources.erase(s.name);
          return kind;
        },
        [&](const Mask &) -> NodeKind {
          return ExpressionOperation{Expression(declaration)};
        },
        [](const Curve &) -> NodeKind { return ConstantOperation{}; });
  }
  void DeclareTriggerOutputs(NodeId node) {
    if (node >= graph.nodes_.size())
      return;
    graph.nodes_[node].outputs = {{"progress", ValueType::kScalar},
                                  {"activeFirings", ResourceType::kFirings},
                                  {"acceptedCount", ResourceType::kCount}};
  }
  void ApplyResultTransform(const RecipeDeclaration &declaration,
                            OutputRef result, OutputRef base) {
    if (!declaration.resultTransform || result.node >= graph.nodes_.size() ||
        result.node >= rejected.size())
      return;
    const std::size_t function = *declaration.resultTransform;
    const OutputRef mean = Constant(0.5f);
    if (function >= functions.size() || !functions[function]) {
      rejected[result.node] = true;
      return;
    }
    const AppliedCurve curve{*functions[function], mean};
    if (declaration.valueType == ValueType::kScalar) {
      graph.nodes_[result.node].kind =
          CallOperation{curve.function, {base, curve.mean}};
      return;
    }
    const OutputRef mapped =
        MapComponents(curve, base, declaration.valueType,
                      declaration.displayName + " transformed value");
    if (mapped.node < graph.nodes_.size())
      graph.nodes_[result.node].kind = graph.nodes_[mapped.node].kind;
    else
      rejected[result.node] = true;
  }
  void LowerDeclaration(std::size_t declaration, OutputRef base) {
    if (declaration >= graph.declarations_.size() ||
        declaration >= results.size() || base.node >= graph.nodes_.size())
      return;
    const RecipeDeclaration &definition = graph.declarations_[declaration];
    NodeKind kind = DeclarationKindOf(definition);
    graph.nodes_[base.node].kind = std::move(kind);
    if (Is<TriggerOperation>(graph.nodes_[base.node].kind))
      DeclareTriggerOutputs(base.node);
    if (definition.resultTransform)
      ApplyResultTransform(definition, results[declaration], base);
  }
  void IndexNamesByResult() {
    auto names = std::move(graph.nodeIndicesByName_);
    graph.nodeIndicesByName_.clear();
    for (const auto &[name, index] : names)
      if (index < results.size() && results[index].node < graph.nodes_.size())
        graph.nodeIndicesByName_[name] = results[index].node;
  }
  void Nodes() {
    const std::vector<OutputRef> bases = AddDeclarationNodes();
    for (std::size_t i = 0; i < bases.size(); ++i)
      LowerDeclaration(i, bases[i]);
    BindOutputs();
    IndexNamesByResult();
  }
  OutputRef Project(OutputRef value, ValueType type,
                    const std::string &expression, const std::string &label) {
    auto program = Program::Parse(expression);
    if (!program)
      return {};
    return Add(
        ExpressionOperation{BoundExpression{std::move(*program), {value}, {}}},
        type, label);
  }
  OutputRef MapComponents(const AppliedCurve &curve, OutputRef source,
                          GraphValueType type, const std::string &label) {
    const auto *numeric = Get<ValueType>(type);
    if (!numeric || *numeric == ValueType::kScalar)
      return Add(MapFunctionOperation{curve.function, {source, curve.mean}},
                 type, label);
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
          Add(MapFunctionOperation{curve.function, {component, curve.mean}},
              ValueType::kScalar, label + " mapped component"));
    }
    return Add(std::move(result), type, label);
  }
  void BindOutput(std::string property, OutputRef value) {
    graph.outputBindings_.push_back({std::move(property), value});
  }
  [[nodiscard]] const Program *
  CurveProgramAt(const std::string &location) const {
    const auto original = graph.resultTransformsByLocation_.find(location);
    if (original == graph.resultTransformsByLocation_.end() ||
        original->second >= graph.declarations_.size())
      return nullptr;
    const RecipeDeclaration &declaration =
        graph.declarations_[original->second];
    return declaration.expression ? &declaration.expression->program : nullptr;
  }
  [[nodiscard]] GraphValueType NodeTypeOf(OutputRef value) const {
    if (value.node >= graph.nodes_.size() ||
        graph.nodes_[value.node].outputs.empty())
      return ValueType::kScalar;
    return graph.nodes_[value.node].outputs.front().type;
  }
  OutputRef Tinted(OutputRef source, const Vec3Param &color,
                   const std::string &location) {
    auto tint = Program::Parse("@value * @color");
    if (!tint)
      return source;
    return Add(ExpressionOperation{BoundExpression{
                   std::move(*tint), {source, Parameter(color)}, {}}},
               ValueType::kVec3, location + " tinted source");
  }
  OutputRef TransformedSource(const Layer &layer, OutputRef source,
                              FunctionId curve, const std::string &location) {
    const Program *program = CurveProgramAt(location);
    GraphValueType type = NodeTypeOf(source);
    const OutputRef mean = program && program->UsesMean()
                               ? SourceMean(layer, source, type, location)
                               : Constant(0.5f);
    if (layer.color) {
      source = Tinted(source, *layer.color, location);
      type = ValueType::kVec3;
    }
    return MapComponents(AppliedCurve{curve, mean}, source, type,
                         location + " transformed source");
  }
  void BindLayer(std::size_t output, std::size_t layer,
                 const Layer &definition) {
    const std::string location = LayerWhere(output, layer);
    OutputRef source = Match(
        definition.source, [&](const Ref &ref) { return Reference(ref); },
        [&](const Vec3 &value) { return Constant(value); });
    if (const auto curve = graph.transformsByLocation_.find(location);
        curve != graph.transformsByLocation_.end())
      source = TransformedSource(definition, source, curve->second, location);
    BindOutput(location + " source", source);
    BindOutput(location + " opacity", Parameter(definition.opacity));
    if (definition.color)
      BindOutput(location + " color", definition.curve
                                          ? Constant(Vec3{1, 1, 1})
                                          : Parameter(*definition.color));
    if (definition.mask)
      BindOutput(location + " mask", Reference(*definition.mask));
  }
  void BindOptional(const std::string &property,
                    const std::optional<Param> &value) {
    if (value)
      BindOutput(property, Parameter(*value));
  }
  void BindSurfaceScalars(const std::string &label,
                          const SlotScalars &scalars) {
    BindOptional(label + " strength", scalars.strength);
    BindOptional(label + " scale", scalars.scale);
    BindOptional(label + " weight", scalars.weight);
    BindOptional(label + " screenSpaceScale", scalars.screenSpaceScale);
    BindOptional(label + " logMicrofacetDensity", scalars.logMicrofacetDensity);
    BindOptional(label + " microfacetRoughness", scalars.microfacetRoughness);
    BindOptional(label + " densityRandomization", scalars.densityRandomization);
    BindOptional(label + " roughness", scalars.roughness);
    BindOptional(label + " level", scalars.level);
    BindOptional(label + " thickness", scalars.thickness);
    if (scalars.color)
      BindOutput(label + " color", Parameter(*scalars.color));
  }
  void BindSurfaceOutput(std::size_t output, const SurfaceOutput &surface) {
    for (std::size_t layer = 0; layer < surface.stack.size(); ++layer)
      BindLayer(output, layer, surface.stack[layer]);
    BindSurfaceScalars(OutputWhere(output), surface.scalars);
  }
  void BindLightOutput(std::size_t output, const LightOutput &light) {
    const std::string label = OutputWhere(output);
    BindOutput(label + " offset", Parameter(light.offset));
    BindOutput(label + " color", Parameter(light.color));
    BindOutput(label + " intensity", Parameter(light.intensity));
    BindOutput(label + " size", Parameter(light.size));
    BindOutput(label + " cutoff", Parameter(light.cutoff));
  }
  void BindShellOutputs() {
    BindOutput("shell opacity", Parameter(recipe.shell.opacity));
    BindOutput("shell rimPower", Parameter(recipe.shell.rimPower));
    BindOutput("shell emissive", Parameter(recipe.shell.emissive));
    BindOutput("shell inflate", Parameter(recipe.shell.pose.inflate));
    BindOutput("shell offset", Parameter(recipe.shell.pose.offset));
    BindOutput("shell scale", Parameter(recipe.shell.pose.scale));
    BindOutput("shell spin", Parameter(recipe.shell.pose.spin));
  }
  void BindOutputs() {
    for (std::size_t i = 0; i < recipe.outputs.size(); ++i) {
      if (const SurfaceOutput *surface = SurfaceOutputOf(recipe, i))
        BindSurfaceOutput(i, *surface);
      else if (const LightOutput *light = LightOutputOf(recipe, i))
        BindLightOutput(i, *light);
    }
    BindShellOutputs();
  }
  void ResetDerivedState() {
    const std::size_t count = graph.nodes_.size();
    graph.disabled_.assign(
        rejected.begin(),
        rejected.begin() +
            static_cast<std::ptrdiff_t>(std::min(count, rejected.size())));
    graph.disabled_.resize(count, false);
    graph.sampleDependent_.assign(count, false);
    graph.changing_.assign(count, false);
    graph.dependencyOrder_.clear();
    graph.signalEvaluationOrder_.clear();
  }
  [[nodiscard]] static NodeTraits TraitsOf(const NodeKind &kind) {
    NodeTraits traits{Stateful(kind), false, Is<ReductionOperation>(kind)};
    if (const auto *external = Get<ExternalInput>(kind)) {
      traits.sampleDependent = Is<SampleUvInput>(external->source);
      traits.backendRequired = Is<NodePositionInput>(external->source);
      traits.changing = Is<TimeInput>(external->source) ||
                        Is<DeltaTimeInput>(external->source) ||
                        Is<ActorValueSignal>(external->source) ||
                        Is<ActorStateSignal>(external->source) ||
                        Is<EnchantmentSignal>(external->source) ||
                        Is<RootTransformInput>(external->source) ||
                        Is<EventInput>(external->source);
    }
    return traits;
  }
  [[nodiscard]] NodeTraits WithInputTraits(NodeTraits own,
                                           std::span<const OutputRef> inputs,
                                           const AnalysisWalk &walk) const {
    for (const OutputRef input : inputs) {
      if (input.node >= graph.nodes_.size() ||
          input.node >= walk.backendRequired.size())
        continue;
      own.backendRequired =
          own.backendRequired || walk.backendRequired[input.node];
      own.changing = own.changing || graph.changing_[input.node];
      own.sampleDependent =
          own.sampleDependent || graph.sampleDependent_[input.node];
    }
    return own;
  }
  void InheritDisabled(NodeId node, std::span<const OutputRef> inputs) {
    for (const OutputRef input : inputs)
      if (input.node < graph.nodes_.size())
        graph.disabled_[node] =
            graph.disabled_[node] || graph.disabled_[input.node];
  }
  [[nodiscard]] bool IsValidFunction(FunctionId function) const {
    return function < graph.functions_.size() &&
           !graph.functions_[function].isDisabled;
  }
  [[nodiscard]] bool IsValidCall(NodeId node, FunctionId function,
                                 std::span<const OutputRef> arguments) const {
    if (!IsValidFunction(function))
      return false;
    const FunctionDefinition &definition = graph.functions_[function];
    if (arguments.size() != definition.parameters.size())
      return false;
    for (std::size_t i = 0; i < arguments.size(); ++i)
      if (graph.OutputType(arguments[i]) !=
          std::optional<GraphValueType>{definition.parameters[i].type})
        return false;
    const std::optional<GraphValueType> result =
        FunctionResultTypeOf(definition);
    return result && !graph.nodes_[node].outputs.empty() &&
           graph.nodes_[node].outputs.front().type == *result;
  }
  [[nodiscard]] bool
  IsValidFunctionBinding(const BoundFunction &binding) const {
    if (!IsValidFunction(binding.function))
      return false;
    const FunctionDefinition &function = graph.functions_[binding.function];
    std::vector<bool> bound(function.parameters.size());
    if (binding.sampledParameter >= bound.size() ||
        function.parameters[binding.sampledParameter].type !=
            ValueType::kScalar)
      return false;
    bound[binding.sampledParameter] = true;
    for (const BoundFunctionArgument &argument : binding.arguments) {
      if (argument.parameter >= bound.size() || bound[argument.parameter])
        return false;
      bound[argument.parameter] = true;
      if (graph.OutputType(argument.value) !=
          std::optional<GraphValueType>{
              function.parameters[argument.parameter].type})
        return false;
    }
    return std::ranges::find(bound, false) == bound.end();
  }
  [[nodiscard]] bool
  IsValidReduction(NodeId node, const ReductionOperation &reduction) const {
    const std::optional<GraphValueType> inputType =
        graph.OutputType(reduction.value);
    if (!inputType || !Is<ValueType>(*inputType) ||
        graph.nodes_[node].outputs.empty() ||
        graph.nodes_[node].outputs.front().type != *inputType)
      return false;
    return static_cast<unsigned>(reduction.kind) <=
           static_cast<unsigned>(ReductionKind::kMaximum);
  }
  [[nodiscard]] bool IsValidOperation(NodeId node) const {
    const NodeKind &kind = graph.nodes_[node].kind;
    if (const auto *call = Get<CallOperation>(kind))
      return IsValidCall(node, call->function, call->arguments);
    if (const auto *map = Get<MapFunctionOperation>(kind))
      return IsValidCall(node, map->function, map->arguments);
    if (const auto *expression = Get<ExpressionOperation>(kind))
      return std::ranges::all_of(expression->expression.functionBindings,
                                 [&](const BoundFunction &binding) {
                                   return IsValidFunctionBinding(binding);
                                 });
    if (const auto *reduction = Get<ReductionOperation>(kind))
      return IsValidReduction(node, *reduction);
    return true;
  }
  void RejectBackendState(NodeId node) {
    graph.disabled_[node] = true;
    graph.diagnostics_.push_back(
        {Severity::kError, graph.nodes_[node].displayName,
         "render results cannot feed tick signal state"});
  }
  void AppendToOrders(NodeId node, NodeTraits traits) {
    const bool reduction = Is<ReductionOperation>(graph.nodes_[node].kind);
    graph.changing_[node] = traits.changing;
    graph.sampleDependent_[node] = traits.sampleDependent && !reduction;
    graph.dependencyOrder_.push_back(node);
    if (graph.sampleDependent_[node] || traits.backendRequired)
      return;
    graph.tickOrder_.push_back(node);
    if (traits.changing)
      graph.changingTickOrder_.push_back(node);
  }
  void SettleNode(const PostOrderFrame &frame, AnalysisWalk &walk) {
    const NodeId node = frame.node;
    if (node >= graph.nodes_.size() || node >= walk.marks.size())
      return;
    const NodeKind &kind = graph.nodes_[node].kind;
    const NodeTraits traits =
        WithInputTraits(TraitsOf(kind), frame.inputs, walk);
    walk.backendRequired[node] = traits.backendRequired;
    InheritDisabled(node, frame.inputs);
    if (!IsValidOperation(node))
      graph.disabled_[node] = true;
    if (Stateful(kind) && traits.backendRequired)
      RejectBackendState(node);
    AppendToOrders(node, traits);
    walk.marks[node] = VisitMark::kDone;
  }
  void OpenNextInput(AnalysisWalk &walk) {
    if (walk.stack.empty())
      return;
    PostOrderFrame &frame = walk.stack.back();
    if (frame.next >= frame.inputs.size())
      return;
    const OutputRef input = frame.inputs[frame.next++];
    const NodeId waiting = frame.node;
    if (input.node >= graph.nodes_.size() || input.node >= walk.marks.size() ||
        input.output >= graph.nodes_[input.node].outputs.size()) {
      graph.disabled_[waiting] = true;
      return;
    }
    if (walk.marks[input.node] == VisitMark::kOpen) {
      graph.disabled_[waiting] = true;
      graph.disabled_[input.node] = true;
      return;
    }
    if (walk.marks[input.node] == VisitMark::kNew) {
      walk.marks[input.node] = VisitMark::kOpen;
      walk.stack.push_back(
          {input.node, InputsOf(graph.nodes_[input.node].kind), 0});
    }
  }
  void AnalyzeFrom(NodeId root, AnalysisWalk &walk) {
    if (root >= graph.nodes_.size() || root >= walk.marks.size())
      return;
    walk.marks[root] = VisitMark::kOpen;
    walk.stack.push_back({root, InputsOf(graph.nodes_[root].kind), 0});
    while (!walk.stack.empty()) {
      if (walk.stack.back().next < walk.stack.back().inputs.size()) {
        OpenNextInput(walk);
        continue;
      }
      const PostOrderFrame settled = std::move(walk.stack.back());
      walk.stack.pop_back();
      SettleNode(settled, walk);
    }
  }
  void OrderSignalEvaluation() {
    for (const NodeId node : graph.tickOrder_)
      if (const auto *signal = graph.SignalAt(node);
          signal && graph.FindSignalIndex(signal->name) == node)
        graph.signalEvaluationOrder_.push_back(node);
  }
  void Analyze() {
    ResetDerivedState();
    const std::size_t count = graph.nodes_.size();
    AnalysisWalk walk{std::vector<VisitMark>(count, VisitMark::kNew),
                      {},
                      std::vector<bool>(count, false)};
    for (NodeId root = 0; root < count; ++root)
      if (walk.marks[root] == VisitMark::kNew)
        AnalyzeFrom(root, walk);
    graph.lowered_ = true;
    OrderSignalEvaluation();
  }
  void Run() {
    Functions();
    Nodes();
    Analyze();
  }
};
void LowerRecipeGraph(RecipeGraph &graph, const Recipe &recipe) {
  RecipeGraphLowering{graph, recipe, {}, {}, {}, {}, {}}.Run();
}
}
