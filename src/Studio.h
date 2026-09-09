#pragma once

#include "Mesh.h"
#include "Recipe.h"
#include "Snapshot.h"
#include "View.h"

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace WornEnchantmentPBR::Studio
{

	enum class Mode
	{
		kCompose,
		kPaint,
		kDesign,
	};
	inline constexpr std::size_t                  kModeCount = 3;
	inline constexpr std::array<Mode, kModeCount> kModes{ Mode::kCompose, Mode::kPaint, Mode::kDesign };
	[[nodiscard]] std::string_view ModeName(Mode a_mode) noexcept;

	struct Layout
	{
		Mode  mode = Mode::kCompose;
		bool  contextRows = true;
		bool  stack = true;
		bool  inspector = true;
		bool  signals = true;
		bool  regionEditor = false;
		bool  designPanel = false;
		float widgetScale = 1.0f;
		float compositeSize = 160.0f;
		float cellSize = 40.0f;
		float rowThumbnail = 32.0f;
		float inspectorThumbnail = 96.0f;
		float stackSplit = 0.5f;
		float resourcesShare = 0.35f;
		bool  developerSignals = true;
		bool  implemented = true;
	};
	inline constexpr std::array<Layout, kModeCount> kLayouts{
		Layout{ .mode = Mode::kCompose },
		Layout{ .mode = Mode::kPaint, .contextRows = false, .signals = false, .regionEditor = true },
		Layout{ .mode = Mode::kDesign, .stack = false, .inspector = false, .signals = false, .designPanel = true, .widgetScale = 1.6f, .compositeSize = 128.0f, .developerSignals = false, .implemented = false },
	};
	[[nodiscard]] Layout LayoutFor(Mode a_mode) noexcept;

	struct Selection
	{
		PieceRef                   piece;
		std::string                recipeID;
		std::string                geometry;
		Target                     target = Target::kMaterial;
		std::optional<Slot>        slot;
		std::optional<std::size_t> layer;
	};

	[[nodiscard]] const PieceRow*    SelectedPiece(const Snapshot& a_snapshot, const Selection& a_selection) noexcept;
	[[nodiscard]] const RecipeRow*   SelectedRecipe(const PieceRow* a_piece, const Selection& a_selection) noexcept;
	[[nodiscard]] const GeometryRow* SelectedGeometry(const RecipeRow* a_recipe, const Selection& a_selection) noexcept;
	[[nodiscard]] std::optional<PieceRef> RequestOf(const Selection& a_selection) noexcept;
	void ResolveSelection(Selection& a_selection, const Snapshot& a_snapshot) noexcept;
	[[nodiscard]] const OutputRow*   SelectedOutput(const GeometryRow* a_geometry, const Selection& a_selection) noexcept;

	enum class CellState
	{
		kWritten,
		kEmpty,
		kAbsent,
		kExcluded,
		kRefused,
	};

	struct Cell
	{
		Surface                    surface = Surface::kMaterial;
		Slot                       slot = Slot::kEmissive;
		CellState                  state = CellState::kAbsent;
		std::string                reason;
		std::optional<std::size_t> output;
		std::size_t                layers = 0;
		bool                       animated = false;
		bool                       replace = false;
		TextureHandle              composite = nullptr;
		std::vector<ScalarRow>     scalars;
		std::vector<std::string>   badges;
		bool                       isolated = false;
	};

	struct LightCell
	{
		bool                       present = false;
		std::optional<std::size_t> output;
		std::string                description;
		bool                       isolated = false;
	};

	struct Board
	{
		std::vector<Cell>        cells;
		LightCell                light;
		std::string              shell;
	};

	[[nodiscard]] const Cell* CellAt(const Board& a_board, Surface a_surface, Slot a_slot) noexcept;
	[[nodiscard]] Board       BuildBoard(const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Selection& a_selection, const View& a_view);

	struct LayerStackRow
	{
		std::size_t index = 0;
		LayerRow    layer;
		bool        muted = false;
		bool        soloed = false;
		bool        selected = false;
	};

	struct ForeignRow
	{
		std::string recipeID;
		int         priority = 0;
		LayerRow    layer;
	};

	struct LayerStack
	{
		std::size_t             output = 0;
		Surface                 surface = Surface::kMaterial;
		Slot                    slot = Slot::kEmissive;
		std::vector<LayerStackRow>   rows;
		std::vector<ForeignRow> below;
		std::vector<ForeignRow> above;
		TextureHandle           composite = nullptr;
		bool                    animated = false;
		std::uint32_t           size = 0;
		std::string             problem;
		std::vector<ScalarRow>  scalars;
		std::vector<Blend>      blends;
		std::vector<std::string> masks;
		std::vector<std::string> scalarSignals;
		std::vector<std::string> colorSignals;
		bool                    isolated = false;
	};

	[[nodiscard]] std::optional<LayerStack> BuildStackView(const PieceRow& a_piece, const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Selection& a_selection, const View& a_view);

	struct Inspector
	{
		std::size_t              output = 0;
		std::size_t              layer = 0;
		Slot                     slot = Slot::kEmissive;
		LayerRow                 row;
		std::optional<PictureRow>  source;
		std::optional<PictureRow>  mask;
		std::vector<SignalRow>   signals;
		std::optional<TextRow>   curve;
		std::vector<Blend>       blends;
		std::vector<std::string> sources;
		std::vector<std::string> masks;
		std::vector<std::string> curves;
		std::vector<std::string> scalarSignals;
		std::vector<std::string> colorSignals;
	};

	[[nodiscard]] std::optional<Inspector> BuildInspector(const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Selection& a_selection);

	[[nodiscard]] LightRow LightRowOf(const Recipe& a_recipe);
	[[nodiscard]] ShellRow ShellRowOf(const Recipe& a_recipe);

	struct SignalNames
	{
		std::vector<std::string> scalar;
		std::vector<std::string> color;
		std::vector<std::string> vec2;
		std::vector<std::string> triggers;
	};
	[[nodiscard]] SignalNames SignalNamesOf(const RecipeRow& a_recipe);

	[[nodiscard]] SourceRow                 SourceRowOf(const Source& a_source, std::size_t a_references);
	[[nodiscard]] std::optional<SourceKind> SourceKindOf(const SourceRow& a_row);

	struct SignalList
	{
		std::vector<SignalRow> tunable;
		std::vector<SignalRow> developer;
	};
	[[nodiscard]] SignalList BuildSignalList(const RecipeRow& a_recipe, const Layout& a_layout);

	[[nodiscard]] bool NameMatches(std::string_view a_name, std::string_view a_filter) noexcept;

	[[nodiscard]] std::string UniqueName(std::string_view a_stem, std::span<const std::string> a_taken);

	[[nodiscard]] std::string ReferenceText(std::string_view a_name);
	[[nodiscard]] std::string ReferenceName(std::string_view a_text);

	[[nodiscard]] std::string GeometryLabel(std::string_view a_name, std::string_view a_armorName);

	[[nodiscard]] std::vector<PartitionRow> PartitionsOf(const MeshData& a_mesh);
	[[nodiscard]] std::vector<BoneRow>      BonesOf(const MeshData& a_mesh);
	struct MeshFacts
	{
		std::vector<PartitionRow> partitions;
		std::vector<BoneRow>      bones;
	};
	[[nodiscard]] MeshFacts FactsOf(const MeshData& a_mesh);
}
