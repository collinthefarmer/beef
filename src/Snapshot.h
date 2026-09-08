#pragma once

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

	struct SignalRow
	{
		std::string          name;
		SignalKindId         kind = SignalKindId::kConstant;
		ValueType            type = ValueType::kScalar;
		Value                value;
		bool                 inert = false;
		std::optional<Value> constant;
		std::string          text;
		std::string          curve;
		std::string          problem;
		std::string          event;
		std::size_t          references = 0;
		SignalKind           definition = ConstantSignal{};
	};

	struct TextRow
	{
		std::string name;
		std::string text;
		std::size_t references = 0;
	};

	struct LayerRow
	{
		std::string   source;
		std::string   mask;
		std::string   blend;
		float         opacity = 1.0f;
		std::string   opacityText;
		std::string   color;
		std::string   curve;
		std::string   channels;
		std::string   problem;
		TextureHandle texture = nullptr;
	};

	struct ScalarRow
	{
		std::string name;
		Value       value;
		std::string text;
	};

	struct OutputRow
	{
		std::size_t            index = 0;
		std::string            target;
		Surface                surface = Surface::kMaterial;
		Slot                   slot = Slot::kEmissive;
		std::string            slotName;
		bool                   light = false;
		bool                   replace = false;
		bool                   animated = false;
		std::uint32_t          size = 0;
		std::string            problem;
		std::vector<ScalarRow> scalars;
		std::vector<LayerRow>  layers;
		TextureHandle          texture = nullptr;
	};

	struct PictureRow
	{
		std::string   name;
		std::string   description;
		ValueType     type = ValueType::kScalar;
		TextureHandle texture = nullptr;
		ShaderChannel channel = ShaderChannel::kRgb;
		bool          animated = false;
		std::string   problem;
	};

	struct SlotRow
	{
		Slot        slot = Slot::kEmissive;
		std::string original;
		std::string written;
		std::string problem;
	};

	struct PartitionRow
	{
		std::uint32_t slot = 0;
		std::string   name;
		std::size_t   triangles = 0;
	};
	struct BoneRow
	{
		std::string name;
		float       coverage = 0.0f;
	};

	struct GeometryRow
	{
		std::string            name;
		bool                   privateMaterial = false;
		bool                   meshRead = false;
		std::vector<PartitionRow> partitions;
		std::vector<BoneRow>      bones;
		std::vector<MeshIsland>      islands;
		std::vector<MaterialCluster> clusters;
		std::string            shell;
		std::vector<SlotRow>   materialSlots;
		std::vector<SlotRow>   shellSlots;
		std::vector<PictureRow>  sources;
		std::vector<PictureRow>  masks;
		std::vector<OutputRow> outputs;
	};

	struct SourceRow
	{
		std::string name;
		std::string kind;
		std::string path;
		std::string channel;
		std::string space;
		std::string scroll;
		std::string tile;
		std::string mirrorU;
		std::string mirrorV;
		std::string transpose;
		std::string mip;
		std::string material;
		std::string bake;
		std::string partition;
		std::string bones;
		std::string axis;
		std::string from;
		std::string trigger;
		std::string speed;
		std::string width;
		std::string decay;
		std::string shape;
		std::string clusters;
		std::string weights;
		std::string seed;
		std::string iterations;
		std::size_t references = 0;
	};

	struct LightRow
	{
		bool        present = false;
		std::size_t output = 0;
		std::string color;
		std::string intensity;
		std::string size;
		std::string cutoff;
		std::string offset;
		bool        shadow = false;
		std::string bones;
		std::string bonesMax;
		std::string bonesMinShare;
		std::string bonesNames;
	};

	struct ShellRow
	{
		ShellMaterial material = ShellMaterial::kPbrCopy;
		ShellBlend    blend = ShellBlend::kAdditive;
		bool          depthBias = true;
		float         alphaTest = 0.0f;
		std::string   alpha;
		std::string   rimPower;
		std::string   emissive;
		std::string   inflate;
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
		std::vector<RecipeKey>     keys;
		int                        priority = 0;
		float                      time = 0.0f;
		bool                       dirty = false;
		ShellMaterial              shellMaterial = ShellMaterial::kPbrCopy;
		std::vector<SignalRow>     signals;
		std::vector<TextRow>       curves;
		std::vector<std::string>   masks;
		std::vector<TextRow>       maskRows;
		std::vector<SourceRow>     sourceRows;
		std::vector<GeometryRow>   geometries;
		std::string                light;
		std::optional<std::size_t> lightOutput;
		std::vector<Diagnostic>    problems;
		std::size_t                undoDepth = 0;
		std::size_t                redoDepth = 0;
		LightRow                   lightRow;
		ShellRow                   shellRow;
	};

	struct KeyChoice
	{
		PieceKey    key;
		std::string text;
	};

	struct PieceRow
	{
		FormID                 actorID = 0;
		std::string            actorName;
		FormID                 armorID = 0;
		std::string            armorName;
		bool                   firstPerson = false;
		std::vector<KeyChoice> keys;
		std::vector<RecipeRow> recipes;
	};

	struct SnapshotRequest
	{
		FormID actorID = 0;
		FormID armorID = 0;
		bool   firstPerson = false;
	};

	struct Snapshot
	{
		using SignalRow = Studio::SignalRow;
		using TextRow = Studio::TextRow;
		using LayerRow = Studio::LayerRow;
		using ScalarRow = Studio::ScalarRow;
		using OutputRow = Studio::OutputRow;
		using PictureRow = Studio::PictureRow;
		using SlotRow = Studio::SlotRow;
		using GeometryRow = Studio::GeometryRow;
		using RecipeRow = Studio::RecipeRow;
		using PieceRow = Studio::PieceRow;

		std::uint64_t         version = 0;
		std::uint32_t         tickMS = 0;
		View                  view;
		std::vector<PieceRow> pieces;
	};
}
