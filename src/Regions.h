#pragma once

// Region presets: the shipped file of body regions (a partition times the
// weights of a few bones) and material regions (thresholds over the
// geometry's own maps), the plain names of bones and partitions, whether a
// preset resolves on a geometry's facts, and the edits that draw one into
// the scratch mask. Engine-free; the mesh facts come from the snapshot.

#include "Edits.h"
#include "MaskStack.h"
#include "Mesh.h"
#include "Recipe.h"
#include "Snapshot.h"

#include <cstddef>
#include <cstdint>
#include <expected>
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
	[[nodiscard]] ScratchState ScratchOf(const RecipeRow& a_recipe);

	// What a recipe already has, for a term to reuse and a new row to avoid:
	// its sources by name and definition (a source of the same definition
	// is reused whatever its name), and every source and mask name taken.
	struct Existing
	{
		std::vector<std::pair<std::string, SourceKind>> sources;
		std::vector<std::string>                        taken;
	};
	[[nodiscard]] Existing ExistingOf(const RecipeRow& a_recipe);

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

	// ------------------------------------------------------------ paint recipe
	// Painting previews through the ordinary apply path: a transient recipe,
	// a clone of the active one with its outputs replaced by one emissive
	// output whose single white layer is masked by the scratch, keyed to the
	// worn armor and isolated while Paint is open. It is never written; Keep
	// copies the region back into the active recipe.
	inline constexpr std::string_view kPaintRecipe = "paint";
	inline constexpr int              kPaintPriority = 1000;

	[[nodiscard]] MaterialOutput PaintOutput(Surface a_surface);
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
