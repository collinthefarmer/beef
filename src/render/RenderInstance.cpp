// GPL-3.0-only with the additional permission in COPYING.md.
#include "render/RenderInstance.h"

#include "diagnostics/Metrics.h"

#include "render/MeshReader.h"
#include "render/SourceSampling.h"
#include <algorithm>
#include <cmath>

namespace BetterEnchantmentEffects {
namespace {
bool SameRenderValue(const RenderValue &a, const RenderValue &b) {
  if (a.index() != b.index())
    return false;
  if (const auto *value = Get<Value>(a))
    return *value == *Get<Value>(b);
  if (const auto *filter = Get<LayerFilter>(a))
    return *filter == *Get<LayerFilter>(b);
  if (const auto *stack = Get<StackResult>(a))
    return !stack->texture && !Get<StackResult>(b)->texture;
  return false;
}
bool SameTexture(const TextureRef &a, const TextureRef &b) {
  return a.get() == b.get() && a.Generation() == b.Generation();
}
bool SameImport(const Value &a, const Value &b) { return a == b; }
bool SameImport(const TextureView &a, const TextureView &b) {
  return SameTexture(a.texture, b.texture) && a.sampling == b.sampling &&
         a.normalize == b.normalize;
}
bool SameImport(const MaterialInputs &a, const MaterialInputs &b) {
  return SameTexture(a.diffuse, b.diffuse) && SameTexture(a.normal, b.normal) &&
         SameTexture(a.rmaos, b.rmaos) &&
         SameTexture(a.displacement, b.displacement) &&
         a.flatDisplacement == b.flatDisplacement;
}
bool SameImport(const RenderTransform &a, const RenderTransform &b) {
  return a.root.get() == b.root.get();
}
bool SameImport(const RenderFirings &a, const RenderFirings &b) {
  return a == b;
}
bool SameImport(const LayerFilter &a, const LayerFilter &b) { return a == b; }
bool SameImport(const StackResult &a, const StackResult &b) {
  return SameTexture(a.texture, b.texture) &&
         a.contentVersion == b.contentVersion;
}
bool SameImport(const std::shared_ptr<const MaterialSample> &a,
                const std::shared_ptr<const MaterialSample> &b) {
  return a == b || (a && b && *a == *b);
}
template <class T>
bool SameImport(const std::shared_ptr<T> &a, const std::shared_ptr<T> &b) {
  return a == b;
}
bool SameImportedValue(const RenderValue &a, const RenderValue &b) {
  if (a.index() != b.index())
    return false;
  return Match(a, [&](const auto &value) {
    return SameImport(value, *Get<std::decay_t<decltype(value)>>(b));
  });
}
std::expected<std::vector<RenderValueRef>, std::string>
SelectStackInputs(const CompositeStackStep &step, const RenderValue &control) {
  const auto *filter = Get<LayerFilter>(control);
  if (!filter)
    return std::unexpected("stack visibility is unavailable");
  std::vector<RenderValueRef> inputs;
  for (std::size_t i = 0; i < step.layers.size(); ++i) {
    if (filter->Hides(i))
      continue;
    const auto &layer = step.layers[i];
    inputs.push_back(layer.source);
    inputs.push_back(layer.opacity);
    if (layer.mask)
      inputs.push_back(*layer.mask);
    if (layer.color)
      inputs.push_back(*layer.color);
  }
  if (!inputs.empty())
    inputs.push_back(step.base);
  return inputs;
}
struct Arguments {
  std::span<const ResolvedRenderInput<RenderValue>> values;
  template <class T> const T *Find(RenderValueRef ref) const {
    const auto found = std::ranges::find_if(
        values, [&](const auto &value) { return value.input == ref; });
    return found == values.end() ? nullptr : Get<T>(found->value);
  }
};
TextureView View(std::shared_ptr<TextureLab::RenderTarget> target,
                 ValueType type = ValueType::kScalar) {
  TextureView view;
  view.texture = TextureRef{target};
  view.target = std::move(target);
  view.sampling.meshSpace = true;
  view.sampling.channel =
      type == ValueType::kScalar ? ShaderChannel::kR : ShaderChannel::kRgb;
  return view;
}
std::shared_ptr<TextureLab::RenderTarget>
AcquireStepTarget(RenderScratch &scratch, TextureRequirements requirements) {
  if (const auto target = scratch.target.lock())
    return target;
  const auto target = TextureLab::GetSingleton()->Acquire(
      requirements.size, "render step", requirements.format,
      requirements.mipPolicy);
  scratch.target = target;
  return target;
}
std::expected<RenderValue, std::string>
DrawProgram(const InterpreterProgram &program,
            std::span<const RenderValueRef> inputs,
            std::span<const RenderValueRef> lookups, const Arguments &arguments,
            TextureRequirements requirements, RenderScratch &scratch) {
  if (inputs.size() != program.Inputs().size() ||
      lookups.size() != program.FunctionLookups().size())
    return std::unexpected("interpreter binding count mismatch");
  TextureLab::InterpreterBindings bindings;
  bindings.inputCount = static_cast<std::uint32_t>(inputs.size());
  bindings.textureCount = static_cast<std::uint32_t>(program.TextureCount());
  bindings.lookupCount = static_cast<std::uint32_t>(lookups.size());
  if (inputs.size() > bindings.values.size() ||
      lookups.size() > bindings.lookups.size() ||
      bindings.textureCount > bindings.textures.size())
    return std::unexpected("interpreter binding limit exceeded");
  for (std::size_t i = 0; i < inputs.size(); ++i) {
    if (const auto *texture =
            Get<InterpreterTextureInput>(program.Inputs()[i])) {
      const auto *view = arguments.Find<TextureView>(inputs[i]);
      if (!view || !view->texture || texture->slot >= bindings.textures.size())
        return std::unexpected("interpreter texture is unavailable");
      bindings.textures[texture->slot] = {view->texture.get(), view->sampling,
                                          view->normalize};
    } else {
      const auto *value = arguments.Find<Value>(inputs[i]);
      if (!value)
        return std::unexpected("interpreter value is unavailable");
      bindings.values[i] = Match(
          *value, [](float v) { return Vec3{v, v, v}; },
          [](Vec2 v) { return Vec3{v.x, v.y, 0}; }, [](Vec3 v) { return v; });
    }
  }
  for (std::size_t i = 0; i < lookups.size(); ++i) {
    const auto *lookup =
        arguments.Find<std::shared_ptr<TextureLab::Lookup>>(lookups[i]);
    if (!lookup || !*lookup)
      return std::unexpected("interpreter lookup is unavailable");
    bindings.lookups[i] = lookup->get();
  }
  const auto target = AcquireStepTarget(scratch, requirements);
  if (!target ||
      !TextureLab::GetSingleton()->RenderProgram(*target, program, bindings))
    return std::unexpected("interpreter draw failed");
  return RenderValue{View(target, program.ResultType())};
}
constexpr std::uint64_t kReleaseAfterIdleTicks = 30;
std::string StepSpanKey(const RenderStep &step) {
  const auto requirements = RequirementsOf(step.kind);
  if (!requirements)
    return std::string{StepKindName(step.kind)};
  return std::format(
      "{} {} {}", StepKindName(step.kind), requirements->size.Pixels(),
      requirements->format == TextureFormat::kRgba32Float ? "f32" : "rgba8");
}
bool Releasable(const RenderStep &step, const RenderValue &value) {
  return !Is<CompositeStackStep>(step.kind) &&
         (Is<TextureView>(value) ||
          Is<std::shared_ptr<const BakeBuffers>>(value));
}
std::expected<RenderValue, std::string>
ExecuteStep(const RenderStep &step,
            std::span<const ResolvedRenderInput<RenderValue>> inputs,
            RenderScratch &scratch) {
  const Arguments args{inputs};
  auto *lab = TextureLab::GetSingleton();
  return Match(
      step.kind,
      [&](const UnavailableStep &k) -> std::expected<RenderValue, std::string> {
        return std::unexpected(k.problem);
      },
      [&](const ConstantRenderStep &k)
          -> std::expected<RenderValue, std::string> { return k.value; },
      [&](const BuildBakeBuffersStep &k)
          -> std::expected<RenderValue, std::string> {
        const auto *entry = args.Find<std::shared_ptr<MeshEntry>>(k.mesh);
        if (!entry || !*entry || !(*entry)->mesh)
          return std::unexpected("mesh is unavailable");
        BakeBuffers buffers;
        if (const auto *kind = Get<BakeKind>(k.operation)) {
          if (Is<ComponentIdBake>(*kind))
            buffers = BuildIslandBake(*(*entry)->mesh, (*entry)->analysis,
                                      IslandSource::kComponent);
          else if (Is<ChartIdBake>(*kind))
            buffers = BuildIslandBake(*(*entry)->mesh, (*entry)->analysis,
                                      IslandSource::kChart);
          else
            buffers = BuildBake(*(*entry)->mesh, *kind);
        } else {
          const auto *reference = Get<RenderValueRef>(k.operation);
          const auto *origin =
              reference ? args.Find<Value>(*reference) : nullptr;
          if (!origin || !Get<Vec3>(*origin))
            return std::unexpected("distance origin is unavailable");
          buffers = BuildDistanceBake(*(*entry)->mesh, AsVec3(*origin));
        }
        if (!buffers.problem.empty())
          return std::unexpected(buffers.problem);
        return std::make_shared<const BakeBuffers>(std::move(buffers));
      },
      [&](const BakeMeshStep &k) -> std::expected<RenderValue, std::string> {
        const auto *buffers =
            args.Find<std::shared_ptr<const BakeBuffers>>(k.buffers);
        const auto target = AcquireStepTarget(scratch, k.requirements);
        if (!buffers || !*buffers || !target ||
            !lab->BakeMesh(*target, **buffers))
          return std::unexpected("mesh bake failed");
        auto view = View(target, (*buffers)->vector ? ValueType::kVec3
                                                    : ValueType::kScalar);
        view.sampling.nearest = k.nearest;
        return view;
      },
      [&](const NormalSlopeStep &k) -> std::expected<RenderValue, std::string> {
        const auto *material = args.Find<MaterialInputs>(k.material);
        if (!material || !material->normal)
          return std::unexpected("normal map is unavailable");
        const auto target = AcquireStepTarget(scratch, k.requirements);
        TextureLab::LayerParams params;
        params.mode = TextureLab::Mode::kChannel;
        params.map = {material->normal.get(),
                      TextureLab::MapReading::kNormalSlope};
        params.channel.slope = true;
        if (!target || !lab->Render(*target, nullptr, params))
          return std::unexpected("normal slope draw failed");
        return View(target);
      },
      [&](const SubmitMaterialSampleStep &k)
          -> std::expected<RenderValue, std::string> {
        const auto *material = args.Find<MaterialInputs>(k.material);
        if (!material)
          return std::unexpected("material is unavailable");
        if (!lab->SubmitMaterialSample(material->rmaos.get(),
                                       material->diffuse.get(),
                                       scratch.material))
          return std::unexpected("material sampling failed");
        return RenderValue{Value{0.0f}};
      },
      [&](const ClusterMaterialStep &k)
          -> std::expected<RenderValue, std::string> {
        const auto *sample =
            args.Find<std::shared_ptr<const MaterialSample>>(k.sample);
        if (!sample || !*sample)
          return std::unexpected("material sample is unavailable");
        auto analysis = ClusterMaterial(**sample, k.settings);
        if (analysis.clusters.empty())
          return std::unexpected("material analysis has no clusters");
        return std::make_shared<const MaterialAnalysis>(std::move(analysis));
      },
      [&](const DrawClustersStep &k)
          -> std::expected<RenderValue, std::string> {
        const auto *material = args.Find<MaterialInputs>(k.material);
        const auto *analysis =
            args.Find<std::shared_ptr<const MaterialAnalysis>>(k.analysis);
        const auto target = AcquireStepTarget(scratch, k.requirements);
        if (!material || !analysis || !*analysis || !target ||
            !lab->RenderClusters(*target, material->rmaos.get(),
                                 material->diffuse.get(), **analysis))
          return std::unexpected("cluster draw failed");
        auto view = View(target);
        view.sampling.nearest = true;
        return view;
      },
      [&](const SampleFieldStep &k) -> std::expected<RenderValue, std::string> {
        if (!k.field.graph)
          return std::unexpected("field graph is unavailable");
        const auto *node = k.field.graph->NodeAt(k.field.output.node);
        if (!node)
          return std::unexpected("field operation is unavailable");
        TextureView view;
        if (const auto *image = Get<ImageOperation>(node->kind)) {
          const auto *texture = args.Find<TextureView>(k.texture);
          if (!texture || !texture->texture)
            return std::unexpected("image is unavailable");
          view = *texture;
          view.sampling.channel = ShaderChannelOf(image->channel);
          view.sampling.meshSpace = image->space == ImageSpace::kMesh;
          view.sampling.transform.sourceMip = image->mip;
          const auto *coordinateNode =
              k.field.graph->NodeAt(image->coordinates.node);
          const auto *coordinates =
              coordinateNode
                  ? Get<TextureCoordinatesOperation>(coordinateNode->kind)
                  : nullptr;
          if (coordinates) {
            view.sampling.transform.mirrorU = coordinates->mirror[0];
            view.sampling.transform.mirrorV = coordinates->mirror[1];
            view.sampling.transform.transpose = coordinates->transpose;
            std::size_t i = 0;
            const auto coordinate = [&]() -> std::optional<Vec2> {
              if (i >= k.coordinates.size())
                return std::nullopt;
              const auto *value = args.Find<Value>(k.coordinates[i++]);
              return value && Get<Vec2>(*value) ? std::optional{AsVec2(*value)}
                                                : std::nullopt;
            };
            if (coordinates->scroll) {
              auto v = coordinate();
              if (!v)
                return std::unexpected("scroll is unavailable");
              view.sampling.transform.uOffset = v->x;
              view.sampling.transform.vOffset = v->y;
            }
            if (coordinates->tile) {
              auto v = coordinate();
              if (!v)
                return std::unexpected("tile is unavailable");
              view.sampling.transform.tileU = std::max(v->x, 0.01f);
              view.sampling.transform.tileV = std::max(v->y, 0.01f);
            }
          }
        } else if (const auto *materialOp =
                       Get<MaterialOperation>(node->kind)) {
          const auto *material = args.Find<MaterialInputs>(k.texture);
          if (!material)
            return std::unexpected("material is unavailable");
          auto channel = materialOp->channel;
          if (channel == MaterialChannel::kRelief)
            channel = material->flatDisplacement
                          ? MaterialChannel::kOcclusion
                          : MaterialChannel::kDisplacement;
          view.texture = MaterialTexture(MaterialMapOf(channel), *material);
          view.sampling.channel = ShaderChannelOf(channel);
          view.sampling.meshSpace = true;
        } else
          return std::unexpected("unsupported field sampling operation");
        if (!view.texture)
          return std::unexpected("field texture is unavailable");
        const auto type = k.field.graph->OutputType(k.field.output);
        const auto *numeric = type ? Get<ValueType>(*type) : nullptr;
        if (!numeric)
          return std::unexpected("field is not numeric");
        const RenderValueRef reference = RenderInputRef{0};
        const std::array<ResolvedRenderInput<RenderValue>, 1> bound{
            {{reference, view, 0}}};
        const std::array refs{reference};
        return DrawProgram(InterpreterProgram::Sample(*numeric), refs, {},
                           Arguments{bound}, k.requirements, scratch);
      },
      [&](const SubmitReductionStep &k)
          -> std::expected<RenderValue, std::string> {
        const auto *view = args.Find<TextureView>(k.value);
        if (!view || !view->target)
          return std::unexpected("reduction field is unavailable");
        const auto extent = TextureLab::ExtentOf(view->texture.get());
        if (!extent || extent->width != k.domain.width ||
            extent->height != k.domain.height)
          return std::unexpected(
              "reduction field extent does not match its domain");
        if (!lab->SubmitReduction(*view->target, k.kind, k.type,
                                  scratch.reduction))
          return std::unexpected("reduction could not be drawn");
        return RenderValue{Value{0.0f}};
      },
      [&](const BuildLookupStep &k) -> std::expected<RenderValue, std::string> {
        const auto *function =
            k.graph ? k.graph->FunctionAt(k.function) : nullptr;
        if (!function || k.sampledParameter >= function->parameters.size())
          return std::unexpected("lookup function is unavailable");
        std::vector<Value> arguments(function->parameters.size(), Value{0.0f});
        for (const auto &bound : k.boundArguments) {
          const auto *value = args.Find<Value>(bound.value);
          if (!value || bound.parameter >= arguments.size())
            return std::unexpected("lookup argument is unavailable");
          arguments[bound.parameter] = *value;
        }
        std::array<float, 256> samples{};
        for (std::size_t i = 0; i < samples.size(); ++i) {
          arguments[k.sampledParameter] = static_cast<float>(i) / 255.0f;
          const auto value = EvaluateFunction(*k.graph, k.function, arguments);
          const auto *number = Get<float>(value);
          if (!number || !std::isfinite(*number))
            return std::unexpected("lookup produced an invalid scalar");
          samples[i] = *number;
        }
        auto lookup = lab->CreateLookup(samples);
        if (!lookup)
          return std::unexpected("lookup allocation failed");
        return lookup;
      },
      [&](const EvaluateValueStep &k)
          -> std::expected<RenderValue, std::string> {
        const auto *node = k.value.graph
                               ? k.value.graph->NodeAt(k.value.output.node)
                               : nullptr;
        if (!node)
          return std::unexpected("uniform operation is unavailable");
        const auto refs = InputsOf(node->kind);
        bool available = refs.size() == k.inputs.size();
        const auto read = [&](OutputRef ref) -> Value {
          for (std::size_t i = 0; i < refs.size() && i < k.inputs.size(); ++i)
            if (refs[i] == ref) {
              if (const auto *value = args.Find<Value>(k.inputs[i]))
                return *value;
            }
          available = false;
          return 0.0f;
        };
        const auto call = [&](FunctionId function,
                              std::span<const OutputRef> refs) {
          std::vector<Value> values;
          for (auto ref : refs)
            values.push_back(read(ref));
          return EvaluateFunction(*k.value.graph, function, values);
        };
        const Value result = Match(
            node->kind,
            [&](const ExpressionOperation &operation) -> Value {
              std::vector<Value> values;
              for (auto ref : operation.expression.valueBindings)
                values.push_back(read(ref));
              Program::Inputs inputs;
              inputs.refs = values;
              inputs.callFunction = [&](std::size_t index, float x, float) {
                if (index >= operation.expression.functionBindings.size()) {
                  available = false;
                  return 0.0f;
                }
                const auto &binding =
                    operation.expression.functionBindings[index];
                const auto *function =
                    k.value.graph->FunctionAt(binding.function);
                if (!function ||
                    binding.sampledParameter >= function->parameters.size()) {
                  available = false;
                  return 0.0f;
                }
                std::vector<Value> args(function->parameters.size(),
                                        Value{0.0f});
                args[binding.sampledParameter] = x;
                for (const auto &arg : binding.arguments) {
                  if (arg.parameter >= args.size()) {
                    available = false;
                    return 0.0f;
                  }
                  args[arg.parameter] = read(arg.value);
                }
                return AsScalar(
                    EvaluateFunction(*k.value.graph, binding.function, args));
              };
              return operation.expression.program.Evaluate(inputs);
            },
            [&](const CallOperation &operation) {
              return call(operation.function, operation.arguments);
            },
            [&](const MapFunctionOperation &operation) {
              return call(operation.function, operation.arguments);
            },
            [&](const VectorOperation &operation) -> Value {
              if (operation.components.size() == 2)
                return Vec2{AsScalar(read(operation.components[0])),
                            AsScalar(read(operation.components[1]))};
              if (operation.components.size() == 3)
                return Vec3{AsScalar(read(operation.components[0])),
                            AsScalar(read(operation.components[1])),
                            AsScalar(read(operation.components[2]))};
              available = false;
              return 0.0f;
            },
            [&](const auto &) -> Value {
              available = false;
              return 0.0f;
            });
        const auto vector = AsVec3(result);
        if (!available || TypeOf(result) != k.type ||
            !std::isfinite(vector.x) || !std::isfinite(vector.y) ||
            !std::isfinite(vector.z))
          return std::unexpected("uniform evaluation failed");
        return result;
      },
      [&](const EvaluateProgramStep &k) {
        return DrawProgram(k.program, k.inputs, k.lookups, args, k.requirements,
                           scratch);
      },
      [&](const MapFieldStep &k) {
        const std::array refs{k.value}, lookups{k.lookup};
        return DrawProgram(InterpreterProgram::Map(), refs, lookups, args,
                           k.requirements, scratch);
      },
      [&](const ComposeVectorStep &k)
          -> std::expected<RenderValue, std::string> {
        if (k.components.size() > 3)
          return std::unexpected("invalid vector width");
        std::array<bool, 3> textures{};
        for (std::size_t i = 0; i < k.components.size(); ++i)
          textures[i] = args.Find<TextureView>(k.components[i]) != nullptr;
        auto program = InterpreterProgram::Compose(
            std::span{textures}.first(k.components.size()));
        if (!program)
          return std::unexpected(program.error());
        return DrawProgram(*program, k.components, {}, args, k.requirements,
                           scratch);
      },
      [&](const DrawRippleStep &k) -> std::expected<RenderValue, std::string> {
        const auto *positions = args.Find<TextureView>(k.positions);
        const auto *node = k.field.graph
                               ? k.field.graph->NodeAt(k.field.output.node)
                               : nullptr;
        const auto *operation =
            node ? Get<RippleOperation>(node->kind) : nullptr;
        if (!positions || !positions->texture || !operation)
          return std::unexpected("ripple inputs are unavailable");
        const auto operands = InputsOf(node->kind);
        std::vector<OutputRef> fields;
        for (auto ref : operands)
          if (ref != operation->coordinates)
            fields.push_back(ref);
        const auto refOf =
            [&](OutputRef output) -> std::optional<RenderValueRef> {
          for (std::size_t i = 0; i < fields.size() && i < k.inputs.size(); ++i)
            if (fields[i] == output)
              return k.inputs[i];
          return std::nullopt;
        };
        const auto number = [&](OutputRef output) -> const Value * {
          const auto ref = refOf(output);
          return ref ? args.Find<Value>(*ref) : nullptr;
        };
        const auto meshRef = refOf(operation->geometry);
        const auto *mesh =
            meshRef ? args.Find<std::shared_ptr<MeshEntry>>(*meshRef) : nullptr;
        if (!mesh || !*mesh || !(*mesh)->mesh)
          return std::unexpected("ripple geometry is unavailable");
        const auto firingRef = refOf(operation->firings);
        const auto *firings =
            firingRef ? args.Find<RenderFirings>(*firingRef) : nullptr;
        const auto *speed = number(operation->speed),
                   *width = number(operation->width),
                   *decay = number(operation->decay),
                   *time = number(operation->time),
                   *direction = number(operation->direction);
        if (!firings || !speed || !width || !decay || !time || !direction)
          return std::unexpected("ripple parameters are unavailable");
        TextureLab::RipplePass pass;
        pass.positions = positions->texture.get();
        pass.frame = kPositionFrame;
        pass.speed = AsScalar(*speed);
        pass.width = AsScalar(*width);
        pass.decay = AsScalar(*decay);
        pass.disc = operation->shape == RippleShape::kDisc;
        const auto d = AsVec3(*direction);
        const float length = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (length > 1e-4f) {
          pass.directional = true;
          pass.direction = {d.x / length, d.y / length, d.z / length};
        }
        for (const auto &firing : firings->firings) {
          if (pass.firingCount >= pass.firings.size())
            break;
          pass.firings[pass.firingCount++] = {
              firing.origin.value_or((*mesh)->mesh->center),
              std::max(0.0f, AsScalar(*time) - firing.startTime)};
        }
        const auto target = AcquireStepTarget(scratch, k.requirements);
        if (!target || !lab->RenderRipple(*target, pass))
          return std::unexpected("ripple draw failed");
        return View(target);
      },
      [&](const CompositeStackStep &k)
          -> std::expected<RenderValue, std::string> {
        const auto *filter = args.Find<LayerFilter>(k.visibility);
        if (!filter)
          return std::unexpected("stack visibility is unavailable");
        std::vector<const PlannedLayer *> layers;
        for (std::size_t i = 0; i < k.layers.size(); ++i)
          if (!filter->Hides(i))
            layers.push_back(&k.layers[i]);
        if (layers.empty())
          return StackResult{};
        const auto *base = args.Find<StackResult>(k.base);
        if (!base)
          return std::unexpected("stack base is unavailable");
        auto layerRequirements = k.requirements;
        layerRequirements.mipPolicy = MipPolicy::kNone;
        const auto target = AcquireStepTarget(scratch, layerRequirements);
        if (!target)
          return std::unexpected("stack target is unavailable");
        auto *alternate =
            layers.size() > 1
                ? lab->Scratch(k.requirements.size, k.requirements.format,
                               MipPolicy::kNone)
                : nullptr;
        if (layers.size() > 1 && !alternate)
          return std::unexpected("stack alternate target is unavailable");
        TextureRef previous = base->texture;
        auto *write = layers.size() % 2 ? target.get() : alternate;
        auto *other = layers.size() % 2 ? alternate : target.get();
        for (const auto *layer : layers) {
          TextureLab::LayerParams params;
          params.mode = TextureLab::Mode::kLayer;
          auto &pass = params.layer;
          pass.previous = previous.get();
          const auto *source = args.Find<TextureView>(layer->source);
          const auto *value = args.Find<Value>(layer->source);
          const auto *opacity = args.Find<Value>(layer->opacity);
          if (!opacity || (!source && !value))
            return std::unexpected(
                "visible layer source or opacity is unavailable");
          Vec3 color{1, 1, 1};
          if (source) {
            if (!source->texture)
              return std::unexpected("visible layer texture is unavailable");
            pass.source = source->texture.get();
            pass.input = source->sampling;
            pass.normalize = source->normalize;
          } else
            color = AsVec3(*value);
          if (layer->color) {
            const auto *tint = args.Find<Value>(*layer->color);
            if (!tint)
              return std::unexpected("visible layer color is unavailable");
            const auto c = AsVec3(*tint);
            color = {color.x * c.x, color.y * c.y, color.z * c.z};
          }
          pass.color[0] = color.x;
          pass.color[1] = color.y;
          pass.color[2] = color.z;
          pass.opacity = AsScalar(*opacity);
          pass.blend = BlendShaderMode(layer->blend);
          pass.channels =
              (layer->channels.r ? 1u : 0u) | (layer->channels.g ? 2u : 0u) |
              (layer->channels.b ? 4u : 0u) | (layer->channels.a ? 8u : 0u);
          if (layer->mask) {
            const auto *mask = args.Find<TextureView>(*layer->mask);
            if (!mask || !mask->texture)
              return std::unexpected("visible layer mask is unavailable");
            pass.mask = mask->texture.get();
            pass.maskChannel = mask->sampling.channel;
          }
          if (!write || !lab->Render(*write, nullptr, params))
            return std::unexpected("stack draw failed");
          previous = TextureRef{write->Texture()};
          std::swap(write, other);
        }
        if (k.requirements.mipPolicy == MipPolicy::kGenerate)
          lab->GenerateMipsFor(*target);
        return StackResult{previous};
      });
}
}
RenderInstance::RenderInstance(
    RenderPlan plan, std::vector<GeometryInputs> inputs,
    std::vector<std::shared_ptr<const RecipeGraph>> graphs)
    : graphs_(std::move(graphs)), geometries_(std::move(inputs)),
      execution_(std::move(plan)) {
  for (auto &geometry : geometries_)
    geometry.render.reset();
}
const RenderPlan &RenderInstance::Plan() const noexcept {
  return execution_.Plan();
}
std::expected<void, std::string>
RenderInstance::UpdateInput(RenderInputId input, RenderValue value) {
  if (input >= execution_.Inputs().size())
    return std::unexpected("invalid imported render value");
  const auto actual = Match(
      value, [](const Value &v) -> RenderValueType { return TypeOf(v); },
      [](const TextureView &) -> RenderValueType {
        return RenderResourceType::kTexture;
      },
      [](const std::shared_ptr<MeshEntry> &) -> RenderValueType {
        return RenderResourceType::kMesh;
      },
      [](const MaterialInputs &) -> RenderValueType {
        return RenderResourceType::kMaterial;
      },
      [](const RenderTransform &) -> RenderValueType {
        return RenderResourceType::kTransform;
      },
      [](const RenderFirings &) -> RenderValueType {
        return RenderResourceType::kFirings;
      },
      [](const std::shared_ptr<const BakeBuffers> &) -> RenderValueType {
        return RenderResourceType::kBakeBuffers;
      },
      [](const std::shared_ptr<const MaterialSample> &) -> RenderValueType {
        return RenderResourceType::kMaterialSample;
      },
      [](const std::shared_ptr<const MaterialAnalysis> &) -> RenderValueType {
        return RenderResourceType::kMaterialAnalysis;
      },
      [](const std::shared_ptr<TextureLab::Lookup> &) -> RenderValueType {
        return RenderResourceType::kLookup;
      },
      [](const LayerFilter &) -> RenderValueType {
        return RenderResourceType::kVisibility;
      },
      [](const StackResult &) -> RenderValueType {
        return RenderResourceType::kStack;
      });
  if (actual != Plan().inputs[input].type) {
    execution_.UnsetInput(input);
    return std::unexpected("imported render value has an incompatible type");
  }
  if (const auto *numeric = Get<Value>(value)) {
    const auto number = AsVec3(*numeric);
    if (!std::isfinite(number.x) || !std::isfinite(number.y) ||
        !std::isfinite(number.z)) {
      execution_.UnsetInput(input);
      return std::unexpected("imported numeric value is not finite");
    }
  }
  const auto &current = execution_.Inputs()[input].value;
  return execution_.SetInput(input, value,
                             !current || !SameImportedValue(*current, value));
}
std::expected<void, std::string>
RenderInstance::Update(const RecipeGraph &graph, std::size_t instance,
                       const SignalState &signals) {
  for (std::size_t i = 0; i < Plan().inputs.size(); ++i) {
    const auto &input = Plan().inputs[i];
    const auto *binding = Get<TextureValue>(input.binding);
    if (!binding || binding->graph != &graph || binding->instance != instance)
      continue;
    const auto *node = graph.NodeAt(binding->output.node);
    const GeometryInputs *geometry = GeometryOf(input.geometry);
    if (!node || !geometry) {
      execution_.UnsetInput(i);
      continue;
    }
    const auto *external = Get<ExternalInput>(node->kind);
    std::optional<RenderValue> value;
    bool stableResource = false;
    if (const auto *numberType = Get<ValueType>(input.type)) {
      const auto *position =
          external ? Get<NodePositionInput>(external->source) : nullptr;
      if (position) {
        if (const auto origin = NodeBindPosition(
                geometry->geometry.get(), geometry->root.get(), position->name))
          value = Value{*origin};
      } else {
        const auto number = signals.ValueOf(binding->output);
        const auto v = AsVec3(number);
        if (TypeOf(number) == *numberType && std::isfinite(v.x) &&
            std::isfinite(v.y) && std::isfinite(v.z))
          value = number;
      }
    } else if (input.type == RenderValueType{RenderResourceType::kFirings}) {
      RenderFirings firings;
      const auto name = graph.NameOf(binding->output.node);
      for (const auto &firing : signals.Firings(binding->output)) {
        if (firings.firings.size() >= TextureLab::kRippleFirings)
          break;
        auto origin = Match(
            signals.AnchorOf(name, firing),
            [](const std::monostate &) { return std::optional<Vec3>{}; },
            [&](const CarriedPoint &point) {
              return std::optional{
                  ToRootSpace(geometry->root.get(), point.position)};
            },
            [&](const AnchorNode &anchor) {
              return NodeBindPosition(geometry->geometry.get(),
                                      geometry->root.get(), anchor.node);
            });
        firings.firings.push_back({origin, firing.startTime});
      }
      value = std::move(firings);
    } else if (external) {
      if (Is<GeometryInput>(external->source)) {
        stableResource = true;
        if (execution_.Inputs()[i].value)
          continue;
        const auto mesh =
            Compositor::GetSingleton()->MeshOf(geometry->geometry.get());
        if (mesh)
          value = *mesh;
      } else if (Is<MaterialInput>(external->source)) {
        stableResource = true;
        value = geometry->material;
      } else if (const auto *texture = Get<TextureInput>(external->source)) {
        stableResource = true;
        if (execution_.Inputs()[i].value)
          continue;
        const auto loaded =
            Compositor::GetSingleton()->LoadImage(texture->path);
        if (loaded)
          value = TextureView{loaded};
      } else if (Is<RootTransformInput>(external->source))
        value = RenderTransform{geometry->root};
    }
    if (!value) {
      execution_.UnsetInput(i);
      continue;
    }
    if (stableResource && execution_.Inputs()[i].value)
      continue;
    if (auto set = UpdateInput(i, std::move(*value)); !set)
      return set;
  }
  return {};
}
std::expected<StackOutcome, std::string>
RenderInstance::Render(StepOutputRef output, const LayerFilter &filter,
                       const StackBase &base) {
  if (output.step >= Plan().steps.size())
    return std::unexpected("invalid stack output");
  const auto *stack = Get<CompositeStackStep>(Plan().steps[output.step].kind);
  if (!stack)
    return std::unexpected("output is not a stack");
  const auto *baseInput = Get<RenderInputRef>(stack->base);
  const auto *visibility = Get<RenderInputRef>(stack->visibility);
  if (!baseInput || !visibility || baseInput->input >= Plan().inputs.size())
    return std::unexpected("stack controls must be imported");
  const GeometryInputs *geometry =
      GeometryOf(Plan().inputs[baseInput->input].geometry);
  if (!geometry)
    return std::unexpected("stack geometry is unavailable");
  TextureRef texture = base.texture;
  const bool neutralHeight =
      stack->slot == Slot::kHeight && geometry->material.flatDisplacement;
  if (!texture && !neutralHeight) {
    const auto material =
        MaterialTexture(BaseMapOf(stack->slot), geometry->material);
    if (IsNonPlaceholderTexture(material))
      texture = material;
  }
  if (!texture && neutralHeight) {
    const auto neutral = Compositor::GetSingleton()->NeutralHeight();
    if (!neutral)
      return std::unexpected("neutral height base is unavailable");
    texture = TextureRef{neutral};
  }
  if (auto set =
          UpdateInput(baseInput->input,
                      StackResult{texture, base.texture ? base.contentVersion
                                                        : ChangeVersion{0}});
      !set)
    return std::unexpected(set.error());
  if (auto set = UpdateInput(visibility->input, filter); !set)
    return std::unexpected(set.error());
  Metrics::CountRenderEvaluation();
  const auto result = Demand(output);
  if (!result && AwaitingFirstReadback())
    return StackOutcome{StackPending{}};
  if (!result)
    return std::unexpected(result.error());
  const auto *value = Get<StackResult>(result->value);
  if (!value)
    return std::unexpected("stack returned the wrong type");
  return StackOutcome{StackResult{value->texture, result->changeVersion}};
}
void RenderInstance::CollectReadback(RenderInputId input) {
  if (input >= Plan().inputs.size())
    return;
  const auto *binding = Get<ReadbackBinding>(Plan().inputs[input].binding);
  if (!binding || binding->submission >= Plan().steps.size())
    return;
  auto *scratch = execution_.ScratchOf(binding->submission);
  if (!scratch)
    return;
  auto *lab = TextureLab::GetSingleton();
  const auto &kind = Plan().steps[binding->submission].kind;
  const auto import = [&](auto &&collected, auto &&toValue) {
    if (!collected)
      return;
    if (!*collected) {
      execution_.UnsetInput(input);
      return;
    }
    static_cast<void>(UpdateInput(input, toValue(std::move(**collected))));
  };
  if (const auto *reduction = Get<SubmitReductionStep>(kind))
    import(lab->CollectReduction(
               reduction->kind, reduction->type,
               {reduction->domain.width, reduction->domain.height},
               scratch->reduction),
           [](Value value) { return RenderValue{value}; });
  else if (Is<SubmitMaterialSampleStep>(kind))
    import(lab->CollectMaterialSample(scratch->material),
           [](MaterialSample sample) {
             return RenderValue{
                 std::make_shared<const MaterialSample>(std::move(sample))};
           });
}
void RenderInstance::CollectReadbacks() {
  for (RenderInputId input = 0; input < Plan().inputs.size(); ++input)
    if (Is<ReadbackBinding>(Plan().inputs[input].binding))
      CollectReadback(input);
}
bool RenderInstance::AwaitingFirstReadback() const {
  for (RenderInputId input = 0; input < Plan().inputs.size(); ++input) {
    const auto *binding = Get<ReadbackBinding>(Plan().inputs[input].binding);
    if (!binding || execution_.Inputs()[input].value ||
        binding->submission >= execution_.Steps().size())
      continue;
    const auto &scratch = execution_.Steps()[binding->submission].scratch;
    if (!PendingNewestFirst(scratch.reduction.ring).empty() ||
        scratch.material.pending)
      return true;
  }
  return false;
}
std::expected<ResolvedRenderInput<RenderValue>, std::string>
RenderInstance::Demand(RenderValueRef output) {
  return execution_.Evaluate(
      output,
      [](const RenderStep &step,
         std::span<const ResolvedRenderInput<RenderValue>> inputs,
         RenderScratch &scratch) {
        Metrics::CountStepExecution();
        auto *lab = TextureLab::GetSingleton();
        std::optional<TextureLab::TimedSpan> span;
        if (lab->Timing())
          span.emplace(*lab, StepSpanKey(step));
        return ExecuteStep(step, inputs, scratch);
      },
      SameRenderValue, SelectStackInputs);
}
const GeometryInputs *RenderInstance::GeometryOf(GeometryId id) const noexcept {
  return IndexOf(id) < geometries_.size() ? &geometries_[IndexOf(id)] : nullptr;
}
bool RenderInstance::BeginFrame(std::uint64_t frame) {
  if (frame_ == frame)
    return false;
  frame_ = frame;
  execution_.AdvanceEpoch();
  Metrics::CountStepReleases(
      execution_.ReleaseIdle(kReleaseAfterIdleTicks, Releasable));
  Metrics::CountStepRestores(execution_.Restores() - restoresReported_);
  restoresReported_ = execution_.Restores();
  CollectReadbacks();
  return true;
}
std::optional<TextureView> RenderInstance::Texture(RenderValueRef output) {
  const std::optional<RenderValue> *value = nullptr;
  if (const auto *ref = Get<StepOutputRef>(output);
      ref && ref->output == 0 && ref->step < execution_.Steps().size()) {
    if (execution_.Steps()[ref->step].released)
      static_cast<void>(Demand(output));
    execution_.Touch(ref->step);
    value = &execution_.Steps()[ref->step].outputs.front().value;
  }
  if (const auto *ref = Get<RenderInputRef>(output);
      ref && ref->input < execution_.Inputs().size())
    value = &execution_.Inputs()[ref->input].value;
  if (value && *value)
    if (const auto *texture = Get<TextureView>(**value)) {
      if (texture->target && texture->target->Mips() == MipPolicy::kNone)
        TextureLab::GetSingleton()->GenerateMipsFor(*texture->target);
      return *texture;
    }
  return std::nullopt;
}
std::optional<TextureView>
RenderInstance::Inspect(const RecipeGraph &graph, OutputRef output,
                        std::size_t instance, const RE::BSGeometry *geometry) {
  for (const auto &binding : Plan().values)
    if (binding.value == TextureValue{&graph, output, instance} &&
        GeometryOf(binding.geometry) &&
        GeometryOf(binding.geometry)->geometry.get() == geometry)
      if (const auto texture = Texture(binding.result))
        return texture;
  return std::nullopt;
}
}
