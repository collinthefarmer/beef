#pragma once

// The studio's view models: what the compose page draws, as plain records
// built from the snapshot and the page's selection by pure functions.
// Nothing here touches ImGui or the engine; the tests build these from a
// snapshot made of the canonical recipe. The page draws a record through
// the widgets and turns what comes back into edits (Edits.h) or view
// changes (View.h).

#include "Edits.h"
#include "Recipe.h"
#include "Snapshot.h"
#include "View.h"

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace WornEnchantmentPBR::Studio
{
	// ------------------------------------------------------------------ modes

	// A mode is a layout, a widget scale and a tool set over the same view
	// models: Compose is the board, the stack and the selected layer's
	// inspector; Signals is the signal table, and later the triggers and the
	// debug clock; Paint adds the region editor to Compose; Design is tuning
	// while looking.
	enum class Mode
	{
		kCompose,
		kSignals,
		kPaint,
		kDesign,
	};
	inline constexpr std::array<Mode, 4> kModes{ Mode::kCompose, Mode::kSignals, Mode::kPaint, Mode::kDesign };
	[[nodiscard]] std::string_view ModeName(Mode a_mode) noexcept;

	// The panes sit under each other in one narrow column as collapsible
	// sections, so the game stays in view beside the menu; the pictures are
	// sized for that column.
	struct Layout
	{
		Mode  mode = Mode::kCompose;
		bool  stack = true;
		bool  inspector = true;
		bool  signals = false;  // the signal table
		bool  regionEditor = false;
		bool  designPanel = false;
		float widgetScale = 1.0f;          // multiplies field widths and grip sizes
		float compositeSize = 160.0f;      // the stack's composite, in pixels
		float cellSize = 40.0f;            // a board cell's thumbnail
		float rowThumbnail = 32.0f;        // a stack row's thumbnail
		float inspectorThumbnail = 96.0f;
		float stackSplit = 0.5f;           // the share of the stack's width the layer list takes; the inspector takes the rest
		bool  developerSignals = true;  // efsh, trigger, av and other rows a designer does not tune
	};
	[[nodiscard]] Layout LayoutFor(Mode a_mode) noexcept;

	// -------------------------------------------------------------- selection

	// What the page has chosen. Resolving it against a snapshot yields the
	// chosen row or a sensible default, never null on a non-empty snapshot.
	struct Selection
	{
		FormID                     actorID = 0;
		FormID                     armorID = 0;
		bool                       firstPerson = false;
		std::string                recipeID;
		std::string                geometry;
		std::optional<std::size_t> output;  // the selected cell, by output index
		std::optional<std::size_t> layer;   // within that output
		std::string                region;  // a mask name; empty = whole piece
	};

	[[nodiscard]] const PieceRow*    SelectedPiece(const Snapshot& a_snapshot, const Selection& a_selection) noexcept;
	[[nodiscard]] const RecipeRow*   SelectedRecipe(const PieceRow* a_piece, const Selection& a_selection) noexcept;
	[[nodiscard]] const GeometryRow* SelectedGeometry(const RecipeRow* a_recipe, const Selection& a_selection) noexcept;
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
		std::size_t                layersInRegion = 0;  // of them, masked by the selected region
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
		std::vector<std::string> regions;  // every mask name of the recipe
		std::string              region;   // the selected one, empty = whole piece
		std::string              shell;    // the shell's description, empty when none
	};

	[[nodiscard]] const Cell* CellAt(const Board& a_board, Surface a_surface, Slot a_slot) noexcept;
	[[nodiscard]] Board       BuildBoard(const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Selection& a_selection, const View& a_view);

	// ------------------------------------------------------------------ stack

	struct StackRow
	{
		std::size_t index = 0;  // into the output's stack (file order)
		LayerRow    layer;
		bool        inRegion = true;  // masked by the selected region, or no region selected
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
	struct StackView
	{
		std::size_t             output = 0;
		Surface                 surface = Surface::kMaterial;
		Slot                    slot = Slot::kEmissive;
		std::string             title;  // "emissive on shell"
		std::vector<StackRow>   rows;
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

	[[nodiscard]] std::optional<StackView> BuildStackView(const PieceRow& a_piece, const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Selection& a_selection, const View& a_view);

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
		std::optional<ImageRow>  source;
		std::optional<ImageRow>  mask;
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

	// ------------------------------------------------------------------ forms

	// What a field requires: the kind of value decides the badge it wears,
	// the rule its tooltip states, and whether a @signal may stand in for the
	// value (scalar, colour and vector).
	enum class FieldKind
	{
		kScalar,      // a number, or @signal of scalar type
		kColor,       // r, g, b (one number for all three), or @signal of colour type
		kVector,      // x, y, z as a position, direction or scale, or @signal of vector type
		kReference,   // @name of a row
		kExpression,  // the recipe language
		kCurve,       // an expression in x, or @curve
		kMask,        // an expression per texel over sources and masks
		kChannels,    // a subset of rgba
	};

	// The detail modal a field opens: the row its text names, shown with its
	// picture or its own editor.
	enum class FieldDetail
	{
		kSource,
		kCurve,
		kOpacity,
		kColor,
		kMask,
	};
	[[nodiscard]] std::string_view FieldDetailName(FieldDetail a_detail) noexcept;

	// Committed text becomes an edit, or nothing when it does not parse.
	using FieldBinding = std::function<std::optional<RecipeEdit>(const std::string&)>;

	// One field of a form: what the page draws as a row of the field table.
	// `detail` is set only when the modal would show something, so a detail
	// button appears only where there is content.
	struct FieldSpec
	{
		std::string                name;
		FieldKind                  kind = FieldKind::kScalar;
		std::string                text;   // the value as written
		std::vector<std::string>   names;  // what the signal or reference combo offers
		bool                       allowEmpty = false;
		std::optional<FieldDetail> detail;
		std::optional<Value>       value;  // the live value, shown as a swatch before the input
		FieldBinding               bind;
	};

	// The selected layer's fields: source, curve, opacity, colour, mask,
	// channels, in that order.
	[[nodiscard]] std::vector<FieldSpec> InspectorForm(const Inspector& a_inspector);
	// The stack's slot scalars, one field each, in the slot's order.
	[[nodiscard]] std::vector<FieldSpec> ScalarForm(const StackView& a_stack);

	// ------------------------------------------------------------------ names

	// Names in "@name" form, for the reference combos.
	[[nodiscard]] std::string ReferenceText(std::string_view a_name);
	[[nodiscard]] std::string ReferenceName(std::string_view a_text);  // strips a leading '@'

	// What a geometry is called in the menu. An authored shape keeps its
	// name. A shape the engine built from an armor addon is named
	// " (<addon id>)[<index>]/ (<armor id>) [<weight>%]", which reads as
	// "<armor> shape <index> (addon <id>)"; the raw name stays the key.
	[[nodiscard]] std::string GeometryLabel(std::string_view a_name, std::string_view a_armorName);
}
