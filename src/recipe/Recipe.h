// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "Core.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace BetterEnchantmentEffects {
struct FormKey {
  std::string file;
  std::uint32_t localId = 0;

  [[nodiscard]] static std::optional<FormKey> Parse(std::string_view a_text);
  [[nodiscard]] std::string ToString() const;
  [[nodiscard]] bool operator==(const FormKey &a_other) const noexcept;
};

struct FormRef {
  std::string text;
  std::optional<FormKey> key;

  [[nodiscard]] static FormRef From(std::string_view a_text);
  [[nodiscard]] bool Resolved() const noexcept { return key.has_value(); }
  [[nodiscard]] bool operator==(const FormRef &a_other) const noexcept {
    return text == a_other.text;
  }
};

using Param = std::variant<float, Ref>;
using Vec2Param = std::variant<std::array<Param, 2>, Ref>;
using Vec3Param = std::variant<std::array<Param, 3>, Ref>;

struct CurveRef {
  std::string text;
  [[nodiscard]] std::optional<std::string> Named() const;
  [[nodiscard]] bool operator==(const CurveRef &) const = default;
};

enum class KeyKind {
  kDefault,
  kEnchanted,
  kMaterial,
  kKeyword,
  kArmor,
  kEffectShader,
  kEnchantment,
  kMagicEffect,
};
inline constexpr std::size_t kKeyKindCount = 8;

enum class KeyOperand {
  kNone,
  kForm,
  kGlob,
};

struct WornPiece;

struct KeyKindSpec {
  KeyKind value;
  std::string_view name;
  int priority;
  KeyOperand operand;
  bool enchantmentDerived;
  std::optional<FormKey> WornPiece::*singleForm;
  std::vector<FormKey> WornPiece::*formList;
};

[[nodiscard]] std::string_view KeyKindName(KeyKind a_kind) noexcept;
[[nodiscard]] KeyOperand KeyOperandOf(KeyKind a_kind) noexcept;
[[nodiscard]] int DefaultPriority(KeyKind a_kind) noexcept;
[[nodiscard]] bool EnchantmentDerived(KeyKind a_kind) noexcept;

using KeyOperandValue = std::variant<std::monostate, FormRef, std::string>;

struct RecipeKey {
  KeyKind kind = KeyKind::kDefault;
  KeyOperandValue operand{};

  [[nodiscard]] const FormRef *Form() const noexcept {
    return Get<FormRef>(operand);
  }
  [[nodiscard]] FormRef *Form() noexcept { return Get<FormRef>(operand); }
  [[nodiscard]] std::string_view Glob() const noexcept;
  [[nodiscard]] std::string ToString() const;
  [[nodiscard]] bool operator==(const RecipeKey &) const = default;
};

enum class SelectorKind {
  kAddon,
  kGeometry,
  kTexture,
};
inline constexpr std::size_t kSelectorKindCount = 3;

struct SelectorClause {
  SelectorKind kind = SelectorKind::kGeometry;
  std::variant<FormRef, std::string> operand;

  [[nodiscard]] const FormRef *Form() const noexcept {
    return Get<FormRef>(operand);
  }
  [[nodiscard]] FormRef *Form() noexcept { return Get<FormRef>(operand); }
  [[nodiscard]] std::string_view Glob() const noexcept;
  [[nodiscard]] bool operator==(const SelectorClause &) const = default;
};

struct Selector {
  std::vector<SelectorClause> anyOf;
  [[nodiscard]] bool All() const noexcept { return anyOf.empty(); }
  [[nodiscard]] bool operator==(const Selector &) const = default;
};

struct GeometryIdentity {
  std::optional<FormKey> addon;
  std::string name;
  std::string diffusePath;
};

[[nodiscard]] bool GlobMatch(std::string_view a_glob,
                             std::string_view a_text) noexcept;
[[nodiscard]] bool Matches(const Selector &a_selector,
                           const GeometryIdentity &a_geometry);

enum class Waveform {
  kSine,
  kTriangle,
  kSquare,
  kSaw,
};
inline constexpr std::size_t kWaveformCount = 4;

struct ConstantSignal {
  Value value = 0.0f;
  [[nodiscard]] bool operator==(const ConstantSignal &) const = default;
};
struct WaveSignal {
  Param base = 0.0f;
  Param amplitude = 1.0f;
  Param period = 1.0f;
  Param phase = 0.0f;
  Waveform waveform = Waveform::kSine;
  [[nodiscard]] bool operator==(const WaveSignal &) const = default;
};
struct RampSignal {
  Param from = 0.0f;
  Param to = 1.0f;
  Param seconds = 1.0f;
  [[nodiscard]] bool operator==(const RampSignal &) const = default;
};
enum class EfshField {
  kFillAlpha,
  kFillColor,
  kEdgeAlpha,
  kEdgeColor,
  kScroll,
};
inline constexpr std::size_t kEfshFieldCount = 5;
struct EfshSignal {
  EfshField field = EfshField::kFillAlpha;
  FormRef record;
  [[nodiscard]] bool operator==(const EfshSignal &) const = default;
};
enum class Measure {
  kCurrent,
  kBase,
  kPermanent,
  kTemporaryModifier,
  kDamage,
  kMax,
};
inline constexpr std::size_t kMeasureCount = 6;
struct ActorValueSignal {
  std::string actorValue;
  Measure measure = Measure::kCurrent;
  [[nodiscard]] bool operator==(const ActorValueSignal &) const = default;
};
enum class ActorStateKind {
  kInCombat,
  kSneaking,
  kWeaponDrawn,
  kSwimming,
  kSprinting,
  kMounted,
  kMovementSpeed,
  kPosition,
  kTarget,
  kHasTarget,
};
inline constexpr std::size_t kActorStateCount = 10;
struct ActorStateSignal {
  ActorStateKind kind = ActorStateKind::kInCombat;
  [[nodiscard]] bool operator==(const ActorStateSignal &) const = default;
};
[[nodiscard]] constexpr bool VectorValued(ActorStateKind a_kind) noexcept {
  return a_kind == ActorStateKind::kPosition ||
         a_kind == ActorStateKind::kTarget;
}
enum class EnchantmentField {
  kMagnitude,
  kCost,
};
inline constexpr std::size_t kEnchantmentFieldCount = 2;
struct EnchantmentSignal {
  EnchantmentField field = EnchantmentField::kMagnitude;
  [[nodiscard]] bool operator==(const EnchantmentSignal &) const = default;
};

struct ValueRange {
  std::optional<float> min;
  std::optional<float> max;
  [[nodiscard]] bool operator==(const ValueRange &) const = default;
};
struct EventFilter {
  std::string node;
  std::string arg;
  ValueRange value;
  [[nodiscard]] bool operator==(const EventFilter &) const = default;
};
struct EventOrigin {
  std::string event;
  EventFilter filter;
  [[nodiscard]] bool operator==(const EventOrigin &) const = default;
};
struct PluginOrigin {
  std::string id;
  [[nodiscard]] bool operator==(const PluginOrigin &) const = default;
};
struct WhenOrigin {
  Ref when;
  std::optional<Ref> value;
  [[nodiscard]] bool operator==(const WhenOrigin &) const = default;
};
using TriggerOrigin = std::variant<EventOrigin, PluginOrigin, WhenOrigin>;

struct WorldAnchor {
  [[nodiscard]] bool operator==(const WorldAnchor &) const = default;
};
struct NodeAnchor {
  std::string node;
  [[nodiscard]] bool operator==(const NodeAnchor &) const = default;
};
using TriggerAnchor = std::variant<std::monostate, WorldAnchor, NodeAnchor>;

struct TriggerSignal {
  TriggerOrigin origin = EventOrigin{};
  Param lifetime = 1.0f;
  std::uint32_t max = 4;
  ValueType payload = ValueType::kScalar;
  TriggerAnchor anchor;
  [[nodiscard]] bool operator==(const TriggerSignal &) const = default;
};
struct PayloadSignal {
  Ref trigger;
  [[nodiscard]] bool operator==(const PayloadSignal &) const = default;
};
struct CounterSignal {
  Ref trigger;
  std::optional<Ref> reset;
  std::optional<Param> cap;
  [[nodiscard]] bool operator==(const CounterSignal &) const = default;
};
struct AccumulateSignal {
  Ref trigger;
  Param decay = 1.0f;
  [[nodiscard]] bool operator==(const AccumulateSignal &) const = default;
};
struct NoiseSignal {
  Param frequency = 1.0f;
  Param amplitude = 1.0f;
  std::uint32_t seed = 0;
  [[nodiscard]] bool operator==(const NoiseSignal &) const = default;
};
struct GradientStop {
  float at = 0.0f;
  Vec3Param color = std::array<Param, 3>{0.0f, 0.0f, 0.0f};
  [[nodiscard]] bool operator==(const GradientStop &) const = default;
};
struct GradientSignal {
  Param t = 0.0f;
  std::vector<GradientStop> stops;
  [[nodiscard]] bool operator==(const GradientSignal &) const = default;
};
struct RateSignal {
  Ref of;
  [[nodiscard]] bool operator==(const RateSignal &) const = default;
};
struct SmoothSignal {
  Ref of;
  Param seconds = 1.0f;
  [[nodiscard]] bool operator==(const SmoothSignal &) const = default;
};
struct ToRootSignal {
  Ref of;
  [[nodiscard]] bool operator==(const ToRootSignal &) const = default;
};
struct ExprSignal {
  std::string text;
  [[nodiscard]] bool operator==(const ExprSignal &) const = default;
};

using SignalKind =
    std::variant<ConstantSignal, WaveSignal, RampSignal, EfshSignal,
                 ActorValueSignal, ActorStateSignal, EnchantmentSignal,
                 TriggerSignal, PayloadSignal, CounterSignal, AccumulateSignal,
                 NoiseSignal, GradientSignal, RateSignal, SmoothSignal,
                 ToRootSignal, ExprSignal>;

struct Signal {
  std::string name;
  SignalKind kind = ConstantSignal{};
  std::optional<CurveRef> curve;
  std::string note;
  [[nodiscard]] bool operator==(const Signal &) const = default;
};

enum class SignalKindId {
  kConstant,
  kWave,
  kRamp,
  kEfsh,
  kActorValue,
  kActorState,
  kEnchantment,
  kTrigger,
  kPayload,
  kCounter,
  kAccumulate,
  kNoise,
  kGradient,
  kRate,
  kSmooth,
  kToRoot,
  kExpr,
};
inline constexpr std::size_t kSignalKindCount = 17;

struct SignalKindSpec {
  SignalKindId value;
  std::string_view name;
  bool tunable;
};

[[nodiscard]] SignalKindId SignalKindOf(const SignalKind &a_kind) noexcept;
[[nodiscard]] std::string_view SignalKindName(SignalKindId a_kind) noexcept;
[[nodiscard]] std::optional<SignalKindId>
ParseSignalKind(std::string_view a_name) noexcept;
[[nodiscard]] bool SignalKindTunable(SignalKindId a_kind) noexcept;
[[nodiscard]] std::optional<SignalKind>
DefaultSignalKind(std::string_view a_name);

struct Curve {
  std::string name;
  std::string text;
  std::string note;
  [[nodiscard]] bool operator==(const Curve &) const = default;
};

enum class ImageChannel {
  kRgb,
  kR,
  kG,
  kB,
  kA,
  kLuma,
};
inline constexpr std::size_t kImageChannelCount = 6;
enum class ImageSpace {
  kTiled,
  kMesh,
};
inline constexpr std::size_t kImageSpaceCount = 2;
struct ImageSource {
  std::string path;
  ImageChannel channel = ImageChannel::kRgb;
  ImageSpace space = ImageSpace::kTiled;
  std::optional<Vec2Param> scroll;
  std::optional<Vec2Param> tile;
  std::array<bool, 2> mirror{false, false};
  bool transpose = false;
  float mip = 0.0f;
  [[nodiscard]] bool operator==(const ImageSource &) const = default;
};
enum class MaterialChannel {
  kDiffuseRgb,
  kDiffuseLuma,
  kNormalSlope,
  kNormalRgb,
  kRoughness,
  kMetallic,
  kOcclusion,
  kReflectance,
  kRmaosRgb,
  kDisplacement,
  kRelief,
};
inline constexpr std::size_t kMaterialChannelCount = 11;
struct MaterialSource {
  MaterialChannel channel = MaterialChannel::kDiffuseLuma;
  [[nodiscard]] bool operator==(const MaterialSource &) const = default;
};
struct PositionBake {
  [[nodiscard]] bool operator==(const PositionBake &) const = default;
};
struct LocalPositionBake {
  [[nodiscard]] bool operator==(const LocalPositionBake &) const = default;
};
struct NormalBake {
  [[nodiscard]] bool operator==(const NormalBake &) const = default;
};
struct UvBake {
  [[nodiscard]] bool operator==(const UvBake &) const = default;
};
enum class BipedSlot : std::uint32_t {};
struct BipedSlotSpec {
  BipedSlot slot;
  std::string_view name;
};
inline constexpr BipedSlot kFirstBipedSlot{30};
inline constexpr BipedSlot kLastBipedSlot{61};

[[nodiscard]] std::optional<BipedSlot>
BipedSlotFromName(std::string_view a_name) noexcept;
[[nodiscard]] std::optional<std::string_view>
BipedSlotName(BipedSlot a_slot) noexcept;

struct PartitionBake {
  BipedSlot bipedSlot{32};
  [[nodiscard]] bool operator==(const PartitionBake &) const = default;
};
struct BoneWeightBake {
  std::vector<std::string> bones;
  [[nodiscard]] bool operator==(const BoneWeightBake &) const = default;
};
struct ComponentIdBake {
  [[nodiscard]] bool operator==(const ComponentIdBake &) const = default;
};
struct ChartIdBake {
  [[nodiscard]] bool operator==(const ChartIdBake &) const = default;
};
using BakeKind =
    std::variant<PositionBake, LocalPositionBake, NormalBake, UvBake,
                 PartitionBake, BoneWeightBake, ComponentIdBake, ChartIdBake>;
struct BakeSource {
  BakeKind bake = PositionBake{};
  [[nodiscard]] bool operator==(const BakeSource &) const = default;
};
struct DistanceSource {
  std::string from;
  [[nodiscard]] bool operator==(const DistanceSource &) const = default;
};
enum class RippleShape {
  kRing,
  kDisc,
};
inline constexpr std::size_t kRippleShapeCount = 2;
struct RippleSource {
  Ref trigger;
  Param speed = 100.0f;
  Param width = 10.0f;
  Param decay = 1.0f;
  RippleShape shape = RippleShape::kRing;
  Vec3Param direction = std::array<Param, 3>{0.0f, 0.0f, 0.0f};
  [[nodiscard]] bool operator==(const RippleSource &) const = default;
};
inline constexpr std::uint8_t kMaxMaterialClusters = 8;
inline constexpr std::uint32_t kMaxClusterIterations = 256;
inline constexpr float kMaxChannelWeight = 10.0f;
struct ChannelWeights {
  float roughness = 1.0f;
  float metallic = 1.0f;
  float occlusion = 0.5f;
  float reflectance = 0.5f;
  float luma = 1.0f;
  [[nodiscard]] bool operator==(const ChannelWeights &) const = default;
};
struct ClusterSettings {
  std::uint8_t clusters = 4;
  ChannelWeights weights;
  std::uint32_t seed = 1;
  std::uint32_t iterations = 32;
  [[nodiscard]] bool operator==(const ClusterSettings &) const = default;
};
struct MaterialClustersSource {
  ClusterSettings settings;
  [[nodiscard]] bool operator==(const MaterialClustersSource &) const = default;
};

struct ClusterWeightField {
  const char *name;
  float ChannelWeights::*member;
};
inline constexpr ClusterWeightField kClusterWeightFields[]{
    {"roughness", &ChannelWeights::roughness},
    {"metallic", &ChannelWeights::metallic},
    {"occlusion", &ChannelWeights::occlusion},
    {"reflectance", &ChannelWeights::reflectance},
    {"luma", &ChannelWeights::luma},
};

using SourceKind =
    std::variant<ImageSource, MaterialSource, BakeSource, DistanceSource,
                 RippleSource, MaterialClustersSource>;
enum class SourceKindId {
  kImage,
  kMaterial,
  kBake,
  kDistance,
  kRipple,
  kMaterialClusters,
};
inline constexpr std::size_t kSourceKindCount = 6;
static_assert(kSourceKindCount == std::variant_size_v<SourceKind>);
[[nodiscard]] inline SourceKindId
SourceKindIdOf(const SourceKind &a_kind) noexcept {
  return FromIndex<SourceKindId>(a_kind.index());
}

struct Source {
  std::string name;
  SourceKind kind = MaterialSource{};
  std::string note;
  [[nodiscard]] bool operator==(const Source &) const = default;
};

struct Mask {
  std::string name;
  std::string text;
  std::string note;
  [[nodiscard]] bool operator==(const Mask &) const = default;
};

enum class Surface {
  kMaterial,
  kShell,
};
inline constexpr std::size_t kSurfaceCount = 2;
[[nodiscard]] std::string_view SurfaceName(Surface a_surface) noexcept;
[[nodiscard]] std::optional<Surface>
ParseSurface(std::string_view a_name) noexcept;

enum class Target {
  kMaterial,
  kShell,
  kLight,
};
[[nodiscard]] std::string_view TargetName(Target a_target) noexcept;
[[nodiscard]] Surface SurfaceOf(Target a_target) noexcept;
[[nodiscard]] Target TargetOf(Surface a_surface) noexcept;
enum class Slot {
  kDiffuse,
  kEmissive,
  kRmaos,
  kNormal,
  kHeight,
  kFuzz,
  kGlint,
  kCoat,
  kSubsurface,
};
inline constexpr std::size_t kSlotCount = 9;
[[nodiscard]] std::string_view SlotName(Slot a_slot) noexcept;

enum class Resolution {
  kFull,
  kHalf,
  kQuarter,
};
inline constexpr std::size_t kResolutionCount = 3;
[[nodiscard]] std::string_view ResolutionName(Resolution a_resolution) noexcept;
[[nodiscard]] std::optional<Resolution>
ParseResolution(std::string_view a_name) noexcept;
[[nodiscard]] Resolution DefaultSlotResolution(Slot a_slot) noexcept;
[[nodiscard]] std::uint32_t ResolutionDivisor(Resolution a_resolution) noexcept;

[[nodiscard]] ValueType SourceType(const Source &a_source) noexcept;

[[nodiscard]] bool IsName(std::string_view a_text) noexcept;

[[nodiscard]] std::string ParamText(const Param &a_param);
[[nodiscard]] std::optional<Param> ParseParam(std::string_view a_text);
[[nodiscard]] std::string Vec3ParamText(const Vec3Param &a_param);
[[nodiscard]] std::optional<Vec3Param> ParseVec3Param(std::string_view a_text);
[[nodiscard]] std::optional<Vec3Param> ParseColorParam(std::string_view a_text);
void NormaliseColor(std::array<Param, 3> &a_parts) noexcept;
[[nodiscard]] std::string Vec2ParamText(const Vec2Param &a_param);
[[nodiscard]] std::optional<Vec2Param> ParseVec2Param(std::string_view a_text);

struct SlotScalars {
  std::optional<Param> strength;
  std::optional<Param> scale;
  std::optional<Vec3Param> color;
  std::optional<Param> weight;
  std::optional<Param> screenSpaceScale;
  std::optional<Param> logMicrofacetDensity;
  std::optional<Param> microfacetRoughness;
  std::optional<Param> densityRandomization;
  std::optional<Param> roughness;
  std::optional<Param> level;
  std::optional<Param> thickness;
  [[nodiscard]] bool operator==(const SlotScalars &) const = default;
};

enum class Blend {
  kReplace,
  kMultiply,
  kAdd,
  kSubtract,
  kScreen,
  kReorient,
};
inline constexpr std::size_t kBlendCount = 6;

struct ChannelSet {
  bool r = true;
  bool g = true;
  bool b = true;
  bool a = true;
  [[nodiscard]] static std::optional<ChannelSet> Parse(std::string_view a_text);
  [[nodiscard]] std::string ToString() const;
  [[nodiscard]] bool operator==(const ChannelSet &) const = default;
};

using LayerSource = std::variant<Ref, Vec3>;

[[nodiscard]] std::string_view BlendName(Blend a_blend) noexcept;
[[nodiscard]] std::optional<Blend> ParseBlend(std::string_view a_name) noexcept;
struct BlendSpec {
  Blend value;
  std::string_view name;
  std::uint32_t shaderMode;
  bool normalStackOnly;
};
[[nodiscard]] std::uint32_t BlendShaderMode(Blend a_blend) noexcept;
[[nodiscard]] std::string LayerSourceText(const LayerSource &a_source);
[[nodiscard]] std::optional<LayerSource>
ParseLayerSource(std::string_view a_text);
[[nodiscard]] std::string_view
MaterialChannelName(MaterialChannel a_channel) noexcept;
[[nodiscard]] std::optional<MaterialChannel>
ParseMaterialChannel(std::string_view a_name) noexcept;
[[nodiscard]] std::string_view
ImageChannelName(ImageChannel a_channel) noexcept;
[[nodiscard]] std::optional<ImageChannel>
ParseImageChannel(std::string_view a_name) noexcept;
[[nodiscard]] std::string_view ImageSpaceName(ImageSpace a_space) noexcept;
[[nodiscard]] std::optional<ImageSpace>
ParseImageSpace(std::string_view a_name) noexcept;
[[nodiscard]] std::string_view RippleShapeName(RippleShape a_shape) noexcept;
[[nodiscard]] std::optional<RippleShape>
ParseRippleShape(std::string_view a_name) noexcept;
[[nodiscard]] std::string_view
SourceKindName(const SourceKind &a_kind) noexcept;
[[nodiscard]] std::optional<SourceKind>
DefaultSourceKind(std::string_view a_name);
[[nodiscard]] std::string_view BakeKindName(const BakeKind &a_bake) noexcept;
[[nodiscard]] std::optional<BakeKind> DefaultBakeKind(std::string_view a_name);
[[nodiscard]] std::string DescribeSource(const SourceKind &a_kind);

struct Layer {
  LayerSource source = Ref{};
  std::optional<CurveRef> curve;
  Blend blend = Blend::kReplace;
  Param opacity = 1.0f;
  std::optional<Vec3Param> color;
  std::optional<Ref> mask;
  ChannelSet channels;
  std::string note;
  [[nodiscard]] bool operator==(const Layer &) const = default;
};

struct SurfaceOutput {
  Surface surface = Surface::kMaterial;
  Slot slot = Slot::kEmissive;
  SlotScalars scalars;
  Selector selector;
  bool replace = false;
  std::optional<Resolution> resolution;
  std::vector<Layer> stack;
  std::string note;
  [[nodiscard]] bool operator==(const SurfaceOutput &) const = default;
};

struct SkinnedBones {
  std::uint32_t max = 2;
  float minShare = 0.0f;
  [[nodiscard]] bool operator==(const SkinnedBones &) const = default;
};
struct NamedBones {
  std::vector<std::string> bones;
  [[nodiscard]] bool operator==(const NamedBones &) const = default;
};
using Bones = std::variant<SkinnedBones, NamedBones>;

struct LightOutput {
  Bones bones = SkinnedBones{};
  Vec3Param offset = std::array<Param, 3>{0.0f, 0.0f, 0.0f};
  Vec3Param color = std::array<Param, 3>{1.0f, 1.0f, 1.0f};
  Param intensity = 1.0f;
  Param size = 1.4142f;
  Param cutoff = 1.0f;
  bool shadow = false;
  Selector selector;
  bool replace = false;
  std::string note;
  [[nodiscard]] bool operator==(const LightOutput &) const = default;
};

using Output = std::variant<SurfaceOutput, LightOutput>;

enum class ShellMaterial {
  kPbrCopy,
  kVanilla,
};
inline constexpr std::size_t kShellMaterialCount = 2;
enum class ShellBlend {
  kAdditive,
  kAlpha,
};
inline constexpr std::size_t kShellBlendCount = 2;

struct ShellPose {
  Vec3Param inflate = std::array<Param, 3>{0.0f, 0.0f, 0.0f};
  Vec3Param offset = std::array<Param, 3>{0.0f, 0.0f, 0.0f};
  Param scale = 1.0f;
  Vec3 scalePoint;
  Param spin = 0.0f;
  Vec3 spinAxis{0.0f, 0.0f, 1.0f};
  [[nodiscard]] bool operator==(const ShellPose &) const = default;
};

struct ShellSettings {
  ShellMaterial material = ShellMaterial::kPbrCopy;
  ShellBlend blend = ShellBlend::kAdditive;
  bool depthBias = true;
  float alphaTest = 0.0f;
  Param opacity = 1.0f;
  Param rimPower = 0.0f;
  Param emissive = 0.0f;
  ShellPose pose;
  [[nodiscard]] bool operator==(const ShellSettings &) const = default;
};

[[nodiscard]] std::string_view
ShellMaterialName(ShellMaterial a_material) noexcept;
[[nodiscard]] std::optional<ShellMaterial>
ParseShellMaterial(std::string_view a_name) noexcept;
[[nodiscard]] std::string_view ShellBlendName(ShellBlend a_blend) noexcept;
[[nodiscard]] std::optional<ShellBlend>
ParseShellBlend(std::string_view a_name) noexcept;

enum class ScalarField {
  kStrength,
  kScale,
  kColor,
  kWeight,
  kScreenSpaceScale,
  kLogMicrofacetDensity,
  kMicrofacetRoughness,
  kDensityRandomization,
  kRoughness,
  kLevel,
  kThickness,
};
inline constexpr std::size_t kScalarFieldCount = 11;
[[nodiscard]] std::string_view ScalarFieldName(ScalarField a_field) noexcept;
[[nodiscard]] std::optional<ScalarField>
ParseScalarField(std::string_view a_name) noexcept;

using ScalarMember = std::variant<std::optional<Param> SlotScalars::*,
                                  std::optional<Vec3Param> SlotScalars::*>;
struct ScalarFieldSpec {
  ScalarField value;
  std::string_view name;
  ScalarMember member;
  float fallback;
};
[[nodiscard]] float ScalarFallback(ScalarField a_field) noexcept;

enum class MaterialMap {
  kNone,
  kDiffuse,
  kNormal,
  kRmaos,
  kDisplacement,
};
[[nodiscard]] MaterialMap BaseMapOf(Slot a_slot) noexcept;

struct ImageChannelSpec {
  ImageChannel value;
  std::string_view name;
  ShaderChannel channel;
};
[[nodiscard]] ShaderChannel ShaderChannelOf(ImageChannel a_channel) noexcept;

struct MaterialChannelSpec {
  MaterialChannel value;
  std::string_view name;
  MaterialMap map;
  ShaderChannel channel;
  ValueType type;
};
[[nodiscard]] MaterialMap MaterialMapOf(MaterialChannel a_channel) noexcept;
[[nodiscard]] ShaderChannel ShaderChannelOf(MaterialChannel a_channel) noexcept;
[[nodiscard]] ValueType MaterialChannelType(MaterialChannel a_channel) noexcept;
[[nodiscard]] bool Thresholdable(MaterialChannel a_channel) noexcept;

struct SlotSpec {
  Slot value;
  std::string_view name;
  ChannelSet channels;
  std::string_view note;
  std::span<const ScalarField> scalars;
  bool scalarsRequired;
  std::span<const Slot> excludes;
  MaterialMap baseMap;
};

[[nodiscard]] std::span<const Slot> SlotsOf(Surface a_surface,
                                            ShellMaterial a_shell) noexcept;
[[nodiscard]] bool SurfaceHasSlot(Surface a_surface, ShellMaterial a_shell,
                                  Slot a_slot) noexcept;

[[nodiscard]] std::span<const ScalarField> ScalarsOf(Slot a_slot) noexcept;
[[nodiscard]] bool ScalarRequired(Slot a_slot, ScalarField a_field) noexcept;

[[nodiscard]] bool SlotsExclude(Slot a_first, Slot a_second) noexcept;

[[nodiscard]] bool BlendAllowed(Slot a_slot, Blend a_blend) noexcept;

[[nodiscard]] ChannelSet ChannelsOf(Slot a_slot) noexcept;
[[nodiscard]] std::string_view SlotChannelNote(Slot a_slot) noexcept;

[[nodiscard]] std::optional<Param> *ScalarOf(SlotScalars &a_scalars,
                                             ScalarField a_field) noexcept;
[[nodiscard]] const std::optional<Param> *
ScalarOf(const SlotScalars &a_scalars, ScalarField a_field) noexcept;

using VariantKey = std::variant<FormRef, Selector>;

struct Variant {
  std::string name;
  VariantKey key = Selector{};
  std::map<std::string, Value> overrides;
  [[nodiscard]] bool operator==(const Variant &) const = default;
};

struct Metadata {
  std::string name;
  std::string author;
  std::string description;
  std::string version;
  std::string imported;
  std::string meta;
  [[nodiscard]] bool operator==(const Metadata &) const = default;
};

struct Clock {
  float speed = 1.0f;
  [[nodiscard]] bool operator==(const Clock &) const = default;
};

inline constexpr int kRecipeFormat = 1;

inline constexpr std::size_t kMaxRecipeRows = 4096;
inline constexpr std::size_t kMaxRecipeDepth = 32;

enum class MergeMode {
  kStack,
  kReplace,
  kSampled,
};
inline constexpr std::size_t kMergeModeCount = 3;
[[nodiscard]] std::string_view MergeModeName(MergeMode a_mode) noexcept;
[[nodiscard]] std::optional<MergeMode>
ParseMergeMode(std::string_view a_name) noexcept;

struct Recipe {
  std::string id;
  Metadata metadata;
  std::vector<RecipeKey> keys;
  std::optional<int> priority;
  MergeMode mergeMode = MergeMode::kStack;
  Clock clock;
  std::vector<Signal> signals;
  std::vector<Curve> curves;
  std::vector<Source> sources;
  std::vector<Mask> masks;
  std::vector<Output> outputs;
  ShellSettings shell;
  std::vector<Variant> variants;

  [[nodiscard]] const Signal *
  FindSignal(std::string_view a_name) const noexcept;
  [[nodiscard]] const Curve *FindCurve(std::string_view a_name) const noexcept;
  [[nodiscard]] const Source *
  FindSource(std::string_view a_name) const noexcept;
  [[nodiscard]] const Mask *FindMask(std::string_view a_name) const noexcept;
  [[nodiscard]] bool operator==(const Recipe &) const = default;
};

enum class Severity {
  kWarning,
  kError,
};

struct Diagnostic {
  Severity severity = Severity::kError;
  std::string where;
  std::string message;
};

[[nodiscard]] Diagnostic
MakeDiagnostic(Severity a_severity, std::string a_where, std::string a_message);

[[nodiscard]] std::optional<Diagnostic>
DiagnosticOf(std::string_view a_where,
             const std::optional<std::string> &a_message);

struct Reporter {
  std::vector<Diagnostic> &out;
  std::string where;

  Reporter(std::vector<Diagnostic> &a_out, std::string_view a_where)
      : out(a_out), where(a_where) {}

  void Error(std::string a_message) const {
    out.push_back(
        MakeDiagnostic(Severity::kError, where, std::move(a_message)));
  }
  void Warn(std::string a_message) const {
    out.push_back(
        MakeDiagnostic(Severity::kWarning, where, std::move(a_message)));
  }
  [[nodiscard]] Reporter At(std::string_view a_where) const {
    return Reporter{out, a_where};
  }
};

[[nodiscard]] std::string SignalWhere(std::string_view a_signal);
[[nodiscard]] std::string CurveWhere(std::string_view a_curve);
[[nodiscard]] std::string SourceWhere(std::string_view a_source);
[[nodiscard]] std::string MaskWhere(std::string_view a_mask);
[[nodiscard]] std::string OutputWhere(std::size_t a_output);
[[nodiscard]] std::string LayerWhere(std::size_t a_output, std::size_t a_layer);
[[nodiscard]] std::string VariantWhere(std::string_view a_variant);
[[nodiscard]] std::string KeyWhere(const RecipeKey &a_key);

[[nodiscard]] bool RowLevel(const Diagnostic &a_diagnostic) noexcept;
[[nodiscard]] bool
HasErrors(std::span<const Diagnostic> a_diagnostics) noexcept;
[[nodiscard]] bool
HasRecipeErrors(std::span<const Diagnostic> a_diagnostics) noexcept;
[[nodiscard]] std::string
ProblemText(const std::optional<Diagnostic> &a_problem);

struct LoadResult {
  std::optional<Recipe> recipe;
  std::vector<Diagnostic> diagnostics;
  std::vector<Diagnostic> inputDiagnostics;
  [[nodiscard]] bool HasErrors() const noexcept;
  [[nodiscard]] bool HasRecipeErrors() const noexcept;
};

[[nodiscard]] LoadResult ParseRecipe(std::string_view a_json,
                                     std::string_view a_id);
[[nodiscard]] std::string SerializeRecipe(const Recipe &a_recipe);

[[nodiscard]] std::vector<Diagnostic>
Validate(const Recipe &a_recipe,
         std::span<const Diagnostic> a_inputDiagnostics = {});
[[nodiscard]] std::vector<Diagnostic> CheckRecipeFields(const Recipe &a_recipe);

struct WornPiece {
  std::optional<FormKey> magicEffect;
  std::optional<FormKey> enchantment;
  std::optional<FormKey> effectShader;
  std::optional<FormKey> armor;
  std::vector<FormKey> keywords;
  std::vector<std::string> diffusePaths;
  [[nodiscard]] bool Enchanted() const noexcept {
    return magicEffect || enchantment || effectShader;
  }
};

[[nodiscard]] std::uint32_t SamplingHash(std::uint32_t a_actor) noexcept;

struct ResolvedRecipe {
  const Recipe *recipe = nullptr;
  RecipeKey key;
  int priority = 0;
  std::size_t loadOrder = 0;
};

enum class SelectionOutcome {
  kNonmatching,
  kFallbackSuppressed,
  kSampledOut,
  kSelected
};
inline constexpr std::size_t kSelectionOutcomeCount = 4;

struct RecipeSelection {
  std::string id;
  SelectionOutcome outcome = SelectionOutcome::kNonmatching;
};

[[nodiscard]] std::string_view
SelectionOutcomeName(SelectionOutcome a_outcome) noexcept;

[[nodiscard]] std::vector<ResolvedRecipe>
Resolve(const WornPiece &a_piece, std::span<const Recipe> a_loaded,
        std::uint32_t a_seed = 0,
        std::vector<RecipeSelection> *a_selections = nullptr);

[[nodiscard]] bool AnyUnenchantedKey(std::span<const Recipe> a_loaded) noexcept;

struct PieceKey {
  KeyKind kind;
  FormKey form;
};
[[nodiscard]] std::vector<PieceKey> KeyChoicesOf(const WornPiece &a_piece);
[[nodiscard]] const PieceKey *
DefaultKeyChoice(std::span<const PieceKey> a_choices) noexcept;
[[nodiscard]] RecipeKey RecipeKeyOf(const PieceKey &a_key,
                                    std::string_view a_text);

[[nodiscard]] bool VariantApplies(const Variant &a_variant,
                                  const FormKey &a_armor) noexcept;
[[nodiscard]] bool VariantApplies(const Variant &a_variant,
                                  const GeometryIdentity &a_geometry);
[[nodiscard]] Recipe ApplyVariant(const Recipe &a_recipe,
                                  const Variant &a_variant);

[[nodiscard]] bool IsAnimated(const Recipe &a_recipe,
                              std::string_view a_signal);
[[nodiscard]] bool IsAnimated(const Recipe &a_recipe, const Source &a_source);
[[nodiscard]] bool IsAnimated(const Recipe &a_recipe, const Mask &a_mask);
[[nodiscard]] bool IsAnimated(const Recipe &a_recipe, const Output &a_output);

[[nodiscard]] bool
RecipeInputsAreActorIndependent(const Recipe &a_recipe) noexcept;
[[nodiscard]] bool ShareableAcrossActors(const Recipe &a_recipe,
                                         const Output &a_output);
[[nodiscard]] bool ShareableAcrossActors(const Recipe &a_recipe,
                                         const Mask &a_mask);
}
