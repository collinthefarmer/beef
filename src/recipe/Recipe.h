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
  kMaterial,
  kKeyword,
  kArmor,
  kEffectShader,
  kEnchantment,
  kMagicEffect,
};
inline constexpr std::size_t kKeyKindCount = 7;

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
struct PulseSignal {
  Param base = 0.0f;
  Param amplitude = 1.0f;
  Param period = 1.0f;
  Param phase = 0.0f;
  Waveform waveform = Waveform::kSine;
  [[nodiscard]] bool operator==(const PulseSignal &) const = default;
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
  kHostileDistance,
};
inline constexpr std::size_t kActorStateCount = 4;
struct ActorStateSignal {
  ActorStateKind kind = ActorStateKind::kInCombat;
  [[nodiscard]] bool operator==(const ActorStateSignal &) const = default;
};
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
  std::string at;
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

struct TriggerSignal {
  TriggerOrigin origin = EventOrigin{};
  Param lifetime = 1.0f;
  std::uint32_t max = 4;
  [[nodiscard]] bool operator==(const TriggerSignal &) const = default;
};
enum class PayloadField {
  kValue,
  kPosition,
  kNormal,
};
inline constexpr std::size_t kPayloadFieldCount = 3;
struct PayloadSignal {
  Ref trigger;
  PayloadField field = PayloadField::kValue;
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
struct DeltaSignal {
  Ref of;
  [[nodiscard]] bool operator==(const DeltaSignal &) const = default;
};
struct SmoothSignal {
  Ref of;
  Param seconds = 1.0f;
  [[nodiscard]] bool operator==(const SmoothSignal &) const = default;
};
struct ExprSignal {
  std::string text;
  [[nodiscard]] bool operator==(const ExprSignal &) const = default;
};

using SignalKind =
    std::variant<ConstantSignal, PulseSignal, RampSignal, EfshSignal,
                 ActorValueSignal, ActorStateSignal, EnchantmentSignal,
                 TriggerSignal, PayloadSignal, CounterSignal, AccumulateSignal,
                 NoiseSignal, GradientSignal, DeltaSignal, SmoothSignal,
                 ExprSignal>;

struct Signal {
  std::string name;
  SignalKind kind = ConstantSignal{};
  std::optional<CurveRef> curve;
  [[nodiscard]] bool operator==(const Signal &) const = default;
};

enum class SignalKindId {
  kConstant,
  kPulse,
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
  kDelta,
  kSmooth,
  kExpr,
};
inline constexpr std::size_t kSignalKindCount = 16;

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
  kRoughness,
  kMetallic,
  kOcclusion,
  kReflectance,
  kDisplacement,
  kRelief,
};
inline constexpr std::size_t kMaterialChannelCount = 9;
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
struct WorldUpBake {
  [[nodiscard]] bool operator==(const WorldUpBake &) const = default;
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
    std::variant<PositionBake, LocalPositionBake, WorldUpBake, PartitionBake,
                 BoneWeightBake, ComponentIdBake, ChartIdBake>;
struct BakeSource {
  BakeKind bake = PositionBake{};
  [[nodiscard]] bool operator==(const BakeSource &) const = default;
};
enum class UvAxis {
  kU,
  kV,
};
inline constexpr std::size_t kUvAxisCount = 2;
struct UvSource {
  UvAxis axis = UvAxis::kU;
  [[nodiscard]] bool operator==(const UvSource &) const = default;
};
struct DistanceSource {
  std::variant<std::string, Vec3> from = std::string{};
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
  [[nodiscard]] bool operator==(const RippleSource &) const = default;
};
inline constexpr std::uint8_t kMaxMaterialClusters = 8;
inline constexpr std::uint32_t kMaxClusterIterations = 256;
inline constexpr float kMaxChannelWeight = 10.0f;
struct MaterialClustersSource {
  std::uint8_t clusters = 4;
  float roughness = 1.0f;
  float metallic = 1.0f;
  float occlusion = 0.5f;
  float reflectance = 0.5f;
  float luma = 1.0f;
  std::uint32_t seed = 1;
  std::uint32_t iterations = 32;
  [[nodiscard]] bool operator==(const MaterialClustersSource &) const = default;
};

struct ClusterWeightField {
  const char *name;
  float MaterialClustersSource::*member;
};
inline constexpr ClusterWeightField kClusterWeightFields[]{
    {"roughness", &MaterialClustersSource::roughness},
    {"metallic", &MaterialClustersSource::metallic},
    {"occlusion", &MaterialClustersSource::occlusion},
    {"reflectance", &MaterialClustersSource::reflectance},
    {"luma", &MaterialClustersSource::luma},
};

using SourceKind =
    std::variant<ImageSource, MaterialSource, BakeSource, UvSource,
                 DistanceSource, RippleSource, MaterialClustersSource>;
enum class SourceKindId {
  kImage,
  kMaterial,
  kBake,
  kUv,
  kDistance,
  kRipple,
  kMaterialClusters,
};
inline constexpr std::size_t kSourceKindCount = 7;
static_assert(kSourceKindCount == std::variant_size_v<SourceKind>);
[[nodiscard]] inline SourceKindId
SourceKindIdOf(const SourceKind &a_kind) noexcept {
  return FromIndex<SourceKindId>(a_kind.index());
}

struct Source {
  std::string name;
  SourceKind kind = MaterialSource{};
  [[nodiscard]] bool operator==(const Source &) const = default;
};

struct Mask {
  std::string name;
  std::string text;
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
  kLerp,
  kNormal,
};
inline constexpr std::size_t kBlendCount = 7;

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
[[nodiscard]] std::string_view UvAxisName(UvAxis a_axis) noexcept;
[[nodiscard]] std::optional<UvAxis>
ParseUvAxis(std::string_view a_name) noexcept;
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
  std::optional<FormRef> bulb;
  Selector selector;
  bool replace = false;
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
  Param alpha = 1.0f;
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

struct Recipe {
  std::string id;
  Metadata metadata;
  std::vector<RecipeKey> keys;
  std::optional<int> priority;
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
  [[nodiscard]] bool HasErrors() const noexcept;
  [[nodiscard]] bool HasRecipeErrors() const noexcept;
};

[[nodiscard]] LoadResult ParseRecipe(std::string_view a_json,
                                     std::string_view a_id);
[[nodiscard]] std::string SerializeRecipe(const Recipe &a_recipe);

[[nodiscard]] std::vector<Diagnostic> Validate(const Recipe &a_recipe);

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

struct ResolvedRecipe {
  const Recipe *recipe = nullptr;
  RecipeKey key;
  int priority = 0;
};

[[nodiscard]] std::vector<ResolvedRecipe>
Resolve(const WornPiece &a_piece, std::span<const Recipe> a_loaded);

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
}
