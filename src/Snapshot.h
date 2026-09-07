#pragma once

// The menu's read model: a copy of everything the runtime knows per actor,
// piece, recipe, geometry and output, taken once per page draw by
// Manager::LatestSnapshot. Engine-free so the studio's view models compile
// natively and are tested without the game. The texture pointers are opaque
// here: the lab keeps them alive, and only the menu's widgets dereference
// them, through TextureLab::Preview.

#include "Analysis.h"
#include "Core.h"
#include "Recipe.h"
#include "View.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace RE
{
	class NiSourceTexture;
}

namespace WornEnchantmentPBR::Studio
{
	using TextureHandle = RE::NiSourceTexture*;
	using FormID = std::uint32_t;

	// A signal of the applied recipe: its live value this tick and, for the
	// kinds a designer tunes, the editable text.
	struct SignalRow
	{
		std::string          name;
		std::string          kind;  // KindName: "constant", "expr", "efsh", "trigger", ...
		ValueType            type = ValueType::kScalar;
		Value                value;
		bool                 inert = false;
		std::optional<Value> constant;  // a constant signal's value, editable
		std::string          text;      // an expr signal's expression, editable
		std::string          curve;     // the row's curve, editable
		std::string          problem;   // why the row is inert, when it is
		std::string          event;     // a trigger's event id, for the fire button; empty otherwise
		std::size_t          references = 0;  // places that name it; 0 = removable
	};

	struct TextRow
	{
		std::string name;
		std::string text;
		std::size_t references = 0;  // places that name it; 0 = removable
	};

	// A layer as the file has it, with the compositor's verdict where it has one.
	struct LayerRow
	{
		std::string   source;  // LayerSourceText: "@name" or "r, g, b"
		std::string   mask;    // "@name" or empty
		std::string   blend;   // BlendName
		float         opacity = 1.0f;
		std::string   opacityText;  // ParamText
		std::string   color;        // Vec3ParamText or empty
		std::string   curve;
		std::string   channels;  // ChannelSet::ToString
		std::string   problem;   // why the layer is skipped, when it is
		TextureHandle texture = nullptr;  // the source image, when it is one
	};

	struct ScalarRow
	{
		std::string name;  // ScalarFieldName
		Value       value;
		std::string text;  // the parameter as written
	};

	struct OutputRow
	{
		std::size_t            index = 0;  // into Recipe::outputs
		std::string            target;     // "material", "shell", "light"
		Surface                surface = Surface::kMaterial;
		Slot                   slot = Slot::kEmissive;  // material outputs
		std::string            slotName;                // SlotName, empty for a light
		bool                   light = false;
		bool                   replace = false;
		bool                   animated = false;
		std::uint32_t          size = 0;
		std::string            problem;
		std::vector<ScalarRow> scalars;  // the slot's scalars, resolved now
		std::vector<LayerRow>  layers;
		TextureHandle          texture = nullptr;  // the composite
	};

	// A source or mask of the recipe as the compositor reads it on this geometry.
	struct ImageRow
	{
		std::string   name;
		std::string   kind;  // DescribeSource, or the mask's expression
		ValueType     type = ValueType::kScalar;  // what the texel reads as (SourceType; a mask is scalar)
		TextureHandle texture = nullptr;
		std::uint32_t channel = 4;  // preview channel: 0..3, 4 rgb, 5 luminance
		bool          animated = false;
		std::string   problem;
	};

	// One slot of a surface as the binding wrote it. `problem` is the
	// binding's refusal when the material cannot take the slot (a hair model,
	// a coat flag, no texture field), which is how material rules reach the
	// board.
	struct SlotRow
	{
		Slot        slot = Slot::kEmissive;
		std::string original;
		std::string written;
		std::string problem;
	};

	// What the geometry's mesh offers, once read: partitions by biped slot,
	// and the bones it is skinned to with the share of vertices each moves.
	struct PartitionRow
	{
		std::uint32_t slot = 0;
		std::string   name;
		std::size_t   triangles = 0;
	};
	struct BoneRow
	{
		std::string name;
		float       coverage = 0.0f;  // 0..1
	};

	struct GeometryRow
	{
		std::string            name;
		bool                   privateMaterial = false;
		bool                   meshRead = false;  // the rows below are filled once the mesh has been read
		std::vector<PartitionRow> partitions;
		std::vector<BoneRow>      bones;
		std::vector<MeshRegion>      regions;   // the mesh analysis, copied from the cache entry
		std::vector<MaterialCluster> clusters;  // the material analysis at its default settings
		std::string            shell;  // description, empty when none
		std::vector<SlotRow>   materialSlots;
		std::vector<SlotRow>   shellSlots;
		std::vector<ImageRow>  sources;
		std::vector<ImageRow>  masks;
		std::vector<OutputRow> outputs;
	};

	// A source as the file has it, every setting as text, for the Sources
	// tab and its form. Only the kind's own settings are filled; toggles read
	// "on" or "off". Studio::SourceRowOf builds one from a Source and
	// Studio::SourceKindOf reads one back.
	struct SourceRow
	{
		std::string name;
		std::string kind;  // SourceKindName
		std::string path;  // image
		std::string channel;
		std::string space;
		std::string scroll;  // Vec2ParamText, empty for none
		std::string tile;
		std::string mirrorU;
		std::string mirrorV;
		std::string transpose;
		std::string mip;
		std::string material;   // material: the channel
		std::string bake;       // bake: BakeKindName
		std::string partition;  // bake partition: the biped slot's name
		std::string bones;      // bake boneWeight: comma-separated
		std::string axis;       // uv
		std::string from;       // distance: a node name, or "x, y, z"
		std::string trigger;    // ripple: "@name"
		std::string speed;
		std::string width;
		std::string decay;
		std::string shape;
		std::string clusters;    // materialClusters: a whole number
		std::string weights;     // materialClusters: "roughness, metallic, occlusion, reflectance, luma"
		std::string seed;
		std::string iterations;
		std::size_t references = 0;
	};

	// The recipe's light as the file has it, for its panel: every parameter
	// as text, the bones as a kind with its settings.
	struct LightRow
	{
		bool        present = false;
		std::size_t output = 0;  // into Recipe::outputs
		std::string color;       // Vec3ParamText
		std::string intensity;   // ParamText
		std::string size;
		std::string cutoff;
		std::string offset;      // Vec3ParamText
		bool        shadow = false;
		std::string bones;       // "skinned" or "named"
		std::string bonesMax;    // skinned: how many
		std::string bonesMinShare;
		std::string bonesNames;  // named: comma-separated
	};

	// The recipe's shell settings as the file has it, for its panel.
	struct ShellRow
	{
		ShellMaterial material = ShellMaterial::kPbrCopy;
		ShellBlend    blend = ShellBlend::kAdditive;
		bool          depthBias = true;
		float         alphaTest = 0.0f;
		std::string   alpha;     // ParamText
		std::string   rimPower;
		std::string   emissive;
		std::string   inflate;   // Vec3ParamText
		std::string   offset;
		std::string   scale;
		std::string   spin;
		Vec3          scalePoint;
		Vec3          spinAxis{ 0.0f, 0.0f, 1.0f };
	};

	struct RecipeRow
	{
		std::string                id;
		std::string                key;
		int                        priority = 0;
		float                      time = 0.0f;
		bool                       dirty = false;
		ShellMaterial              shellMaterial = ShellMaterial::kPbrCopy;
		std::vector<SignalRow>     signals;
		std::vector<TextRow>       curves;
		std::vector<std::string>   masks;  // every mask name, even where no geometry is bound
		std::vector<TextRow>       maskRows;  // the masks with their expressions and reference counts
		std::vector<SourceRow>     sourceRows;
		std::vector<GeometryRow>   geometries;
		std::string                light;  // description, empty when none
		std::optional<std::size_t> lightOutput;
		std::vector<Diagnostic>    problems;  // the store's row problems for this recipe
		std::size_t                undoDepth = 0;  // edits that Undo would take back
		std::size_t                redoDepth = 0;
		LightRow                   lightRow;
		ShellRow                   shellRow;
	};

	// A key a new recipe could take from the worn piece: the magic effect,
	// enchantment and effect shader it carries, the armor, its keywords.
	struct KeyChoice
	{
		KeyKind     kind = KeyKind::kArmor;
		std::string text;  // the editor ID when known, else the form key
		FormKey     key;
	};

	struct PieceRow
	{
		FormID                 actorID = 0;
		std::string            actorName;
		FormID                 armorID = 0;
		std::string            armorName;
		bool                   firstPerson = false;
		std::vector<KeyChoice> keys;     // what a new recipe can be keyed to, most specific first
		std::vector<RecipeRow> recipes;  // merge order, lowest priority first
	};

	// What the menu is looking at: the tick builds full rows for that piece
	// alone and light rows (ids, keys, depths) for every other piece.
	struct SnapshotRequest
	{
		FormID actorID = 0;
		FormID armorID = 0;
		bool   firstPerson = false;
	};

	// Built on the game thread once per tick while the menu watches, and
	// published whole: the menu reads one immutable snapshot per frame and
	// never the live state. Equal versions are the same rows.
	struct Snapshot
	{
		// The rows by their names, so Manager::Snapshot::PieceRow still reads.
		using SignalRow = Studio::SignalRow;
		using TextRow = Studio::TextRow;
		using LayerRow = Studio::LayerRow;
		using ScalarRow = Studio::ScalarRow;
		using OutputRow = Studio::OutputRow;
		using ImageRow = Studio::ImageRow;
		using SlotRow = Studio::SlotRow;
		using GeometryRow = Studio::GeometryRow;
		using RecipeRow = Studio::RecipeRow;
		using PieceRow = Studio::PieceRow;

		std::uint64_t         version = 0;
		std::uint32_t         tickMS = 0;
		View                  view;  // how the piece was looked at when the rows were built
		std::vector<PieceRow> pieces;
	};
}
