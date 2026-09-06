#include "ComposePage.h"

#include "Edits.h"
#include "Forms.h"
#include "Manager.h"
#include "MenuState.h"
#include "MenuWidgets.h"
#include "Settings.h"
#include "Studio.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <span>
#include <string>
#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include "extern/SKSEMenuFramework.h"
#pragma clang diagnostic pop

namespace ImGui = ImGuiMCP;
using ImGuiMCP::ImVec2;

// The compose page draws view models built from the snapshot and the page
// state, and collects what the widgets ask for as intents. Nothing here
// writes the selection while drawing: after the frame, Dispatch runs every
// intent through Reduce (the state change) and Perform (its effect on the
// manager and the view), so every record drawn in a frame came from the
// state the frame began with.
namespace WornEnchantmentPBR::Studio
{
	namespace
	{
		using Widgets::TableStyle;
		using Widgets::Width;
		using Intents = std::vector<Intent>;

		constexpr float       kRowDimAlpha = 0.45f;  // rows outside the selected region
		constexpr float       kFilterWidth = 160.0f;  // a table's name filter
		constexpr const char* kLayerPayload = "WEPBR_LAYER";

		// The tables' looks. A grid is rows of data (the board, the signals,
		// the curves); a context table fits its pickers; a form is fields
		// with an input each; the layer list is the stack's rows.
		constexpr TableStyle kGridStyle{ .borders = TableStyle::Borders::kAll, .stretch = true, .headers = true, .rowBackground = true };
		constexpr TableStyle kContextStyle{ .borders = TableStyle::Borders::kAll, .stretch = false, .headers = true, .rowBackground = false };
		constexpr TableStyle kFormStyle{ .borders = TableStyle::Borders::kInnerHorizontal, .stretch = true, .headers = false, .rowBackground = false };
		constexpr TableStyle kLayerStyle{ .borders = TableStyle::Borders::kInnerHorizontal, .stretch = true, .headers = true, .rowBackground = true };
		constexpr TableStyle kFooterStyle{ .borders = TableStyle::Borders::kAll, .stretch = true, .headers = true, .rowBackground = false };

		// ------------------------------------------------------- dispatch

		// Text that does not parse never becomes an edit; it is reported and
		// the field shows the model again.
		void Refuse(std::string_view a_field, const std::string& a_text)
		{
			logger::warn("{} not applied: '{}' does not parse", a_field, a_text);
		}

		// The effect of an intent beyond the page state: an edit goes through
		// the manager, which retires the wearers, applies it to the store's
		// copy, re-validates and re-applies (a refused edit is a log line);
		// solo, mute, freeze, scrub and speed set the view the tick reads;
		// undo, redo, a new recipe and a fired trigger are the manager's.
		void Perform(const Intent& a_intent)
		{
			auto* manager = Manager::GetSingleton();
			auto& view = manager->Debug();
			Match(
				a_intent,
				[&](const EditRecipe& i) {
					manager->EditRecipe(i.recipe, [edit = i.edit](Recipe& a_recipe) {
						if (const auto problem = Apply(a_recipe, edit)) {
							logger::warn("edit refused: {} ({}: {})", Describe(edit), problem->where, problem->message);
						}
					});
				},
				// Isolation has three levels, recipe, output and layer, and turning
				// a level off leaves the levels above it as they were. Isolating a
				// different recipe re-applies the wearers so it is bound alone.
				[&](const SoloRecipe& i) { manager->Isolate(i.on ? i.recipe : std::string{}, -1, -1); },
				[&](const SoloOutput& i) {
					if (i.on) {
						manager->Isolate(i.recipe, static_cast<int>(i.output), -1);
					} else {
						manager->Isolate(view.isolateRecipe, -1, -1);
					}
				},
				[&](const SoloLayer& i) {
					if (i.on) {
						manager->Isolate(i.recipe, static_cast<int>(i.output), static_cast<int>(i.layer));
					} else {
						manager->Isolate(view.isolateRecipe, view.isolateOutput, -1);
					}
				},
				[&](const MuteLayer& i) {
					const LayerKey key{ i.recipe, i.output, i.layer };
					if (i.on) {
						view.muted.insert(key);
					} else {
						view.muted.erase(key);
					}
				},
				[&](const SetFreeze& i) {
					view.freeze = i.on;
					if (i.on) {
						view.scrubSeconds = i.at;  // freezing holds the moment, not the slider's old value
					}
				},
				[&](const SetScrub& i) {
					view.freeze = true;
					view.scrubSeconds = i.seconds;
				},
				[&](const SetSpeed& i) { view.speed = std::clamp(i.speed, 0.0f, 8.0f); },
				// One tick of the recipe clock, held frozen at the new moment.
				[&](const StepClock&) {
					view.freeze = true;
					view.scrubSeconds += static_cast<float>(GetSettings().TickIntervalMS()) * 0.001f * view.speed;
				},
				[&](const Undo& i) { manager->UndoRecipe(i.recipe); },
				[&](const Redo& i) { manager->RedoRecipe(i.recipe); },
				[&](const CreateRecipe& i) { manager->NewRecipe(i.id, i.armorID); },
				[&](const FireTrigger& i) { manager->QueueEvent(i.actorID, EventRecord{ i.event, {} }); },
				[](const auto&) {});
		}

		void Dispatch(Intents& a_intents, MenuState& a_state)
		{
			for (const auto& intent : a_intents) {
				Perform(intent);
				Reduce(a_state, intent);
			}
			a_intents.clear();
		}

		// A recipe edit as an intent, for the widgets that make them.
		void Post(Intents& a_out, const std::string& a_recipe, RecipeEdit a_edit)
		{
			a_out.push_back(EditRecipe{ a_recipe, std::move(a_edit) });
		}

		// ---------------------------------------------------------- forms

		// The input for a field, by its kind: a reference is a combo over the
		// names; a choice a combo over plain names; a toggle a checkbox; an
		// expression, mask, channel set or text is a badge and a text field;
		// a value (scalar, colour, vector, curve) is a value field whose badge
		// switches between text and a signal combo.
		[[nodiscard]] std::optional<std::string> FieldInput(const FieldSpec& a_field, float a_scale)
		{
			switch (a_field.kind) {
			case FieldKind::kReference:
				Widgets::Badge(a_field.kind);
				return Widgets::ReferenceCombo("value", a_field.text, a_field.names, a_field.allowEmpty, Width::Fill(), a_scale);
			case FieldKind::kChoice:
				Widgets::Badge(a_field.kind);
				return Widgets::ChoiceCombo("value", a_field.text, a_field.names, Width::Fill(), a_scale);
			case FieldKind::kToggle: {
				Widgets::Badge(a_field.kind);
				bool on = a_field.text == "on";
				if (Widgets::Toggle("##value", on, "")) {
					return std::string{ on ? "on" : "off" };
				}
				return std::nullopt;
			}
			case FieldKind::kExpression:
			case FieldKind::kMask:
			case FieldKind::kChannels:
			case FieldKind::kText:
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
		[[nodiscard]] std::optional<FieldDetail> DrawForm(const char* a_id, std::span<const FieldSpec> a_form, const std::string& a_recipe, float a_scale, Intents& a_out)
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
						Post(a_out, a_recipe, *edit);
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
		[[nodiscard]] std::optional<ViewGeometry> NextGeometry(const RecipeRow& a_recipe, const GeometryRow& a_geometry)
		{
			const auto& shapes = a_recipe.geometries;
			if (shapes.empty()) {
				return std::nullopt;
			}
			const auto        it = std::ranges::find(shapes, a_geometry.name, &GeometryRow::name);
			const std::size_t at = it == shapes.end() ? 0 : static_cast<std::size_t>(it - shapes.begin());
			return ViewGeometry{ shapes[(at + 1) % shapes.size()].name };
		}

		// The region lens: whole piece, or one of the recipe's masks. The
		// caller sets the width and the label.
		void RegionChoice(const char* a_label, const Board& a_board, Intents& a_out)
		{
			const auto preview = a_board.region.empty() ? std::string{ "whole piece" } : ReferenceText(a_board.region);
			if (ImGui::BeginCombo(a_label, preview.c_str())) {
				if (ImGui::Selectable("whole piece", a_board.region.empty())) {
					a_out.push_back(PickRegion{});
				}
				for (const auto& name : a_board.regions) {
					if (ImGui::Selectable(ReferenceText(name).c_str(), name == a_board.region)) {
						a_out.push_back(PickRegion{ name });
					}
				}
				ImGui::EndCombo();
			}
			Widgets::Tooltip("Filters the stack to the layers masked by this mask; Add layer binds it first.");
		}

		void SelectionCombo(const Snapshot& a_snapshot, const PieceRow* a_piece, const char* a_label, Intents& a_out)
		{
			const auto preview = a_piece ? std::format("{} / {} ({})", a_piece->actorName, a_piece->armorName, a_piece->firstPerson ? "1st" : "3rd") : std::string{ "nothing applied" };
			if (ImGui::BeginCombo(a_label, preview.c_str())) {
				std::size_t i = 0;
				for (const auto& p : a_snapshot.pieces) {
					const auto label = std::format("{} / {} ({})##sel{}", p.actorName, p.armorName, p.firstPerson ? "1st" : "3rd", i++);
					if (ImGui::Selectable(label.c_str(), &p == a_piece)) {
						a_out.push_back(PickPiece{ p.actorID, p.armorID, p.firstPerson });
					}
				}
				ImGui::EndCombo();
			}
		}

		void RecipeCombo(const PieceRow& a_piece, const RecipeRow& a_recipe, const char* a_label, Intents& a_out)
		{
			if (ImGui::BeginCombo(a_label, a_recipe.id.c_str())) {
				for (const auto& r : a_piece.recipes) {
					if (ImGui::Selectable(std::format("{} ({}, priority {})", r.id, r.key, r.priority).c_str(), &r == &a_recipe)) {
						a_out.push_back(PickRecipe{ r.id });
					}
				}
				ImGui::EndCombo();
			}
		}

		void IsolateCheckbox(const RecipeRow& a_recipe, const char* a_label, Intents& a_out)
		{
			const auto& view = Manager::GetSingleton()->GetView();
			bool        isolating = view.Isolating();
			std::string text;
			if (isolating) {
				text = "isolating " + view.isolateRecipe;
				if (view.isolateOutput >= 0) {
					text += std::format(" output {}", view.isolateOutput);
				}
				if (view.isolateLayer >= 0) {
					text += std::format(" layer {}", view.isolateLayer);
				}
			}
			if (Widgets::Toggle(a_label, isolating, text)) {
				a_out.push_back(SoloRecipe{ a_recipe.id, isolating });
			}
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

		// Picking a written cell picks its top layer (the last applied), so
		// the inspector opens on something at once; a single-layer stack needs
		// no second click.
		[[nodiscard]] PickCell PickOf(const Cell& a_cell)
		{
			return PickCell{ a_cell.surface, a_cell.slot, a_cell.layers > 0 ? std::optional{ a_cell.layers - 1 } : std::nullopt };
		}

		void DrawWrittenCell(const Cell& a_cell, const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Board& a_board, const Selection& a_selection, const Layout& a_layout, Intents& a_out)
		{
			const bool selected = a_selection.target != Target::kLight && SurfaceOf(a_selection.target) == a_cell.surface && a_selection.slot == a_cell.slot;
			if (Widgets::ThumbnailButton("cell", a_cell.composite, 4, a_cell.animated, a_layout.cellSize * a_layout.widgetScale)) {
				a_out.push_back(PickOf(a_cell));
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
					a_out.push_back(SoloOutput{ a_recipe.id, *a_cell.output, solo });
				}
			}
			ImGui::EndGroup();
		}

		void DrawCell(const Cell* a_cell, const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Board& a_board, const Selection& a_selection, const Layout& a_layout, Intents& a_out)
		{
			if (!a_cell) {
				return;
			}
			const float side = a_layout.cellSize * a_layout.widgetScale;
			switch (a_cell->state) {
			case CellState::kAbsent:
				return;  // the surface has no such slot: nothing to draw
			case CellState::kWritten:
				DrawWrittenCell(*a_cell, a_recipe, a_geometry, a_board, a_selection, a_layout, a_out);
				return;
			case CellState::kEmpty:
				if (ImGui::Button("+", ImVec2{ side, side })) {
					Post(a_out, a_recipe.id, AddOutput{ a_cell->surface, a_cell->slot });
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
					a_out.push_back(PickOf(*a_cell));
				}
				return;
			}
		}

		void DrawLightCell(const LightCell& a_light, const RecipeRow& a_recipe, Intents& a_out)
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
					a_out.push_back(SoloOutput{ a_recipe.id, *a_light.output, solo });
				}
			}
		}

		void DrawBoard(const Board& a_board, const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Selection& a_selection, const Layout& a_layout, Intents& a_out)
		{
			if (!Widgets::Section("Board", true)) {
				return;
			}
			Widgets::NextItemWidth(Width::Px(200.0f));
			RegionChoice("region", a_board, a_out);
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
				DrawCell(CellAt(a_board, Surface::kMaterial, slot), a_recipe, a_geometry, a_board, a_selection, a_layout, a_out);
				ImGui::PopID();
				table.Cell();
				ImGui::PushID(static_cast<int>(i * 2 + 1));
				DrawCell(CellAt(a_board, Surface::kShell, slot), a_recipe, a_geometry, a_board, a_selection, a_layout, a_out);
				ImGui::PopID();
			}
			// Lights take no mask, so the light row hides under a region lens.
			if (a_board.region.empty()) {
				table.Cell();
				ImGui::TextUnformatted("light");
				table.Cell();
				DrawLightCell(a_board.light, a_recipe, a_out);
				table.Cell();
			}
			table.End();
		}

		// -------------------------------------------------------- context
		// Two labelled rows of choices. The recipe row: the recipe applied
		// alone, the piece, the recipe within it, New, Undo and Redo. The
		// edit row: the picked output alone, the target (material, shell or
		// the light: the format's word for where an output goes), the slot on
		// it, the region lens, and Clear.

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

		void TargetChoice(const Selection& a_selection, Intents& a_out)
		{
			constexpr Target targets[]{ Target::kMaterial, Target::kShell, Target::kLight };
			if (!ImGui::BeginCombo("##target", std::string{ TargetName(a_selection.target) }.c_str())) {
				return;
			}
			for (const auto target : targets) {
				if (ImGui::Selectable(std::string{ TargetName(target) }.c_str(), target == a_selection.target)) {
					a_out.push_back(PickTarget{ target });
				}
			}
			ImGui::EndCombo();
		}

		void SlotChoice(const Board& a_board, Surface a_surface, const Cell* a_picked, Intents& a_out)
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
						a_out.push_back(PickOf(*cell));
					} else {
						a_out.push_back(PickSlot{ cell->slot });
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

		// The picked cell, or null when the light or nothing is picked.
		[[nodiscard]] const Cell* PickedCell(const Board& a_board, const Selection& a_selection) noexcept
		{
			if (a_selection.target == Target::kLight || !a_selection.slot) {
				return nullptr;
			}
			return CellAt(a_board, SurfaceOf(a_selection.target), *a_selection.slot);
		}

		// An empty recipe keyed to the worn armor, under an id typed here; it
		// is selected as soon as the snapshot carries it.
		void NewRecipePopup(const PieceRow& a_piece, Intents& a_out)
		{
			if (!ImGui::BeginPopup("new-recipe")) {
				return;
			}
			Widgets::Dim(std::format("an empty recipe keyed to {}", a_piece.armorName));
			const auto id = Widgets::LiveTextField("id", "recipe id (its file name)", Width::Px(240.0f), 1.0f);
			const bool ready = !id.empty();
			if (!ready) {
				ImGui::BeginDisabled();
			}
			if (ImGui::Button("Create") && ready) {
				a_out.push_back(CreateRecipe{ std::string{ id }, a_piece.armorID });
				ImGui::CloseCurrentPopup();
			}
			if (!ready) {
				ImGui::EndDisabled();
			}
			ImGui::EndPopup();
		}

		void UndoRedoButtons(const RecipeRow& a_recipe, Intents& a_out)
		{
			if (a_recipe.undoDepth == 0) {
				ImGui::BeginDisabled();
			}
			if (ImGui::Button("Undo") && a_recipe.undoDepth > 0) {
				a_out.push_back(Undo{ a_recipe.id });
			}
			if (a_recipe.undoDepth == 0) {
				ImGui::EndDisabled();
			}
			Widgets::Tooltip(std::format("{} edit(s) to undo (Ctrl+Z)", a_recipe.undoDepth));
			ImGui::SameLine();
			if (a_recipe.redoDepth == 0) {
				ImGui::BeginDisabled();
			}
			if (ImGui::Button("Redo") && a_recipe.redoDepth > 0) {
				a_out.push_back(Redo{ a_recipe.id });
			}
			if (a_recipe.redoDepth == 0) {
				ImGui::EndDisabled();
			}
			Widgets::Tooltip(std::format("{} edit(s) to redo (Ctrl+Y)", a_recipe.redoDepth));
		}

		void DrawRecipeContext(const Snapshot& a_snapshot, const PieceRow& a_piece, const RecipeRow& a_recipe, Intents& a_out)
		{
			auto table = Widgets::Table::Begin("recipe-context", { { "S", Width::Fit() }, { "selection", Width::Fit() }, { "recipe", Width::Fit() }, { "history", Width::Fit() } }, kContextStyle);
			if (!table.Open()) {
				return;
			}
			table.Cell();
			IsolateCheckbox(a_recipe, "##isolate", a_out);
			Widgets::Tooltip("solo the recipe: apply it alone");
			table.Cell();
			Widgets::NextItemWidth(Width::Fit(std::format("{} / {} (3rd)", a_piece.actorName, a_piece.armorName)));
			SelectionCombo(a_snapshot, &a_piece, "##selection", a_out);
			table.Cell();
			Widgets::NextItemWidth(Width::Fit(a_recipe.id));
			RecipeCombo(a_piece, a_recipe, "##recipe", a_out);
			ImGui::SameLine();
			if (ImGui::Button("New")) {
				ImGui::OpenPopup("new-recipe");
			}
			NewRecipePopup(a_piece, a_out);
			table.Cell();
			UndoRedoButtons(a_recipe, a_out);
			table.End();
		}

		// Clear removes the picked output, layers and all, so the slot reads
		// empty again (or the recipe has no light); the target and slot stay
		// picked.
		void ClearButton(const RecipeRow& a_recipe, std::optional<std::size_t> a_output, Intents& a_out)
		{
			const std::optional<std::size_t> output = a_output;
			if (!output) {
				ImGui::BeginDisabled();
			}
			if (ImGui::Button("Clear") && output) {
				Post(a_out, a_recipe.id, RemoveOutput{ *output });
			}
			if (!output) {
				ImGui::EndDisabled();
			}
			Widgets::Tooltip("remove the picked slot's output and every layer in it");
		}

		// What the stack pane can show for a target: the shell and the light
		// have settings; the material and the shell have a stack. The switch
		// is the page's, and the pane falls back to what the target has.
		struct PaneChoice
		{
			bool hasSettings = false;
			bool hasStack = false;
			bool settings = false;  // what is shown
		};

		[[nodiscard]] PaneChoice ChoosePane(Target a_target, bool a_settingsWanted) noexcept
		{
			PaneChoice choice;
			choice.hasSettings = a_target != Target::kMaterial;
			choice.hasStack = a_target != Target::kLight;
			choice.settings = choice.hasStack ? (a_settingsWanted && choice.hasSettings) : true;
			return choice;
		}

		// Columns fit their content and each combo is as wide as its preview,
		// so a long name is never clipped while a short neighbour has room.
		void DrawEditContext(const Board& a_board, const RecipeRow& a_recipe, const Cell* a_picked, const Selection& a_selection, const PaneChoice& a_pane, Intents& a_out)
		{
			const bool light = a_selection.target == Target::kLight;
			auto       table = Widgets::Table::Begin("context", { { "S", Width::Fit() }, { "target", Width::Fit() }, { "show", Width::Fit() }, { "slot", Width::Fit() }, { "region", Width::Fit() }, { "", Width::Fill() }, { "", Width::Fit() } }, kContextStyle);
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
					a_out.push_back(SoloOutput{ a_recipe.id, *output, solo });
				}
				if (!output) {
					ImGui::EndDisabled();
				}
			}
			table.Cell();
			Widgets::NextItemWidth(Width::Fit("material"));
			TargetChoice(a_selection, a_out);
			table.Cell();
			if (Widgets::SwitchButton("settings", a_pane.settings, a_pane.hasSettings)) {
				a_out.push_back(ShowSettings{ true });
			}
			ImGui::SameLine();
			if (Widgets::SwitchButton("stack", !a_pane.settings, a_pane.hasStack)) {
				a_out.push_back(ShowSettings{ false });
			}
			table.Cell();
			if (light) {
				Widgets::Dim("the recipe's light");
			} else {
				Widgets::NextItemWidth(Width::Fit(a_picked ? SlotLabel(*a_picked) : std::string{ "choose a slot" }));
				SlotChoice(a_board, SurfaceOf(a_selection.target), a_picked, a_out);
			}
			table.Cell();
			Widgets::NextItemWidth(Width::Fit(a_board.region.empty() ? std::string{ "whole piece" } : ReferenceText(a_board.region)));
			RegionChoice("##region", a_board, a_out);
			table.Cell();
			table.Cell();
			ClearButton(a_recipe, light ? a_board.light.output : (a_picked ? a_picked->output : std::nullopt), a_out);
			table.End();
		}

		// Both rows, then what the pick needs under them: Add output for an
		// empty slot, the light's cell for the light, the reason for a refused
		// one. Returns the picked cell.
		const Cell* DrawContext(const Snapshot& a_snapshot, const Board& a_board, const PieceRow& a_piece, const RecipeRow& a_recipe, const Selection& a_selection, const PaneChoice& a_pane, Intents& a_out)
		{
			const Cell* picked = PickedCell(a_board, a_selection);
			DrawRecipeContext(a_snapshot, a_piece, a_recipe, a_out);
			Widgets::Rule();
			DrawEditContext(a_board, a_recipe, picked, a_selection, a_pane, a_out);

			if (a_selection.target == Target::kLight) {
				if (a_board.light.present) {
					DrawLightCell(a_board.light, a_recipe, a_out);
				} else if (ImGui::Button("Add light")) {
					Post(a_out, a_recipe.id, AddLight{});
				}
				return nullptr;
			}
			if (!picked) {
				return nullptr;
			}
			if (picked->state == CellState::kEmpty) {
				if (ImGui::Button("Add output")) {
					Post(a_out, a_recipe.id, AddOutput{ picked->surface, picked->slot });
				}
				Widgets::Tooltip(std::format("add an empty stack on {} of the {}", SlotName(picked->slot), SurfaceName(picked->surface)));
				return picked;
			}
			if (!picked->reason.empty()) {
				Widgets::Warn(picked->reason);
			}
			return picked;
		}

		// ---------------------------------------------------------- stack
		// The stack is a list of rows, base first. A row is the index, remove,
		// the grip, solo, mute, the source's type, the blend and the source;
		// the selected row's fields are drawn beside the table.

		void DrawInspectorFields(const Inspector& a_inspector, const RecipeRow& a_recipe, const Layout& a_layout, Intents& a_out);

		void DrawScalars(const StackView& a_stack, const std::string& a_id, float a_scale, Intents& a_out)
		{
			if (a_stack.scalars.empty()) {
				return;
			}
			// Scalars open no detail; the returned detail is not used.
			[[maybe_unused]] const auto detail = DrawForm("scalars", ScalarForm(a_stack), a_id, a_scale, a_out);
		}

		[[nodiscard]] Widgets::Table BeginLayerTable()
		{
			return Widgets::Table::Begin("layers", { { "#", Width::Fit() }, { "", Width::Fit() }, { "", Width::Fit() }, { "S", Width::Fit() }, { "M", Width::Fit() }, { "type", Width::Px(ImGui::GetFrameHeight()) }, { "blend", Width::Fit() }, { "source", Width::Fill() } }, kLayerStyle);
		}

		void DrawForeignRow(Widgets::Table& a_table, const ForeignRow& a_row)
		{
			a_table.Cell();
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
		}

		// One layer as a table row, in its own ID scope. Clicking the grip or
		// the name selects it.
		void DrawStackRow(Widgets::Table& a_table, const StackView& a_stack, const StackRow& a_row, const RecipeRow& a_recipe, Intents& a_out)
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
			if (ImGui::SmallButton("X")) {
				Post(a_out, id, RemoveLayer{ output, index });
			}
			Widgets::Tooltip("remove this layer");
			a_table.Cell();
			if (Widgets::DragHandle(kLayerPayload, index)) {
				a_out.push_back(PickLayer{ index });
			}
			if (const auto move = Widgets::DropTarget(kLayerPayload, index)) {
				Post(a_out, id, MoveLayer{ output, move->from, move->to });
			}
			a_table.Cell();
			bool solo = a_row.soloed;
			if (Widgets::Toggle("##solo", solo, "solo: show this layer alone")) {
				a_out.push_back(SoloLayer{ id, output, index, solo });
			}
			a_table.Cell();
			bool mute = a_row.muted;
			if (Widgets::Toggle("##mute", mute, "mute: hide this layer")) {
				a_out.push_back(MuteLayer{ id, output, index, mute });
			}
			a_table.Cell();
			Widgets::Badge(constant ? FieldKind::kColor : FieldKind::kReference);
			a_table.Cell();
			// Every blend combo the same width: the widest name the slot accepts.
			if (const auto blend = Widgets::BlendCombo("blend", a_row.layer.blend, a_stack.blends, Width::Px(Widgets::BlendWidth(a_stack.blends)), 1.0f)) {
				Post(a_out, id, SetLayerBlend{ output, index, *blend });
			}
			a_table.Cell();
			if (ImGui::Selectable(a_row.layer.source.c_str(), a_row.selected)) {
				a_out.push_back(PickLayer{ index });
			}
			if (!a_row.layer.problem.empty()) {
				Widgets::Tooltip(a_row.layer.problem);
			}
			if (!a_row.inRegion) {
				ImGui::PopStyleVar();
			}
			ImGui::PopID();
		}

		// A new layer is applied last and lands at the bottom, at the index
		// the stack has now; under a region lens it is masked by the region
		// before anything else is chosen. Reduce selects it.
		void DrawAddLayer(const StackView& a_stack, const RecipeRow& a_recipe, const Selection& a_selection, Intents& a_out)
		{
			if (ImGui::SmallButton("Add layer")) {
				Layer layer = DefaultLayer();
				if (!a_selection.region.empty()) {
					layer.mask = Ref{ a_selection.region };
				}
				Post(a_out, a_recipe.id, AddLayer{ a_stack.output, layer, a_stack.rows.size() });
			}
		}

		// The layer list: in application order, top to bottom, the recipes
		// merging before this one, then this stack's base first and its last
		// applied layer at the bottom, then the recipes merging after; and
		// Add layer.
		void DrawLayers(const StackView& a_stack, const RecipeRow& a_recipe, const Selection& a_selection, Intents& a_out)
		{
			auto table = BeginLayerTable();
			if (table.Open()) {
				for (const auto& foreign : a_stack.below) {
					DrawForeignRow(table, foreign);
				}
				for (const auto& row : a_stack.rows) {
					DrawStackRow(table, a_stack, row, a_recipe, a_out);
				}
				for (const auto& foreign : a_stack.above) {
					DrawForeignRow(table, foreign);
				}
				table.End();
			}
			DrawAddLayer(a_stack, a_recipe, a_selection, a_out);
			Widgets::HelpMarker("Drag the :: grip onto another row to reorder; click the grip or the name to open the layer's fields beside the stack. S solos, M mutes. Enter commits a text field; a drag commits on release.");
		}

		// The selected layer's fields, flush against the split's rule; the
		// layer's picture, when it has one, goes under them.
		void DrawInspector(const StackView& a_stack, const std::optional<Inspector>& a_inspector, const RecipeRow& a_recipe, const Layout& a_layout, Intents& a_out)
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
			DrawInspectorFields(*a_inspector, a_recipe, a_layout, a_out);
			if (row != a_stack.rows.end() && row->layer.texture) {
				Widgets::Thumbnail(row->layer.texture, 4, false, a_layout.inspectorThumbnail * a_layout.widgetScale);
			}
			ImGui::PopID();
		}

		// The composite as rendered on the shape viewed; on a piece with
		// several shapes, clicking it views the next one.
		void DrawComposite(const StackView& a_stack, const PieceRow& a_piece, const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Layout& a_layout, Intents& a_out)
		{
			if (a_recipe.geometries.size() < 2) {
				Widgets::Thumbnail(a_stack.composite, 4, a_stack.animated, a_layout.compositeSize);
				return;
			}
			if (Widgets::ThumbnailButton("composite", a_stack.composite, 4, a_stack.animated, a_layout.compositeSize)) {
				if (const auto next = NextGeometry(a_recipe, a_geometry)) {
					a_out.push_back(*next);
				}
			}
			Widgets::Tooltip(std::format("viewed on {} (one of {} shapes; the recipe applies to all)\nclick: view the next shape\nraw name: {}", GeometryLabel(a_geometry.name, a_piece.armorName), a_recipe.geometries.size(), a_geometry.name));
		}

		void DrawStack(const std::optional<StackView>& a_stack, const std::optional<Inspector>& a_inspector, const PieceRow& a_piece, const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Selection& a_selection, Layout& a_layout, Intents& a_out)
		{
			if (!a_stack) {
				Widgets::Dim("choose a target and a slot");
				return;
			}
			const auto& stack = *a_stack;
			const auto& id = a_recipe.id;
			// The edit row above names the slot and carries its solo toggle.
			ImGui::PushID(static_cast<int>(stack.output));
			if (!stack.problem.empty()) {
				Widgets::Problem(stack.problem);
			}
			DrawComposite(stack, a_piece, a_recipe, a_geometry, a_layout, a_out);
			ImGui::SameLine();
			ImGui::BeginGroup();
			Widgets::Dim(std::format("composite {} px, {}", stack.size, stack.animated ? "animated" : "static"));
			DrawScalars(stack, id, a_layout.widgetScale, a_out);
			ImGui::EndGroup();

			// An empty stack offers Add layer alone. Otherwise the layers on the
			// left, the selected layer's fields on the right, split by a
			// draggable vertical rule.
			if (stack.rows.empty()) {
				DrawAddLayer(stack, a_recipe, a_selection, a_out);
				ImGui::PopID();
				return;
			}
			Widgets::Split(
				"stack-split", a_layout.stackSplit,
				[&]() { DrawLayers(stack, a_recipe, a_selection, a_out); },
				[&]() { DrawInspector(stack, a_inspector, a_recipe, a_layout, a_out); });
			ImGui::PopID();
		}

		// -------------------------------------------------------- signals

		// The value control for a signal a designer tunes: the same input the
		// inspector's fields use, from the signal's form. A trigger with an
		// event id gets Fire, which posts one firing through the manager's
		// queue. A row with no form otherwise reads as its kind. Drawn in
		// the signal's ID scope.
		void DrawSignalEditor(const std::string& a_id, const SignalRow& a_signal, FormID a_actorID, float a_scale, Intents& a_out)
		{
			const auto field = SignalForm(a_signal);
			if (!field) {
				if (!a_signal.event.empty()) {
					if (ImGui::SmallButton("Fire")) {
						a_out.push_back(FireTrigger{ a_actorID, a_signal.event });
					}
					ImGui::SameLine();
				}
				Widgets::Dim(a_signal.kind + ": edits in the file");
				return;
			}
			if (const auto text = FieldInput(*field, a_scale)) {
				const std::optional<RecipeEdit> edit = field->bind ? field->bind(*text) : std::nullopt;
				if (edit) {
					Post(a_out, a_id, *edit);
				} else {
					Refuse(field->name, *text);
				}
			}
		}

		// The curve column: a combo over the recipe's declared curves, or none.
		void DrawSignalCurve(const std::string& a_id, const SignalRow& a_signal, std::span<const std::string> a_curves, float a_width, float a_scale, Intents& a_out)
		{
			if (const auto chosen = Widgets::ReferenceCombo("curve", a_signal.curve, a_curves, true, Width::Px(a_width), a_scale)) {
				Post(a_out, a_id, SetSignalCurve{ a_signal.name, chosen->empty() ? std::nullopt : std::optional{ CurveRef{ *chosen } } });
			}
			Widgets::Tooltip("a declared curve applied to the signal's value; none passes it through");
		}

		// One signal as a row: the name (a field: committing another name
		// renames the row and repoints every reference to it; its kind in the
		// tooltip), the editor, the curve, "=", and the live value, or inert
		// with the reason.
		// A row's remove button, greyed while anything references the row.
		void RemoveRowButton(std::size_t a_references, const std::function<void()>& a_remove)
		{
			if (a_references > 0) {
				ImGui::BeginDisabled();
			}
			if (ImGui::SmallButton("X") && a_references == 0) {
				a_remove();
			}
			if (a_references > 0) {
				ImGui::EndDisabled();
			}
			Widgets::Tooltip(a_references > 0 ? std::format("referenced in {} place(s)", a_references) : "remove this row");
		}

		void DrawSignalRow(Widgets::Table& a_table, const std::string& a_id, const SignalRow& a_signal, bool a_tunable, std::span<const std::string> a_curves, float a_curveWidth, FormID a_actorID, float a_scale, Intents& a_out)
		{
			ImGui::PushID(a_signal.name.c_str());
			a_table.Cell();
			RemoveRowButton(a_signal.references, [&]() { Post(a_out, a_id, RemoveSignal{ a_signal.name }); });
			a_table.Cell();
			if (const auto renamed = Widgets::TextField("name", a_signal.name, Width::Fill(), a_scale)) {
				Post(a_out, a_id, RenameSignal{ a_signal.name, *renamed });
			}
			Widgets::Tooltip(a_signal.kind);
			a_table.Cell();
			DrawSignalEditor(a_id, a_signal, a_actorID, a_scale, a_out);
			a_table.Cell();
			if (a_tunable) {
				DrawSignalCurve(a_id, a_signal, a_curves, a_curveWidth, a_scale, a_out);
			} else {
				Widgets::Dim(a_signal.curve);
			}
			a_table.Cell();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("=");
			a_table.Cell();
			ImGui::AlignTextToFramePadding();
			if (a_signal.inert) {
				Widgets::Problem("inert");
				Widgets::Tooltip(a_signal.problem.empty() ? "inert" : a_signal.problem);
			} else {
				Widgets::ValueSwatch(a_signal.value);
			}
			ImGui::PopID();
		}

		// The signal table, in its own pane; the rows are those the name
		// filter on its rule passes.
		void DrawSignals(const PieceRow& a_piece, const RecipeRow& a_recipe, const Layout& a_layout, std::string_view a_filter, Intents& a_out)
		{
			const auto  list = BuildSignalList(a_recipe, a_layout);
			const auto& id = a_recipe.id;
			const float scale = a_layout.widgetScale;

			// Every curve combo the same width: the widest curve name, or "none";
			// every name field the same width: the widest signal name.
			std::vector<std::string> curveNames;
			std::vector<std::string> curveTexts{ "none" };
			for (const auto& curve : a_recipe.curves) {
				curveNames.push_back(curve.name);
				curveTexts.push_back(ReferenceText(curve.name));
			}
			const float curveWidth = Widgets::WidestOf(curveTexts) * scale;
			std::vector<std::string> names;
			for (const auto& signal : a_recipe.signals) {
				names.push_back(signal.name);
			}
			const float nameWidth = Widgets::WidestOf(names) * scale;

			auto signals = Widgets::Table::Begin("signals", { { "", Width::Fit() }, { "signal", Width::Px(nameWidth) }, { "edit", Width::Fill() }, { "curve", Width::Px(curveWidth) }, { "=", Width::Fit() }, { "value", Width::Fit() } }, kGridStyle);
			if (!signals.Open()) {
				return;
			}
			for (const auto& signal : list.tunable) {
				if (NameMatches(signal.name, a_filter)) {
					DrawSignalRow(signals, id, signal, true, curveNames, curveWidth, a_piece.actorID, scale, a_out);
				}
			}
			for (const auto& signal : list.developer) {
				if (NameMatches(signal.name, a_filter)) {
					DrawSignalRow(signals, id, signal, false, curveNames, curveWidth, a_piece.actorID, scale, a_out);
				}
			}
			signals.End();
		}

		// The declared curves, in their own pane, those the name filter on
		// its rule passes: each an expression in x, its name a field.
		void DrawCurves(const RecipeRow& a_recipe, const Layout& a_layout, std::string_view a_filter, Intents& a_out)
		{
			const auto& id = a_recipe.id;
			const float scale = a_layout.widgetScale;
			std::vector<std::string> names;
			for (const auto& curve : a_recipe.curves) {
				names.push_back(curve.name);
			}
			const float nameWidth = Widgets::WidestOf(names) * scale;
			auto        curves = Widgets::Table::Begin("curves", { { "", Width::Fit() }, { "curve", Width::Px(nameWidth) }, { "expression", Width::Fill() } }, kGridStyle);
			if (!curves.Open()) {
				return;
			}
			for (const auto& curve : a_recipe.curves) {
				if (!NameMatches(curve.name, a_filter)) {
					continue;
				}
				ImGui::PushID(curve.name.c_str());
				curves.Cell();
				RemoveRowButton(curve.references, [&]() { Post(a_out, id, RemoveCurve{ curve.name }); });
				curves.Cell();
				if (const auto renamed = Widgets::TextField("name", curve.name, Width::Fill(), scale)) {
					Post(a_out, id, RenameCurve{ curve.name, *renamed });
				}
				curves.Cell();
				Widgets::Badge(FieldKind::kCurve);
				if (const auto edited = Widgets::TextField("text", curve.text, Width::Fill(), scale)) {
					Post(a_out, id, SetCurve{ curve.name, *edited });
				}
				ImGui::PopID();
			}
			curves.End();
		}

		// Add and the name filter for the open tab, at the right edge of the
		// resources rule, in the tab's own ID scope so each tab keeps its
		// filter and the two Add buttons never share an ID.
		[[nodiscard]] Widgets::RuleLine ResourcesRule(ResourceTab a_tab, const RecipeRow& a_recipe, float a_scale, std::string_view& a_filter, Intents& a_out)
		{
			const float addWidth = Widgets::FitWidth("Add");
			const float rightWidth = addWidth + Widgets::ItemSpacingX() + kFilterWidth * a_scale;
			return Widgets::RuleLine{ "Resources", rightWidth, [=, &a_recipe, &a_filter, &a_out]() {
									 ImGui::PushID(static_cast<int>(a_tab));
									 if (ImGui::Button("Add", ImVec2{ addWidth, 0.0f })) {
										 std::vector<std::string> names;
										 if (a_tab == ResourceTab::kSignals) {
											 for (const auto& signal : a_recipe.signals) {
												 names.push_back(signal.name);
											 }
											 Post(a_out, a_recipe.id, AddSignal{ UniqueName("signal", names) });
										 } else {
											 for (const auto& curve : a_recipe.curves) {
												 names.push_back(curve.name);
											 }
											 Post(a_out, a_recipe.id, AddCurve{ UniqueName("curve", names) });
										 }
									 }
									 ImGui::SameLine();
									 a_filter = Widgets::LiveTextField("filter", "filter by name", Width::Px(kFilterWidth), a_scale);
									 ImGui::PopID();
								 } };
		}

		// The resources pane: a tab per table, the tab bar fixed and each
		// table scrolling inside its tab. The tab bar owns the click; the
		// state follows it, so the rule above serves the open tab from the
		// next frame on.
		void DrawResources(const PieceRow& a_piece, const RecipeRow& a_recipe, const Layout& a_layout, ResourceTab a_open, std::string_view a_filter, Intents& a_out)
		{
			if (!ImGui::BeginTabBar("resources")) {
				return;
			}
			for (const auto tab : kResourceTabs) {
				const std::string name{ ResourceTabName(tab) };
				if (!ImGui::BeginTabItem(name.c_str())) {
					continue;
				}
				if (tab != a_open) {
					a_out.push_back(ShowResource{ tab });
				}
				if (ImGui::BeginChild(name.c_str(), ImVec2{ 0.0f, 0.0f }, 0, 0)) {
					switch (tab) {
					case ResourceTab::kSignals:
						DrawSignals(a_piece, a_recipe, a_layout, a_filter, a_out);
						break;
					case ResourceTab::kCurves:
						DrawCurves(a_recipe, a_layout, a_filter, a_out);
						break;
					}
				}
				ImGui::EndChild();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		// ------------------------------------------------------ inspector

		// A source or mask row the layer reads, with its picture and definition.
		void DrawImageRow(const std::string& a_id, const ImageRow& a_image, bool a_editable, const Layout& a_layout, Intents& a_out)
		{
			const float scale = a_layout.widgetScale;
			Widgets::Thumbnail(a_image.texture, a_image.channel, a_image.animated, a_layout.inspectorThumbnail * scale);
			ImGui::TextUnformatted((ReferenceText(a_image.name) + " =").c_str());
			ImGui::SameLine();
			if (a_editable) {
				Widgets::Badge(FieldKind::kMask);
				if (const auto edited = Widgets::TextField("text", a_image.kind, Width::Fill(), scale)) {
					Post(a_out, a_id, SetMask{ a_image.name, *edited });
				}
			} else {
				ImGui::TextWrapped("%s", a_image.kind.c_str());
			}
			if (!a_image.problem.empty()) {
				Widgets::Warn(a_image.problem);
			}
		}

		// The signal a parameter text names, drawn as its editor inside a modal.
		void DrawSignalDetail(const std::string& a_id, const Inspector& a_inspector, const std::string& a_text, FormID a_actorID, float a_scale, Intents& a_out)
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
			DrawSignalEditor(a_id, *it, a_actorID, a_scale, a_out);
			if (it->inert) {
				Widgets::Problem(it->problem.empty() ? "inert" : "inert: " + it->problem);
			}
			ImGui::PopID();
		}

		void DrawDetailModal(FieldDetail a_detail, const Inspector& a_in, const RecipeRow& a_recipe, FormID a_actorID, const Layout& a_layout, Intents& a_out)
		{
			const auto& id = a_recipe.id;
			const float scale = a_layout.widgetScale;
			switch (a_detail) {
			case FieldDetail::kSource:
				if (a_in.source) {
					const bool isMask = std::ranges::find(a_in.masks, a_in.source->name) != a_in.masks.end();
					DrawImageRow(id, *a_in.source, isMask, a_layout, a_out);
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
						Post(a_out, id, SetCurve{ a_in.curve->name, *edited });
					}
				} else {
					Widgets::Dim("no declared curve; the layer's curve is inline or empty");
				}
				break;
			case FieldDetail::kOpacity:
				DrawSignalDetail(id, a_in, a_in.row.opacityText, a_actorID, scale, a_out);
				break;
			case FieldDetail::kColor:
				DrawSignalDetail(id, a_in, a_in.row.color, a_actorID, scale, a_out);
				break;
			case FieldDetail::kMask:
				if (a_in.mask) {
					DrawImageRow(id, *a_in.mask, true, a_layout, a_out);
				} else {
					Widgets::Dim("no mask");
				}
				break;
			}
			if (ImGui::Button("close")) {
				ImGui::CloseCurrentPopup();
			}
		}

		// The detail modals live outside the table so their ids match the
		// buttons' scope; one per field, opened by its button. The actor is
		// the piece's, for a trigger fired from a modal; the body sets it.
		FormID g_modalActor = 0;

		void DrawInspectorFields(const Inspector& a_inspector, const RecipeRow& a_recipe, const Layout& a_layout, Intents& a_out)
		{
			const std::optional<FieldDetail> open = DrawForm("fields", InspectorForm(a_inspector), a_recipe.id, a_layout.widgetScale, a_out);
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
					DrawDetailModal(detail, a_inspector, a_recipe, g_modalActor, a_layout, a_out);
					ImGui::EndPopup();
				}
			}
		}

		// ---------------------------------------------------------- body

		// Everything under the mode bar is drawn in the recipe's ID scope, so a
		// field's key names the same field only while the same recipe is shown.
		// The context rows stay in view; under them two panes scroll on their
		// own, separated by rules: the stack and the resources. The resources
		// take a share of what is left under the rows; the stack takes the rest.
		void DrawBody(const Snapshot& a_snapshot, const PieceRow* a_piece, const RecipeRow* a_recipe, const GeometryRow* a_geometry, MenuState& a_state, Intents& a_out)
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
			const auto& view = Manager::GetSingleton()->GetView();
			const auto& selection = a_state.selection;
			Layout&     layout = a_state.layout;
			const float scale = layout.widgetScale;
			g_modalActor = a_piece->actorID;
			ImGui::PushID(a_recipe->id.c_str());

			const auto  board = BuildBoard(*a_recipe, *a_geometry, selection, view);
			const auto  pane = ChoosePane(selection.target, a_state.settings);
			const Cell* picked = layout.stack ? DrawContext(a_snapshot, board, *a_piece, *a_recipe, selection, pane, a_out) : nullptr;
			const bool  resources = layout.signals;
			// The pane's rule names what the switch shows: the target's settings
			// or the stack.
			const char* paneTitle = pane.settings ? (selection.target == Target::kLight ? "Light settings" : "Shell settings") : "Stack";
			Widgets::Rule({}, Widgets::RuleLine::Text(paneTitle));
			const float under = ImGui::GetContentRegionAvail().y;
			const float resourcesHeight = resources ? under * layout.resourcesShare : 0.0f;
			const float stackHeight = resources ? -(resourcesHeight + Widgets::RuleHeight()) : 0.0f;

			if (ImGui::BeginChild("stack-pane", ImVec2{ 0.0f, stackHeight }, 0, 0)) {
				// The pane shows the target's settings (the light's panel, the
				// shell's settings) or the picked slot's stack, as switched.
				if (pane.settings) {
					if (selection.target == Target::kLight) {
						if (a_recipe->lightRow.present) {
							[[maybe_unused]] const auto detail = DrawForm("light", LightForm(a_recipe->lightRow, SignalNamesOf(*a_recipe)), a_recipe->id, scale, a_out);
						} else {
							Widgets::Dim("the recipe has no light");
						}
					} else {
						[[maybe_unused]] const auto detail = DrawForm("shell", ShellForm(a_recipe->shellRow, SignalNamesOf(*a_recipe)), a_recipe->id, scale, a_out);
					}
				} else if (picked && picked->output) {
					const auto stack = BuildStackView(*a_piece, *a_recipe, *a_geometry, selection, view);
					const auto inspector = layout.inspector ? BuildInspector(*a_recipe, *a_geometry, selection) : std::nullopt;
					DrawStack(stack, inspector, *a_piece, *a_recipe, *a_geometry, selection, layout, a_out);
				}
			}
			ImGui::EndChild();

			if (resources) {
				std::string_view filter;
				Widgets::Rule({}, ResourcesRule(a_state.resource, *a_recipe, scale, filter, a_out));
				DrawResources(*a_piece, *a_recipe, layout, a_state.resource, filter, a_out);
			}
			ImGui::PopID();
		}

		// -------------------------------------------------------- footer
		// The clock: Freeze, one step, the speed, and the scrubber across the
		// remaining width. Running, the scrubber follows the clock; frozen, it
		// is the scrub. Taking hold of it freezes at the moment grabbed, so a
		// drag never fights the clock. The clock runs on unbounded; the slider
		// shows it within the current minute and moves it within that minute,
		// so recipes that read `time` never see a wrap.

		void DrawFooter(const RecipeRow* a_recipe, Intents& a_out)
		{
			const auto& view = Manager::GetSingleton()->GetView();
			const float now = a_recipe ? a_recipe->time : 0.0f;
			Widgets::Rule({}, Widgets::RuleLine::Text("Timeline"));
			auto table = Widgets::Table::Begin("footer", { { "freeze", Width::Fit() }, { "step", Width::Fit() }, { "speed", Width::Fit() }, { "t (s)", Width::Fill() } }, kFooterStyle);
			if (!table.Open()) {
				return;
			}
			table.Cell();
			bool freeze = view.freeze;
			if (Widgets::Toggle("##freeze", freeze, "")) {
				a_out.push_back(SetFreeze{ freeze, now });
			}
			table.Cell();
			if (ImGui::SmallButton(">|")) {
				if (!view.freeze) {
					a_out.push_back(SetFreeze{ true, now });
				}
				a_out.push_back(StepClock{});
			}
			Widgets::Tooltip("advance the recipe one tick and hold");
			table.Cell();
			float speed = view.speed;
			Widgets::NextItemWidth(Width::Px(120.0f));
			if (ImGui::SliderFloat("##speed", &speed, 0.0f, 4.0f, "%.2fx")) {
				a_out.push_back(SetSpeed{ speed });
			}
			Widgets::Tooltip("multiplies every recipe's clock");
			table.Cell();
			const float actual = view.freeze ? view.scrubSeconds : now;
			const float minute = std::floor(actual / 60.0f) * 60.0f;
			float       shown = actual - minute;
			Widgets::NextItemWidth(Width::Fill());
			const bool changed = ImGui::SliderFloat("##scrub", &shown, 0.0f, 60.0f, "%.2f");
			if (changed || ImGui::IsItemActive()) {
				a_out.push_back(SetScrub{ minute + shown });
			}
			if (minute > 0.0f) {
				Widgets::Tooltip(std::format("minute {} of the clock; t = {:.2f} s", static_cast<int>(minute / 60.0f) + 1, actual));
			}
			table.End();
		}

		// Ctrl+Z and Ctrl+Y, while no field has the keyboard.
		void HistoryKeys(const RecipeRow* a_recipe, const MenuState& a_state, Intents& a_out)
		{
			const auto* io = ImGui::GetIO();
			if (!a_recipe || !io || !io->KeyCtrl || a_state.activeField != kNoField) {
				return;
			}
			if (ImGui::IsKeyPressed(ImGuiMCP::ImGuiKey_Z, false)) {
				a_out.push_back(Undo{ a_recipe->id });
			}
			if (ImGui::IsKeyPressed(ImGuiMCP::ImGuiKey_Y, false)) {
				a_out.push_back(Redo{ a_recipe->id });
			}
		}
	}

	// ---------------------------------------------------------------- page

	void __stdcall RenderStudio()
	{
		auto*      manager = Manager::GetSingleton();
		const auto snapshot = manager->TakeSnapshot();
		auto&      state = State();
		Intents    intents;

		Mode mode = state.mode;
		if (Widgets::ModeBar(mode)) {
			intents.push_back(SetMode{ mode });
		}

		const auto* piece = SelectedPiece(snapshot, state.selection);
		const auto* recipe = SelectedRecipe(piece, state.selection);
		const auto* geometry = SelectedGeometry(recipe, state.selection);

		// The body scrolls above a footer pinned to the bottom of the page.
		const float footer = Widgets::RuleHeight() + ImGui::GetFrameHeightWithSpacing() * 2.0f + 8.0f;  // the rule, the header row, the row, the table's padding
		if (ImGui::BeginChild("studio-body", ImVec2{ 0.0f, -footer }, 0, 0)) {
			DrawBody(snapshot, piece, recipe, geometry, state, intents);
		}
		ImGui::EndChild();
		DrawFooter(recipe, intents);
		HistoryKeys(recipe, state, intents);
		Dispatch(intents, state);
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
		const auto& view = Manager::GetSingleton()->GetView();
		const auto  board = BuildBoard(*recipe, *geometry, state.selection, view);
		Intents     intents;
		ImGui::PushID(recipe->id.c_str());
		DrawBoard(board, *recipe, *geometry, state.selection, state.layout, intents);
		ImGui::PopID();
		Dispatch(intents, state);
	}
}
