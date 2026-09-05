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
		constexpr float       kRowDimAlpha = 0.45f;  // rows outside the selected region
		constexpr const char* kLayerPayload = "WEPBR_LAYER";
		constexpr int         kTableFlags = ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_Borders | ImGuiMCP::ImGuiTableFlags_SizingStretchProp;

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

		[[nodiscard]] std::optional<Ref> MaskRefOf(const std::string& a_text)
		{
			const auto name = ReferenceName(a_text);
			return name.empty() ? std::nullopt : std::optional{ Ref{ name } };
		}

		[[nodiscard]] std::optional<CurveRef> CurveRefOf(const std::string& a_text)
		{
			return a_text.empty() ? std::nullopt : std::optional{ CurveRef{ a_text } };
		}

		void PostScalar(const std::string& a_id, std::size_t a_output, const std::string& a_name, const std::string& a_text)
		{
			const auto field = ParseScalarField(a_name);
			if (!field) {
				logger::warn("scalar '{}' is not a slot field", a_name);
				return;
			}
			if (*field == ScalarField::kColor) {
				if (const auto color = ParseVec3Param(a_text)) {
					Post(a_id, SetColorScalar{ a_output, *color });
				} else {
					Refuse("color", a_text);
				}
				return;
			}
			if (const auto value = ParseParam(a_text)) {
				Post(a_id, SetScalar{ a_output, *field, *value });
			} else {
				Refuse(a_name, a_text);
			}
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

		// A pane is a collapsible section in the one column, so a pane can be
		// folded away to keep the column short.
		[[nodiscard]] bool BeginPane(const char* a_title, bool a_openByDefault = true)
		{
			return ImGui::CollapsingHeader(a_title, a_openByDefault ? ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen : 0);
		}

		// ------------------------------------------------------ selection

		// A recipe's rows are the same for every shape of the piece, but the
		// compositor renders them per shape (its own maps, bakes and size), so
		// this picks whose rendering is shown; edits reach every shape. Entries
		// are labelled for reading (GeometryLabel) and identified by the raw
		// name after "##", so two shapes with one label stay distinct. The
		// caller sets the width and the label.
		void GeometryChoice(const char* a_label, const PieceRow& a_piece, const RecipeRow& a_recipe, const GeometryRow& a_geometry, Selection& a_selection)
		{
			if (ImGui::BeginCombo(a_label, GeometryLabel(a_geometry.name, a_piece.armorName).c_str())) {
				for (const auto& g : a_recipe.geometries) {
					const auto label = GeometryLabel(g.name, a_piece.armorName) + "##" + g.name;
					if (ImGui::Selectable(label.c_str(), &g == &a_geometry)) {
						a_selection.geometry = g.name;
					}
					Widgets::Tooltip(g.name);
				}
				ImGui::EndCombo();
			}
			Widgets::Tooltip("The piece has several shapes. The recipe applies to all of them; this chooses whose composites, thumbnails and slot states are shown. Raw name: " + a_geometry.name);
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
			if (!BeginPane("Board")) {
				return;
			}
			ImGui::SetNextItemWidth(200.0f);
			RegionChoice("region", a_board, a_state.selection);
			if (!a_board.shell.empty()) {
				Widgets::Dim(a_board.shell);
			}
			if (!ImGui::BeginTable("board", 3, kTableFlags)) {
				return;
			}
			ImGui::TableSetupColumn("slot", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableSetupColumn("material");
			ImGui::TableSetupColumn("shell");
			ImGui::TableHeadersRow();
			for (std::size_t i = 0; i < kSlotCount; ++i) {
				const auto slot = static_cast<Slot>(i);
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(std::string{ SlotName(slot) }.c_str());
				ImGui::TableNextColumn();
				ImGui::PushID(static_cast<int>(i * 2));
				DrawCell(CellAt(a_board, Surface::kMaterial, slot), a_recipe, a_geometry, a_board, a_state, a_view, a_layout);
				ImGui::PopID();
				ImGui::TableNextColumn();
				ImGui::PushID(static_cast<int>(i * 2 + 1));
				DrawCell(CellAt(a_board, Surface::kShell, slot), a_recipe, a_geometry, a_board, a_state, a_view, a_layout);
				ImGui::PopID();
			}
			// Lights take no mask, so the light row hides under a region lens.
			if (a_board.region.empty()) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted("light");
				ImGui::TableNextColumn();
				DrawLightCell(a_board.light, a_recipe, a_view);
				ImGui::TableNextColumn();
			}
			ImGui::EndTable();
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

		// Returns the picked cell, or null when the light or nothing is picked.
		// The picked cell for the state as it stands now. Looked up again after
		// the combos have run, since a pick changes the state mid-frame and a
		// cell found before it would put the old pick back.
		[[nodiscard]] const Cell* PickedCell(const Board& a_board, const MenuState& a_state) noexcept
		{
			if (a_state.target == PickedTarget::kLight || !a_state.slot) {
				return nullptr;
			}
			const Surface surface = a_state.target == PickedTarget::kShell ? Surface::kShell : Surface::kMaterial;
			return CellAt(a_board, surface, *a_state.slot);
		}

		const Cell* DrawContext(const Snapshot& a_snapshot, const Board& a_board, const PieceRow& a_piece, const RecipeRow& a_recipe, const GeometryRow& a_geometry, MenuState& a_state, View& a_view)
		{
			const bool    shapes = a_recipe.geometries.size() > 1;
			const bool    light = a_state.target == PickedTarget::kLight;
			const Surface surface = a_state.target == PickedTarget::kShell ? Surface::kShell : Surface::kMaterial;
			const Cell*   picked = PickedCell(a_board, a_state);

			// Two labelled rows. The first is the recipe: S (the recipe applied
			// alone), the selection and the recipe within it. The second is what
			// is being edited: S (the picked output alone), target, slot, region,
			// and the shape viewed on. Columns fit their content and each combo is
			// as wide as its preview, so a long name is never clipped while a
			// short neighbour has room to spare.
			if (ImGui::BeginTable("recipe-context", 3, ImGuiMCP::ImGuiTableFlags_Borders | ImGuiMCP::ImGuiTableFlags_SizingFixedFit)) {
				ImGui::TableSetupColumn("S");
				ImGui::TableSetupColumn("selection");
				ImGui::TableSetupColumn("recipe");
				ImGui::TableHeadersRow();
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				IsolateCheckbox(&a_recipe, "##isolate");
				Widgets::Tooltip("solo the recipe: apply it alone");
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(Widgets::FitWidth(std::format("{} / {} (3rd)", a_piece.actorName, a_piece.armorName)));
				SelectionCombo(a_snapshot, "##selection");
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(Widgets::FitWidth(a_recipe.id));
				RecipeCombo(&a_piece, "##recipe");
				ImGui::EndTable();
			}
			Widgets::Rule();
			if (ImGui::BeginTable("context", shapes ? 5 : 4, ImGuiMCP::ImGuiTableFlags_Borders | ImGuiMCP::ImGuiTableFlags_SizingFixedFit)) {
				ImGui::TableSetupColumn("S");
				ImGui::TableSetupColumn("target");
				ImGui::TableSetupColumn("slot");
				ImGui::TableSetupColumn("region");
				if (shapes) {
					ImGui::TableSetupColumn("viewed on");
				}
				ImGui::TableHeadersRow();
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				{
					const std::optional<std::size_t> output = light ? a_board.light.output : (picked ? picked->output : std::nullopt);
					bool solo = light ? a_board.light.isolated : (picked && picked->isolated);
					if (!output) {
						ImGui::BeginDisabled();
					}
					if (ImGui::Checkbox("##solo", &solo) && output) {
						SoloOutput(a_view, a_recipe.id, *output, solo);
					}
					if (!output) {
						ImGui::EndDisabled();
					}
					Widgets::Tooltip("solo the picked output: show it alone");
				}
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(Widgets::FitWidth("material"));
				TargetChoice(a_state);
				ImGui::TableNextColumn();
				if (light) {
					Widgets::Dim("the recipe's light");
				} else {
					ImGui::SetNextItemWidth(Widgets::FitWidth(picked ? SlotLabel(*picked) : std::string{ "choose a slot" }));
					SlotChoice(a_board, surface, picked, a_state);
				}
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(Widgets::FitWidth(a_board.region.empty() ? std::string{ "whole piece" } : ReferenceText(a_board.region)));
				RegionChoice("##region", a_board, a_state.selection);
				if (shapes) {
					ImGui::TableNextColumn();
					ImGui::SetNextItemWidth(Widgets::FitWidth(GeometryLabel(a_geometry.name, a_piece.armorName)));
					GeometryChoice("##viewedon", a_piece, a_recipe, a_geometry, a_state.selection);
				}
				ImGui::EndTable();
			}

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

		constexpr int kFieldTable = ImGuiMCP::ImGuiTableFlags_BordersInnerH | ImGuiMCP::ImGuiTableFlags_SizingStretchProp;

		// A three-column table is the layout for every group of typed inputs:
		// the field's name, its type badge (with the detail button when the
		// field has details), and the input filling the rest.
		[[nodiscard]] bool BeginFieldTable(const char* a_id)
		{
			if (!ImGui::BeginTable(a_id, 3, kFieldTable)) {
				return false;
			}
			const float side = ImGui::GetFrameHeight();
			// A fixed column with no width sizes to its widest label ("channels").
			ImGui::TableSetupColumn("field", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 0.0f);
			ImGui::TableSetupColumn("", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, side);
			ImGui::TableSetupColumn("value");
			return true;
		}

		// Starts a row: the name, the badge, and the detail button when a_detail
		// is set; leaves the cursor in the value cell. Returns true when the
		// detail button was clicked.
		// Starts a row: the name, then the detail button when a_detailKey is
		// set; leaves the cursor in the value cell, where the input draws its
		// own badge. Returns true when the detail button was clicked.
		bool FieldRow(const char* a_name, const std::string* a_detailKey)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(a_name);
			ImGui::TableNextColumn();
			bool clicked = false;
			if (a_detailKey) {
				clicked = Widgets::DetailButton(*a_detailKey);
			}
			ImGui::TableNextColumn();
			return clicked;
		}

		void DrawScalars(const StackView& a_stack, const std::string& a_id, float a_scale)
		{
			if (a_stack.scalars.empty() || !BeginFieldTable("scalars")) {
				return;
			}
			for (const auto& scalar : a_stack.scalars) {
				const auto key = std::format("scalar:{}:{}:{}", a_id, a_stack.output, scalar.name);
				const bool colour = ParseScalarField(scalar.name) == std::optional{ ScalarField::kColor };
				FieldRow(scalar.name.c_str(), nullptr);
				Widgets::ValueSwatch(key + ":swatch", scalar.value);
				ImGui::SameLine();
				if (const auto edited = Widgets::ValueField(key, colour ? Widgets::FieldType::kColor : Widgets::FieldType::kScalar, scalar.text, colour ? a_stack.colorSignals : a_stack.scalarSignals, false, a_scale)) {
					PostScalar(a_id, a_stack.output, scalar.name, *edited);
				}
			}
			ImGui::EndTable();
		}

		// The layer table's columns: index, grip, solo, mute, the source's
		// type, blend, the source, remove.
		[[nodiscard]] bool BeginLayerTable()
		{
			if (!ImGui::BeginTable("layers", 8, ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_BordersInnerH | ImGuiMCP::ImGuiTableFlags_SizingStretchProp)) {
				return false;
			}
			const float side = ImGui::GetFrameHeight();
			ImGui::TableSetupColumn("#", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 0.0f);
			ImGui::TableSetupColumn("", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 0.0f);
			ImGui::TableSetupColumn("S", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 0.0f);
			ImGui::TableSetupColumn("M", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 0.0f);
			ImGui::TableSetupColumn("type", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, side);
			ImGui::TableSetupColumn("blend", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 0.0f);
			ImGui::TableSetupColumn("source");
			ImGui::TableSetupColumn("", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 0.0f);
			ImGui::TableHeadersRow();
			return true;
		}

		void DrawForeignRow(const ForeignRow& a_row)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TableNextColumn();
			ImGui::TableNextColumn();
			ImGui::TableNextColumn();
			ImGui::TableNextColumn();
			Widgets::Dim(a_row.layer.source.starts_with('@') ? "@" : "c");
			ImGui::TableNextColumn();
			Widgets::Dim(a_row.layer.blend);
			ImGui::TableNextColumn();
			Widgets::Dim(std::format("{}  ({}, priority {}: {} {})", a_row.layer.source, a_row.recipe, a_row.priority, a_row.layer.opacityText, a_row.layer.mask));
			ImGui::TableNextColumn();
		}

		// One layer as a table row. Clicking the grip or the name selects it;
		// its fields are drawn under the table.
		void DrawStackRow(const StackView& a_stack, const StackRow& a_row, const RecipeRow& a_recipe, MenuState& a_state, View& a_view)
		{
			const auto&       id = a_recipe.id;
			const std::size_t output = a_stack.output;
			const std::size_t index = a_row.index;
			const bool        constant = !a_row.layer.source.starts_with('@');

			ImGui::PushID(static_cast<int>(index));
			if (!a_row.inRegion) {
				ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_Alpha, kRowDimAlpha);
			}
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::Text("%zu", index);
			ImGui::TableNextColumn();
			if (Widgets::DragHandle(kLayerPayload, index)) {
				a_state.selection.layer = index;
			}
			if (const auto move = Widgets::DropTarget(kLayerPayload, index)) {
				Post(id, MoveLayer{ output, move->from, move->to });
			}
			ImGui::TableNextColumn();
			bool solo = a_row.soloed;
			if (ImGui::Checkbox("##solo", &solo)) {
				SoloLayer(a_view, id, output, index, solo);
			}
			Widgets::Tooltip("solo: show this layer alone");
			ImGui::TableNextColumn();
			bool mute = a_row.muted;
			if (ImGui::Checkbox("##mute", &mute)) {
				MuteLayer(a_view, id, output, index, mute);
			}
			Widgets::Tooltip("mute: hide this layer");
			ImGui::TableNextColumn();
			Widgets::Badge(constant ? Widgets::FieldType::kColor : Widgets::FieldType::kReference);
			ImGui::TableNextColumn();
			// Every blend combo the same width: the widest name the slot accepts.
			if (const auto blend = Widgets::BlendCombo("blend", a_row.layer.blend, a_stack.blends, Widgets::BlendWidth(a_stack.blends), 1.0f)) {
				Post(id, SetLayerBlend{ output, index, *blend });
			}
			ImGui::TableNextColumn();
			if (ImGui::Selectable((a_row.layer.source + "##source").c_str(), a_row.selected)) {
				a_state.selection.layer = index;
			}
			if (!a_row.layer.problem.empty()) {
				Widgets::Tooltip(a_row.layer.problem);
			}
			ImGui::TableNextColumn();
			if (ImGui::SmallButton("X")) {
				Post(id, RemoveLayer{ output, index });
			}
			Widgets::Tooltip("remove this layer");
			if (!a_row.inRegion) {
				ImGui::PopStyleVar();
			}
			ImGui::PopID();
		}

		void DrawStack(const std::optional<StackView>& a_stack, const std::optional<Inspector>& a_inspector, const RecipeRow& a_recipe, MenuState& a_state, View& a_view, const Layout& a_layout)
		{
			if (!a_stack) {
				Widgets::Dim("choose a target and a slot");
				return;
			}
			Widgets::Rule();
			const auto& stack = *a_stack;
			const auto& id = a_recipe.id;
			ImGui::TextUnformatted(stack.title.c_str());
			if (stack.isolated) {
				ImGui::SameLine();
				Widgets::Warn("(solo)");
			}
			if (!stack.problem.empty()) {
				Widgets::Problem(stack.problem);
			}
			Widgets::Thumbnail(stack.composite, 4, stack.animated, a_layout.compositeSize);
			ImGui::SameLine();
			ImGui::BeginGroup();
			Widgets::Dim(std::format("composite {} px, {}", stack.size, stack.animated ? "animated" : "static"));
			DrawScalars(stack, id, a_layout.widgetScale);
			ImGui::EndGroup();

			// The layers on the left, the selected layer's fields on the right,
			// split by a draggable vertical rule.
			if (ImGui::BeginTable("stack-split", 2, ImGuiMCP::ImGuiTableFlags_Resizable | ImGuiMCP::ImGuiTableFlags_BordersInnerV | ImGuiMCP::ImGuiTableFlags_SizingStretchProp)) {
				ImGui::TableSetupColumn("layers");
				ImGui::TableSetupColumn("inspect");
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				// In application order, top to bottom: the recipes merging before
				// this one, then this stack's base first and its last applied layer
				// at the bottom, then the recipes merging after.
				if (BeginLayerTable()) {
					for (const auto& foreign : stack.below) {
						DrawForeignRow(foreign);
					}
					for (const auto& row : stack.rows) {
						DrawStackRow(stack, row, a_recipe, a_state, a_view);
					}
					for (const auto& foreign : stack.above) {
						DrawForeignRow(foreign);
					}
					ImGui::EndTable();
				}
				if (ImGui::SmallButton("Add layer")) {
					Layer layer = DefaultLayer();
					// Under a region lens a new layer is masked by the region before
					// anything else is chosen.
					if (!a_state.selection.region.empty()) {
						layer.mask = Ref{ a_state.selection.region };
					}
					Post(id, AddLayer{ stack.output, layer, std::nullopt });
					// The new layer is applied last and lands at the bottom, at the index
					// the stack has now; it is selected as soon as the snapshot carries it.
					a_state.selection.layer = stack.rows.size();
				}
				Widgets::HelpMarker("Drag the :: grip onto another row to reorder; click the grip or the name to open the layer's fields beside the stack. S solos, M mutes. Enter commits a text field; a drag commits on release.");

				ImGui::TableNextColumn();
				// The selected layer's fields, headed by the layer, its source and its
				// solo and mute state.
				if (a_layout.inspector && a_inspector) {
					const auto row = std::ranges::find(stack.rows, a_inspector->layer, &StackRow::index);
					const bool soloed = row != stack.rows.end() && row->soloed;
					const bool muted = row != stack.rows.end() && row->muted;
					Widgets::Dim(std::format("Inspect: #{} - {}{}{}", a_inspector->layer, a_inspector->row.source, soloed ? " (soloed)" : "", muted ? " (muted)" : ""));
					if (!a_inspector->row.problem.empty()) {
						Widgets::Warn(a_inspector->row.problem);
					}
					Widgets::Thumbnail(row != stack.rows.end() ? row->layer.texture : nullptr, 4, false, a_layout.inspectorThumbnail * a_layout.widgetScale);
					ImGui::SameLine();
					ImGui::BeginGroup();
					DrawInspectorFields(*a_inspector, a_recipe, a_layout);
					ImGui::EndGroup();
				} else {
					Widgets::Dim("click a layer to inspect it");
				}
				ImGui::EndTable();
			}
		}

		// -------------------------------------------------------- signals

		// The value control for a signal a designer tunes; developer rows
		// are shown by the caller as text.
		void DrawSignalEditor(const std::string& a_id, const SignalRow& a_signal, float a_scale)
		{
			const auto key = "sig:" + a_id + ":" + a_signal.name;
			if (a_signal.constant) {
				if (const auto* number = Get<float>(*a_signal.constant)) {
					Widgets::Badge(Widgets::FieldType::kScalar);
					if (const auto edited = Widgets::DragField(key, *number, Widgets::kFillWidth, a_scale)) {
						Post(a_id, SetConstant{ a_signal.name, *edited });
					}
				} else if (const auto* color = Get<Vec3>(*a_signal.constant)) {
					Widgets::Badge(Widgets::FieldType::kColor);
					if (const auto edited = Widgets::ColorField(key, *color, Widgets::kFillWidth, a_scale)) {
						Post(a_id, SetConstant{ a_signal.name, *edited });
					}
				} else {
					Widgets::Dim("vec2 constants edit in the file");
				}
			} else if (a_signal.kind == "expr") {
				Widgets::Badge(Widgets::FieldType::kExpression);
				if (const auto edited = Widgets::TextField(key, a_signal.text, Widgets::kFillWidth, a_scale)) {
					Post(a_id, SetExpression{ a_signal.name, *edited });
				}
			} else {
				Widgets::Dim(a_signal.kind + " rows edit in the file");
			}
		}

		void DrawSignalRow(const std::string& a_id, const SignalRow& a_signal, bool a_tunable, float a_scale)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(a_signal.name.c_str());
			ImGui::TableNextColumn();
			Widgets::Dim(a_signal.kind);
			ImGui::TableNextColumn();
			Widgets::ValueSwatch("sigval:" + a_id + ":" + a_signal.name, a_signal.value);
			ImGui::TableNextColumn();
			if (a_tunable) {
				DrawSignalEditor(a_id, a_signal, a_scale);
			} else {
				Widgets::Dim("read-only");
			}
			ImGui::TableNextColumn();
			if (a_tunable) {
				Widgets::Badge(Widgets::FieldType::kCurve);
				if (const auto edited = Widgets::TextField("sigcurve:" + a_id + ":" + a_signal.name, a_signal.curve, Widgets::kFillWidth, a_scale)) {
					Post(a_id, SetSignalCurve{ a_signal.name, CurveRefOf(*edited) });
				}
			} else {
				Widgets::Dim(a_signal.curve);
			}
			ImGui::TableNextColumn();
			if (a_signal.inert) {
				Widgets::Problem(a_signal.problem.empty() ? "inert" : "inert: " + a_signal.problem);
			}
		}

		void DrawSignals(const RecipeRow& a_recipe, const Layout& a_layout)
		{
			const auto list = BuildSignalList(a_recipe, a_layout);
			const auto& id = a_recipe.id;
			if (!BeginPane("Signals")) {
				return;
			}
			ImGui::Text("t = %.2f s", a_recipe.time);
			if (ImGui::BeginTable("signals", 6, kTableFlags)) {
				ImGui::TableSetupColumn("signal");
				ImGui::TableSetupColumn("kind");
				ImGui::TableSetupColumn("value");
				ImGui::TableSetupColumn("edit");
				ImGui::TableSetupColumn("curve");
				ImGui::TableSetupColumn("state");
				ImGui::TableHeadersRow();
				for (const auto& signal : list.tunable) {
					DrawSignalRow(id, signal, true, a_layout.widgetScale);
				}
				for (const auto& signal : list.developer) {
					DrawSignalRow(id, signal, false, a_layout.widgetScale);
				}
				ImGui::EndTable();
			}
			if (a_recipe.curves.empty()) {
				return;
			}
			ImGui::SeparatorText("Curves (expressions in x; mean is the source's mean)");
			if (ImGui::BeginTable("curves", 2, kTableFlags)) {
				ImGui::TableSetupColumn("curve");
				ImGui::TableSetupColumn("expression");
				ImGui::TableHeadersRow();
				for (const auto& curve : a_recipe.curves) {
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(ReferenceText(curve.name).c_str());
					ImGui::TableNextColumn();
					Widgets::Badge(Widgets::FieldType::kCurve);
					if (const auto edited = Widgets::TextField("curve:" + id + ":" + curve.name, curve.text, Widgets::kFillWidth, a_layout.widgetScale)) {
						Post(id, SetCurve{ curve.name, *edited });
					}
				}
				ImGui::EndTable();
			}
		}

		// ------------------------------------------------------ inspector

		void PostLayerSource(const std::string& a_id, std::size_t a_output, std::size_t a_layer, const std::string& a_text)
		{
			if (const auto source = ParseLayerSource(a_text)) {
				Post(a_id, SetLayerSource{ a_output, a_layer, *source });
			} else {
				Refuse("source", a_text);
			}
		}

		void PostLayerOpacity(const std::string& a_id, std::size_t a_output, std::size_t a_layer, const std::string& a_text)
		{
			if (const auto opacity = ParseParam(a_text)) {
				Post(a_id, SetLayerOpacity{ a_output, a_layer, *opacity });
			} else {
				Refuse("opacity", a_text);
			}
		}

		void PostLayerColor(const std::string& a_id, std::size_t a_output, std::size_t a_layer, const std::string& a_text)
		{
			if (a_text.empty()) {
				Post(a_id, SetLayerColor{ a_output, a_layer, std::nullopt });
				return;
			}
			if (const auto color = ParseVec3Param(a_text)) {
				Post(a_id, SetLayerColor{ a_output, a_layer, *color });
			} else {
				Refuse("color", a_text);
			}
		}

		// A source or mask row the layer reads, with its picture and definition.
		void DrawImageRow(const std::string& a_id, const std::string& a_key, const ImageRow& a_image, bool a_editable, const Layout& a_layout)
		{
			const float scale = a_layout.widgetScale;
			Widgets::Thumbnail(a_image.texture, a_image.channel, a_image.animated, a_layout.inspectorThumbnail * scale);
			ImGui::TextUnformatted((ReferenceText(a_image.name) + " =").c_str());
			ImGui::SameLine();
			if (a_editable) {
				Widgets::Badge(Widgets::FieldType::kMask);
				if (const auto edited = Widgets::TextField(a_key, a_image.kind, Widgets::kFillWidth, scale)) {
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
			ImGui::Text("%s (%s)", ReferenceText(it->name).c_str(), it->kind.c_str());
			ImGui::SameLine();
			Widgets::ValueSwatch("detail:" + a_id + ":" + it->name, it->value);
			DrawSignalEditor(a_id, *it, a_scale);
			if (it->inert) {
				Widgets::Problem(it->problem.empty() ? "inert" : "inert: " + it->problem);
			}
		}

		// Which field's details a row has open; one modal at a time.
		enum class Detail
		{
			kNone,
			kSource,
			kCurve,
			kOpacity,
			kColor,
			kMask,
		};

		[[nodiscard]] const char* DetailTitle(Detail a_detail) noexcept
		{
			switch (a_detail) {
			case Detail::kSource:
				return "source";
			case Detail::kCurve:
				return "curve";
			case Detail::kOpacity:
				return "opacity";
			case Detail::kColor:
				return "colour";
			case Detail::kMask:
				return "mask";
			default:
				return "";
			}
		}

		void DrawDetailModal(Detail a_detail, const Inspector& a_in, const RecipeRow& a_recipe, const std::string& a_key, const Layout& a_layout)
		{
			const auto& id = a_recipe.id;
			const float scale = a_layout.widgetScale;
			switch (a_detail) {
			case Detail::kSource:
				if (a_in.source) {
					const bool isMask = std::ranges::find(a_in.masks, a_in.source->name) != a_in.masks.end();
					DrawImageRow(id, a_key + "sourcetext", *a_in.source, isMask, a_layout);
				} else {
					Widgets::Dim("a constant colour, or a name no source or mask has");
				}
				break;
			case Detail::kCurve:
				if (a_in.curve) {
					ImGui::TextUnformatted((ReferenceText(a_in.curve->name) + " =").c_str());
					ImGui::SameLine();
					Widgets::Badge(Widgets::FieldType::kCurve);
					if (const auto edited = Widgets::TextField(a_key + "curvetext", a_in.curve->text, Widgets::kFillWidth, scale)) {
						Post(id, SetCurve{ a_in.curve->name, *edited });
					}
				} else {
					Widgets::Dim("no declared curve; the layer's curve is inline or empty");
				}
				break;
			case Detail::kOpacity:
				DrawSignalDetail(id, a_in, a_in.row.opacityText, scale);
				break;
			case Detail::kColor:
				DrawSignalDetail(id, a_in, a_in.row.color, scale);
				break;
			case Detail::kMask:
				if (a_in.mask) {
					DrawImageRow(id, a_key + "masktext", *a_in.mask, true, a_layout);
				} else {
					Widgets::Dim("no mask");
				}
				break;
			default:
				break;
			}
			if (ImGui::Button("close")) {
				ImGui::CloseCurrentPopup();
			}
		}

		void DrawInspectorFields(const Inspector& a_inspector, const RecipeRow& a_recipe, const Layout& a_layout)
		{
			const auto&       in = a_inspector;
			const auto&       id = a_recipe.id;
			const std::size_t output = in.output;
			const std::size_t layer = in.layer;
			const auto        key = std::format("inspect:{}:{}:{}:", id, output, layer);
			const float       scale = a_layout.widgetScale;
			Detail            open = Detail::kNone;

			// A detail button appears only where the modal would show something.
			const auto namesSignal = [&](const std::string& a_text) {
				return a_text.starts_with('@') && std::ranges::find(in.signals, ReferenceName(a_text), &SignalRow::name) != in.signals.end();
			};
			const bool sourceDetail = in.source.has_value();
			const bool curveDetail = in.curve.has_value();
			const bool opacityDetail = namesSignal(in.row.opacityText);
			const bool colorDetail = namesSignal(in.row.color);
			const bool maskDetail = in.mask.has_value();

			if (BeginFieldTable("fields")) {
				// A layer's source may be a source or a mask row, so the combo lists both.
				std::vector<std::string> sourceNames = in.sources;
				sourceNames.insert(sourceNames.end(), in.masks.begin(), in.masks.end());
				const auto sourceKey = key + "source";
				if (FieldRow("source", sourceDetail ? &sourceKey : nullptr)) {
					open = Detail::kSource;
				}
				if (const auto text = Widgets::ValueField(key + "source", Widgets::FieldType::kColor, in.row.source, sourceNames, false, scale)) {
					PostLayerSource(id, output, layer, *text);
				}

				const auto curveKey = key + "curve";
				if (FieldRow("curve", curveDetail ? &curveKey : nullptr)) {
					open = Detail::kCurve;
				}
				if (const auto text = Widgets::ValueField(key + "curve", Widgets::FieldType::kCurve, in.row.curve, in.curves, true, scale)) {
					Post(id, SetLayerCurve{ output, layer, CurveRefOf(*text) });
				}

				const auto opacityKey = key + "opacity";
				if (FieldRow("opacity", opacityDetail ? &opacityKey : nullptr)) {
					open = Detail::kOpacity;
				}
				if (const auto text = Widgets::ValueField(key + "opacity", Widgets::FieldType::kScalar, in.row.opacityText, in.scalarSignals, false, scale)) {
					PostLayerOpacity(id, output, layer, *text);
				}

				const auto colorKey = key + "color";
				if (FieldRow("colour", colorDetail ? &colorKey : nullptr)) {
					open = Detail::kColor;
				}
				if (const auto text = Widgets::ValueField(key + "color", Widgets::FieldType::kColor, in.row.color, in.colorSignals, true, scale)) {
					PostLayerColor(id, output, layer, *text);
				}

				const auto maskKey = key + "mask";
				if (FieldRow("mask", maskDetail ? &maskKey : nullptr)) {
					open = Detail::kMask;
				}
				Widgets::Badge(Widgets::FieldType::kReference);
				if (const auto chosen = Widgets::ReferenceCombo(key + "maskref", in.row.mask, in.masks, true, Widgets::kFillWidth, scale)) {
					Post(id, SetLayerMask{ output, layer, MaskRefOf(*chosen) });
				}

				FieldRow("channels", nullptr);
				Widgets::Badge(Widgets::FieldType::kChannels);
				if (const auto edited = Widgets::TextField(key + "channels", in.row.channels, Widgets::kFillWidth, scale)) {
					if (const auto set = ChannelSet::Parse(*edited)) {
						Post(id, SetLayerChannels{ output, layer, *set });
					} else {
						Refuse("channels", *edited);
					}
				}
				ImGui::EndTable();
			}

			// The detail modals live outside the table so their ids match the
			// buttons' scope; one per field, opened by its button.
			for (const auto detail : { Detail::kSource, Detail::kCurve, Detail::kOpacity, Detail::kColor, Detail::kMask }) {
				const auto title = std::format("{} of layer {}###detail{}", DetailTitle(detail), layer, static_cast<int>(detail));
				if (open == detail) {
					ImGui::OpenPopup(title.c_str());
				}
				// An auto-resizing window starts narrow and wrapped text then wraps
				// every few characters; a floor on the width keeps a definition on
				// one or two lines.
				ImGui::SetNextWindowSizeConstraints(ImVec2{ 480.0f, 0.0f }, ImVec2{ 960.0f, 800.0f });
				if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiMCP::ImGuiWindowFlags_AlwaysAutoResize)) {
					DrawDetailModal(detail, in, a_recipe, key, a_layout);
					ImGui::EndPopup();
				}
			}
		}
	}

	// ---------------------------------------------------------------- page

	void DrawBody(const Snapshot& a_snapshot, const PieceRow* a_piece, const RecipeRow* a_recipe, const GeometryRow* a_geometry, MenuState& a_state, const Layout& a_layout);
	void DrawFooter(const RecipeRow* a_recipe);

	void __stdcall RenderStudio()
	{
		auto*      manager = Manager::GetSingleton();
		const auto snapshot = manager->TakeSnapshot();
		auto&      state = State();
		RenderStatus(snapshot);
		Widgets::ModeBar(state.mode);
		const auto layout = LayoutFor(state.mode);

		const auto* piece = SelectedPiece(snapshot, state.selection);
		const auto* recipe = SelectedRecipe(piece, state.selection);
		const auto* geometry = SelectedGeometry(recipe, state.selection);

		// The body scrolls above a footer pinned to the bottom of the page.
		const float footer = ImGui::GetFrameHeightWithSpacing() * 2.0f + ImGui::GetTextLineHeight() * 2.0f + 16.0f;  // the rule's gaps, the header row, the row
		if (ImGui::BeginChild("studio-body", ImVec2{ 0.0f, -footer }, 0, 0)) {
			DrawBody(snapshot, piece, recipe, geometry, state, layout);
		}
		ImGui::EndChild();
		DrawFooter(recipe);
	}

	void DrawBody(const Snapshot& a_snapshot, const PieceRow* a_piece, const RecipeRow* a_recipe, const GeometryRow* a_geometry, MenuState& a_state, const Layout& a_layout)
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
		auto& view = Manager::GetSingleton()->Debug();
		if (a_layout.stack) {
			const auto  board = BuildBoard(*a_recipe, *a_geometry, a_state.selection, view);
			const auto* picked = DrawContext(a_snapshot, board, *a_piece, *a_recipe, *a_geometry, a_state, view);
			if (picked && picked->output) {
				const auto stack = BuildStackView(*a_piece, *a_recipe, *a_geometry, a_state.selection, view);
				const auto inspector = a_layout.inspector ? BuildInspector(*a_recipe, *a_geometry, a_state.selection) : std::nullopt;
				DrawStack(stack, inspector, *a_recipe, a_state, view, a_layout);
			}
		}
		if (a_layout.signals) {
			DrawSignals(*a_recipe, a_layout);
		}
	}

	// The footer: the clock, as a labelled table; the scrubber takes the
	// remaining width.
	void DrawFooter(const RecipeRow* a_recipe)
	{
		Widgets::Rule();
		if (!ImGui::BeginTable("footer", 2, ImGuiMCP::ImGuiTableFlags_Borders | ImGuiMCP::ImGuiTableFlags_SizingStretchProp)) {
			return;
		}
		ImGui::TableSetupColumn("freeze", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 0.0f);
		ImGui::TableSetupColumn("t (s)");
		ImGui::TableHeadersRow();
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		FreezeCheckbox(a_recipe, "##freeze");
		ImGui::TableNextColumn();
		ImGui::SetNextItemWidth(Widgets::kFillWidth);
		ScrubSlider(a_recipe, "##scrub");
		ImGui::EndTable();
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
		if (recipe->geometries.size() > 1) {
			ImGui::SetNextItemWidth(Widgets::FitWidth(GeometryLabel(geometry->name, piece->armorName)));
			GeometryChoice("viewed on", *piece, *recipe, *geometry, state.selection);
		}
		auto&      view = Manager::GetSingleton()->Debug();
		const auto board = BuildBoard(*recipe, *geometry, state.selection, view);
		DrawBoard(board, *recipe, *geometry, state, view, LayoutFor(state.mode));
	}
}
