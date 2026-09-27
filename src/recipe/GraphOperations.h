// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "recipe/Expression.h"
#include "recipe/Recipe.h"
#include "recipe/Reduction.h"

#include <limits>

namespace BetterEnchantmentEffects {
using NodeId = std::size_t;
using FunctionId = std::size_t;
struct OutputRef {
  NodeId node = std::numeric_limits<NodeId>::max();
  std::size_t output = 0;
  bool operator==(const OutputRef &) const = default;
};
enum class ResourceType {
  kFirings,
  kCount,
  kEvents,
  kTexture,
  kMaterial,
  kGeometry,
  kTransform,
  kEffectShader
};
using GraphValueType = std::variant<ValueType, ResourceType>;
struct NodeOutput {
  std::string name;
  GraphValueType type = ValueType::kScalar;
};
struct TimeInput {};
struct DeltaTimeInput {};
struct SampleUvInput {};
struct RootTransformInput {};
struct GeometryInput {};
struct MaterialInput {};
struct TextureInput {
  std::string path;
};
struct EffectShaderInput {
  FormRef record;
};
struct EventInput {
  std::variant<EventOrigin, PluginOrigin> origin;
};
struct NodePositionInput {
  std::string name;
};
using ExternalSource =
    std::variant<TimeInput, DeltaTimeInput, SampleUvInput, RootTransformInput,
                 GeometryInput, MaterialInput, TextureInput, EffectShaderInput,
                 EventInput, NodePositionInput, ActorValueSignal,
                 ActorStateSignal, EnchantmentSignal>;
struct ExternalInput {
  ExternalSource source;
};
struct ConstantOperation {
  Value value = 0.0f;
};
struct VectorOperation {
  std::vector<OutputRef> components;
};
struct BoundFunctionArgument {
  std::size_t parameter = 0;
  OutputRef value;
};
struct BoundFunction {
  FunctionId function = 0;
  std::size_t sampledParameter = 0;
  std::vector<BoundFunctionArgument> arguments;
};
struct BoundExpression {
  Program program;
  std::vector<OutputRef> valueBindings;
  std::vector<BoundFunction> functionBindings;
};
struct ExpressionOperation {
  BoundExpression expression;
};
struct ParameterOperation {
  std::size_t parameter = 0;
};
struct MapFunctionOperation {
  FunctionId function = 0;
  std::vector<OutputRef> arguments;
};
struct CallOperation {
  FunctionId function = 0;
  std::vector<OutputRef> arguments;
};
struct WaveOperation {
  OutputRef base, amplitude, period, phase, deltaTime;
  Waveform waveform = Waveform::kSine;
};
struct RampOperation {
  OutputRef from, to, seconds, time;
};
struct EffectShaderOperation {
  OutputRef record, time;
  EfshField field;
};
struct NoiseOperation {
  OutputRef frequency, amplitude, time;
  std::uint32_t seed = 0;
};
struct CompiledGradientStop {
  float at = 0;
  OutputRef color;
};
struct GradientOperation {
  OutputRef position;
  std::vector<CompiledGradientStop> stops;
};
struct ToRootOperation {
  OutputRef value, transform;
};
struct EventTriggerInput {
  OutputRef events;
};
struct ConditionTriggerInput {
  OutputRef condition;
  std::optional<OutputRef> payload;
};
struct TriggerOperation {
  std::variant<EventTriggerInput, ConditionTriggerInput> origin;
  OutputRef time, lifetime;
  std::uint32_t max = 4;
  ValueType payloadType = ValueType::kScalar;
  TriggerAnchor anchor;
};
struct HoldOperation {
  OutputRef firings;
};
struct CounterOperation {
  OutputRef count;
  std::optional<OutputRef> reset, cap;
};
struct AccumulateOperation {
  OutputRef count, decay, deltaTime;
};
struct RateOperation {
  OutputRef value, deltaTime;
};
struct SmoothOperation {
  OutputRef value, seconds, deltaTime;
};
struct TextureCoordinatesOperation {
  OutputRef uv;
  std::optional<OutputRef> scroll, tile;
  std::array<bool, 2> mirror{false, false};
  bool transpose = false;
};
struct ImageOperation {
  OutputRef texture, coordinates;
  ImageChannel channel;
  ImageSpace space;
  float mip = 0;
};
struct MaterialOperation {
  OutputRef material, coordinates;
  MaterialChannel channel;
};
struct BakeOperation {
  OutputRef geometry, coordinates;
  BakeKind bake;
};
struct DistanceOperation {
  OutputRef geometry, coordinates, origin;
};
struct RippleOperation {
  OutputRef firings, geometry, coordinates, transform, time, speed, width,
      decay, direction;
  RippleShape shape;
};
struct MaterialClustersOperation {
  OutputRef material, geometry, coordinates;
  ClusterSettings settings;
};
struct ReductionOperation {
  ReductionKind kind = ReductionKind::kMean;
  OutputRef value;
};
using NodeKind = std::variant<
    ConstantOperation, ExternalInput, VectorOperation, ExpressionOperation,
    ParameterOperation, CallOperation, MapFunctionOperation, WaveOperation,
    RampOperation, EffectShaderOperation, NoiseOperation, GradientOperation,
    ToRootOperation, TriggerOperation, HoldOperation, CounterOperation,
    AccumulateOperation, RateOperation, SmoothOperation,
    TextureCoordinatesOperation, ImageOperation, MaterialOperation,
    BakeOperation, DistanceOperation, RippleOperation,
    MaterialClustersOperation, ReductionOperation>;
struct RecipeNode {
  std::string displayName;
  NodeKind kind;
  std::vector<NodeOutput> outputs;
};
struct FunctionParameter {
  std::string name;
  ValueType type = ValueType::kScalar;
};
struct FunctionDefinition {
  std::string displayName;
  std::vector<FunctionParameter> parameters;
  std::vector<RecipeNode> nodes;
  OutputRef result;
  bool isDisabled = false;
};
[[nodiscard]] std::vector<OutputRef> InputsOf(const NodeKind &a_kind);
[[nodiscard]] bool Stateful(const NodeKind &a_kind);
}
