#include "ComposePage.h"

#include "Edits.h"
#include "Manager.h"
#include "Menu.h"
#include "MenuState.h"
#include "MenuWidgets.h"
#include "Studio.h"

#include <algorithm>
#include <format>
#include <span>
#include <string>
#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include "extern/SKSEMenuFramework.h"
#pragma clang diagnostic pop

namespace ImGui = ImGuiMCP;
using ImGuiMCP::ImVec2;

namespace WornEnchantmentPBR::Studio
{
	namespace
	{
		using Widgets::TableStyle;
		using Widgets::Width;

		constexpr float       kRowDimAlpha = 0.45f;  // rows outside the selected region
		constexpr const char* kLayerPayload = "WEPBR_LAYER";

		// The tables' looks. A grid is rows of data (the board, the signals,
		// the curves); a context table fits its pickers; a form is fields
		// with an input each; the layer list is the stack's rows.
		constexpr TableStyle kGridStyle{ .borders = TableStyle::Borders::kAll, .stretch = true, .headers = true, .rowBackground = true };
		constexpr TableStyle kContextStyle{ .borders = TableStyle::Borders::kAll, .stretch = false, .headers = true, .rowBackground = false };
		constexpr TableStyle kFormStyle{ .borders = TableStyle::Borders::kInnerHorizontal, .stretch = true, .headers = false, .rowBackground = false };
		constexpr TableStyle kLayerStyle{ .borders = TableStyle::Borders::kInnerHorizontal, .stretch = true, .headers = true, .rowBackground = true };
		constexpr TableStyle kFooterStyle{ .borders = TableStyle::Borders::kAll, .stretch = true, .headers = true, .rowBackground = false };

		// ---------------------------------------------------------- edits

		// Every edit goes through the manager, which retires the wearers,
		// applies it to the store's copy, re-validates and re-applies. A
		// refused edit is a log line; the recipe is unchanged.
		void Post(const std::string& a_id, RecipeEdit a_edit)
		{
			Manager::GetSingleton()->EditRecipe(a_id, [edit = std::move(a_edit)](Recipe& a_recipe) {
				if (const auto problem = Apply(a_recipe, edit)) {
					logger::warn("edit refused: {} ({}: {})", Describe(edit), problem->where, problem->message);
				}
			});
		}

		// Text that does not parse never becomes an edit; it is reported and
		// the field shows the model again.
		void Refuse(std::string_view a_field, const std::string& a_text)
		{
			logger::warn("{} not applied: '{}' does not parse", a_field, a_text);
		}

		// ----------------------------------------------------------- view
		// Solo and mute live in the manager's View, which the tick reads.

		// Isolation has three levels, recipe, output and layer, and turning a
		// level off leaves the levels above it as they were: a layer's solo
		// off keeps the output and the recipe isolated, an output's solo off
		// keeps the recipe. Only the header's checkbox clears the recipe. Solo
		// goes through the manager, since isolating a different recipe
		// re-applies the wearers so the recipe is bound alone.
		void SoloOutput(View& a_view, const std::string& a_recipe, std::size_t a_output, bool a_on)
		{
			if (a_on) {
				Manager::GetSingleton()->Isolate(a_recipe, static_cast<int>(a_output), -1);
			} else {
				Manager::GetSingleton()->Isolate(a_view.isolateRecipe, -1, -1);
			}
		}

		void SoloLayer(View& a_view, const std::string& a_recipe, std::size_t a_output, std::size_t a_layer, bool a_on)
		{
			if (a_on) {
				Manager::GetSingleton()->Isolate(a_recipe, static_cast<int>(a_output), static_cast<int>(a_layer));
			} else {
				Manager::GetSingleton()->Isolate(a_view.isolateRecipe, a_view.isolateOutput, -1);
			}
		}

		// Selecting a cell selects its top layer (the last applied), so the
		// inspector opens on something at once; a single-layer stack needs no
		// second click.
		void SelectCell(MenuState& a_state, const Cell& a_cell)
		{
			a_state.target = a_cell.surface == Surface::kShell ? PickedTarget::kShell : PickedTarget::kMaterial;
			a_state.slot = a_cell.slot;
			a_state.selection.output = a_cell.output;
			a_state.selection.layer = a_cell.layers > 0 ? std::optional{ a_cell.layers - 1 } : std::nullopt;
		}

		void MuteLayer(View& a_view, const std::string& a_recipe, std::size_t a_output, std::size_t a_layer, bool a_on)
		{
			const LayerKey key{ a_recipe, a_output, a_layer };
			if (a_on) {
				a_view.muted.insert(key);
			} else {
				a_view.muted.erase(key);
			}
		}

		// ---------------------------------------------------------- forms

		// The input for a field, by its kind: a reference is a combo over the
		// names; an expression, mask or channel set is a badge and a text
		// field; a value (scalar, colour, vector, curve) is a value field whose
		// badge switches between text and a signal combo.
		[[nodiscard]] std::optional<std::string> FieldInput(const FieldSpec& a_field, float a_scale)
		{
			switch (a_field.kind) {
			case FieldKind::kReference:
				Widgets::Badge(a_field.kind);
				return Widgets::ReferenceCombo("value", a_field.text, a_field.names, a_field.allowEmpty, Width::Fill(), a_scale);
			case FieldKind::kExpression:
			case FieldKind::kMask:
			case FieldKind::kChannels:
				Widgets::Badge(a_field.kind);
				return Widgets::TextField("value", a_field.text, Width::Fill(), a_scale);
			case FieldKind::kScalar:
			case FieldKind::kColor:
			case FieldKind::kVector:
			case FieldKind::kCurve:
				return Widgets::ValueField("value", a_field.kind, a_field.text, a_field.names, a_field.allowEmpty, a_scale);
			}
			return std::nullopt;
		}

		// A form as the field table: the field's name, its detail button where
		// it has details, and the input filling the rest, each row in its own
		// ID scope. A committed text becomes the field's edit, or a log line
		// when it does not parse. Returns the detail whose button was clicked.
		[[nodiscard]] std::optional<FieldDetail> DrawForm(const char* a_id, std::span<const FieldSpec> a_form, const std::string& a_recipe, float a_scale)
		{
			std::optional<FieldDetail> open;
			auto                       table = Widgets::Table::Begin(a_id, { { "field", Width::Fit() }, { "", Width::Px(ImGui::GetFrameHeight()) }, { "value", Width::Fill() } }, kFormStyle);
			if (!table.Open()) {
				return open;
			}
			for (const auto& field : a_form) {
				ImGui::PushID(field.name.c_str());
				table.Cell();
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(field.name.c_str());
				table.Cell();
				if (field.detail && Widgets::DetailButton()) {
					open = field.detail;
				}
				table.Cell();
				if (field.value) {
					Widgets::ValueSwatch(*field.value);
					ImGui::SameLine();
				}
				if (const auto text = FieldInput(field, a_scale)) {
					const std::optional<RecipeEdit> edit = field.bind ? field.bind(*text) : std::nullopt;
					if (edit) {
						Post(a_recipe, *edit);
					} else {
						Refuse(field.name, *text);
					}
				}
				ImGui::PopID();
			}
			table.End();
			return open;
		}

		// ------------------------------------------------------ selection

		// A recipe's rows are the same for every shape of the piece, but the
		// compositor renders them per shape (its own maps, bakes and size), so
		// the selection names whose rendering is shown; edits reach every
		// shape. Viewing moves to the next shape of the recipe, wrapping.
		void ViewNextGeometry(const RecipeRow& a_recipe, const GeometryRow& a_geometry, Selection& a_selection)
		{
			const auto& shapes = a_recipe.geometries;
			if (shapes.empty()) {
				return;
			}
			const auto        it = std::ranges::find(shapes, a_geometry.name, &GeometryRow::name);
			const std::size_t at = it == shapes.end() ? 0 : static_cast<std::size_t>(it - shapes.begin());
			a_selection.geometry = shapes[(at + 1) % shapes.size()].name;
		}

		// The region lens: whole piece, or one of the recipe's masks. The
		// caller sets the width and the label.
		void RegionChoice(const char* a_label, const Board& a_board, Selection& a_selection)
		{
			const auto preview = a_board.region.empty() ? std::string{ "whole piece" } : ReferenceText(a_board.region);
			if (ImGui::BeginCombo(a_label, preview.c_str())) {
				if (ImGui::Selectable("whole piece", a_board.region.empty())) {
					a_selection.region.clear();
				}
				for (const auto& name : a_board.regions) {
					if (ImGui::Selectable(ReferenceText(name).c_str(), name == a_board.region)) {
						a_selection.region = name;
					}
				}
				ImGui::EndCombo();
			}
			Widgets::Tooltip("Filters the stack to the layers masked by this mask; Add layer binds it first.");
		}

		// ---------------------------------------------------------- board

		[[nodiscard]] std::string_view SurfaceName(Surface a_surface) noexcept
		{
			return a_surface == Surface::kMaterial ? "material" : "shell";
		}

		[[nodiscard]] std::span<const SlotRow> SlotRowsOf(const GeometryRow& a_geometry, Surface a_surface) noexcept
		{
			return a_surface == Surface::kMaterial ? a_geometry.materialSlots : a_geometry.shellSlots;
		}

		// What the binding wrote on the cell's slot, and the slot's scalars.
		[[nodiscard]] std::string CellTooltip(const Cell& a_cell, const GeometryRow& a_geometry)
		{
			auto text = std::format("{} on {}", SlotName(a_cell.slot), SurfaceName(a_cell.surface));
			for (const auto& row : SlotRowsOf(a_geometry, a_cell.surface)) {
				if (row.slot != a_cell.slot) {
					continue;
				}
				text += "\noriginal: " + row.original;
				text += "\nwritten: " + (row.written == row.original ? std::string{ "(original)" } : row.written);
				if (!row.problem.empty()) {
					text += "\n" + row.problem;
				}
			}
			for (const auto& scalar : a_cell.scalars) {
				text += std::format("\n{} = {}", scalar.name, Widgets::ValueText(scalar.value));
			}
			if (!a_cell.reason.empty()) {
				text += "\n" + a_cell.reason;
			}
			return text;
		}

		[[nodiscard]] std::string Joined(std::span<const std::string> a_names)
		{
			std::string text;
			for (const auto& name : a_names) {
				text += (text.empty() ? "" : ", ") + name;
			}
			return text;
		}

		void DrawWrittenCell(const Cell& a_cell, const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Board& a_board, MenuState& a_state, View& a_view, const Layout& a_layout)
		{
			auto&      selection = a_state.selection;
			const bool selected = a_cell.output && selection.output == a_cell.output;
			if (Widgets::ThumbnailButton("cell", a_cell.composite, 4, a_cell.animated, a_layout.cellSize * a_layout.widgetScale)) {
				SelectCell(a_state, a_cell);
			}
			Widgets::Tooltip(CellTooltip(a_cell, a_geometry));
			ImGui::SameLine();
			ImGui::BeginGroup();
			const auto count = a_board.region.empty() ?
			                       std::format("{} layer{}", a_cell.layers, a_cell.layers == 1 ? "" : "s") :
			                       std::format("{} of {} layers", a_cell.layersInRegion, a_cell.layers);
			if (selected) {
				Widgets::Ok(count);
			} else {
				ImGui::TextUnformatted(count.c_str());
			}
			if (!a_cell.badges.empty()) {
				Widgets::Dim(Joined(a_cell.badges));
			}
			if (a_cell.replace) {
				Widgets::Dim("replace");
			}
			if (a_cell.output) {
				bool solo = a_cell.isolated;
				if (Widgets::SoloMute(solo, nullptr)) {
					SoloOutput(a_view, a_recipe.id, *a_cell.output, solo);
				}
			}
			ImGui::EndGroup();
		}

		void DrawCell(const Cell* a_cell, const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Board& a_board, MenuState& a_state, View& a_view, const Layout& a_layout)
		{
			if (!a_cell) {
				return;
			}
			const float side = a_layout.cellSize * a_layout.widgetScale;
			switch (a_cell->state) {
			case CellState::kAbsent:
				return;  // the surface has no such slot: nothing to draw
			case CellState::kWritten:
				DrawWrittenCell(*a_cell, a_recipe, a_geometry, a_board, a_state, a_view, a_layout);
				return;
			case CellState::kEmpty:
				if (ImGui::Button("+", ImVec2{ side, side })) {
					Post(a_recipe.id, AddOutput{ a_cell->surface, a_cell->slot });
				}
				Widgets::Tooltip(std::format("add an output on {} of the {}", SlotName(a_cell->slot), SurfaceName(a_cell->surface)));
				return;
			case CellState::kExcluded:
				ImGui::BeginDisabled();
				ImGui::Button("+", ImVec2{ side, side });
				ImGui::EndDisabled();
				Widgets::Tooltip(a_cell->reason);
				return;
			case CellState::kRefused:
				Widgets::Problem("refused");
				Widgets::Tooltip(CellTooltip(*a_cell, a_geometry));
				if (a_cell->output && ImGui::SmallButton("select")) {
					SelectCell(a_state, *a_cell);
				}
				return;
			}
		}

		void DrawLightCell(const LightCell& a_light, const RecipeRow& a_recipe, View& a_view)
		{
			if (!a_light.present) {
				Widgets::Dim("none");
				return;
			}
			ImGui::TextUnformatted(a_light.description.c_str());
			if (a_light.output) {
				ImGui::SameLine();
				bool solo = a_light.isolated;
				if (Widgets::SoloMute(solo, nullptr)) {
					SoloOutput(a_view, a_recipe.id, *a_light.output, solo);
				}
			}
		}

		void DrawBoard(const Board& a_board, const RecipeRow& a_recipe, const GeometryRow& a_geometry, MenuState& a_state, View& a_view, const Layout& a_layout)
		{
			if (!Widgets::Section("Board", true)) {
				return;
			}
			Widgets::NextItemWidth(Width::Px(200.0f));
			RegionChoice("region", a_board, a_state.selection);
			if (!a_board.shell.empty()) {
				Widgets::Dim(a_board.shell);
			}
			auto table = Widgets::Table::Begin("board", { { "slot", Width::Px(80.0f) }, { "material", Width::Fill() }, { "shell", Width::Fill() } }, kGridStyle);
			if (!table.Open()) {
				return;
			}
			for (std::size_t i = 0; i < kSlotCount; ++i) {
				const auto slot = static_cast<Slot>(i);
				table.Cell();
				ImGui::TextUnformatted(std::string{ SlotName(slot) }.c_str());
				table.Cell();
				ImGui::PushID(static_cast<int>(i * 2));
				DrawCell(CellAt(a_board, Surface::kMaterial, slot), a_recipe, a_geometry, a_board, a_state, a_view, a_layout);
				ImGui::PopID();
				table.Cell();
				ImGui::PushID(static_cast<int>(i * 2 + 1));
				DrawCell(CellAt(a_board, Surface::kShell, slot), a_recipe, a_geometry, a_board, a_state, a_view, a_layout);
				ImGui::PopID();
			}
			// Lights take no mask, so the light row hides under a region lens.
			if (a_board.region.empty()) {
				table.Cell();
				ImGui::TextUnformatted("light");
				table.Cell();
				DrawLightCell(a_board.light, a_recipe, a_view);
				table.Cell();
			}
			table.End();
		}

		// -------------------------------------------------------- context
		// One labelled row of choices: the target (material, shell or the
		// light: the format's word for where an output goes), the slot on it,
		// the region lens, and the shape viewed on. The board's cells say what
		// each slot holds.

		[[nodiscard]] std::string SlotLabel(const Cell& a_cell)
		{
			const std::string name{ SlotName(a_cell.slot) };
			switch (a_cell.state) {
			case CellState::kWritten:
				return std::format("{} ({} layer{})", name, a_cell.layers, a_cell.layers == 1 ? "" : "s");
			case CellState::kRefused:
				return name + " (refused)";
			case CellState::kExcluded:
				return name + " (excluded)";
			case CellState::kEmpty:
				return name + " (empty)";
			case CellState::kAbsent:
				return name;
			}
			return name;
		}

		void TargetChoice(MenuState& a_state)
		{
			const char* targets[]{ "material", "shell", "light" };
			int         target = static_cast<int>(a_state.target);
			if (ImGui::Combo("##target", &target, targets, 3) && target >= 0 && target < 3 && static_cast<PickedTarget>(target) != a_state.target) {
				a_state.target = static_cast<PickedTarget>(target);
				a_state.slot.reset();
				a_state.selection.output.reset();
				a_state.selection.layer.reset();
			}
			Widgets::Tooltip("Where an output goes: the geometry's own material, the recipe's shell clone, or its light.");
		}

		void SlotChoice(const Board& a_board, Surface a_surface, const Cell* a_picked, MenuState& a_state)
		{
			if (!ImGui::BeginCombo("##slot", a_picked ? SlotLabel(*a_picked).c_str() : "choose a slot")) {
				return;
			}
			for (std::size_t i = 0; i < kSlotCount; ++i) {
				const auto* cell = CellAt(a_board, a_surface, static_cast<Slot>(i));
				if (!cell || cell->state == CellState::kAbsent) {
					continue;
				}
				ImGui::PushID(static_cast<int>(i));
				const bool excluded = cell->state == CellState::kExcluded;
				if (excluded) {
					ImGui::BeginDisabled();
				}
				if (ImGui::Selectable(SlotLabel(*cell).c_str(), cell == a_picked)) {
					if (cell->output) {
						SelectCell(a_state, *cell);
					} else {
						a_state.slot = cell->slot;
						a_state.selection.output.reset();
						a_state.selection.layer.reset();
					}
				}
				if (excluded) {
					ImGui::EndDisabled();
				}
				if (!cell->reason.empty()) {
					Widgets::Tooltip(cell->reason);
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}

		// The picked cell for the state as it stands now, or null when the
		// light or nothing is picked. Looked up again after the combos have
		// run, since a pick changes the state mid-frame and a cell found
		// before it would put the old pick back.
		[[nodiscard]] const Cell* PickedCell(const Board& a_board, const MenuState& a_state) noexcept
		{
			if (a_state.target == PickedTarget::kLight || !a_state.slot) {
				return nullptr;
			}
			const Surface surface = a_state.target == PickedTarget::kShell ? Surface::kShell : Surface::kMaterial;
			return CellAt(a_board, surface, *a_state.slot);
		}

		// The first context row is the recipe: S (the recipe applied alone),
		// the selection and the recipe within it; and, at the far right past
		// a spacer, Clear layers for the picked output.
		void DrawRecipeContext(const Snapshot& a_snapshot, const PieceRow& a_piece, const RecipeRow& a_recipe, const Cell* a_picked, MenuState& a_state)
		{
			auto table = Widgets::Table::Begin("recipe-context", { { "S", Width::Fit() }, { "selection", Width::Fit() }, { "recipe", Width::Fit() }, { "", Width::Fill() }, { "", Width::Fit() } }, kContextStyle);
			if (!table.Open()) {
				return;
			}
			table.Cell();
			IsolateCheckbox(&a_recipe, "##isolate");
			Widgets::Tooltip("solo the recipe: apply it alone");
			table.Cell();
			Widgets::NextItemWidth(Width::Fit(std::format("{} / {} (3rd)", a_piece.actorName, a_piece.armorName)));
			SelectionCombo(a_snapshot, "##selection");
			table.Cell();
			Widgets::NextItemWidth(Width::Fit(a_recipe.id));
			RecipeCombo(&a_piece, "##recipe");
			table.Cell();
			table.Cell();
			{
				// Only a picked output with layers can be cleared.
				const std::optional<std::size_t> output = a_picked ? a_picked->output : std::nullopt;
				const bool                       clearable = output && a_picked->layers > 0;
				if (!clearable) {
					ImGui::BeginDisabled();
				}
				if (ImGui::Button("Clear layers") && clearable) {
					Post(a_recipe.id, ClearLayers{ *output });
					a_state.selection.layer.reset();
				}
				if (!clearable) {
					ImGui::EndDisabled();
				}
				Widgets::Tooltip("remove every layer of the picked output");
			}
			table.End();
		}

		// The second context row is what is being edited: S (the picked
		// output alone), target, slot and region. Columns fit their content
		// and each combo is as wide as its preview, so a long name is never
		// clipped while a short neighbour has room to spare.
		void DrawEditContext(const Board& a_board, const RecipeRow& a_recipe, const Cell* a_picked, MenuState& a_state, View& a_view)
		{
			const bool    light = a_state.target == PickedTarget::kLight;
			const Surface surface = a_state.target == PickedTarget::kShell ? Surface::kShell : Surface::kMaterial;
			auto          table = Widgets::Table::Begin("context", { { "S", Width::Fit() }, { "target", Width::Fit() }, { "slot", Width::Fit() }, { "region", Width::Fit() } }, kContextStyle);
			if (!table.Open()) {
				return;
			}
			table.Cell();
			{
				const std::optional<std::size_t> output = light ? a_board.light.output : (a_picked ? a_picked->output : std::nullopt);
				bool                             solo = light ? a_board.light.isolated : (a_picked && a_picked->isolated);
				if (!output) {
					ImGui::BeginDisabled();
				}
				if (Widgets::Toggle("##solo", solo, "solo the picked output: show it alone") && output) {
					SoloOutput(a_view, a_recipe.id, *output, solo);
				}
				if (!output) {
					ImGui::EndDisabled();
				}
			}
			table.Cell();
			Widgets::NextItemWidth(Width::Fit("material"));
			TargetChoice(a_state);
			table.Cell();
			if (light) {
				Widgets::Dim("the recipe's light");
			} else {
				Widgets::NextItemWidth(Width::Fit(a_picked ? SlotLabel(*a_picked) : std::string{ "choose a slot" }));
				SlotChoice(a_board, surface, a_picked, a_state);
			}
			table.Cell();
			Widgets::NextItemWidth(Width::Fit(a_board.region.empty() ? std::string{ "whole piece" } : ReferenceText(a_board.region)));
			RegionChoice("##region", a_board, a_state.selection);
			table.End();
		}

		const Cell* DrawContext(const Snapshot& a_snapshot, const Board& a_board, const PieceRow& a_piece, const RecipeRow& a_recipe, MenuState& a_state, View& a_view)
		{
			const Cell* picked = PickedCell(a_board, a_state);
			DrawRecipeContext(a_snapshot, a_piece, a_recipe, picked, a_state);
			Widgets::Rule();
			DrawEditContext(a_board, a_recipe, picked, a_state, a_view);

			if (a_state.target == PickedTarget::kLight) {
				DrawLightCell(a_board.light, a_recipe, a_view);
				return nullptr;
			}
			picked = PickedCell(a_board, a_state);
			if (!picked) {
				return nullptr;
			}
			if (picked->state == CellState::kEmpty) {
				if (ImGui::Button("Add output")) {
					Post(a_recipe.id, AddOutput{ picked->surface, picked->slot });
				}
				Widgets::Tooltip(std::format("add an empty stack on {} of the {}", SlotName(picked->slot), SurfaceName(picked->surface)));
				return picked;
			}
			// The output the picked cell holds follows edits: a new output lands
			// here, and a removed one leaves the cell empty.
			if (picked->output && a_state.selection.output != picked->output) {
				SelectCell(a_state, *picked);
			}
			if (!picked->reason.empty()) {
				Widgets::Warn(picked->reason);
			}
			return picked;
		}

		// ---------------------------------------------------------- stack
		// The stack is a list of rows, base at the bottom. A row is the grip,
		// the index and the thumbnail on the left, and on the right its source
		// line, its controls, and, when it is the selected row, the inspector
		// for it.

		void DrawInspectorFields(const Inspector& a_inspector, const RecipeRow& a_recipe, const Layout& a_layout);

		void DrawScalars(const StackView& a_stack, const std::string& a_id, float a_scale)
		{
			if (a_stack.scalars.empty()) {
				return;
			}
			// Scalars open no detail; the returned detail is not used.
			[[maybe_unused]] const auto detail = DrawForm("scalars", ScalarForm(a_stack), a_id, a_scale);
		}

		// The layer table's columns: index, grip, solo, mute, the source's
		// type, blend, the source, remove.
		[[nodiscard]] Widgets::Table BeginLayerTable()
		{
			return Widgets::Table::Begin("layers", { { "#", Width::Fit() }, { "", Width::Fit() }, { "S", Width::Fit() }, { "M", Width::Fit() }, { "type", Width::Px(ImGui::GetFrameHeight()) }, { "blend", Width::Fit() }, { "source", Width::Fill() }, { "", Width::Fit() } }, kLayerStyle);
		}

		void DrawForeignRow(Widgets::Table& a_table, const ForeignRow& a_row)
		{
			a_table.Cell();
			a_table.Cell();
			a_table.Cell();
			a_table.Cell();
			a_table.Cell();
			Widgets::Dim(a_row.layer.source.starts_with('@') ? "@" : "c");
			a_table.Cell();
			Widgets::Dim(a_row.layer.blend);
			a_table.Cell();
			Widgets::Dim(std::format("{}  ({}, priority {}: {} {})", a_row.layer.source, a_row.recipe, a_row.priority, a_row.layer.opacityText, a_row.layer.mask));
			a_table.Cell();
		}

		// One layer as a table row, in its own ID scope. Clicking the grip or
		// the name selects it; its fields are drawn beside the table.
		void DrawStackRow(Widgets::Table& a_table, const StackView& a_stack, const StackRow& a_row, const RecipeRow& a_recipe, MenuState& a_state, View& a_view)
		{
			const auto&       id = a_recipe.id;
			const std::size_t output = a_stack.output;
			const std::size_t index = a_row.index;
			const bool        constant = !a_row.layer.source.starts_with('@');

			ImGui::PushID(static_cast<int>(index));
			if (!a_row.inRegion) {
				ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_Alpha, kRowDimAlpha);
			}
			a_table.Cell();
			ImGui::AlignTextToFramePadding();
			ImGui::Text("%zu", index);
			a_table.Cell();
			if (Widgets::DragHandle(kLayerPayload, index)) {
				a_state.selection.layer = index;
			}
			if (const auto move = Widgets::DropTarget(kLayerPayload, index)) {
				Post(id, MoveLayer{ output, move->from, move->to });
			}
			a_table.Cell();
			bool solo = a_row.soloed;
			if (Widgets::Toggle("##solo", solo, "solo: show this layer alone")) {
				SoloLayer(a_view, id, output, index, solo);
			}
			a_table.Cell();
			bool mute = a_row.muted;
			if (Widgets::Toggle("##mute", mute, "mute: hide this layer")) {
				MuteLayer(a_view, id, output, index, mute);
			}
			a_table.Cell();
			Widgets::Badge(constant ? FieldKind::kColor : FieldKind::kReference);
			a_table.Cell();
			// Every blend combo the same width: the widest name the slot accepts.
			if (const auto blend = Widgets::BlendCombo("blend", a_row.layer.blend, a_stack.blends, Width::Px(Widgets::BlendWidth(a_stack.blends)), 1.0f)) {
				Post(id, SetLayerBlend{ output, index, *blend });
			}
			a_table.Cell();
			if (ImGui::Selectable(a_row.layer.source.c_str(), a_row.selected)) {
				a_state.selection.layer = index;
			}
			if (!a_row.layer.problem.empty()) {
				Widgets::Tooltip(a_row.layer.problem);
			}
			a_table.Cell();
			if (ImGui::SmallButton("X")) {
				Post(id, RemoveLayer{ output, index });
			}
			Widgets::Tooltip("remove this layer");
			if (!a_row.inRegion) {
				ImGui::PopStyleVar();
			}
			ImGui::PopID();
		}

		// The layer list: in application order, top to bottom, the recipes
		// merging before this one, then this stack's base first and its last
		// applied layer at the bottom, then the recipes merging after; and
		// Add layer.
		void DrawLayers(const StackView& a_stack, const RecipeRow& a_recipe, MenuState& a_state, View& a_view)
		{
			auto table = BeginLayerTable();
			if (table.Open()) {
				for (const auto& foreign : a_stack.below) {
					DrawForeignRow(table, foreign);
				}
				for (const auto& row : a_stack.rows) {
					DrawStackRow(table, a_stack, row, a_recipe, a_state, a_view);
				}
				for (const auto& foreign : a_stack.above) {
					DrawForeignRow(table, foreign);
				}
				table.End();
			}
			if (ImGui::SmallButton("Add layer")) {
				Layer layer = DefaultLayer();
				// Under a region lens a new layer is masked by the region before
				// anything else is chosen.
				if (!a_state.selection.region.empty()) {
					layer.mask = Ref{ a_state.selection.region };
				}
				Post(a_recipe.id, AddLayer{ a_stack.output, layer, std::nullopt });
				// The new layer is applied last and lands at the bottom, at the index
				// the stack has now; it is selected as soon as the snapshot carries it.
				a_state.selection.layer = a_stack.rows.size();
			}
			Widgets::HelpMarker("Drag the :: grip onto another row to reorder; click the grip or the name to open the layer's fields beside the stack. S solos, M mutes. Enter commits a text field; a drag commits on release.");
		}

		// The selected layer's fields beside its picture; the layer list to
		// the left says which layer it is.
		void DrawInspector(const StackView& a_stack, const std::optional<Inspector>& a_inspector, const RecipeRow& a_recipe, const Layout& a_layout)
		{
			if (!a_layout.inspector || !a_inspector) {
				Widgets::Dim("click a layer to inspect it");
				return;
			}
			const auto row = std::ranges::find(a_stack.rows, a_inspector->layer, &StackRow::index);
			ImGui::PushID(static_cast<int>(a_inspector->layer));
			if (!a_inspector->row.problem.empty()) {
				Widgets::Warn(a_inspector->row.problem);
			}
			Widgets::Thumbnail(row != a_stack.rows.end() ? row->layer.texture : nullptr, 4, false, a_layout.inspectorThumbnail * a_layout.widgetScale);
			ImGui::SameLine();
			ImGui::BeginGroup();
			DrawInspectorFields(*a_inspector, a_recipe, a_layout);
			ImGui::EndGroup();
			ImGui::PopID();
		}

		// The composite as rendered on the shape viewed; on a piece with
		// several shapes, clicking it views the next one.
		void DrawComposite(const StackView& a_stack, const PieceRow& a_piece, const RecipeRow& a_recipe, const GeometryRow& a_geometry, MenuState& a_state, const Layout& a_layout)
		{
			if (a_recipe.geometries.size() < 2) {
				Widgets::Thumbnail(a_stack.composite, 4, a_stack.animated, a_layout.compositeSize);
				return;
			}
			if (Widgets::ThumbnailButton("composite", a_stack.composite, 4, a_stack.animated, a_layout.compositeSize)) {
				ViewNextGeometry(a_recipe, a_geometry, a_state.selection);
			}
			Widgets::Tooltip(std::format("viewed on {} (one of {} shapes; the recipe applies to all)\nclick: view the next shape\nraw name: {}", GeometryLabel(a_geometry.name, a_piece.armorName), a_recipe.geometries.size(), a_geometry.name));
		}

		void DrawStack(const std::optional<StackView>& a_stack, const std::optional<Inspector>& a_inspector, const PieceRow& a_piece, const RecipeRow& a_recipe, const GeometryRow& a_geometry, MenuState& a_state, View& a_view, Layout& a_layout)
		{
			if (!a_stack) {
				Widgets::Dim("choose a target and a slot");
				return;
			}
			const auto& stack = *a_stack;
			const auto& id = a_recipe.id;
			ImGui::PushID(static_cast<int>(stack.output));
			ImGui::TextUnformatted(stack.title.c_str());
			if (stack.isolated) {
				ImGui::SameLine();
				Widgets::Warn("(solo)");
			}
			if (!stack.problem.empty()) {
				Widgets::Problem(stack.problem);
			}
			DrawComposite(stack, a_piece, a_recipe, a_geometry, a_state, a_layout);
			ImGui::SameLine();
			ImGui::BeginGroup();
			Widgets::Dim(std::format("composite {} px, {}", stack.size, stack.animated ? "animated" : "static"));
			DrawScalars(stack, id, a_layout.widgetScale);
			ImGui::EndGroup();

			// The layers on the left, the selected layer's fields on the right,
			// split by a draggable vertical rule.
			Widgets::Split(
				"stack-split", a_layout.stackSplit,
				[&]() { DrawLayers(stack, a_recipe, a_state, a_view); },
				[&]() { DrawInspector(stack, a_inspector, a_recipe, a_layout); });
			ImGui::PopID();
		}

		// -------------------------------------------------------- signals

		// The value control for a signal a designer tunes; developer rows
		// are shown by the caller as text. Drawn in the signal's ID scope.
		void DrawSignalEditor(const std::string& a_id, const SignalRow& a_signal, float a_scale)
		{
			if (a_signal.constant) {
				if (const auto* number = Get<float>(*a_signal.constant)) {
					Widgets::Badge(FieldKind::kScalar);
					if (const auto edited = Widgets::DragField("value", *number, Width::Fill(), a_scale)) {
						Post(a_id, SetConstant{ a_signal.name, *edited });
					}
				} else if (const auto* color = Get<Vec3>(*a_signal.constant)) {
					Widgets::Badge(FieldKind::kColor);
					if (const auto edited = Widgets::ColorField("value", *color, Width::Fill(), a_scale)) {
						Post(a_id, SetConstant{ a_signal.name, *edited });
					}
				} else {
					Widgets::Dim("vec2 constants edit in the file");
				}
			} else if (a_signal.kind == "expr") {
				Widgets::Badge(FieldKind::kExpression);
				if (const auto edited = Widgets::TextField("value", a_signal.text, Width::Fill(), a_scale)) {
					Post(a_id, SetExpression{ a_signal.name, *edited });
				}
			} else {
				Widgets::Dim(a_signal.kind + " rows edit in the file");
			}
		}

		void DrawSignalRow(Widgets::Table& a_table, const std::string& a_id, const SignalRow& a_signal, bool a_tunable, float a_scale)
		{
			ImGui::PushID(a_signal.name.c_str());
			a_table.Cell();
			ImGui::TextUnformatted(a_signal.name.c_str());
			a_table.Cell();
			Widgets::Dim(a_signal.kind);
			a_table.Cell();
			Widgets::ValueSwatch(a_signal.value);
			a_table.Cell();
			if (a_tunable) {
				DrawSignalEditor(a_id, a_signal, a_scale);
			} else {
				Widgets::Dim("read-only");
			}
			a_table.Cell();
			if (a_tunable) {
				Widgets::Badge(FieldKind::kCurve);
				if (const auto edited = Widgets::TextField("curve", a_signal.curve, Width::Fill(), a_scale)) {
					// An empty text clears the curve.
					Post(a_id, SetSignalCurve{ a_signal.name, edited->empty() ? std::nullopt : std::optional{ CurveRef{ *edited } } });
				}
			} else {
				Widgets::Dim(a_signal.curve);
			}
			a_table.Cell();
			if (a_signal.inert) {
				Widgets::Problem(a_signal.problem.empty() ? "inert" : "inert: " + a_signal.problem);
			}
			ImGui::PopID();
		}

		void DrawSignals(const RecipeRow& a_recipe, const Layout& a_layout)
		{
			const auto  list = BuildSignalList(a_recipe, a_layout);
			const auto& id = a_recipe.id;
			if (!Widgets::Section("Signals", true)) {
				return;
			}
			ImGui::Text("t = %.2f s", a_recipe.time);
			auto signals = Widgets::Table::Begin("signals", { { "signal", Width::Fill() }, { "kind", Width::Fill() }, { "value", Width::Fill() }, { "edit", Width::Fill() }, { "curve", Width::Fill() }, { "state", Width::Fill() } }, kGridStyle);
			if (signals.Open()) {
				for (const auto& signal : list.tunable) {
					DrawSignalRow(signals, id, signal, true, a_layout.widgetScale);
				}
				for (const auto& signal : list.developer) {
					DrawSignalRow(signals, id, signal, false, a_layout.widgetScale);
				}
				signals.End();
			}
			if (a_recipe.curves.empty()) {
				return;
			}
			ImGui::SeparatorText("Curves (expressions in x; mean is the source's mean)");
			auto curves = Widgets::Table::Begin("curves", { { "curve", Width::Fill() }, { "expression", Width::Fill() } }, kGridStyle);
			if (!curves.Open()) {
				return;
			}
			for (const auto& curve : a_recipe.curves) {
				ImGui::PushID(curve.name.c_str());
				curves.Cell();
				ImGui::TextUnformatted(ReferenceText(curve.name).c_str());
				curves.Cell();
				Widgets::Badge(FieldKind::kCurve);
				if (const auto edited = Widgets::TextField("text", curve.text, Width::Fill(), a_layout.widgetScale)) {
					Post(id, SetCurve{ curve.name, *edited });
				}
				ImGui::PopID();
			}
			curves.End();
		}

		// ------------------------------------------------------ inspector

		// A source or mask row the layer reads, with its picture and definition.
		void DrawImageRow(const std::string& a_id, const ImageRow& a_image, bool a_editable, const Layout& a_layout)
		{
			const float scale = a_layout.widgetScale;
			Widgets::Thumbnail(a_image.texture, a_image.channel, a_image.animated, a_layout.inspectorThumbnail * scale);
			ImGui::TextUnformatted((ReferenceText(a_image.name) + " =").c_str());
			ImGui::SameLine();
			if (a_editable) {
				Widgets::Badge(FieldKind::kMask);
				if (const auto edited = Widgets::TextField("text", a_image.kind, Width::Fill(), scale)) {
					Post(a_id, SetMask{ a_image.name, *edited });
				}
			} else {
				ImGui::TextWrapped("%s", a_image.kind.c_str());
			}
			if (!a_image.problem.empty()) {
				Widgets::Warn(a_image.problem);
			}
		}

		// The signal a parameter text names, drawn as its editor inside a modal.
		void DrawSignalDetail(const std::string& a_id, const Inspector& a_inspector, const std::string& a_text, float a_scale)
		{
			const auto name = ReferenceName(a_text);
			const auto it = std::ranges::find(a_inspector.signals, name, &SignalRow::name);
			if (a_text.empty() || !a_text.starts_with('@') || it == a_inspector.signals.end()) {
				Widgets::Dim("a literal; choose a @signal to tune it here");
				return;
			}
			ImGui::PushID(it->name.c_str());
			ImGui::Text("%s (%s)", ReferenceText(it->name).c_str(), it->kind.c_str());
			ImGui::SameLine();
			Widgets::ValueSwatch(it->value);
			DrawSignalEditor(a_id, *it, a_scale);
			if (it->inert) {
				Widgets::Problem(it->problem.empty() ? "inert" : "inert: " + it->problem);
			}
			ImGui::PopID();
		}

		void DrawDetailModal(FieldDetail a_detail, const Inspector& a_in, const RecipeRow& a_recipe, const Layout& a_layout)
		{
			const auto& id = a_recipe.id;
			const float scale = a_layout.widgetScale;
			switch (a_detail) {
			case FieldDetail::kSource:
				if (a_in.source) {
					const bool isMask = std::ranges::find(a_in.masks, a_in.source->name) != a_in.masks.end();
					DrawImageRow(id, *a_in.source, isMask, a_layout);
				} else {
					Widgets::Dim("a constant colour, or a name no source or mask has");
				}
				break;
			case FieldDetail::kCurve:
				if (a_in.curve) {
					ImGui::TextUnformatted((ReferenceText(a_in.curve->name) + " =").c_str());
					ImGui::SameLine();
					Widgets::Badge(FieldKind::kCurve);
					if (const auto edited = Widgets::TextField("text", a_in.curve->text, Width::Fill(), scale)) {
						Post(id, SetCurve{ a_in.curve->name, *edited });
					}
				} else {
					Widgets::Dim("no declared curve; the layer's curve is inline or empty");
				}
				break;
			case FieldDetail::kOpacity:
				DrawSignalDetail(id, a_in, a_in.row.opacityText, scale);
				break;
			case FieldDetail::kColor:
				DrawSignalDetail(id, a_in, a_in.row.color, scale);
				break;
			case FieldDetail::kMask:
				if (a_in.mask) {
					DrawImageRow(id, *a_in.mask, true, a_layout);
				} else {
					Widgets::Dim("no mask");
				}
				break;
			}
			if (ImGui::Button("close")) {
				ImGui::CloseCurrentPopup();
			}
		}

		void DrawInspectorFields(const Inspector& a_inspector, const RecipeRow& a_recipe, const Layout& a_layout)
		{
			const std::optional<FieldDetail> open = DrawForm("fields", InspectorForm(a_inspector), a_recipe.id, a_layout.widgetScale);

			// The detail modals live outside the table so their ids match the
			// buttons' scope; one per field, opened by its button.
			for (const auto detail : { FieldDetail::kSource, FieldDetail::kCurve, FieldDetail::kOpacity, FieldDetail::kColor, FieldDetail::kMask }) {
				const auto title = std::format("{} of layer {}###detail{}", FieldDetailName(detail), a_inspector.layer, static_cast<int>(detail));
				if (open == detail) {
					ImGui::OpenPopup(title.c_str());
				}
				// An auto-resizing window starts narrow and wrapped text then wraps
				// every few characters; a floor on the width keeps a definition on
				// one or two lines.
				ImGui::SetNextWindowSizeConstraints(ImVec2{ 480.0f, 0.0f }, ImVec2{ 960.0f, 800.0f });
				if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiMCP::ImGuiWindowFlags_AlwaysAutoResize)) {
					DrawDetailModal(detail, a_inspector, a_recipe, a_layout);
					ImGui::EndPopup();
				}
			}
		}
	}

	// ---------------------------------------------------------------- page

	void DrawBody(const Snapshot& a_snapshot, const PieceRow* a_piece, const RecipeRow* a_recipe, const GeometryRow* a_geometry, MenuState& a_state);
	void DrawFooter(const RecipeRow* a_recipe);

	void __stdcall RenderStudio()
	{
		auto*      manager = Manager::GetSingleton();
		const auto snapshot = manager->TakeSnapshot();
		auto&      state = State();
		RenderStatus(snapshot);
		if (Widgets::ModeBar(state.mode)) {
			state.layout = LayoutFor(state.mode);
		}

		const auto* piece = SelectedPiece(snapshot, state.selection);
		const auto* recipe = SelectedRecipe(piece, state.selection);
		const auto* geometry = SelectedGeometry(recipe, state.selection);

		// The body scrolls above a footer pinned to the bottom of the page.
		const float footer = ImGui::GetFrameHeightWithSpacing() * 2.0f + ImGui::GetTextLineHeight() * 2.0f + 16.0f;  // the rule's gaps, the header row, the row
		if (ImGui::BeginChild("studio-body", ImVec2{ 0.0f, -footer }, 0, 0)) {
			DrawBody(snapshot, piece, recipe, geometry, state);
		}
		ImGui::EndChild();
		DrawFooter(recipe);
	}

	// Everything under the mode bar is drawn in the recipe's ID scope, so a
	// field's key names the same field only while the same recipe is shown.
	void DrawBody(const Snapshot& a_snapshot, const PieceRow* a_piece, const RecipeRow* a_recipe, const GeometryRow* a_geometry, MenuState& a_state)
	{
		if (!a_piece || !a_recipe) {
			Widgets::Dim("nothing applied; equip enchanted PBR armor or press Re-apply all on the Recipes page");
			return;
		}
		if (!a_geometry) {
			Widgets::Dim("no geometry bound for the selected recipe");
			return;
		}
		if (a_state.mode == Mode::kPaint || a_state.mode == Mode::kDesign) {
			ImGui::Text("%s mode is not built yet", std::string{ ModeName(a_state.mode) }.c_str());
			return;
		}
		auto&   view = Manager::GetSingleton()->Debug();
		Layout& layout = a_state.layout;
		ImGui::PushID(a_recipe->id.c_str());
		if (layout.stack) {
			const auto  board = BuildBoard(*a_recipe, *a_geometry, a_state.selection, view);
			const auto* picked = DrawContext(a_snapshot, board, *a_piece, *a_recipe, a_state, view);
			if (picked && picked->output) {
				const auto stack = BuildStackView(*a_piece, *a_recipe, *a_geometry, a_state.selection, view);
				const auto inspector = layout.inspector ? BuildInspector(*a_recipe, *a_geometry, a_state.selection) : std::nullopt;
				DrawStack(stack, inspector, *a_piece, *a_recipe, *a_geometry, a_state, view, layout);
			}
		}
		if (layout.signals) {
			DrawSignals(*a_recipe, layout);
		}
		ImGui::PopID();
	}

	// The footer: the clock, as a labelled table; the scrubber takes the
	// remaining width.
	void DrawFooter(const RecipeRow* a_recipe)
	{
		Widgets::Rule();
		auto table = Widgets::Table::Begin("footer", { { "freeze", Width::Fit() }, { "t (s)", Width::Fill() } }, kFooterStyle);
		if (!table.Open()) {
			return;
		}
		table.Cell();
		FreezeCheckbox(a_recipe, "##freeze");
		table.Cell();
		Widgets::NextItemWidth(Width::Fill());
		ScrubSlider(a_recipe, "##scrub");
		table.End();
	}

	void DrawBoardPage(const Snapshot& a_snapshot)
	{
		auto&       state = State();
		const auto* piece = SelectedPiece(a_snapshot, state.selection);
		const auto* recipe = SelectedRecipe(piece, state.selection);
		if (!piece || !recipe) {
			return;
		}
		const auto* geometry = SelectedGeometry(recipe, state.selection);
		if (!geometry) {
			Widgets::Dim("no geometry bound for the selected recipe");
			return;
		}
		// The board is viewed on the shape the studio views; the composite
		// there cycles it.
		if (recipe->geometries.size() > 1) {
			Widgets::Dim("viewed on " + GeometryLabel(geometry->name, piece->armorName));
		}
		auto&      view = Manager::GetSingleton()->Debug();
		const auto board = BuildBoard(*recipe, *geometry, state.selection, view);
		ImGui::PushID(recipe->id.c_str());
		DrawBoard(board, *recipe, *geometry, state, view, state.layout);
		ImGui::PopID();
	}
}
