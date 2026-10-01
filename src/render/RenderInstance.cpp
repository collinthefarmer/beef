// GPL-3.0-only with the additional permission in COPYING.md.
#include "render/RenderInstance.h"

#include "diagnostics/Metrics.h"

#include "render/MeshReader.h"
#include "render/SourceSampling.h"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace BetterEnchantmentEffects {
namespace {
struct Arguments {
  std::span<const ResolvedRenderInput<RenderValue>> values;
  template <class T> [[nodiscard]] const T *Find(RenderValueRef ref) const {
    const auto found = std::ranges::find_if(
        values, [&](const auto &value) { return value.input == ref; });
    return found == values.end() ? nullptr : Get<T>(found->value);
  }
};
struct StepExecution {
  Arguments arguments;
  RenderScratch &scratch;
  TextureLab &lab;
};
struct ProgramOperands {
  std::span<const RenderValueRef> inputs;
  std::span<const RenderValueRef> lookups;
};
struct ValueOperands {
  const RecipeGraph &graph;
  std::span<const OutputRef> refs;
  std::span<const RenderValueRef> inputs;
  const Arguments &arguments;
  bool available = true;
};
struct RippleOperands {
  std::vector<OutputRef> fields;
  std::span<const RenderValueRef> inputs;
  const Arguments &arguments;
};
using StepResult = std::expected<RenderValue, std::string>;
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
    std::ranges::copy(LayerOperands(step, step.layers[i]),
                      std::back_inserter(inputs));
  }
  if (!inputs.empty())
    inputs.push_back(step.base);
  return inputs;
}
TextureLab::LayerInput LayerFieldSampling() {
  TextureLab::LayerInput input;
  input.meshSpace = true;
  input.channel = ShaderChannel::kRgb;
  return input;
}
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
AcquireStepTarget(StepExecution &execution, TextureRequirements requirements) {
  if (const auto target = execution.scratch.target.lock())
    return target;
  const auto target =
      execution.lab.Acquire(requirements.size, "render step",
                            requirements.format, requirements.mipPolicy);
  execution.scratch.target = target;
  return target;
}
Vec3 ProgramValueOf(const Value &value) {
  return Match(
      value, [](float v) { return Vec3{v, v, v}; },
      [](Vec2 v) { return Vec3{v.x, v.y, 0}; }, [](Vec3 v) { return v; });
}
bool WithinBindingLimits(const TextureLab::ProgramBindings &bindings,
                         std::span<const ProgramInput> slots,
                         std::size_t textureCount,
                         const ProgramOperands &operands) {
  return operands.inputs.size() == slots.size() &&
         operands.inputs.size() <= bindings.values.size() &&
         operands.lookups.size() <= bindings.lookups.size() &&
         textureCount <= bindings.textures.size();
}
std::expected<void, std::string>
BindProgramTexture(TextureLab::ProgramBindings &bindings,
                   const ProgramTextureInput &texture, RenderValueRef input,
                   const Arguments &arguments) {
  const auto *view = arguments.Find<TextureView>(input);
  if (!view || !view->texture || texture.slot >= bindings.textures.size())
    return std::unexpected("program texture is unavailable");
  bindings.textures[texture.slot] = {view->texture.get(), view->sampling,
                                     view->normalize};
  return {};
}
std::expected<void, std::string> BindProgramInputs(
    TextureLab::ProgramBindings &bindings, std::span<const ProgramInput> slots,
    std::span<const RenderValueRef> inputs, const Arguments &arguments) {
  for (std::size_t i = 0;
       i < inputs.size() && i < slots.size() && i < bindings.values.size();
       ++i) {
    if (const auto *texture = Get<ProgramTextureInput>(slots[i])) {
      if (auto bound =
              BindProgramTexture(bindings, *texture, inputs[i], arguments);
          !bound)
        return bound;
      continue;
    }
    const auto *value = arguments.Find<Value>(inputs[i]);
    if (!value)
      return std::unexpected("program value is unavailable");
    bindings.values[i] = ProgramValueOf(*value);
  }
  return {};
}
std::expected<void, std::string>
BindProgramLookups(TextureLab::ProgramBindings &bindings,
                   std::span<const RenderValueRef> lookups,
                   const Arguments &arguments) {
  for (std::size_t i = 0; i < lookups.size() && i < bindings.lookups.size();
       ++i) {
    const auto *lookup =
        arguments.Find<std::shared_ptr<TextureLab::Lookup>>(lookups[i]);
    if (!lookup || !*lookup)
      return std::unexpected("program lookup is unavailable");
    bindings.lookups[i] = lookup->get();
  }
  return {};
}
std::expected<TextureLab::ProgramBindings, std::string>
BindProgram(std::span<const ProgramInput> slots, std::size_t textureCount,
            const ProgramOperands &operands, const Arguments &arguments) {
  TextureLab::ProgramBindings bindings;
  if (!WithinBindingLimits(bindings, slots, textureCount, operands))
    return std::unexpected("program binding limit exceeded");
  bindings.inputCount = static_cast<std::uint32_t>(operands.inputs.size());
  bindings.textureCount = static_cast<std::uint32_t>(textureCount);
  bindings.lookupCount = static_cast<std::uint32_t>(operands.lookups.size());
  if (auto bound =
          BindProgramInputs(bindings, slots, operands.inputs, arguments);
      !bound)
    return std::unexpected(bound.error());
  if (auto bound = BindProgramLookups(bindings, operands.lookups, arguments);
      !bound)
    return std::unexpected(bound.error());
  return bindings;
}
StepResult DrawProgram(const FieldProgram &program,
                       const ProgramOperands &operands,
                       TextureRequirements requirements,
                       StepExecution &execution) {
  if (operands.lookups.size() != program.FunctionLookups().size())
    return std::unexpected("program binding count mismatch");
  const auto bindings = BindProgram(program.Inputs(), program.TextureCount(),
                                    operands, execution.arguments);
  if (!bindings)
    return std::unexpected(bindings.error());
  const auto target = AcquireStepTarget(execution, requirements);
  if (!target || !execution.lab.RenderProgram(*target, program, *bindings))
    return std::unexpected("program draw failed");
  return RenderValue{View(target, program.ResultType())};
}
std::expected<const PackedLayerFields *, std::string>
LayerFieldPackFor(const CompositeStackStep &stack,
                  std::vector<std::size_t> fields, RenderScratch &scratch) {
  if (scratch.layerFieldPack && scratch.layerFieldPack->fields == fields)
    return &scratch.layerFieldPack->packed;
  std::vector<const LayerField *> used;
  for (const auto field : fields) {
    if (field >= stack.fields.size())
      return std::unexpected("visible layer field is missing");
    used.push_back(&stack.fields[field]);
  }
  auto packed = PackLayerFields(used);
  if (!packed)
    return std::unexpected(packed.error());
  scratch.layerFieldPack =
      LayerFieldPack{std::move(fields), std::move(*packed)};
  return &scratch.layerFieldPack->packed;
}
std::expected<TextureLab::BoundLayerFields, std::string>
BindLayerFields(const PackedLayerFields &packed, const Arguments &arguments) {
  auto bindings =
      BindProgram(packed.pack.inputs, packed.pack.textureCount,
                  ProgramOperands{packed.inputs, packed.lookups}, arguments);
  if (!bindings)
    return std::unexpected(bindings.error());
  return TextureLab::BoundLayerFields{packed.pack, *bindings};
}
std::uint32_t ChannelBits(ChannelSet channels) {
  return (channels.r ? 1u : 0u) | (channels.g ? 2u : 0u) |
         (channels.b ? 4u : 0u) | (channels.a ? 8u : 0u);
}
std::expected<TextureLab::LayerPass, std::string>
LayerPassFor(const PlannedLayer &layer, const Arguments &args,
             std::span<const std::size_t> fields) {
  TextureLab::LayerPass pass;
  const auto *opacity = args.Find<Value>(layer.opacity);
  if (!opacity)
    return std::unexpected("visible layer opacity is unavailable");
  Vec3 color{1, 1, 1};
  if (const auto segment = SegmentIndexOf(fields, layer.source)) {
    pass.sourceSegment = *segment;
    pass.input = LayerFieldSampling();
  } else if (const auto *ref = Get<RenderValueRef>(layer.source)) {
    if (const auto *source = args.Find<TextureView>(*ref)) {
      if (!source->texture)
        return std::unexpected("visible layer texture is unavailable");
      pass.source = source->texture.get();
      pass.input = source->sampling;
      pass.normalize = source->normalize;
    } else if (const auto *value = args.Find<Value>(*ref))
      color = AsVec3(*value);
    else
      return std::unexpected("visible layer source is unavailable");
  } else
    return std::unexpected("visible layer field is unavailable");
  if (layer.color) {
    const auto *tint = args.Find<Value>(*layer.color);
    if (!tint)
      return std::unexpected("visible layer color is unavailable");
    const auto c = AsVec3(*tint);
    color = {color.x * c.x, color.y * c.y, color.z * c.z};
  }
  pass.color[0] = color.x;
  pass.color[1] = color.y;
  pass.color[2] = color.z;
  pass.opacity = AsScalar(*opacity);
  pass.blend = BlendShaderMode(layer.blend);
  pass.channels = ChannelBits(layer.channels);
  if (!layer.mask)
    return pass;
  if (const auto segment = SegmentIndexOf(fields, *layer.mask)) {
    pass.maskSegment = *segment;
    pass.maskChannel = ShaderChannel::kRgb;
  } else if (const auto *ref = Get<RenderValueRef>(*layer.mask)) {
    const auto *mask = args.Find<TextureView>(*ref);
    if (!mask || !mask->texture)
      return std::unexpected("visible layer mask is unavailable");
    pass.mask = mask->texture.get();
    pass.maskChannel = mask->sampling.channel;
  } else
    return std::unexpected("visible layer mask field is unavailable");
  return pass;
}
std::vector<const PlannedLayer *> VisibleLayers(const CompositeStackStep &stack,
                                                const LayerFilter &filter) {
  std::vector<const PlannedLayer *> layers;
  for (std::size_t i = 0; i < stack.layers.size(); ++i)
    if (!filter.Hides(i))
      layers.push_back(&stack.layers[i]);
  return layers;
}
std::expected<std::vector<TextureLab::LayerPass>, std::string>
LayerPassesFor(std::span<const PlannedLayer *const> layers,
               const Arguments &arguments,
               std::span<const std::size_t> fields) {
  std::vector<TextureLab::LayerPass> passes;
  for (const auto *layer : layers) {
    if (!layer)
      return std::unexpected("visible layer is missing");
    auto pass = LayerPassFor(*layer, arguments, fields);
    if (!pass)
      return std::unexpected(pass.error());
    passes.push_back(*pass);
  }
  return passes;
}
constexpr std::uint64_t kReleaseAfterIdleMS = 500;
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
BakeBuffers BakeBuffersFor(const MeshData &mesh, const MeshAnalysis &analysis,
                           const BakeKind &kind) {
  if (Is<ComponentIdBake>(kind))
    return BuildIslandBake(mesh, analysis, IslandSource::kComponent);
  if (Is<ChartIdBake>(kind))
    return BuildIslandBake(mesh, analysis, IslandSource::kChart);
  return BuildBake(mesh, kind);
}
std::expected<Vec3, std::string>
DistanceOriginOf(const std::variant<BakeKind, RenderValueRef> &operation,
                 const Arguments &arguments) {
  const auto *reference = Get<RenderValueRef>(operation);
  const auto *origin = reference ? arguments.Find<Value>(*reference) : nullptr;
  if (!origin || !Get<Vec3>(*origin))
    return std::unexpected("distance origin is unavailable");
  return AsVec3(*origin);
}
StepResult ExecuteBuildBakeBuffersStep(const BuildBakeBuffersStep &step,
                                       StepExecution &execution) {
  const auto *entry =
      execution.arguments.Find<std::shared_ptr<MeshEntry>>(step.mesh);
  if (!entry || !*entry || !(*entry)->mesh)
    return std::unexpected("mesh is unavailable");
  const MeshEntry &mesh = **entry;
  BakeBuffers buffers;
  if (const auto *kind = Get<BakeKind>(step.operation))
    buffers = BakeBuffersFor(*mesh.mesh, mesh.analysis, *kind);
  else {
    const auto origin = DistanceOriginOf(step.operation, execution.arguments);
    if (!origin)
      return std::unexpected(origin.error());
    buffers = BuildDistanceBake(*mesh.mesh, *origin);
  }
  if (!buffers.problem.empty())
    return std::unexpected(buffers.problem);
  return std::make_shared<const BakeBuffers>(std::move(buffers));
}
StepResult ExecuteBakeMeshStep(const BakeMeshStep &step,
                               StepExecution &execution) {
  const auto *buffers =
      execution.arguments.Find<std::shared_ptr<const BakeBuffers>>(
          step.buffers);
  const auto target = AcquireStepTarget(execution, step.requirements);
  if (!buffers || !*buffers || !target ||
      !execution.lab.BakeMesh(*target, **buffers))
    return std::unexpected("mesh bake failed");
  auto view =
      View(target, (*buffers)->vector ? ValueType::kVec3 : ValueType::kScalar);
  view.sampling.nearest = step.nearest;
  return view;
}
TextureLab::LayerParams NormalSlopeParamsFor(const MaterialInputs &material) {
  TextureLab::LayerParams params;
  params.mode = TextureLab::Mode::kChannel;
  params.map = {material.normal.get(), TextureLab::MapReading::kNormalSlope};
  params.channel.slope = true;
  return params;
}
StepResult ExecuteNormalSlopeStep(const NormalSlopeStep &step,
                                  StepExecution &execution) {
  const auto *material =
      execution.arguments.Find<MaterialInputs>(step.material);
  if (!material || !material->normal)
    return std::unexpected("normal map is unavailable");
  const auto target = AcquireStepTarget(execution, step.requirements);
  if (!target ||
      !execution.lab.Render(*target, nullptr, NormalSlopeParamsFor(*material)))
    return std::unexpected("normal slope draw failed");
  return View(target);
}
StepResult ExecuteSubmitMaterialSampleStep(const SubmitMaterialSampleStep &step,
                                           StepExecution &execution) {
  const auto *material =
      execution.arguments.Find<MaterialInputs>(step.material);
  if (!material)
    return std::unexpected("material is unavailable");
  if (!execution.lab.SubmitMaterialSample(material->rmaos.get(),
                                          material->diffuse.get(),
                                          execution.scratch.material))
    return std::unexpected("material sampling failed");
  return RenderValue{Value{0.0f}};
}
StepResult ExecuteClusterMaterialStep(const ClusterMaterialStep &step,
                                      StepExecution &execution) {
  const auto *sample =
      execution.arguments.Find<std::shared_ptr<const MaterialSample>>(
          step.sample);
  if (!sample || !*sample)
    return std::unexpected("material sample is unavailable");
  auto analysis = ClusterMaterial(**sample, step.settings);
  if (analysis.clusters.empty())
    return std::unexpected("material analysis has no clusters");
  return std::make_shared<const MaterialAnalysis>(std::move(analysis));
}
StepResult ExecuteDrawClustersStep(const DrawClustersStep &step,
                                   StepExecution &execution) {
  const auto *material =
      execution.arguments.Find<MaterialInputs>(step.material);
  const auto *analysis =
      execution.arguments.Find<std::shared_ptr<const MaterialAnalysis>>(
          step.analysis);
  const auto target = AcquireStepTarget(execution, step.requirements);
  if (!material || !analysis || !*analysis || !target ||
      !execution.lab.RenderClusters(*target, material->rmaos.get(),
                                    material->diffuse.get(), **analysis))
    return std::unexpected("cluster draw failed");
  auto view = View(target);
  view.sampling.nearest = true;
  return view;
}
std::optional<Vec2> CoordinateAt(std::span<const RenderValueRef> values,
                                 std::size_t index,
                                 const Arguments &arguments) {
  if (index >= values.size())
    return std::nullopt;
  const auto *value = arguments.Find<Value>(values[index]);
  if (!value || !Get<Vec2>(*value))
    return std::nullopt;
  return AsVec2(*value);
}
std::expected<void, std::string> ApplyCoordinates(
    TextureView &view, const TextureCoordinatesOperation &coordinates,
    std::span<const RenderValueRef> values, const Arguments &arguments) {
  auto &transform = view.sampling.transform;
  transform.mirrorU = coordinates.mirror[0];
  transform.mirrorV = coordinates.mirror[1];
  transform.transpose = coordinates.transpose;
  std::size_t next = 0;
  if (coordinates.scroll) {
    const auto scroll = CoordinateAt(values, next++, arguments);
    if (!scroll)
      return std::unexpected("scroll is unavailable");
    transform.uOffset = scroll->x;
    transform.vOffset = scroll->y;
  }
  if (coordinates.tile) {
    const auto tile = CoordinateAt(values, next++, arguments);
    if (!tile)
      return std::unexpected("tile is unavailable");
    transform.tileU = std::max(tile->x, 0.01f);
    transform.tileV = std::max(tile->y, 0.01f);
  }
  return {};
}
const TextureCoordinatesOperation *
CoordinatesOperationOf(const RecipeGraph &graph, const ImageOperation &image) {
  const auto *node = graph.NodeAt(image.coordinates.node);
  return node ? Get<TextureCoordinatesOperation>(node->kind) : nullptr;
}
std::expected<TextureView, std::string> ImageView(const ImageOperation &image,
                                                  const SampleFieldStep &step,
                                                  const Arguments &arguments) {
  const auto *texture = arguments.Find<TextureView>(step.texture);
  if (!texture || !texture->texture || !step.field.graph)
    return std::unexpected("image is unavailable");
  TextureView view = *texture;
  view.sampling.channel = ShaderChannelOf(image.channel);
  view.sampling.meshSpace = image.space == ImageSpace::kMesh;
  view.sampling.transform.sourceMip = image.mip;
  const auto *coordinates = CoordinatesOperationOf(*step.field.graph, image);
  if (!coordinates)
    return view;
  if (auto applied =
          ApplyCoordinates(view, *coordinates, step.coordinates, arguments);
      !applied)
    return std::unexpected(applied.error());
  return view;
}
MaterialChannel ResolvedMaterialChannel(MaterialChannel channel,
                                        const MaterialInputs &material) {
  if (channel != MaterialChannel::kRelief)
    return channel;
  return material.flatDisplacement ? MaterialChannel::kOcclusion
                                   : MaterialChannel::kDisplacement;
}
TextureView MaterialView(const MaterialOperation &operation,
                         const MaterialInputs &material) {
  const auto channel = ResolvedMaterialChannel(operation.channel, material);
  TextureView view;
  view.texture = MaterialTexture(MaterialMapOf(channel), material);
  view.sampling.channel = ShaderChannelOf(channel);
  view.sampling.meshSpace = true;
  return view;
}
std::expected<TextureView, std::string>
SampledView(const NodeKind &kind, const SampleFieldStep &step,
            const Arguments &arguments) {
  if (const auto *image = Get<ImageOperation>(kind))
    return ImageView(*image, step, arguments);
  if (const auto *operation = Get<MaterialOperation>(kind)) {
    const auto *material = arguments.Find<MaterialInputs>(step.texture);
    if (!material)
      return std::unexpected("material is unavailable");
    return MaterialView(*operation, *material);
  }
  return std::unexpected("unsupported field sampling operation");
}
StepResult DrawSampledView(const TextureView &view, ValueType type,
                           TextureRequirements requirements,
                           StepExecution &execution) {
  const RenderValueRef reference = RenderInputRef{0};
  const std::array<ResolvedRenderInput<RenderValue>, 1> bound{
      {{reference, view, 0}}};
  const std::array references{reference};
  StepExecution sampling{Arguments{bound}, execution.scratch, execution.lab};
  return DrawProgram(FieldProgram::Sample(type),
                     ProgramOperands{references, {}}, requirements, sampling);
}
StepResult ExecuteSampleFieldStep(const SampleFieldStep &step,
                                  StepExecution &execution) {
  if (!step.field.graph)
    return std::unexpected("field graph is unavailable");
  const RecipeGraph &graph = *step.field.graph;
  const auto *node = graph.NodeAt(step.field.output.node);
  if (!node)
    return std::unexpected("field operation is unavailable");
  const auto view = SampledView(node->kind, step, execution.arguments);
  if (!view)
    return std::unexpected(view.error());
  if (!view->texture)
    return std::unexpected("field texture is unavailable");
  const auto type = graph.OutputType(step.field.output);
  const auto *numeric = type ? Get<ValueType>(*type) : nullptr;
  if (!numeric)
    return std::unexpected("field is not numeric");
  return DrawSampledView(*view, *numeric, step.requirements, execution);
}
StepResult ExecuteSubmitReductionStep(const SubmitReductionStep &step,
                                      StepExecution &execution) {
  const auto *view = execution.arguments.Find<TextureView>(step.value);
  if (!view || !view->target)
    return std::unexpected("reduction field is unavailable");
  const auto extent = TextureLab::ExtentOf(view->texture.get());
  if (!extent || extent->width != step.domain.width ||
      extent->height != step.domain.height)
    return std::unexpected("reduction field extent does not match its domain");
  if (!execution.lab.SubmitReduction(*view->target, step.kind, step.type,
                                     execution.scratch.reduction))
    return std::unexpected("reduction could not be drawn");
  return RenderValue{Value{0.0f}};
}
std::expected<std::vector<Value>, std::string>
LookupArgumentsFor(const BuildLookupStep &step, std::size_t parameterCount,
                   const Arguments &arguments) {
  std::vector<Value> values(parameterCount, Value{0.0f});
  for (const auto &bound : step.boundArguments) {
    const auto *value = arguments.Find<Value>(bound.value);
    if (!value || bound.parameter >= values.size())
      return std::unexpected("lookup argument is unavailable");
    values[bound.parameter] = *value;
  }
  return values;
}
std::expected<std::array<float, 256>, std::string>
LookupSamplesOf(const RecipeGraph &graph, const BuildLookupStep &step,
                std::vector<Value> values) {
  std::array<float, 256> samples{};
  if (step.sampledParameter >= values.size())
    return std::unexpected("lookup function is unavailable");
  for (std::size_t i = 0; i < samples.size(); ++i) {
    values[step.sampledParameter] = static_cast<float>(i) / 255.0f;
    const auto value = EvaluateFunction(graph, step.function, values);
    const auto *number = Get<float>(value);
    if (!number || !std::isfinite(*number))
      return std::unexpected("lookup produced an invalid scalar");
    samples[i] = *number;
  }
  return samples;
}
StepResult ExecuteBuildLookupStep(const BuildLookupStep &step,
                                  StepExecution &execution) {
  const auto *function =
      step.graph ? step.graph->FunctionAt(step.function) : nullptr;
  if (!function || step.sampledParameter >= function->parameters.size())
    return std::unexpected("lookup function is unavailable");
  auto values = LookupArgumentsFor(step, function->parameters.size(),
                                   execution.arguments);
  if (!values)
    return std::unexpected(values.error());
  const auto samples = LookupSamplesOf(*step.graph, step, std::move(*values));
  if (!samples)
    return std::unexpected(samples.error());
  auto lookup = execution.lab.CreateLookup(*samples);
  if (!lookup)
    return std::unexpected("lookup allocation failed");
  return lookup;
}
Value ReadOperand(ValueOperands &operands, OutputRef ref) {
  for (std::size_t i = 0;
       i < operands.refs.size() && i < operands.inputs.size(); ++i)
    if (operands.refs[i] == ref) {
      if (const auto *value =
              operands.arguments.Find<Value>(operands.inputs[i]))
        return *value;
    }
  operands.available = false;
  return 0.0f;
}
std::vector<Value> ReadOperands(ValueOperands &operands,
                                std::span<const OutputRef> refs) {
  std::vector<Value> values;
  values.reserve(refs.size());
  for (const auto ref : refs)
    values.push_back(ReadOperand(operands, ref));
  return values;
}
Value CallFunction(ValueOperands &operands, FunctionId function,
                   std::span<const OutputRef> arguments) {
  return EvaluateFunction(operands.graph, function,
                          ReadOperands(operands, arguments));
}
float CallBoundFunction(ValueOperands &operands, const BoundFunction &binding,
                        float x) {
  const auto *function = operands.graph.FunctionAt(binding.function);
  if (!function || binding.sampledParameter >= function->parameters.size()) {
    operands.available = false;
    return 0.0f;
  }
  std::vector<Value> values(function->parameters.size(), Value{0.0f});
  values[binding.sampledParameter] = x;
  for (const auto &argument : binding.arguments) {
    if (argument.parameter >= values.size()) {
      operands.available = false;
      return 0.0f;
    }
    values[argument.parameter] = ReadOperand(operands, argument.value);
  }
  return AsScalar(EvaluateFunction(operands.graph, binding.function, values));
}
Value EvaluateExpression(ValueOperands &operands,
                         const ExpressionOperation &operation) {
  const auto &expression = operation.expression;
  const auto values = ReadOperands(operands, expression.valueBindings);
  Program::Inputs inputs;
  inputs.refs = values;
  inputs.callFunction = [&](std::size_t index, float x, float) {
    if (index >= expression.functionBindings.size()) {
      operands.available = false;
      return 0.0f;
    }
    return CallBoundFunction(operands, expression.functionBindings[index], x);
  };
  return expression.program.Evaluate(inputs);
}
Value ComposeVector(ValueOperands &operands, const VectorOperation &operation) {
  const auto &components = operation.components;
  if (components.size() == 2)
    return Vec2{AsScalar(ReadOperand(operands, components[0])),
                AsScalar(ReadOperand(operands, components[1]))};
  if (components.size() == 3)
    return Vec3{AsScalar(ReadOperand(operands, components[0])),
                AsScalar(ReadOperand(operands, components[1])),
                AsScalar(ReadOperand(operands, components[2]))};
  operands.available = false;
  return 0.0f;
}
Value EvaluateOperation(ValueOperands &operands, const NodeKind &kind) {
  return Match(
      kind,
      [&](const ExpressionOperation &operation) -> Value {
        return EvaluateExpression(operands, operation);
      },
      [&](const CallOperation &operation) -> Value {
        return CallFunction(operands, operation.function, operation.arguments);
      },
      [&](const MapFunctionOperation &operation) -> Value {
        return CallFunction(operands, operation.function, operation.arguments);
      },
      [&](const VectorOperation &operation) -> Value {
        return ComposeVector(operands, operation);
      },
      [&](const auto &) -> Value {
        operands.available = false;
        return 0.0f;
      });
}
bool FiniteValueOf(const Value &value, ValueType type) {
  const auto vector = AsVec3(value);
  return TypeOf(value) == type && std::isfinite(vector.x) &&
         std::isfinite(vector.y) && std::isfinite(vector.z);
}
StepResult ExecuteEvaluateValueStep(const EvaluateValueStep &step,
                                    StepExecution &execution) {
  const auto *node = step.value.graph
                         ? step.value.graph->NodeAt(step.value.output.node)
                         : nullptr;
  if (!node)
    return std::unexpected("uniform operation is unavailable");
  const auto refs = InputsOf(node->kind);
  ValueOperands operands{*step.value.graph, refs, step.inputs,
                         execution.arguments,
                         refs.size() == step.inputs.size()};
  const Value result = EvaluateOperation(operands, node->kind);
  if (!operands.available || !FiniteValueOf(result, step.type))
    return std::unexpected("uniform evaluation failed");
  return result;
}
StepResult ExecuteEvaluateProgramStep(const EvaluateProgramStep &step,
                                      StepExecution &execution) {
  return DrawProgram(step.program, ProgramOperands{step.inputs, step.lookups},
                     step.requirements, execution);
}
StepResult ExecuteMapFieldStep(const MapFieldStep &step,
                               StepExecution &execution) {
  const std::array values{step.value}, lookups{step.lookup};
  return DrawProgram(FieldProgram::Map(), ProgramOperands{values, lookups},
                     step.requirements, execution);
}
std::array<bool, 3> TextureComponentsOf(const ComposeVectorStep &step,
                                        const Arguments &arguments) {
  std::array<bool, 3> textures{};
  for (std::size_t i = 0; i < step.components.size() && i < textures.size();
       ++i)
    textures[i] = arguments.Find<TextureView>(step.components[i]) != nullptr;
  return textures;
}
StepResult ExecuteComposeVectorStep(const ComposeVectorStep &step,
                                    StepExecution &execution) {
  if (step.components.size() > 3)
    return std::unexpected("invalid vector width");
  const auto textures = TextureComponentsOf(step, execution.arguments);
  auto program =
      FieldProgram::Compose(std::span{textures}.first(step.components.size()));
  if (!program)
    return std::unexpected(program.error());
  return DrawProgram(*program, ProgramOperands{step.components, {}},
                     step.requirements, execution);
}
RippleOperands RippleOperandsOf(const RippleOperation &ripple,
                                const NodeKind &kind,
                                const DrawRippleStep &step,
                                const Arguments &arguments) {
  RippleOperands operands{{}, step.inputs, arguments};
  for (const auto ref : InputsOf(kind))
    if (ref != ripple.coordinates)
      operands.fields.push_back(ref);
  return operands;
}
std::optional<RenderValueRef> RippleInput(const RippleOperands &operands,
                                          OutputRef output) {
  for (std::size_t i = 0;
       i < operands.fields.size() && i < operands.inputs.size(); ++i)
    if (operands.fields[i] == output)
      return operands.inputs[i];
  return std::nullopt;
}
template <class T>
const T *RippleResource(const RippleOperands &operands, OutputRef output) {
  const auto ref = RippleInput(operands, output);
  return ref ? operands.arguments.Find<T>(*ref) : nullptr;
}
const Value *RippleNumber(const RippleOperands &operands, OutputRef output) {
  return RippleResource<Value>(operands, output);
}
void SetRippleDirection(TextureLab::RipplePass &pass, Vec3 direction) {
  const float length =
      std::sqrt(direction.x * direction.x + direction.y * direction.y +
                direction.z * direction.z);
  if (length <= 1e-4f)
    return;
  pass.directional = true;
  pass.direction = {direction.x / length, direction.y / length,
                    direction.z / length};
}
void AddRippleFirings(TextureLab::RipplePass &pass,
                      const RenderFirings &firings, Vec3 center, float time) {
  for (const auto &firing : firings.firings) {
    if (pass.firingCount >= pass.firings.size())
      break;
    pass.firings[pass.firingCount++] = {
        firing.origin.value_or(center),
        std::max(0.0f, time - firing.startTime)};
  }
}
std::expected<TextureLab::RipplePass, std::string>
RipplePassFor(const RippleOperation &ripple, const RippleOperands &operands,
              const TextureView &positions) {
  const auto *mesh =
      RippleResource<std::shared_ptr<MeshEntry>>(operands, ripple.geometry);
  if (!mesh || !*mesh || !(*mesh)->mesh)
    return std::unexpected("ripple geometry is unavailable");
  const auto *firings = RippleResource<RenderFirings>(operands, ripple.firings);
  const auto *speed = RippleNumber(operands, ripple.speed);
  const auto *width = RippleNumber(operands, ripple.width);
  const auto *decay = RippleNumber(operands, ripple.decay);
  const auto *time = RippleNumber(operands, ripple.time);
  const auto *direction = RippleNumber(operands, ripple.direction);
  if (!firings || !speed || !width || !decay || !time || !direction)
    return std::unexpected("ripple parameters are unavailable");
  TextureLab::RipplePass pass;
  pass.positions = positions.texture.get();
  pass.frame = kPositionFrame;
  pass.speed = AsScalar(*speed);
  pass.width = AsScalar(*width);
  pass.decay = AsScalar(*decay);
  pass.disc = ripple.shape == RippleShape::kDisc;
  SetRippleDirection(pass, AsVec3(*direction));
  AddRippleFirings(pass, *firings, (*mesh)->mesh->center, AsScalar(*time));
  return pass;
}
StepResult ExecuteDrawRippleStep(const DrawRippleStep &step,
                                 StepExecution &execution) {
  const auto *positions = execution.arguments.Find<TextureView>(step.positions);
  const auto *node = step.field.graph
                         ? step.field.graph->NodeAt(step.field.output.node)
                         : nullptr;
  const auto *ripple = node ? Get<RippleOperation>(node->kind) : nullptr;
  if (!positions || !positions->texture || !ripple)
    return std::unexpected("ripple inputs are unavailable");
  const auto operands =
      RippleOperandsOf(*ripple, node->kind, step, execution.arguments);
  const auto pass = RipplePassFor(*ripple, operands, *positions);
  if (!pass)
    return std::unexpected(pass.error());
  const auto target = AcquireStepTarget(execution, step.requirements);
  if (!target || !execution.lab.RenderRipple(*target, *pass))
    return std::unexpected("ripple draw failed");
  return View(target);
}
StepResult ExecuteCompositeStackStep(const CompositeStackStep &step,
                                     StepExecution &execution) {
  const auto *filter = execution.arguments.Find<LayerFilter>(step.visibility);
  if (!filter)
    return std::unexpected("stack visibility is unavailable");
  const auto layers = VisibleLayers(step, *filter);
  if (layers.empty())
    return StackResult{};
  const auto *base = execution.arguments.Find<StackResult>(step.base);
  if (!base)
    return std::unexpected("stack base is unavailable");
  const auto fields = VisibleLayerFields(layers);
  const auto passes = LayerPassesFor(layers, execution.arguments, fields);
  if (!passes)
    return std::unexpected(passes.error());
  const auto packed = LayerFieldPackFor(step, fields, execution.scratch);
  if (!packed)
    return std::unexpected(packed.error());
  const auto bound = BindLayerFields(**packed, execution.arguments);
  if (!bound)
    return std::unexpected(bound.error());
  auto layerRequirements = step.requirements;
  layerRequirements.mipPolicy = MipPolicy::kNone;
  const auto target = AcquireStepTarget(execution, layerRequirements);
  if (!target)
    return std::unexpected("stack target is unavailable");
  if (!execution.lab.RenderStack(*target, base->texture.get(), *passes, *bound))
    return std::unexpected("stack draw failed");
  if (step.requirements.mipPolicy == MipPolicy::kGenerate)
    execution.lab.GenerateMipsFor(*target);
  return StackResult{TextureRef{target->Texture()}};
}
StepResult ExecuteStep(const RenderStep &step,
                       std::span<const ResolvedRenderInput<RenderValue>> inputs,
                       RenderScratch &scratch) {
  auto *lab = TextureLab::GetSingleton();
  if (!lab)
    return std::unexpected("texture lab is unavailable");
  StepExecution execution{Arguments{inputs}, scratch, *lab};
  return Match(
      step.kind,
      [](const UnavailableStep &k) -> StepResult {
        return std::unexpected(k.problem);
      },
      [](const ConstantRenderStep &k) -> StepResult { return k.value; },
      [&](const BuildBakeBuffersStep &k) {
        return ExecuteBuildBakeBuffersStep(k, execution);
      },
      [&](const BakeMeshStep &k) { return ExecuteBakeMeshStep(k, execution); },
      [&](const NormalSlopeStep &k) {
        return ExecuteNormalSlopeStep(k, execution);
      },
      [&](const SubmitMaterialSampleStep &k) {
        return ExecuteSubmitMaterialSampleStep(k, execution);
      },
      [&](const ClusterMaterialStep &k) {
        return ExecuteClusterMaterialStep(k, execution);
      },
      [&](const DrawClustersStep &k) {
        return ExecuteDrawClustersStep(k, execution);
      },
      [&](const SampleFieldStep &k) {
        return ExecuteSampleFieldStep(k, execution);
      },
      [&](const SubmitReductionStep &k) {
        return ExecuteSubmitReductionStep(k, execution);
      },
      [&](const BuildLookupStep &k) {
        return ExecuteBuildLookupStep(k, execution);
      },
      [&](const EvaluateValueStep &k) {
        return ExecuteEvaluateValueStep(k, execution);
      },
      [&](const EvaluateProgramStep &k) {
        return ExecuteEvaluateProgramStep(k, execution);
      },
      [&](const MapFieldStep &k) { return ExecuteMapFieldStep(k, execution); },
      [&](const ComposeVectorStep &k) {
        return ExecuteComposeVectorStep(k, execution);
      },
      [&](const DrawRippleStep &k) {
        return ExecuteDrawRippleStep(k, execution);
      },
      [&](const CompositeStackStep &k) {
        return ExecuteCompositeStackStep(k, execution);
      });
}
struct BoundTextureValue {
  const RecipeGraph &graph;
  const TextureValue &binding;
  const GeometryInputs &geometry;
  const SignalState &signals;
  const ExternalInput *external;
};
bool ImportsResource(const RenderValueType &type) {
  return !Get<ValueType>(type) &&
         type != RenderValueType{RenderResourceType::kFirings};
}
bool StableResource(const ExternalInput *external) {
  return external && (Is<GeometryInput>(external->source) ||
                      Is<MaterialInput>(external->source) ||
                      Is<TextureInput>(external->source));
}
std::optional<RenderValue> NumberValueOf(const BoundTextureValue &bound,
                                         ValueType type) {
  const auto *position =
      bound.external ? Get<NodePositionInput>(bound.external->source) : nullptr;
  if (position) {
    if (const auto origin =
            NodeBindPosition(bound.geometry.geometry.get(),
                             bound.geometry.root.get(), position->name))
      return Value{*origin};
    return std::nullopt;
  }
  const auto number = bound.signals.ValueOf(bound.binding.output);
  const auto v = AsVec3(number);
  if (TypeOf(number) == type && std::isfinite(v.x) && std::isfinite(v.y) &&
      std::isfinite(v.z))
    return number;
  return std::nullopt;
}
RenderFirings FiringsOf(const BoundTextureValue &bound) {
  RenderFirings firings;
  const auto name = bound.graph.NameOf(bound.binding.output.node);
  for (const auto &firing : bound.signals.Firings(bound.binding.output)) {
    if (firings.firings.size() >= TextureLab::kRippleFirings)
      break;
    auto origin = Match(
        bound.signals.AnchorOf(name, firing),
        [](const std::monostate &) { return std::optional<Vec3>{}; },
        [&](const CarriedPoint &point) {
          return std::optional{
              ToRootSpace(bound.geometry.root.get(), point.position)};
        },
        [&](const AnchorNode &anchor) {
          return NodeBindPosition(bound.geometry.geometry.get(),
                                  bound.geometry.root.get(), anchor.node);
        });
    firings.firings.push_back({origin, firing.startTime});
  }
  return firings;
}
std::optional<RenderValue> ExternalResourceOf(const BoundTextureValue &bound) {
  if (!bound.external)
    return std::nullopt;
  const ExternalSource &source = bound.external->source;
  if (Is<GeometryInput>(source)) {
    const auto mesh =
        Compositor::GetSingleton()->MeshOf(bound.geometry.geometry.get());
    if (mesh)
      return *mesh;
    return std::nullopt;
  }
  if (Is<MaterialInput>(source))
    return bound.geometry.material;
  if (const auto *texture = Get<TextureInput>(source)) {
    const auto loaded = Compositor::GetSingleton()->LoadImage(texture->path);
    if (loaded)
      return TextureView{loaded};
    return std::nullopt;
  }
  if (Is<RootTransformInput>(source))
    return RenderTransform{bound.geometry.root};
  return std::nullopt;
}
std::optional<RenderValue> ImportedValueOf(const BoundTextureValue &bound,
                                           const RenderValueType &type) {
  if (const auto *numberType = Get<ValueType>(type))
    return NumberValueOf(bound, *numberType);
  if (type == RenderValueType{RenderResourceType::kFirings})
    return FiringsOf(bound);
  return ExternalResourceOf(bound);
}
}
RenderInstance::RenderInstance(
    RenderPlan plan, std::vector<GeometryInputs> geometries,
    std::vector<std::shared_ptr<const RecipeGraph>> graphs)
    : graphs_(std::move(graphs)), geometries_(std::move(geometries)),
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
    if (ImportsResource(input.type) && StableResource(external) &&
        execution_.Inputs()[i].value)
      continue;
    const BoundTextureValue bound{graph, *binding, *geometry, signals,
                                  external};
    std::optional<RenderValue> value = ImportedValueOf(bound, input.type);
    if (!value) {
      execution_.UnsetInput(i);
      continue;
    }
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
  if (!lab)
    return;
  const auto &kind = Plan().steps[binding->submission].kind;
  const auto import = [&](auto &&collected, auto &&toValue) {
    if (!collected)
      return;
    if (!*collected) {
      execution_.UnsetInput(input);
      return;
    }
    if (!UpdateInput(input, toValue(std::move(**collected))))
      execution_.UnsetInput(input);
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
        if (lab && lab->Timing())
          span.emplace(*lab, StepSpanKey(step));
        return ExecuteStep(step, inputs, scratch);
      },
      SameRenderValue, SelectStackInputs);
}
const GeometryInputs *RenderInstance::GeometryOf(GeometryId id) const noexcept {
  return IndexOf(id) < geometries_.size() ? &geometries_[IndexOf(id)] : nullptr;
}
bool RenderInstance::BeginFrame(std::uint64_t frame, std::uint64_t nowMS) {
  if (frame_ == frame)
    return false;
  frame_ = frame;
  execution_.AdvanceClock(nowMS);
  Metrics::CountStepReleases(
      execution_.ReleaseIdle(kReleaseAfterIdleMS, Releasable));
  Metrics::CountStepRestores(execution_.Restores() - restoresReported_);
  restoresReported_ = execution_.Restores();
  CollectReadbacks();
  return true;
}
std::optional<TextureView> RenderInstance::Texture(RenderValueRef output) {
  const std::optional<RenderValue> *value = nullptr;
  if (const auto *ref = Get<StepOutputRef>(output);
      ref && ref->output == 0 && ref->step < execution_.Steps().size()) {
    if (execution_.Steps()[ref->step].released && !Demand(output))
      return std::nullopt;
    execution_.Touch(ref->step);
    value = &execution_.Steps()[ref->step].outputs.front().value;
  }
  if (const auto *ref = Get<RenderInputRef>(output);
      ref && ref->input < execution_.Inputs().size())
    value = &execution_.Inputs()[ref->input].value;
  if (value && *value)
    if (const auto *texture = Get<TextureView>(**value)) {
      if (auto *lab = TextureLab::GetSingleton();
          lab && texture->target && texture->target->Mips() == MipPolicy::kNone)
        lab->GenerateMipsFor(*texture->target);
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
