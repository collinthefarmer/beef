#pragma once

// The studio's view models: what the compose page draws, as plain records
// built from the snapshot and the page's selection by pure functions.
// Nothing here touches ImGui or the engine; the tests build these from a
// snapshot made of the canonical recipe. The page draws a record through
// the widgets and turns what comes back into edits (Edits.h) or view
// changes (View.h).

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
	// ------------------------------------------------------------------ modes

	// A mode is a layout, a widget scale and a tool set over the same view
	// models: Compose is the board, the stack and the selected layer's
	// inspector, with the signal table folded under them; Paint adds the
	// region editor to Compose; Design is tuning while looking.
	enum class Mode
	{
		kCompose,
		kPaint,
		kDesign,
	};
	inline constexpr std::size_t                  kModeCount = 3;
	inline constexpr std::array<Mode, kModeCount> kModes{ Mode::kCompose, Mode::kPaint, Mode::kDesign };
	[[nodiscard]] std::string_view ModeName(Mode a_mode) noexcept;

	// The panes sit under each other in one narrow column as collapsible
	// sections, so the game stays in view beside the menu; the pictures are
	// sized for that column.
	struct Layout
	{
		Mode  mode = Mode::kCompose;
		bool  contextRows = true;  // the recipe and edit rows; Paint has a head line naming the recipe instead
		bool  stack = true;
		bool  inspector = true;
		bool  signals = true;  // the signal table, under a rule below the stack
		bool  regionEditor = false;
		bool  designPanel = false;
		float widgetScale = 1.0f;          // multiplies field widths and grip sizes
		float compositeSize = 160.0f;      // the stack's composite, in pixels
		float cellSize = 40.0f;            // a board cell's thumbnail
		float rowThumbnail = 32.0f;        // a stack row's thumbnail
		float inspectorThumbnail = 96.0f;
		float stackSplit = 0.5f;           // the share of the stack's width the layer list takes; the inspector takes the rest
		// Under the context rows the page is two panes, each scrolling on its
		// own: the stack (composite, scalars, layers and inspector, or the
		// target's settings) and the resources (signals, curves, later masks,
		// as tabs). The share is of the height under the rows; the stack
		// takes the rest.
		float resourcesShare = 0.35f;
		bool  developerSignals = true;  // efsh, trigger, av and other rows a designer does not tune
		bool  implemented = true;       // false shows a "mode is not built yet" placeholder instead of the body
	};
	// One row per mode, in Mode order; LayoutFor is the lookup. Paint drops
	// the resources pane (signals = false): it would edit the paint recipe,
	// which the session discards.
	inline constexpr std::array<Layout, kModeCount> kLayouts{
		Layout{ .mode = Mode::kCompose },
		Layout{ .mode = Mode::kPaint, .contextRows = false, .signals = false, .regionEditor = true },
		Layout{ .mode = Mode::kDesign, .stack = false, .inspector = false, .signals = false, .designPanel = true, .widgetScale = 1.6f, .compositeSize = 128.0f, .developerSignals = false, .implemented = false },
	};
	[[nodiscard]] Layout LayoutFor(Mode a_mode) noexcept;

	// -------------------------------------------------------------- selection

	// Where an output goes: a surface, or the recipe's light.
	enum class Target
	{
		kMaterial,
		kShell,
		kLight,
	};
	[[nodiscard]] std::string_view TargetName(Target a_target) noexcept;  // a surface's own word, or "light"
	[[nodiscard]] Surface          SurfaceOf(Target a_target) noexcept;  // the light reads as the material
	[[nodiscard]] Target           TargetOf(Surface a_surface) noexcept;

	// What the page has chosen, held as keys wherever the thing has one:
	// the piece, the recipe id, the geometry name, the target and slot, the
	// region name. No index but the layer's, which is clamped each frame.
	// Resolving a selection against a snapshot yields the chosen row or a
	// sensible default, never null on a non-empty snapshot; the output the
	// target and slot name is looked up each frame and may be none.
	struct Selection
	{
		FormID                     actorID = 0;
		FormID                     armorID = 0;
		bool                       firstPerson = false;
		std::string                recipeID;
		std::string                geometry;
		Target                     target = Target::kMaterial;
		std::optional<Slot>        slot;
		std::optional<std::size_t> layer;   // within the picked output
	};

	[[nodiscard]] const PieceRow*    SelectedPiece(const Snapshot& a_snapshot, const Selection& a_selection) noexcept;
	[[nodiscard]] const RecipeRow*   SelectedRecipe(const PieceRow* a_piece, const Selection& a_selection) noexcept;
	[[nodiscard]] const GeometryRow* SelectedGeometry(const RecipeRow* a_recipe, const Selection& a_selection) noexcept;
	// What the tick should build full rows for: the selected piece, or
	// nothing when no piece is selected yet.
	[[nodiscard]] std::optional<SnapshotRequest> RequestOf(const Selection& a_selection) noexcept;
	// The layer selection dropped when it names a row past the picked
	// stack's end (an edit the manager refused, a stack that shrank), so no
	// reader can be handed a stale index. A light row (no geometries yet)
	// leaves it alone.
	void ClampSelection(Selection& a_selection, const Snapshot& a_snapshot) noexcept;
	// The first material output on the picked surface and slot; none for the
	// light target, an unpicked slot, or a slot nothing writes.
	[[nodiscard]] const OutputRow*   SelectedOutput(const GeometryRow* a_geometry, const Selection& a_selection) noexcept;

	// ------------------------------------------------------------------ board

	// A cell is a slot on a surface. Written: the recipe has an output there.
	// Empty: it could. Absent: the surface has no such slot (never drawn).
	// Excluded: another output of the recipe takes a feature this slot cannot
	// share (drawn greyed, with the reason). Refused: the output exists but
	// the binding would not take it (the reason is the binding's).
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
		std::optional<std::size_t> output;  // when written or refused
		std::size_t                layers = 0;
		bool                       animated = false;
		bool                       replace = false;
		TextureHandle              composite = nullptr;
		std::vector<ScalarRow>     scalars;
		std::vector<std::string>   badges;  // the masks its layers use, without '@'
		bool                       isolated = false;  // the view shows this output alone
	};

	struct LightCell
	{
		bool                       present = false;
		std::optional<std::size_t> output;
		std::string                description;
		bool                       isolated = false;
	};

	// Cells are slot-major: for each slot in Slot order, the material cell
	// then the shell cell. CellAt finds one; absent cells are present in the
	// vector with state kAbsent so the grid stays rectangular.
	struct Board
	{
		std::vector<Cell>        cells;
		LightCell                light;
		std::string              shell;  // the shell's description, empty when none
	};

	[[nodiscard]] const Cell* CellAt(const Board& a_board, Surface a_surface, Slot a_slot) noexcept;
	[[nodiscard]] Board       BuildBoard(const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Selection& a_selection, const View& a_view);

	// ------------------------------------------------------------------ stack

	struct LayerStackRow
	{
		std::size_t index = 0;  // into the output's stack (file order)
		LayerRow    layer;
		bool        muted = false;
		bool        soloed = false;
		bool        selected = false;
	};

	// A layer another recipe writes on the same slot of the same geometry:
	// below when that recipe merges first, above when it merges after.
	struct ForeignRow
	{
		std::string recipe;
		int         priority = 0;
		LayerRow    layer;
	};

	// The selected cell's stack. Rows are in file order, base first, which
	// is also the order the page draws them: the last applied layer at the
	// bottom.
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
		std::vector<Blend>      blends;  // the blends this slot accepts, for the combo
		std::vector<std::string> masks;  // every mask name of the recipe, for the rows' mask combo
		std::vector<std::string> scalarSignals;  // signals of scalar type, for the scalars' fields
		std::vector<std::string> colorSignals;   // signals of colour type
		bool                    isolated = false;
	};

	[[nodiscard]] std::optional<LayerStack> BuildStackView(const PieceRow& a_piece, const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Selection& a_selection, const View& a_view);

	// -------------------------------------------------------------- inspector

	// The selected layer with its reference chain one level deep: the source
	// and mask rows it reads on this geometry, the signals its opacity and
	// colour name, the curve it applies.
	struct Inspector
	{
		std::size_t              output = 0;
		std::size_t              layer = 0;
		Slot                     slot = Slot::kEmissive;
		LayerRow                 row;
		std::optional<PictureRow>  source;
		std::optional<PictureRow>  mask;
		std::vector<SignalRow>   signals;  // referenced by opacity or colour, in that order
		std::optional<TextRow>   curve;    // a declared curve the layer names
		std::vector<Blend>       blends;
		std::vector<std::string> sources;  // every source and mask name, for the combos
		std::vector<std::string> masks;
		std::vector<std::string> curves;
		std::vector<std::string> scalarSignals;  // signals of scalar type, for opacity
		std::vector<std::string> colorSignals;   // signals of vec3 type, for colour
	};

	[[nodiscard]] std::optional<Inspector> BuildInspector(const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Selection& a_selection);

	// The recipe's light and shell as their panels show them, from the
	// recipe itself; the manager fills the snapshot with these, and the tests
	// build rows the same way.
	[[nodiscard]] LightRow LightRowOf(const Recipe& a_recipe);
	[[nodiscard]] ShellRow ShellRowOf(const Recipe& a_recipe);

	// The recipe's signal names by type, for the value fields' combos.
	struct SignalNames
	{
		std::vector<std::string> scalar;
		std::vector<std::string> color;  // vec3: colours and vectors alike
		std::vector<std::string> vec2;
		std::vector<std::string> triggers;
	};
	[[nodiscard]] SignalNames SignalNamesOf(const RecipeRow& a_recipe);

	// A source as texts and back: the row the tab shows, and the kind its
	// form rebuilds; nothing when a text does not parse.
	[[nodiscard]] SourceRow                 SourceRowOf(const Source& a_source, std::size_t a_references);
	[[nodiscard]] std::optional<SourceKind> SourceKindOf(const SourceRow& a_row);

	// ---------------------------------------------------------------- signals

	// The recipe's signals split for the Edit and Design modes: what a
	// designer tunes (constants, colours, expressions) and what only a
	// developer reads (efsh, triggers, actor values and the rest).
	struct SignalList
	{
		std::vector<SignalRow> tunable;
		std::vector<SignalRow> developer;
	};
	[[nodiscard]] SignalList BuildSignalList(const RecipeRow& a_recipe, const Layout& a_layout);

	// ---------------------------------------------------------------- filters

	// Whether a row passes a table's name filter: an empty filter passes
	// every row; otherwise the name must contain the filter, case ignored.
	[[nodiscard]] bool NameMatches(std::string_view a_name, std::string_view a_filter) noexcept;

	// ------------------------------------------------------------------ names

	// A name not among the taken ones: the stem, else the stem with the
	// first free number from 2 ("signal", "signal2", ...).
	[[nodiscard]] std::string UniqueName(std::string_view a_stem, std::span<const std::string> a_taken);

	// Names in "@name" form, for the reference combos.
	[[nodiscard]] std::string ReferenceText(std::string_view a_name);
	[[nodiscard]] std::string ReferenceName(std::string_view a_text);  // strips a leading '@'

	// What a geometry is called in the menu. An authored geometry keeps its
	// name. A geometry the engine built from an armor addon is named
	// " (<addon id>)[<index>]/ (<armor id>) [<weight>%]", which reads as
	// "<armor> geometry <index> (addon <id>)"; the raw name stays the key.
	[[nodiscard]] std::string GeometryLabel(std::string_view a_name, std::string_view a_armorName);

	// ------------------------------------------------------------- mesh facts
	// What a geometry's mesh offers, computed once per mesh read by the
	// reader's cache and copied into every geometry row that shows it: its
	// partitions by biped slot, and the bones it is skinned to with the
	// share of vertices each one moves.
	[[nodiscard]] std::vector<PartitionRow> PartitionsOf(const MeshData& a_mesh);
	[[nodiscard]] std::vector<BoneRow>      BonesOf(const MeshData& a_mesh);
	struct MeshFacts
	{
		std::vector<PartitionRow> partitions;
		std::vector<BoneRow>      bones;
	};
	[[nodiscard]] MeshFacts FactsOf(const MeshData& a_mesh);
}
