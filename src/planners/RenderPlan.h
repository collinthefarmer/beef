// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/TextureDemand.h"

namespace BetterEnchantmentEffects {
using RenderInputId = std::size_t;
using RenderStepId = std::size_t;
struct RenderInputRef {
  RenderInputId input = 0;
  [[nodiscard]] bool operator==(const RenderInputRef &) const = default;
};
struct StepOutputRef {
  RenderStepId step = 0;
  std::size_t output = 0;
  [[nodiscard]] bool operator==(const StepOutputRef &) const = default;
};
using RenderValueRef = std::variant<RenderInputRef, StepOutputRef>;
enum class RenderResourceType {
  kTexture,
  kMesh,
  kMaterial,
  kTransform,
  kFirings,
  kBakeBuffers,
  kMaterialSample,
  kMaterialAnalysis,
  kLookup,
  kVisibility,
  kStack,
  kSubmission
};
using RenderValueType = std::variant<ValueType, RenderResourceType>;
struct ReductionDomain {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  [[nodiscard]] bool operator==(const ReductionDomain &) const = default;
};
struct StackInputBinding {
  PlacementId placement{};
  std::size_t output = 0;
  GeometryId geometry{};
  [[nodiscard]] bool operator==(const StackInputBinding &) const = default;
};
struct ReadbackBinding {
  RenderStepId submission = 0;
  [[nodiscard]] bool operator==(const ReadbackBinding &) const = default;
};
struct RenderInput {
  RenderValueType type;
  std::variant<TextureValue, StackInputBinding, ReadbackBinding> binding;
  GeometryId geometry{};
};
struct UnavailableStep {
  RenderValueType type;
  std::string problem;
  [[nodiscard]] bool operator==(const UnavailableStep &) const = default;
};
struct ConstantRenderStep {
  Value value;
  [[nodiscard]] bool operator==(const ConstantRenderStep &) const = default;
};
struct BuildBakeBuffersStep {
  RenderValueRef mesh;
  std::variant<BakeKind, RenderValueRef> operation;
  [[nodiscard]] bool operator==(const BuildBakeBuffersStep &) const = default;
};
struct BakeMeshStep {
  RenderValueRef buffers;
  TextureRequirements requirements;
  bool nearest = false;
  [[nodiscard]] bool operator==(const BakeMeshStep &) const = default;
};
struct NormalSlopeStep {
  RenderValueRef material;
  TextureRequirements requirements;
  [[nodiscard]] bool operator==(const NormalSlopeStep &) const = default;
};
struct SubmitMaterialSampleStep {
  RenderValueRef material;
  [[nodiscard]] bool
  operator==(const SubmitMaterialSampleStep &) const = default;
};
struct ClusterMaterialStep {
  RenderValueRef sample;
  ClusterSettings settings;
  [[nodiscard]] bool operator==(const ClusterMaterialStep &) const = default;
};
struct DrawClustersStep {
  RenderValueRef material, analysis;
  TextureRequirements requirements;
  [[nodiscard]] bool operator==(const DrawClustersStep &) const = default;
};
struct SampleFieldStep {
  RenderValueRef texture;
  TextureValue field;
  std::vector<RenderValueRef> coordinates;
  TextureRequirements requirements;
  [[nodiscard]] bool operator==(const SampleFieldStep &) const = default;
};
struct SubmitReductionStep {
  ReductionKind kind;
  RenderValueRef value;
  ValueType type;
  ReductionDomain domain;
  [[nodiscard]] bool operator==(const SubmitReductionStep &) const = default;
};
struct LookupArgument {
  std::size_t parameter = 0;
  RenderValueRef value;
  [[nodiscard]] bool operator==(const LookupArgument &) const = default;
};
struct BuildLookupStep {
  const RecipeGraph *graph = nullptr;
  FunctionId function = 0;
  std::size_t sampledParameter = 0;
  std::vector<LookupArgument> boundArguments;
  std::size_t samples = 256;
  [[nodiscard]] bool operator==(const BuildLookupStep &) const = default;
};
struct EvaluateValueStep {
  TextureValue value;
  std::vector<RenderValueRef> inputs;
  ValueType type;
  [[nodiscard]] bool operator==(const EvaluateValueStep &) const = default;
};
struct EvaluateProgramStep {
  FieldProgram program;
  std::vector<RenderValueRef> inputs;
  std::vector<RenderValueRef> lookups;
  TextureRequirements requirements;
  [[nodiscard]] bool operator==(const EvaluateProgramStep &) const = default;
};
struct MapFieldStep {
  RenderValueRef value, lookup;
  TextureRequirements requirements;
  [[nodiscard]] bool operator==(const MapFieldStep &) const = default;
};
struct ComposeVectorStep {
  std::vector<RenderValueRef> components;
  TextureRequirements requirements;
  [[nodiscard]] bool operator==(const ComposeVectorStep &) const = default;
};
struct DrawRippleStep {
  RenderValueRef positions;
  TextureValue field;
  std::vector<RenderValueRef> inputs;
  TextureRequirements requirements;
  [[nodiscard]] bool operator==(const DrawRippleStep &) const = default;
};
struct LayerField {
  RenderValueRef value;
  FieldProgram program;
  std::vector<RenderValueRef> inputs;
  std::vector<RenderValueRef> lookups;
  [[nodiscard]] bool operator==(const LayerField &) const = default;
};
struct LayerFieldRef {
  std::size_t field = 0;
  [[nodiscard]] bool operator==(const LayerFieldRef &) const = default;
};
using LayerRead = std::variant<RenderValueRef, LayerFieldRef>;
struct PlannedLayer {
  LayerRead source;
  RenderValueRef opacity;
  std::optional<LayerRead> mask;
  std::optional<RenderValueRef> color;
  Blend blend;
  ChannelSet channels;
  [[nodiscard]] bool operator==(const PlannedLayer &) const = default;
};
struct CompositeStackStep {
  RenderValueRef base, visibility;
  std::vector<PlannedLayer> layers;
  TextureRequirements requirements;
  Slot slot;
  std::vector<LayerField> fields;
  [[nodiscard]] bool operator==(const CompositeStackStep &) const = default;
};
using RenderStepKind =
    std::variant<UnavailableStep, ConstantRenderStep, BuildBakeBuffersStep,
                 BakeMeshStep, NormalSlopeStep, SubmitMaterialSampleStep,
                 ClusterMaterialStep, DrawClustersStep, SampleFieldStep,
                 SubmitReductionStep, BuildLookupStep, EvaluateValueStep,
                 EvaluateProgramStep, MapFieldStep, ComposeVectorStep,
                 DrawRippleStep, CompositeStackStep>;
struct RenderStep {
  RenderStepKind kind;
  std::string displayName;
  std::string valueKey;
  std::string shareKey;
};
struct TextureUseBinding {
  TextureUse use;
  RenderValueRef texture;
};
struct StackOutputBinding {
  PlacementId placement{};
  std::size_t output = 0;
  GeometryId geometry{};
  StepOutputRef result;
};
struct RenderValueBinding {
  TextureValue value;
  RenderValueRef result;
  GeometryId geometry{};
};
struct RenderPlan {
  std::vector<RenderValueBinding> values;
  std::vector<RenderInput> inputs;
  std::vector<RenderStep> steps;
  std::vector<TextureUseBinding> textureUses;
  std::vector<StackOutputBinding> stackOutputs;
};
struct RenderStackRequest {
  const RecipeGraph *graph = nullptr;
  const SurfaceOutput *surface = nullptr;
  std::size_t instance = 0;
  PlacementId placement{};
  std::size_t output = 0;
  TextureRequirements requirements;
  GeometryId geometry{};
};
using RenderBindingResolver =
    std::function<ValueBindings(const TextureValue &, GeometryId geometry)>;
struct PackedLayerFields {
  ProgramPack pack;
  std::vector<RenderValueRef> inputs;
  std::vector<RenderValueRef> lookups;
};
[[nodiscard]] const LayerField *ReadField(const CompositeStackStep &stack,
                                          const LayerRead &read);
[[nodiscard]] std::optional<RenderValueRef>
ReadValue(const CompositeStackStep &stack, const LayerRead &read);
[[nodiscard]] std::vector<RenderValueRef>
LayerOperands(const CompositeStackStep &stack, const PlannedLayer &layer);
[[nodiscard]] std::expected<PackedLayerFields, std::string>
PackLayerFields(std::span<const LayerField *const> fields);
[[nodiscard]] std::vector<std::size_t>
VisibleLayerFields(std::span<const PlannedLayer *const> layers);
[[nodiscard]] std::optional<std::uint32_t>
SegmentIndexOf(std::span<const std::size_t> fields, const LayerRead &read);
[[nodiscard]] std::vector<RenderValueRef> InputsOf(const RenderStepKind &step);
[[nodiscard]] std::vector<RenderValueRef>
StepDependencies(const RenderPlan &plan, RenderStepId step);
[[nodiscard]] std::vector<bool> ChangingSteps(const RenderPlan &plan);
[[nodiscard]] RenderPlan MarkShareableSteps(RenderPlan plan);
[[nodiscard]] std::string ShareKeyFor(std::string_view shareKey,
                                      std::span<const Value> operands);
[[nodiscard]] std::vector<std::size_t> LiveConsumers(const RenderPlan &plan);
[[nodiscard]] RenderValueType OutputType(const RenderStepKind &step);
[[nodiscard]] std::string_view StepKindName(const RenderStepKind &step);
[[nodiscard]] std::optional<TextureRequirements>
RequirementsOf(const RenderStepKind &step);
[[nodiscard]] std::optional<RenderValueType> TypeOf(const RenderPlan &plan,
                                                    RenderValueRef value);
[[nodiscard]] std::expected<void, std::string>
ValidateRenderPlan(const RenderPlan &plan);
[[nodiscard]] std::expected<RenderPlan, std::string>
LowerRenderPlan(std::span<const TextureDemand> demands,
                std::span<const RenderStackRequest> stacks,
                const RenderBindingResolver &bindings);
[[nodiscard]] std::expected<RenderPlan, std::string>
BuildRenderPlan(std::span<const TextureDemand> demands,
                std::span<const RenderStackRequest> stacks,
                const RenderBindingResolver &bindings);
}
