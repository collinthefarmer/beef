#pragma once

// Region presets: the shipped file of body regions (a partition times the
// weights of a few bones) and material regions (thresholds over the
// geometry's own maps), the plain names of bones and partitions, whether a
// preset resolves on a geometry's facts, and the edits that draw one into
// the scratch mask. Engine-free; the mesh facts come from the snapshot.

#include "Edits.h"
#include "Forms.h"
#include "MaskStack.h"
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

namespace WornEnchantmentPBR::Studio
{
	// The mask the tools draw into, under a reserved name; Keep renames it,
	// Discard removes it, a save drops it.
	inline constexpr std::string_view kScratchMask = "scratch";

	// What a geometry's mesh offers: its partitions by biped slot, and the
	// bones it is skinned to with the share of vertices each one moves.
	[[nodiscard]] std::vector<PartitionRow> PartitionsOf(const MeshData& a_mesh);
	[[nodiscard]] std::vector<BoneRow>      BonesOf(const MeshData& a_mesh);
	// Both together, computed once per mesh read and copied into snapshot rows.
	struct MeshFacts
	{
		std::vector<PartitionRow> partitions;
		std::vector<BoneRow>      bones;
	};
	[[nodiscard]] MeshFacts FactsOf(const MeshData& a_mesh);

	enum class PresetKind
	{
		kWhere,  // a partition times bone weights
		kWhat,   // thresholds over material channels
	};

	struct RegionPreset
	{
		std::string                        name;
		PresetKind                         kind = PresetKind::kWhere;
		std::optional<std::uint32_t>       partition;  // where: the biped slot
		std::vector<std::string>           bones;      // where: engine names
		std::string                        expression; // what: over the source names below
		std::vector<std::pair<std::string, SourceKind>> sources;  // what: name as the expression reads it, and its definition
	};

	struct Presets
	{
		std::map<std::uint32_t, std::string> partitionNames;  // slot -> plain name
		std::map<std::string, std::string>   boneNames;       // engine name -> plain name
		std::vector<RegionPreset>            where;
		std::vector<RegionPreset>            what;
	};

	// Bounds on the preset file; a file past one is refused whole, never
	// loaded in part.
	inline constexpr std::size_t kMaxPresets = 256;  // per list
	inline constexpr std::size_t kMaxPresetBones = 64;
	inline constexpr std::size_t kMaxPresetSources = 16;

	// The file's text parsed; an error names what is wrong. A What preset's
	// expression is parsed here, so a preset that loads always sets a mask
	// that compiles.
	[[nodiscard]] std::expected<Presets, std::string> ParsePresets(std::string_view a_json);
	// The plain name of a bone or partition, or the engine's own when the
	// table lacks it.
	[[nodiscard]] std::string PlainBoneName(const Presets& a_presets, std::string_view a_bone);
	[[nodiscard]] std::string PlainPartitionName(const Presets& a_presets, std::uint32_t a_slot);

	// Why a preset cannot be made on this geometry (a partition or bone it
	// lacks), or nothing when it can. A What preset resolves anywhere.
	[[nodiscard]] std::optional<std::string> Unresolvable(const RegionPreset& a_preset, const GeometryRow& a_geometry);

	// The working selection's mask row, as the recipe has it.
	struct ScratchState
	{
		bool        present = false;
		std::string text;
	};
	[[nodiscard]] ScratchState ScratchOf(const RecipeRow& a_kind);

	// What a recipe already has, for a term to reuse and a new row to avoid:
	// its sources by name and definition (a source of the same definition
	// is reused whatever its name), and every source and mask name taken.
	struct Existing
	{
		std::vector<std::pair<std::string, SourceKind>> sources;
		std::vector<std::string>                        taken;
	};
	[[nodiscard]] Existing ExistingOf(const RecipeRow& a_kind);

	// The edits that write the stack's built expression into the scratch
	// mask: the row added when absent, the text set when it differs; "0"
	// when nothing is shown, so the row always holds an expression.
	[[nodiscard]] std::vector<RecipeEdit> ScratchEdits(std::span<const Term> a_terms, std::optional<std::size_t> a_solo, const std::set<std::size_t>& a_muted, const ScratchState& a_scratch);

	// A preset as one term of the region stack: the source edits it needs
	// (an existing source of the same definition is reused; a new one is
	// named after the preset's own name for it, unique among the taken
	// names) and the expression that reads them.
	struct PresetTerm
	{
		std::vector<RecipeEdit> edits;
		std::string             expression;
	};
	[[nodiscard]] PresetTerm MaterialiseTerm(const RegionPreset& a_preset, const Existing& a_existing);

	// Where a term's text came from, for its row: the preset whose expression
	// it is over the recipe's sources, a lone reference's name, else
	// "expression".
	[[nodiscard]] std::string TermLabel(std::string_view a_text, const Presets& a_presets, const Existing& a_existing);
	// A kept mask's expression as terms with their labels, for editing.
	[[nodiscard]] std::optional<std::vector<Term>> TermsOfMask(std::string_view a_text, const Presets& a_presets, const Existing& a_existing);
	// A name for Keep from the ingredients: the labels run together in
	// camel case ("chestLeather"), the mask being edited when there is one,
	// "region" when nothing names it.
	[[nodiscard]] std::string ProposedRegionName(std::span<const Term> a_terms, std::string_view a_editing);

	// ------------------------------------------------------------ term templates
	// BuildTerm turns a recipe into the sources it needs (twins reused, as
	// MaterialiseTerm) and its expression; ReadTerm recovers the template a
	// text fits over the recipe's rows, RawTerm when none does; TermLabelOf
	// is the row's words for a recipe (a region's measurements, a preset's
	// name, a reference).
	[[nodiscard]] PresetTerm  BuildTerm(const TermKind& a_kind, const Presets& a_presets, const Existing& a_existing);
	[[nodiscard]] TermKind  ReadTerm(std::string_view a_text, const Presets& a_presets, const RecipeRow& a_kind);
	[[nodiscard]] std::string TermLabelOf(const TermKind& a_kind, const Presets& a_presets, const GeometryRow& a_geometry);

	// A term's settings as a form: one field per setting of the recipe,
	// drawn like any field; a committed text becomes the recipe with that
	// setting changed (or nothing when it does not parse), and the page
	// rebuilds the term from it. A RawTerm has no fields.
	struct TermField
	{
		FieldSpec                                                        field;
		std::function<std::optional<TermKind>(const std::string& a_text)> apply;
	};
	[[nodiscard]] std::vector<TermField> TermForm(const TermKind& a_kind, const Presets& a_presets, const GeometryRow& a_geometry);

	// ------------------------------------------------------------------ offers
	// What the piece can be shown to have, as rows for the Add popup: the
	// mesh's parts and the material's clusters with their measurements, the
	// bones, partitions and channels, the what presets whose range holds
	// texels, and the recipe's masks and sources. An offer the piece cannot
	// make says why; coverage, when measured, is the offer's share of the
	// piece's texels.

	// One section of the Add popup, in display order.
	enum class OfferGroup
	{
		kParts,
		kMaterials,
		kBones,
		kPartitions,
		kChannels,
		kPresets,
		kMasks,
		kSources,
	};
	// A group's word and whether its section starts open: open for the
	// groups the analysis already knows enough to fill.
	struct OfferGroupRow
	{
		OfferGroup       value;
		std::string_view name;
		bool             openByDefault;
	};
	inline constexpr OfferGroupRow kOfferGroups[]{
		{ OfferGroup::kParts, "parts", true },
		{ OfferGroup::kMaterials, "materials", true },
		{ OfferGroup::kBones, "bones", false },
		{ OfferGroup::kPartitions, "partitions", false },
		{ OfferGroup::kChannels, "channels", false },
		{ OfferGroup::kPresets, "presets", false },
		{ OfferGroup::kMasks, "masks", false },
		{ OfferGroup::kSources, "sources", false },
	};
	inline constexpr std::size_t kOfferGroupCount = 8;
	static_assert(std::size(kOfferGroups) == kOfferGroupCount);

	struct TermOffer
	{
		OfferGroup                 group;
		std::string                name;
		std::string                detail;  // the measurements
		std::optional<std::string> unavailable;
		std::optional<float>       coverage;
		TermKind                 kind;
	};
	[[nodiscard]] std::vector<TermOffer> OffersOf(const Presets& a_presets, const RecipeRow& a_recipe, const GeometryRow& a_geometry, std::string_view a_editing);
	// A term's measurements for its row: the detail of the offer it came
	// from when one matches, else what the recipe itself says (a threshold's
	// range, a reference's name, a raw term's text).
	[[nodiscard]] std::string TermDetailOf(const Term& a_term, std::span<const TermOffer> a_offers);

	// ------------------------------------------------------------ paint recipe
	// Painting previews through the ordinary apply path: a transient recipe,
	// a clone of the active one with its outputs replaced by one emissive
	// output whose single white layer is masked by the scratch, keyed to the
	// worn armor and isolated while Paint is open. It is never written; Keep
	// copies the region back into the active recipe.
	inline constexpr std::string_view kPaintRecipe = "paint";
	inline constexpr int              kPaintPriority = 1000;

	[[nodiscard]] SurfaceOutput PaintOutput(Surface a_surface);
	[[nodiscard]] Recipe         PaintRecipe(const Recipe& a_active, RecipeKey a_key, Surface a_surface);
	// The edits that put the paint recipe's scratch mask into the active
	// recipe under a name: every source the text reads that the active
	// recipe lacks (a source of the same definition under another name is
	// reused and the text repointed; a taken name is made unique), then the
	// mask added or set. Empty when the paint recipe has no scratch.
	[[nodiscard]] std::vector<RecipeEdit> KeepEdits(const Recipe& a_paint, const Recipe& a_active, std::string_view a_name);
}

// ------------------------------------------------------------- mesh facts
// What the snapshot shows of a mesh, computed once per cached mesh by the
// reader's cache and copied into every geometry row that shows it.
