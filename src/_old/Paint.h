#pragma once

#include "Edits.h"
#include "Forms.h"
#include "Region.h"
#include "Mesh.h"
#include "Recipe.h"
#include "Snapshot.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects::Studio
{
	inline constexpr std::string_view kScratchMask = "scratch";

	enum class PresetKind
	{
		kWhere,
		kWhat,
	};

	struct RegionPreset
	{
		std::string                        name;
		PresetKind                         kind = PresetKind::kWhere;
		std::optional<std::uint32_t>       partition;
		std::vector<std::string>           bones;
		std::string                        expression;
		std::vector<std::pair<std::string, SourceKind>> sources;
	};

	struct RegionsFile
	{
		std::map<std::uint32_t, std::string> partitionNames;
		std::map<std::string, std::string>   boneNames;
		std::vector<RegionPreset>            where;
		std::vector<RegionPreset>            what;
	};

	inline constexpr std::size_t kMaxPresets = 256;
	inline constexpr std::size_t kMaxPresetBones = 64;
	inline constexpr std::size_t kMaxPresetSources = 16;

	[[nodiscard]] std::expected<RegionsFile, std::string> ParsePresets(std::string_view a_json);
	[[nodiscard]] std::string PlainBoneName(const RegionsFile& a_presets, std::string_view a_bone);
	[[nodiscard]] std::string PlainPartitionName(const RegionsFile& a_presets, std::uint32_t a_slot);

	[[nodiscard]] std::optional<std::string> Unresolvable(const RegionPreset& a_preset, const GeometryRow& a_geometry);

	[[nodiscard]] std::optional<std::string> ScratchOf(const RecipeRow& a_recipe);

	struct Existing
	{
		std::vector<std::pair<std::string, SourceKind>> sources;
		std::vector<std::string>                        taken;
	};
	[[nodiscard]] Existing ExistingOf(const RecipeRow& a_recipe);
	[[nodiscard]] Existing ExistingOf(const Recipe& a_recipe);

	[[nodiscard]] std::vector<RecipeEdit> ScratchEdits(std::span<const Term> a_terms, std::optional<std::size_t> a_solo, const std::set<std::size_t>& a_muted, const std::optional<std::string>& a_scratch);

	struct BuiltTerm
	{
		std::vector<RecipeEdit> edits;
		std::string             expression;
	};
	[[nodiscard]] BuiltTerm MaterialiseTerm(const RegionPreset& a_preset, const Existing& a_existing);

	[[nodiscard]] std::string TermLabel(std::string_view a_text, const RegionsFile& a_presets, const Existing& a_existing);
	[[nodiscard]] std::string ProposedRegionName(std::span<const Term> a_terms, std::string_view a_editing);

	[[nodiscard]] BuiltTerm  BuildTerm(const TermKind& a_kind, const RegionsFile& a_presets, const Existing& a_existing);
	[[nodiscard]] std::string TermLabelOf(const TermKind& a_kind, const RegionsFile& a_presets, const GeometryRow& a_geometry);

	struct TermField
	{
		FormField                                                        field;
		std::function<std::optional<TermKind>(const std::string& a_text)> apply;
	};
	[[nodiscard]] std::vector<TermField> TermForm(const TermKind& a_kind, const RegionsFile& a_presets, const GeometryRow& a_geometry);

	enum class OfferGroup
	{
		kParts,
		kCharts,
		kMaterials,
		kBones,
		kPartitions,
		kChannels,
		kPresets,
		kMasks,
		kSources,
	};
	struct OfferGroupSpec
	{
		OfferGroup       value;
		std::string_view name;
		std::string_view word;
		bool             ofGeometry;
	};
	inline constexpr OfferGroupSpec kOfferGroups[]{
		{ OfferGroup::kParts, "parts", "part", true },
		{ OfferGroup::kCharts, "charts", "chart", true },
		{ OfferGroup::kMaterials, "materials", "material", true },
		{ OfferGroup::kBones, "bones", "bone", true },
		{ OfferGroup::kPartitions, "partitions", "partition", true },
		{ OfferGroup::kChannels, "channels", "channel", true },
		{ OfferGroup::kPresets, "presets", "preset", false },
		{ OfferGroup::kMasks, "masks", "mask", false },
		{ OfferGroup::kSources, "sources", "source", false },
	};
	inline constexpr std::size_t kOfferGroupCount = 9;
	static_assert(std::size(kOfferGroups) == kOfferGroupCount);

	struct TermOffer
	{
		OfferGroup                 group;
		std::string                name;
		std::string                detail;
		std::optional<std::string> unavailable;
		std::optional<float>       coverage;
		TermKind                 kind;
		std::string                geometry;
	};
	[[nodiscard]] std::vector<TermOffer> OffersOf(const RegionsFile& a_presets, const RecipeRow& a_recipe, const GeometryRow& a_geometry, std::string_view a_editing);
	[[nodiscard]] std::vector<TermOffer> OffersOfRecipe(const RegionsFile& a_presets, const RecipeRow& a_recipe, std::string_view a_editing);
	[[nodiscard]] std::string TermDetailOf(const Term& a_term, std::span<const TermOffer> a_offers);

	inline constexpr std::string_view kPaintRecipe = "paint";
	inline constexpr int              kPaintPriority = 1000;

	[[nodiscard]] SurfaceOutput PaintOutput(Surface a_surface);
	[[nodiscard]] std::vector<RecipeEdit> PaintSurfaceEdits(Surface a_surface);
	[[nodiscard]] Recipe         PaintRecipe(const Recipe& a_active, RecipeKey a_key, Surface a_surface);
	[[nodiscard]] std::vector<RecipeEdit> KeepEdits(const Recipe& a_paint, const Recipe& a_active, std::string_view a_name);
}
