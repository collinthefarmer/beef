#pragma once

// Recipe format 1: the records, their JSON, validation, key resolution,
// variants and the static/animated classification. The brief's data model
// section is the specification and schema/recipe.schema.json the file
// format; this header is their C++ shape. Nothing here touches the engine.

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

namespace WornEnchantmentPBR
{
	// ------------------------------------------------------------------ forms

	// Load-order independent identity of a form: plugin file plus local id,
	// written "0x92DED~Skyrim.esm" (po3's convention).
	struct FormKey
	{
		std::string   file;  // as written, e.g. "Skyrim.esm"; compared case-insensitively
		std::uint32_t localId = 0;

		[[nodiscard]] static std::optional<FormKey> Parse(std::string_view a_text);
		[[nodiscard]] std::string                   ToString() const;
		[[nodiscard]] bool                          operator==(const FormKey& a_other) const noexcept;
	};

	// A form as a recipe names it: an editor ID or a form key. Form keys
	// resolve on parse; editor IDs are resolved by the store at load, which
	// fills `key`. Matching compares keys, so an unresolved reference never
	// matches and is reported.
	struct FormRef
	{
		std::string            text;
		std::optional<FormKey> key;

		[[nodiscard]] static FormRef From(std::string_view a_text);
		[[nodiscard]] bool           Resolved() const noexcept { return key.has_value(); }
		[[nodiscard]] bool           operator==(const FormRef& a_other) const noexcept { return text == a_other.text; }
	};

	// ------------------------------------------------------------- parameters

	// A number, or the name of a signal that supplies one.
	using Param = std::variant<float, Ref>;
	using Vec2Param = std::variant<std::array<Param, 2>, Ref>;
	using Vec3Param = std::variant<std::array<Param, 3>, Ref>;

	// "@name" of a declared curve, or an inline expression in x.
	struct CurveRef
	{
		std::string text;
		[[nodiscard]] std::optional<std::string> Named() const;  // the name when it is "@name"
		[[nodiscard]] bool                       operator==(const CurveRef&) const = default;
	};

	// ------------------------------------------------------------------- keys

	// In default priority order: a magic-effect recipe merges over an
	// enchantment recipe, which merges over an effect-shader recipe, and so on.
	enum class KeyKind
	{
		kDefault,
		kMaterial,
		kKeyword,
		kArmor,
		kEffectShader,
		kEnchantment,
		kMagicEffect,
	};
	inline constexpr std::size_t kKeyKindCount = 7;

	// A key's operand: none for default (matches every worn piece
	// unconditionally), a diffuse-path glob for material, a form for every
	// other kind.
	enum class KeyOperand
	{
		kNone,
		kForm,
		kGlob,
	};

	struct WornPiece;  // defined below, in the resolution section

	// One row per key kind: its word, its default merge/specificity priority
	// (DefaultPriority), its operand shape, and whether only an enchanted
	// worn piece can carry it (EnchantmentDerived). `singleForm` and
	// `formList` say how a key of this kind reads off a WornPiece: one form
	// to compare the key's form against, or (keyword alone) every form in a
	// list; both null for default (no form) and material (a glob against
	// diffusePaths, not a WornPiece field) — those two operands are matched
	// as code, not a member read. The table itself lives in Vocabulary.h,
	// once WornPiece is a complete type.
	struct KeyKindRow
	{
		KeyKind                             value;
		std::string_view                    name;
		int                                 priority;
		KeyOperand                          operand;
		bool                                enchantmentDerived;
		std::optional<FormKey> WornPiece::* singleForm;
		std::vector<FormKey> WornPiece::*   formList;
	};

	// Every word, priority, operand and enchantment fact is spelled once, in
	// Vocabulary.h's kKeyKinds; these read that table.
	[[nodiscard]] std::string_view KeyKindName(KeyKind a_kind) noexcept;
	[[nodiscard]] KeyOperand       KeyOperandOf(KeyKind a_kind) noexcept;
	[[nodiscard]] int              DefaultPriority(KeyKind a_kind) noexcept;
	[[nodiscard]] bool             EnchantmentDerived(KeyKind a_kind) noexcept;

	struct RecipeKey
	{
		KeyKind     kind = KeyKind::kDefault;
		FormRef     form;  // every kind but default and material
		std::string glob;  // material: diffuse texture path glob

		[[nodiscard]] std::string ToString() const;
		[[nodiscard]] bool        operator==(const RecipeKey&) const = default;
	};

	// -------------------------------------------------------------- selectors

	enum class SelectorKind
	{
		kAddon,     // the armor addon whose part clone holds the geometry
		kGeometry,  // the shape's name, glob
		kTexture,   // the material's diffuse path, glob
	};

	struct SelectorTerm
	{
		SelectorKind kind = SelectorKind::kGeometry;
		FormRef      form;
		std::string  glob;
		[[nodiscard]] bool operator==(const SelectorTerm&) const = default;
	};

	// Empty means every geometry; otherwise any term may match.
	struct Selector
	{
		std::vector<SelectorTerm> anyOf;
		[[nodiscard]] bool All() const noexcept { return anyOf.empty(); }
		[[nodiscard]] bool operator==(const Selector&) const = default;
	};

	struct GeometryIdentity
	{
		std::optional<FormKey> addon;
		std::string            name;
		std::string            diffusePath;
	};

	// `*` is the only wildcard; case-insensitive; `/` and `\` are equal.
	[[nodiscard]] bool GlobMatch(std::string_view a_glob, std::string_view a_text) noexcept;
	[[nodiscard]] bool Matches(const Selector& a_selector, const GeometryIdentity& a_geometry);

	// ---------------------------------------------------------------- signals

	enum class Waveform
	{
		kSine,
		kTriangle,
		kSquare,
		kSaw,
	};

	struct ConstantSignal
	{
		Value value = 0.0f;
		[[nodiscard]] bool operator==(const ConstantSignal&) const = default;
	};
	// base + amplitude * wave(phase), wave in 0..1; the phase is integrated
	// per tick so a period change never jumps.
	struct PulseSignal
	{
		Param    base = 0.0f;
		Param    amplitude = 1.0f;
		Param    period = 1.0f;
		Param    phase = 0.0f;
		Waveform waveform = Waveform::kSine;
		[[nodiscard]] bool operator==(const PulseSignal&) const = default;
	};
	struct RampSignal
	{
		Param from = 0.0f;
		Param to = 1.0f;
		Param seconds = 1.0f;
		[[nodiscard]] bool operator==(const RampSignal&) const = default;
	};
	enum class EfshField
	{
		kFillAlpha,
		kFillColor,
		kEdgeAlpha,
		kEdgeColor,
		kScroll,  // vec2
	};
	// The record's own animation through the timing port.
	struct EfshSignal
	{
		EfshField field = EfshField::kFillAlpha;
		FormRef   record;
		[[nodiscard]] bool operator==(const EfshSignal&) const = default;
	};
	// The engine's readings of an actor value; current = max - damage.
	enum class Measure
	{
		kCurrent,
		kBase,
		kPermanent,
		kTemporaryModifier,
		kDamage,  // the damage modifier negated: what has been taken off
		kMax,     // permanent plus the temporary modifier
	};
	struct ActorValueSignal
	{
		std::string actorValue;  // the engine's name, e.g. "Health"
		Measure     measure = Measure::kCurrent;
		[[nodiscard]] bool operator==(const ActorValueSignal&) const = default;
	};
	enum class ActorStateKind
	{
		kInCombat,
		kSneaking,
		kWeaponDrawn,
		kHostileDistance,
	};
	struct ActorStateSignal
	{
		ActorStateKind kind = ActorStateKind::kInCombat;
		[[nodiscard]] bool operator==(const ActorStateSignal&) const = default;
	};
	enum class EnchantmentField
	{
		kMagnitude,
		kCost,
	};
	struct EnchantmentSignal
	{
		EnchantmentField field = EnchantmentField::kMagnitude;
		[[nodiscard]] bool operator==(const EnchantmentSignal&) const = default;
	};

	struct ValueRange
	{
		std::optional<float> min;
		std::optional<float> max;
		[[nodiscard]] bool   operator==(const ValueRange&) const = default;
	};
	// Which firings of an event id to accept; empty globs accept anything.
	struct EventFilter
	{
		std::string node;
		std::string arg;
		ValueRange  value;
		[[nodiscard]] bool operator==(const EventFilter&) const = default;
	};
	// An id glob on the plugin's own bus (built in: anim.<graph event>,
	// equip; providers add the rest). `at` supplies a node when the firing has none.
	struct EventSource
	{
		std::string event;
		EventFilter filter;
		std::string at;
		[[nodiscard]] bool operator==(const EventSource&) const = default;
	};
	// A firing another SKSE plugin sends by id.
	struct PluginSource
	{
		std::string id;
		[[nodiscard]] bool operator==(const PluginSource&) const = default;
	};
	// Fires on the tick a scalar goes from at most 0 to above 0; `value` is
	// sampled into the firing.
	struct WhenSource
	{
		Ref                when;
		std::optional<Ref> value;
		[[nodiscard]] bool operator==(const WhenSource&) const = default;
	};
	using TriggerSource = std::variant<EventSource, PluginSource, WhenSource>;

	// Value: age of the newest live firing over its lifetime, 1 when none.
	struct TriggerSignal
	{
		TriggerSource source = EventSource{};
		Param         lifetime = 1.0f;
		std::uint32_t max = 4;
		[[nodiscard]] bool operator==(const TriggerSignal&) const = default;
	};
	enum class PayloadField
	{
		kValue,
		kPosition,
		kNormal,
	};
	// A field of the trigger's newest live firing; holds its last value.
	struct PayloadSignal
	{
		Ref          trigger;
		PayloadField field = PayloadField::kValue;
		[[nodiscard]] bool operator==(const PayloadSignal&) const = default;
	};
	struct CounterSignal
	{
		Ref                  trigger;
		std::optional<Ref>   reset;
		std::optional<Param> cap;
		[[nodiscard]] bool   operator==(const CounterSignal&) const = default;
	};
	struct AccumulateSignal
	{
		Ref   trigger;
		Param decay = 1.0f;  // per second
		[[nodiscard]] bool operator==(const AccumulateSignal&) const = default;
	};
	// Smooth value noise in -amplitude..amplitude.
	struct NoiseSignal
	{
		Param         frequency = 1.0f;
		Param         amplitude = 1.0f;
		std::uint32_t seed = 0;
		[[nodiscard]] bool operator==(const NoiseSignal&) const = default;
	};
	struct GradientStop
	{
		float     at = 0.0f;
		Vec3Param color = std::array<Param, 3>{ 0.0f, 0.0f, 0.0f };
		[[nodiscard]] bool operator==(const GradientStop&) const = default;
	};
	struct GradientSignal
	{
		Param                     t = 0.0f;
		std::vector<GradientStop> stops;
		[[nodiscard]] bool        operator==(const GradientSignal&) const = default;
	};
	// Change of a signal since the previous tick.
	struct DeltaSignal
	{
		Ref                of;
		[[nodiscard]] bool operator==(const DeltaSignal&) const = default;
	};
	// Exponential lag toward a signal.
	struct SmoothSignal
	{
		Ref   of;
		Param seconds = 1.0f;
		[[nodiscard]] bool operator==(const SmoothSignal&) const = default;
	};
	struct ExprSignal
	{
		std::string        text;
		[[nodiscard]] bool operator==(const ExprSignal&) const = default;
	};

	using SignalKind = std::variant<
		ConstantSignal, PulseSignal, RampSignal, EfshSignal, ActorValueSignal, ActorStateSignal,
		EnchantmentSignal, TriggerSignal, PayloadSignal, CounterSignal, AccumulateSignal, NoiseSignal,
		GradientSignal, DeltaSignal, SmoothSignal, ExprSignal>;

	struct Signal
	{
		std::string             name;
		SignalKind              kind = ConstantSignal{};
		std::optional<CurveRef> curve;
		[[nodiscard]] bool      operator==(const Signal&) const = default;
	};

	// ----------------------------------------------------------------- curves

	// A named expression in x (and mean), applied to signals, layer sources
	// and inside masks as @name(value).
	struct Curve
	{
		std::string        name;
		std::string        text;
		[[nodiscard]] bool operator==(const Curve&) const = default;
	};

	// ---------------------------------------------------------------- sources

	enum class ImageChannel
	{
		kRgb,
		kR,
		kG,
		kB,
		kA,
		kLuma,
	};
	inline constexpr std::size_t kImageChannelCount = 6;
	enum class ImageSpace
	{
		kTiled,  // placed by the transform; scrolled, it is a field
		kMesh,   // sampled by the geometry's own UV, untiled
	};
	struct ImageSource
	{
		std::string              path;  // relative to Data/Textures
		ImageChannel             channel = ImageChannel::kRgb;
		ImageSpace               space = ImageSpace::kTiled;
		std::optional<Vec2Param> scroll;
		std::optional<Vec2Param> tile;
		std::array<bool, 2>      mirror{ false, false };
		bool                     transpose = false;
		float                    mip = 0.0f;
		[[nodiscard]] bool       operator==(const ImageSource&) const = default;
	};
	enum class MaterialChannel
	{
		kDiffuseRgb,
		kDiffuseLuma,
		kNormalSlope,
		kRoughness,
		kMetallic,
		kOcclusion,
		kReflectance,
		kDisplacement,
		kRelief,  // the displacement map when the material has a real one, else occlusion
	};
	inline constexpr std::size_t kMaterialChannelCount = 9;
	// The geometry's own maps.
	struct MaterialSource
	{
		MaterialChannel    channel = MaterialChannel::kDiffuseLuma;
		[[nodiscard]] bool operator==(const MaterialSource&) const = default;
	};
	struct PositionBake
	{
		[[nodiscard]] bool operator==(const PositionBake&) const = default;
	};
	// The position mapped into the geometry's own model bound, 0..1 per axis.
	struct LocalPositionBake
	{
		[[nodiscard]] bool operator==(const LocalPositionBake&) const = default;
	};
	struct WorldUpBake
	{
		[[nodiscard]] bool operator==(const WorldUpBake&) const = default;
	};
	struct PartitionBake
	{
		std::uint32_t      slot = 32;  // biped slot number; see BipedSlotName
		[[nodiscard]] bool operator==(const PartitionBake&) const = default;
	};
	struct BoneWeightBake
	{
		std::vector<std::string> bones;
		[[nodiscard]] bool       operator==(const BoneWeightBake&) const = default;
	};
	// The mesh analysis' id map for a source of regions: each texel the
	// region id / 255, rasterised once per geometry. Building one needs the
	// analysis, not the mesh alone.
	struct ComponentIdBake
	{
		[[nodiscard]] bool operator==(const ComponentIdBake&) const = default;
	};
	struct ChartIdBake
	{
		[[nodiscard]] bool operator==(const ChartIdBake&) const = default;
	};
	using BakeKind = std::variant<PositionBake, LocalPositionBake, WorldUpBake, PartitionBake, BoneWeightBake, ComponentIdBake, ChartIdBake>;
	// Rasterised once per geometry from the mesh's own buffers.
	struct BakeSource
	{
		BakeKind           bake = PositionBake{};
		[[nodiscard]] bool operator==(const BakeSource&) const = default;
	};
	enum class UvAxis
	{
		kU,
		kV,
	};
	struct UvSource
	{
		UvAxis             axis = UvAxis::kU;
		[[nodiscard]] bool operator==(const UvSource&) const = default;
	};
	// Static bind-pose distance from a node or a point, over the position bake.
	struct DistanceSource
	{
		std::variant<std::string, Vec3> from = std::string{};
		[[nodiscard]] bool              operator==(const DistanceSource&) const = default;
	};
	enum class RippleShape
	{
		kRing,
		kDisc,
	};
	// Per live firing of a trigger: a front expanding from the firing's origin.
	struct RippleSource
	{
		Ref         trigger;
		Param       speed = 100.0f;  // units per second over the surface
		Param       width = 10.0f;
		Param       decay = 1.0f;  // per second of age
		RippleShape shape = RippleShape::kRing;
		[[nodiscard]] bool operator==(const RippleSource&) const = default;
	};
	// The material's cluster map: each texel the id / 255 of the nearest
	// cluster under these settings, rendered once per geometry. The fields
	// are the k-means settings the analysis runs with (Analysis.h's
	// ClusterSettings, which this header cannot include: Analysis includes
	// Mesh, which includes Recipe); the compositor carries them across.
	inline constexpr std::uint8_t  kMaxMaterialClusters = 8;
	inline constexpr std::uint32_t kMaxClusterIterations = 256;
	inline constexpr float         kMaxChannelWeight = 10.0f;
	struct MaterialClustersSource
	{
		std::uint8_t clusters = 4;  // 1..kMaxMaterialClusters
		// How much each channel counts in the distance between texels, 0..kMaxChannelWeight.
		float              roughness = 1.0f;
		float              metallic = 1.0f;
		float              occlusion = 0.5f;
		float              reflectance = 0.5f;
		float              luma = 1.0f;
		std::uint32_t      seed = 1;         // the same seed and sample give the same clusters
		std::uint32_t      iterations = 32;  // the k-means cap, 1..kMaxClusterIterations
		[[nodiscard]] bool operator==(const MaterialClustersSource&) const = default;
	};
	using SourceKind = std::variant<ImageSource, MaterialSource, BakeSource, UvSource, DistanceSource, RippleSource, MaterialClustersSource>;

	struct Source
	{
		std::string        name;
		SourceKind         kind = MaterialSource{};
		[[nodiscard]] bool operator==(const Source&) const = default;
	};

	// Biped slots by name (body, hands, ...) or number (30..61).
	[[nodiscard]] std::optional<std::uint32_t>    BipedSlotFromName(std::string_view a_name) noexcept;
	[[nodiscard]] std::optional<std::string_view> BipedSlotName(std::uint32_t a_slot) noexcept;

	// ------------------------------------------------------------------ masks

	// An expression evaluated per texel; source and mask names are images.
	struct Mask
	{
		std::string        name;
		std::string        text;
		[[nodiscard]] bool operator==(const Mask&) const = default;
	};

	// ---------------------------------------------------------------- outputs

	enum class Surface
	{
		kMaterial,  // the geometry's own material, private copy installed first
		kShell,     // the recipe's shell clone of the geometry
	};
	[[nodiscard]] std::string_view        SurfaceName(Surface a_surface) noexcept;
	[[nodiscard]] std::optional<Surface>  ParseSurface(std::string_view a_name) noexcept;
	enum class Slot
	{
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

	// The per-texel type a source reads as: a colour for rgb images and the
	// diffuse colour, a position bake; a scalar otherwise.
	[[nodiscard]] ValueType SourceType(const Source& a_source) noexcept;

	// A row name: letters, digits and underscores, not starting with a digit.
	[[nodiscard]] bool IsName(std::string_view a_text) noexcept;

	// ---------------------------------------------------------- text forms
	// How the menu shows and reads a parameter: "@name" for a reference, a
	// number for a constant, "r, g, b" for a constant colour; one number in a
	// colour field stands for all three components.
	[[nodiscard]] std::string                 ParamText(const Param& a_param);
	[[nodiscard]] std::optional<Param>        ParseParam(std::string_view a_text);
	[[nodiscard]] std::string                 Vec3ParamText(const Vec3Param& a_param);
	[[nodiscard]] std::optional<Vec3Param>    ParseVec3Param(std::string_view a_text);
	[[nodiscard]] std::string                 Vec2ParamText(const Vec2Param& a_param);
	[[nodiscard]] std::optional<Vec2Param>    ParseVec2Param(std::string_view a_text);

	// The scalars a slot writes beside its texture; which ones apply is per slot.
	struct SlotScalars
	{
		std::optional<Param>     strength;  // emissive
		std::optional<Param>     scale;     // height
		std::optional<Vec3Param> color;     // fuzz, subsurface
		std::optional<Param>     weight;    // fuzz
		std::optional<Param>     screenSpaceScale;  // glint
		std::optional<Param>     logMicrofacetDensity;
		std::optional<Param>     microfacetRoughness;
		std::optional<Param>     densityRandomization;
		std::optional<Param>     roughness;  // coat
		std::optional<Param>     level;      // coat
		std::optional<Param>     thickness;  // subsurface
		[[nodiscard]] bool       operator==(const SlotScalars&) const = default;
	};

	enum class Blend
	{
		kReplace,
		kMultiply,
		kAdd,
		kSubtract,
		kScreen,
		kLerp,
		kNormal,  // normal stack only: reoriented normal mapping
	};
	inline constexpr std::size_t kBlendCount = 7;

	struct ChannelSet
	{
		bool r = true;
		bool g = true;
		bool b = true;
		bool a = true;
		[[nodiscard]] static std::optional<ChannelSet> Parse(std::string_view a_text);
		[[nodiscard]] std::string                      ToString() const;
		[[nodiscard]] bool                             operator==(const ChannelSet&) const = default;
	};

	using LayerSource = std::variant<Ref, Vec3>;  // a source or mask name, or a constant colour

	[[nodiscard]] std::string_view BlendName(Blend a_blend) noexcept;
	[[nodiscard]] std::optional<Blend> ParseBlend(std::string_view a_name) noexcept;
	// One row per blend: its word, the mode the layer shader's Blend function
	// switches on, and whether only the normal stack takes it.
	struct BlendRow
	{
		Blend            value;
		std::string_view name;
		std::uint32_t    shaderMode;
		bool             normalStackOnly;
	};
	[[nodiscard]] std::uint32_t BlendShaderMode(Blend a_blend) noexcept;
	[[nodiscard]] std::string                LayerSourceText(const LayerSource& a_source);
	[[nodiscard]] std::optional<LayerSource> ParseLayerSource(std::string_view a_text);
	[[nodiscard]] std::string_view                 MaterialChannelName(MaterialChannel a_channel) noexcept;
	[[nodiscard]] std::optional<MaterialChannel>   ParseMaterialChannel(std::string_view a_name) noexcept;
	[[nodiscard]] std::string_view                 ImageChannelName(ImageChannel a_channel) noexcept;
	[[nodiscard]] std::optional<ImageChannel>      ParseImageChannel(std::string_view a_name) noexcept;
	[[nodiscard]] std::string_view                 ImageSpaceName(ImageSpace a_space) noexcept;
	[[nodiscard]] std::optional<ImageSpace>        ParseImageSpace(std::string_view a_name) noexcept;
	[[nodiscard]] std::string_view                 UvAxisName(UvAxis a_axis) noexcept;
	[[nodiscard]] std::optional<UvAxis>            ParseUvAxis(std::string_view a_name) noexcept;
	[[nodiscard]] std::string_view                 RippleShapeName(RippleShape a_shape) noexcept;
	[[nodiscard]] std::optional<RippleShape>       ParseRippleShape(std::string_view a_name) noexcept;
	// A source kind's word ("image", "material", "bake", "uv", "distance",
	// "ripple", "materialClusters") and a bake's ("position", "localPosition",
	// "worldUp", "partition", "boneWeight", "componentId", "chartId"); the
	// defaults of a kind by its word.
	[[nodiscard]] std::string_view                 SourceKindName(const SourceKind& a_kind) noexcept;
	[[nodiscard]] std::optional<SourceKind>        DefaultSourceKind(std::string_view a_name);
	[[nodiscard]] std::string_view                 BakeKindName(const BakeKind& a_bake) noexcept;
	[[nodiscard]] std::optional<BakeKind>          DefaultBakeKind(std::string_view a_name);
	// One line naming a source's kind and its settings, for the menu.
	[[nodiscard]] std::string      DescribeSource(const SourceKind& a_kind);

	struct Layer
	{
		LayerSource              source = Ref{};
		std::optional<CurveRef>  curve;
		Blend                    blend = Blend::kReplace;
		Param                    opacity = 1.0f;
		std::optional<Vec3Param> color;
		std::optional<Ref>       mask;
		ChannelSet               channels;
		[[nodiscard]] bool       operator==(const Layer&) const = default;
	};

	struct MaterialOutput
	{
		Surface            surface = Surface::kMaterial;
		Slot               slot = Slot::kEmissive;
		SlotScalars        scalars;
		Selector           selector;
		bool               replace = false;  // drop lower-priority recipes' stacks and scalars on this slot
		std::vector<Layer> stack;
		[[nodiscard]] bool operator==(const MaterialOutput&) const = default;
	};

	struct SkinnedBones
	{
		std::uint32_t max = 2;
		float         minShare = 0.0f;
		[[nodiscard]] bool operator==(const SkinnedBones&) const = default;
	};
	struct NamedBones
	{
		std::vector<std::string> bones;
		[[nodiscard]] bool       operator==(const NamedBones&) const = default;
	};
	using Bones = std::variant<SkinnedBones, NamedBones>;

	// Third person only; inverse-square through CS. Placement is the bone's
	// skinned centre when the geometry is skinned to it, else its origin,
	// plus the offset.
	struct LightOutput
	{
		Bones                  bones = SkinnedBones{};
		Vec3Param              offset = std::array<Param, 3>{ 0.0f, 0.0f, 0.0f };
		Vec3Param              color = std::array<Param, 3>{ 1.0f, 1.0f, 1.0f };
		Param                  intensity = 1.0f;
		Param                  size = 1.4142f;  // ISL source size
		Param                  cutoff = 1.0f;   // ISL cutoff override; 1 = ISL's default
		bool                   shadow = false;  // fixed at creation
		std::optional<FormRef> bulb;            // optional LIGH form created through the engine
		Selector               selector;
		bool                   replace = false;
		[[nodiscard]] bool     operator==(const LightOutput&) const = default;
	};

	using Output = std::variant<MaterialOutput, LightOutput>;

	// ------------------------------------------------------------------ shell

	enum class ShellMaterial
	{
		kPbrCopy,
		kVanilla,
	};
	enum class ShellBlend
	{
		kAdditive,
		kAlpha,
	};

	// Rewritten per tick into the shell's own skin-to-bone transforms.
	struct ShellPose
	{
		Vec3Param inflate = std::array<Param, 3>{ 0.0f, 0.0f, 0.0f };  // fractions per bone-space axis, X along the bone
		Vec3Param offset = std::array<Param, 3>{ 0.0f, 0.0f, 0.0f };   // world space
		Param     scale = 1.0f;
		Vec3      scalePoint;
		Param     spin = 0.0f;  // turns
		Vec3      spinAxis{ 0.0f, 0.0f, 1.0f };
		[[nodiscard]] bool operator==(const ShellPose&) const = default;
	};

	struct ShellSettings
	{
		ShellMaterial material = ShellMaterial::kPbrCopy;
		ShellBlend    blend = ShellBlend::kAdditive;
		bool          depthBias = true;
		float         alphaTest = 0.0f;  // 0 = off
		Param         alpha = 1.0f;
		Param         rimPower = 0.0f;  // vanilla material only
		Param         emissive = 0.0f;  // vanilla material only
		ShellPose     pose;
		[[nodiscard]] bool operator==(const ShellSettings&) const = default;
	};

	// The shell settings' words, as the file spells them.
	[[nodiscard]] std::string_view             ShellMaterialName(ShellMaterial a_material) noexcept;
	[[nodiscard]] std::optional<ShellMaterial> ParseShellMaterial(std::string_view a_name) noexcept;
	[[nodiscard]] std::string_view             ShellBlendName(ShellBlend a_blend) noexcept;
	[[nodiscard]] std::optional<ShellBlend>    ParseShellBlend(std::string_view a_name) noexcept;

	// ------------------------------------------------------------- slot rules
	// What a surface can take, as the runtime writes it. Validate and the
	// menu's board read the same functions, so a cell the board offers is one
	// the bindings accept. Material-dependent refusals (a hair model, a coat
	// flag on the material, no texture field) are the binding's to report at
	// apply and reach the menu through the snapshot's slot rows.

	enum class ScalarField
	{
		kStrength,  // emissive
		kScale,     // height
		kColor,     // fuzz, subsurface (a colour)
		kWeight,    // fuzz
		kScreenSpaceScale,  // glint
		kLogMicrofacetDensity,
		kMicrofacetRoughness,
		kDensityRandomization,
		kRoughness,  // coat
		kLevel,      // coat
		kThickness,  // subsurface
	};
	inline constexpr std::size_t kScalarFieldCount = 11;
	[[nodiscard]] std::string_view            ScalarFieldName(ScalarField a_field) noexcept;
	[[nodiscard]] std::optional<ScalarField>  ParseScalarField(std::string_view a_name) noexcept;

	// One row per scalar field: its word, which member of SlotScalars holds
	// it (a number or, for kColor, a colour), and its fallback: what the menu
	// fills in when it adds an output that needs the field, and what the
	// binding writes when a file leaves the field out. A colour takes the
	// fallback on every component.
	using ScalarMember = std::variant<std::optional<Param> SlotScalars::*, std::optional<Vec3Param> SlotScalars::*>;
	struct ScalarFieldRow
	{
		ScalarField      value;
		std::string_view name;
		ScalarMember     member;
		float            fallback;
	};
	[[nodiscard]] float ScalarFallback(ScalarField a_field) noexcept;

	// The map of the material a slot edits in place; a stack on such a slot
	// starts from that map, every other stack from transparent black.
	enum class MaterialMap
	{
		kNone,
		kDiffuse,
		kNormal,
		kRmaos,
		kDisplacement,
	};
	[[nodiscard]] MaterialMap BaseMapOf(Slot a_slot) noexcept;

	// One row per image channel: its word and the shader channel it reads.
	struct ImageChannelRow
	{
		ImageChannel     value;
		std::string_view name;
		ShaderChannel    channel;
	};
	[[nodiscard]] ShaderChannel ShaderChannelOf(ImageChannel a_channel) noexcept;

	// One row per material channel: its word; the map it reads and the
	// shader channel of that map, or kNone for a channel the compositor
	// derives (relief picks displacement or occlusion by the map's flatness,
	// normalSlope is rendered from the normal map, both read as red); what
	// a texel of it reads as. A threshold tests a scalar channel only.
	struct MaterialChannelRow
	{
		MaterialChannel  value;
		std::string_view name;
		MaterialMap      map;
		ShaderChannel    channel;
		ValueType        type;
	};
	[[nodiscard]] MaterialMap   MaterialMapOf(MaterialChannel a_channel) noexcept;
	[[nodiscard]] ShaderChannel ShaderChannelOf(MaterialChannel a_channel) noexcept;
	[[nodiscard]] ValueType     MaterialChannelType(MaterialChannel a_channel) noexcept;
	[[nodiscard]] bool          Thresholdable(MaterialChannel a_channel) noexcept;

	// One row per slot: its word; the channels its texture carries meaning
	// in and what each means to Community Shaders; the scalars beside the
	// texture and whether a recipe must give them; the slots CS cannot
	// evaluate on one material beside it; the material map it edits. The
	// functions below are questions over these rows, shared by Validate,
	// the studio's board, the parser and the bindings.
	struct SlotRow
	{
		Slot                         value;
		std::string_view             name;
		ChannelSet                   channels;
		std::string_view             note;
		std::span<const ScalarField> scalars;
		bool                         scalarsRequired;
		std::span<const Slot>        excludes;
		MaterialMap                  baseMap;
	};

	// The slots a surface offers. A material and a PBR-copy shell offer every
	// slot. A vanilla shell offers emissive only: its diffuse and normal maps
	// are never written (the binding has no path for them), and rim and
	// emissive strength are shell scalars.
	[[nodiscard]] std::span<const Slot> SlotsOf(Surface a_surface, ShellMaterial a_shell) noexcept;
	[[nodiscard]] bool                  SurfaceHasSlot(Surface a_surface, ShellMaterial a_shell, Slot a_slot) noexcept;

	// The scalars a slot carries beside its texture, and which of them a
	// recipe must give (glint's four are all optional).
	[[nodiscard]] std::span<const ScalarField> ScalarsOf(Slot a_slot) noexcept;
	[[nodiscard]] bool                         ScalarRequired(Slot a_slot, ScalarField a_field) noexcept;

	// CS evaluates one of coat, subsurface and fuzz per material, and glint
	// excludes fuzz; every other pair coexists. Symmetric; a slot never
	// excludes itself.
	[[nodiscard]] bool SlotsExclude(Slot a_first, Slot a_second) noexcept;

	// Every blend but `normal`, which only the normal stack takes.
	[[nodiscard]] bool BlendAllowed(Slot a_slot, Blend a_blend) noexcept;

	// The channels a slot's texture carries meaning in, as CS reads the map:
	// height is one channel, emissive and normal are rgb, glint has no map
	// (parameters only), the rest use alpha too (visibility on a shell's
	// diffuse; fuzz weight, coat strength, subsurface thickness).
	[[nodiscard]] ChannelSet ChannelsOf(Slot a_slot) noexcept;
	// What each channel of a slot's texture means to Community Shaders, and
	// the exceptions worth showing beside the channels field: height reads
	// red alone, glint has no texture, the diffuse's alpha is the shell's
	// visibility, and the feature maps pack a colour with a weight.
	[[nodiscard]] std::string_view SlotChannelNote(Slot a_slot) noexcept;

	// The scalar of a slot's record by field, so the menu edits one by name.
	// Null for kColor, which is a vector: use SlotScalars::color.
	[[nodiscard]] std::optional<Param>*       ScalarOf(SlotScalars& a_scalars, ScalarField a_field) noexcept;
	[[nodiscard]] const std::optional<Param>* ScalarOf(const SlotScalars& a_scalars, ScalarField a_field) noexcept;

	// --------------------------------------------------------------- variants

	using VariantKey = std::variant<FormRef, Selector>;  // an armor, or geometries below it

	// Overrides named signals with constants; never structure.
	struct Variant
	{
		std::string                  name;
		VariantKey                   key = Selector{};
		std::map<std::string, Value> overrides;
		[[nodiscard]] bool           operator==(const Variant&) const = default;
	};

	// ----------------------------------------------------------------- recipe

	struct Metadata
	{
		std::string        name;
		std::string        author;
		std::string        description;
		std::string        version;
		std::string        imported;  // "<plugin> <version>": generated and not yet edited
		std::string        meta;      // free-form JSON object text, kept verbatim
		[[nodiscard]] bool operator==(const Metadata&) const = default;
	};

	struct Clock
	{
		float speed = 1.0f;  // multiplies the global animation speed
		[[nodiscard]] bool operator==(const Clock&) const = default;
	};

	inline constexpr int kRecipeFormat = 1;

	struct Recipe
	{
		std::string            id;  // the file stem
		Metadata               metadata;
		std::vector<RecipeKey> keys;
		std::optional<int>     priority;
		Clock                  clock;
		std::vector<Signal>    signals;
		std::vector<Curve>     curves;
		std::vector<Source>    sources;
		std::vector<Mask>      masks;
		std::vector<Output>    outputs;
		ShellSettings          shell;
		std::vector<Variant>   variants;

		[[nodiscard]] const Signal* FindSignal(std::string_view a_name) const noexcept;
		[[nodiscard]] const Curve*  FindCurve(std::string_view a_name) const noexcept;
		[[nodiscard]] const Source* FindSource(std::string_view a_name) const noexcept;
		[[nodiscard]] const Mask*   FindMask(std::string_view a_name) const noexcept;
		[[nodiscard]] bool          operator==(const Recipe&) const = default;
	};

	// ------------------------------------------------------- load, save, check

	enum class Severity
	{
		kWarning,
		kError,
	};

	// `where` names the row: "signal glowLevel", "output 2 layer 0", "mask metal".
	struct Diagnostic
	{
		Severity    severity = Severity::kError;
		std::string where;
		std::string message;
	};

	struct LoadResult
	{
		std::optional<Recipe>   recipe;  // absent only when the JSON itself is unreadable
		std::vector<Diagnostic> diagnostics;
		[[nodiscard]] bool      HasErrors() const noexcept;
	};

	// Reads a file's text (comments allowed) into a recipe; rows that fail
	// are reported and skipped, and the whole recipe is validated.
	[[nodiscard]] LoadResult  ParseRecipe(std::string_view a_json, std::string_view a_id);
	// Writes the recipe in the schema's shape, defaults omitted, file order kept.
	[[nodiscard]] std::string SerializeRecipe(const Recipe& a_recipe);

	// Every reference resolves with the right type, the signal graph is
	// acyclic, expressions parse within limits, slots fit their surfaces,
	// exclusive slots are not both bound.
	[[nodiscard]] std::vector<Diagnostic> Validate(const Recipe& a_recipe);

	// ------------------------------------------------------------- resolution

	// What the manager knows about one worn piece; forms already resolved.
	struct WornPiece
	{
		std::optional<FormKey>   magicEffect;
		std::optional<FormKey>   enchantment;
		std::optional<FormKey>   effectShader;
		std::optional<FormKey>   armor;
		std::vector<FormKey>     keywords;
		std::vector<std::string> diffusePaths;  // one per geometry material
		[[nodiscard]] bool       Enchanted() const noexcept { return magicEffect || enchantment || effectShader; }
	};

	struct ResolvedRecipe
	{
		const Recipe* recipe = nullptr;
		RecipeKey     key;  // the highest-priority key that matched
		int           priority = 0;
	};

	// Every loaded recipe (in load order) with a matching key, in merge order:
	// lower priority first, ties by load order. A key held by several recipes
	// belongs to the last loaded. `default` only when nothing
	// enchantment-derived matched.
	[[nodiscard]] std::vector<ResolvedRecipe> Resolve(const WornPiece& a_piece, std::span<const Recipe> a_loaded);

	// True when some loaded recipe can apply without an enchantment.
	[[nodiscard]] bool AnyUnenchantedKey(std::span<const Recipe> a_loaded) noexcept;

	// One form the piece could be keyed to.
	struct KeyChoiceSource
	{
		KeyKind kind;
		FormKey form;
	};
	// Every form the piece could be keyed to, most specific first (by
	// DefaultPriority): every singular kind it carries, then every keyword.
	// Drives the studio's "key a new recipe to..." choices.
	[[nodiscard]] std::vector<KeyChoiceSource> KeyChoicesOf(const WornPiece& a_piece);

	[[nodiscard]] bool VariantApplies(const Variant& a_variant, const FormKey& a_armor) noexcept;
	[[nodiscard]] bool VariantApplies(const Variant& a_variant, const GeometryIdentity& a_geometry);
	// The recipe with each override's signal replaced by a constant of that value.
	[[nodiscard]] Recipe ApplyVariant(const Recipe& a_recipe, const Variant& a_variant);

	// --------------------------------------------------------- classification

	// Animated: changes between ticks. A signal is animated unless it and
	// everything it reads are constants; a source when it scrolls, ripples or
	// reads an animated parameter; a mask when it reads an animated source or
	// signal; a stack when any layer does.
	[[nodiscard]] bool IsAnimated(const Recipe& a_recipe, std::string_view a_signal);
	[[nodiscard]] bool IsAnimated(const Recipe& a_recipe, const Source& a_source);
	[[nodiscard]] bool IsAnimated(const Recipe& a_recipe, const Mask& a_mask);
	[[nodiscard]] bool IsAnimated(const Recipe& a_recipe, const Output& a_output);
}
