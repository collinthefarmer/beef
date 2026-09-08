#include "ComposePage.h"

#include "EditCheck.h"
#include "Expression.h"
#include "Edits.h"
#include "Forms.h"
#include "Manager.h"
#include "MaskStack.h"
#include "MenuState.h"
#include "MenuWidgets.h"
#include "RecipeStore.h"
#include "Regions.h"
#include "Settings.h"
#include "Studio.h"
#include "Vocabulary.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <functional>
#include <span>
#include <string>
#include <variant>
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
		// The light's and the shell's settings are dealt into this many field
		// tables side by side, so the pane shows them without scrolling.
		constexpr std::size_t kSettingsColumns = 2;
		constexpr TableStyle  kColumnsStyle{ .borders = TableStyle::Borders::kNone, .stretch = true, .headers = false, .rowBackground = false };
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
		void Perform(const Intent& a_intent, const View& a_view)
		{
			auto*       manager = Manager::GetSingleton();
			const auto& view = a_view;
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
				// Isolation has three levels, recipe, output and layer. Soloing an
				// output or a layer isolates its recipe too, since nothing shows
				// alone otherwise; when that solo turns off, an isolate it began
				// ends with it, and one the recipe row set stays.
				[&](const SoloRecipe& i) {
					manager->UpdateView([](View& a_live) { a_live.isolatedBySolo = false; });
					manager->Isolate(i.on ? i.recipe : std::string{}, -1, -1);
				},
				[&](const SoloOutput& i) {
					if (i.on) {
						manager->UpdateView([began = !view.Isolating()](View& a_live) { a_live.isolatedBySolo = a_live.isolatedBySolo || began; });
						manager->Isolate(i.recipe, static_cast<int>(i.output), -1);
					} else if (view.isolatedBySolo) {
						manager->UpdateView([](View& a_live) { a_live.isolatedBySolo = false; });
						manager->Isolate(std::string{}, -1, -1);
					} else {
						manager->Isolate(view.isolateRecipe, -1, -1);
					}
				},
				[&](const SoloLayer& i) {
					if (i.on) {
						manager->UpdateView([began = !view.Isolating()](View& a_live) { a_live.isolatedBySolo = a_live.isolatedBySolo || began; });
						manager->Isolate(i.recipe, static_cast<int>(i.output), static_cast<int>(i.layer));
					} else if (view.isolatedBySolo && view.isolateOutput < 0) {
						manager->UpdateView([](View& a_live) { a_live.isolatedBySolo = false; });
						manager->Isolate(std::string{}, -1, -1);
					} else {
						manager->Isolate(view.isolateRecipe, view.isolateOutput, -1);
					}
				},
				[&](const MuteLayer& i) {
					manager->UpdateView([key = LayerKey{ i.recipe, i.output, i.layer }, on = i.on](View& a_live) {
						if (on) {
							a_live.muted.insert(key);
						} else {
							a_live.muted.erase(key);
						}
					});
				},
				[&](const SetFreeze& i) {
					manager->UpdateView([on = i.on, at = i.at](View& a_live) {
						a_live.freeze = on;
						if (on) {
							a_live.scrubSeconds = at;  // freezing holds the moment, not the slider's old value
						}
					});
				},
				[&](const SetScrub& i) {
					manager->UpdateView([seconds = i.seconds](View& a_live) {
						a_live.freeze = true;
						a_live.scrubSeconds = seconds;
					});
				},
				[&](const SetSpeed& i) {
					manager->UpdateView([speed = std::clamp(i.speed, 0.0f, 8.0f)](View& a_live) { a_live.speed = speed; });
				},
				// One tick of the recipe clock, held frozen at the new moment.
				[&](const StepClock&) {
					manager->UpdateView([](View& a_live) {
						a_live.freeze = true;
						a_live.scrubSeconds += static_cast<float>(GetSettings().TickIntervalMS()) * 0.001f * a_live.speed;
					});
				},
				[&](const Undo& i) { manager->UndoRecipe(i.recipe); },
				[&](const Redo& i) { manager->RedoRecipe(i.recipe); },
				[&](const CreateRecipe& i) { manager->NewRecipe(i.id, i.key); },
				[&](const BeginPaint& i) { manager->BeginPaint(i.recipe, i.key, i.surface); },
				[&](const SetPaintSurface& i) { manager->SetPaintSurface(i.surface); },
				[&](const KeepPaint& i) { manager->KeepPaint(i.recipe, i.name); },
				[&](const EndPaint&) { manager->EndPaint(); },
				[&](const ReadMesh& i) { manager->RequestMesh(i.actorID, i.geometry); },
				[&](const FireTrigger& i) { manager->FireAt(i.actorID, i.event, i.node, i.offset, i.random, i.value); },
				[](const auto&) {});
		}

		// Each intent reaches the manager, then the state; the layer
		// selection is clamped against the snapshot the frame was drawn from.
		void Dispatch(Intents& a_intents, MenuState& a_state, const Snapshot& a_snapshot)
		{
			for (const auto& intent : a_intents) {
				Perform(intent, a_snapshot.view);
				Reduce(a_state, intent);
			}
			ClampSelection(a_state.selection, a_snapshot);
			a_intents.clear();
		}

		// A recipe edit as an intent, for the widgets that make them.
		void Post(Intents& a_out, const std::string& a_recipe, RecipeEdit a_edit)
		{
			a_out.push_back(EditRecipe{ a_recipe, std::move(a_edit) });
		}

		// Defined with the region editor; the Masks tab's edit button uses it.
		void EditMaskAsRegion(const RecipeRow& a_recipe, const TextRow& a_mask, Intents& a_out);

		// ---------------------------------------------------------- forms

		// The input for a field, by its kind: a reference is a combo over the
		// names; a choice a combo over plain names; a toggle a checkbox; an
		// expression, mask, channel set or text is a badge and a text field;
		// a value (scalar, colour, vector, curve) is a value field whose badge
		// switches between text and a signal combo.
		[[nodiscard]] std::optional<std::string> FieldInput(const FieldSpec& a_field, float a_scale, const Names& a_names)
		{
			const Widgets::TextCheck check = [&](const std::string& a_text) { return CheckField(a_field, a_text, a_names); };
			const auto*               row = RowOf(kFieldKinds, a_field.kind);
			const FieldInputKind      input = row ? row->input : FieldInputKind::kText;
			switch (input) {
			case FieldInputKind::kCombo:
				Widgets::Badge(a_field.kind);
				return Widgets::ReferenceCombo("value", a_field.text, a_field.names, a_field.allowEmpty, Width::Fill(), a_scale, a_field.creators);
			case FieldInputKind::kChoice:
				Widgets::Badge(a_field.kind);
				return Widgets::ChoiceCombo("value", a_field.text, a_field.names, Width::Fill(), a_scale);
			case FieldInputKind::kToggle: {
				Widgets::Badge(a_field.kind);
				bool on = a_field.text == "on";
				if (Widgets::Toggle("##value", on, "")) {
					return std::string{ on ? "on" : "off" };
				}
				return std::nullopt;
			}
			case FieldInputKind::kText:
				Widgets::Badge(a_field.kind);
				return Widgets::TextField("value", a_field.text, Width::Fill(), a_scale, check);
			case FieldInputKind::kValue:
				return Widgets::ValueField("value", a_field.kind, a_field.text, a_field.names, a_field.allowEmpty, a_scale, check, a_field.creators);
			}
			return std::nullopt;
		}

		// A combo's creator entry makes its row and binds the field, as the
		// edits the field's creator returns; any other text is the field's.
		void PostField(const FieldSpec& a_field, const std::string& a_text, const std::string& a_recipe, Intents& a_out)
		{
			if (std::ranges::find(a_field.creators, a_text) != a_field.creators.end()) {
				if (a_field.create) {
					for (auto& edit : a_field.create(a_text)) {
						Post(a_out, a_recipe, std::move(edit));
					}
				}
				return;
			}
			const std::optional<RecipeEdit> edit = a_field.bind ? a_field.bind(a_text) : std::nullopt;
			if (edit) {
				Post(a_out, a_recipe, *edit);
			} else {
				Refuse(a_field.name, a_text);
			}
		}

		// The field table of a form's fields: the field's name, its detail
		// button where it has details, and the input filling the rest, each
		// row in its own ID scope. A committed text becomes the field's edit,
		// or a log line when it does not parse. Returns the index into
		// a_fields of the field whose detail button was clicked.
		[[nodiscard]] std::optional<std::size_t> DrawFieldTable(const char* a_id, std::span<const FieldSpec> a_fields, const std::string& a_recipe, float a_scale, const Names& a_names, Intents& a_out)
		{
			std::optional<std::size_t> open;
			auto                       table = Widgets::Table::Begin(a_id, { { "field", Width::Fit() }, { "", Width::Px(ImGui::GetFrameHeight()) }, { "value", Width::Fill() } }, kFormStyle);
			if (!table.Open()) {
				return open;
			}
			for (std::size_t i = 0; i < a_fields.size(); ++i) {
				const auto& field = a_fields[i];
				ImGui::PushID(field.name.c_str());
				table.Cell();
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(field.name.c_str());
				table.Cell();
				if (field.detail && Widgets::DetailButton()) {
					open = i;
				}
				table.Cell();
				if (field.value) {
					Widgets::ValueSwatch(*field.value);
					ImGui::SameLine();
				}
				if (const auto text = FieldInput(field, a_scale, a_names)) {
					PostField(field, *text, a_recipe, a_out);
				}
				ImGui::PopID();
			}
			table.End();
			return open;
		}

		// A form as the field table, or as a_columns field tables side by
		// side, the fields dealt out in order so a form the pane cannot show
		// whole fits without scrolling. Returns the index of the field whose
		// detail button was clicked.
		[[nodiscard]] std::optional<std::size_t> DrawForm(const char* a_id, std::span<const FieldSpec> a_form, const std::string& a_recipe, float a_scale, const Names& a_names, Intents& a_out, std::size_t a_columns = 1)
		{
			if (a_columns <= 1) {
				return DrawFieldTable(a_id, a_form, a_recipe, a_scale, a_names, a_out);
			}
			std::optional<std::size_t> open;
			const std::size_t          perColumn = (a_form.size() + a_columns - 1) / a_columns;
			std::vector<Widgets::Column> columns(a_columns, Widgets::Column{ "", Width::Fill() });
			auto                       outer = Widgets::Table::Begin(a_id, columns, kColumnsStyle);
			if (!outer.Open()) {
				return open;
			}
			for (std::size_t c = 0; c < a_columns; ++c) {
				outer.Cell();
				const std::size_t first = (std::min)(c * perColumn, a_form.size());
				const std::size_t count = (std::min)(perColumn, a_form.size() - first);
				ImGui::PushID(static_cast<int>(c));
				if (const auto clicked = DrawFieldTable("column", a_form.subspan(first, count), a_recipe, a_scale, a_names, a_out)) {
					open = first + *clicked;
				}
				ImGui::PopID();
			}
			outer.End();
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

		void IsolateCheckbox(const RecipeRow& a_recipe, const View& a_view, const char* a_label, Intents& a_out)
		{
			const auto& view = a_view;
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
			if (Widgets::ThumbnailButton("cell", a_cell.composite, ShaderChannel::kRgb, a_cell.animated, a_layout.cellSize * a_layout.widgetScale)) {
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
				if (Widgets::SoloButton(solo)) {
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
				if (Widgets::SoloButton(solo)) {
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
			const auto id = Widgets::LiveTextField("id", "recipe id (its file name)", Width::Px(240.0f), 1.0f);
			// The key, from what the piece carries; the combo's pick lives with the popup.
			static std::size_t chosen = 0;
			if (chosen >= a_piece.keys.size()) {
				chosen = 0;
			}
			const auto label = [](const KeyChoice& a_key) { return std::format("{}: {}", KeyKindName(a_key.kind), a_key.text); };
			Widgets::NextItemWidth(Width::Px(240.0f));
			if (ImGui::BeginCombo("##key", a_piece.keys.empty() ? "no key" : label(a_piece.keys[chosen]).c_str())) {
				for (std::size_t i = 0; i < a_piece.keys.size(); ++i) {
					if (ImGui::Selectable(label(a_piece.keys[i]).c_str(), i == chosen)) {
						chosen = i;
					}
				}
				ImGui::EndCombo();
			}
			const bool ready = !id.empty() && !a_piece.keys.empty();
			if (!ready) {
				ImGui::BeginDisabled();
			}
			if (ImGui::Button("Create") && ready) {
				const auto& key = a_piece.keys[chosen];
				RecipeKey   recipeKey;
				recipeKey.kind = key.kind;
				recipeKey.form.text = key.text;
				recipeKey.form.key = key.key;
				a_out.push_back(CreateRecipe{ std::string{ id }, std::move(recipeKey) });
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
			IsolateCheckbox(a_recipe, a_snapshot.view, "##isolate", a_out);
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
		void DrawEditContext(const Board& a_board, const RecipeRow& a_recipe, const Cell* a_picked, const Selection& a_selection, Intents& a_out)
		{
			const bool light = a_selection.target == Target::kLight;
			auto       table = Widgets::Table::Begin("context", { { "S", Width::Px(Widgets::RowButtonWidth()) }, { "target", Width::Fit() }, { "slot", Width::Fit() }, { "region", Width::Fit() }, { "", Width::Fill() }, { "", Width::Fit() } }, kContextStyle);
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
				if (Widgets::SoloButton(solo) && output) {
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

		// The pane's rule: its title, and at the right edge one button naming
		// what the pane would show instead (settings while the stack is
		// shown, the stack while the settings are), greyed when the target
		// lacks it; before it, while the settings are shown, Clear puts the
		// target's settings back to the format's defaults.
		[[nodiscard]] Widgets::RuleLine PaneRule(std::string_view a_title, const PaneChoice& a_pane, const Board& a_board, const RecipeRow& a_recipe, Target a_target, Intents& a_out)
		{
			const float switchWidth = (std::max)(Widgets::ButtonWidth("settings"), Widgets::ButtonWidth("stack"));
			const float clearWidth = Widgets::ButtonWidth("Clear");
			const float rightWidth = switchWidth + (a_pane.settings ? clearWidth + Widgets::ItemSpacingX() : 0.0f);
			return Widgets::RuleLine{ a_title, rightWidth, [=, &a_board, &a_recipe, &a_out]() {
									 if (a_pane.settings) {
										 const bool light = a_target == Target::kLight;
										 const bool present = !light || (a_board.light.present && a_board.light.output.has_value());
										 if (!present) {
											 ImGui::BeginDisabled();
										 }
										 if (ImGui::Button("Clear", ImVec2{ clearWidth, 0.0f }) && present) {
											 if (light) {
												 Post(a_out, a_recipe.id, ResetLight{ a_board.light.output.value_or(0) });
											 } else {
												 Post(a_out, a_recipe.id, ResetShell{});
											 }
										 }
										 if (!present) {
											 ImGui::EndDisabled();
										 }
										 ImGui::SameLine();
									 }
									 const bool enabled = a_pane.settings ? a_pane.hasStack : a_pane.hasSettings;
									 if (!enabled) {
										 ImGui::BeginDisabled();
									 }
									 if (ImGui::Button(a_pane.settings ? "stack" : "settings", ImVec2{ switchWidth, 0.0f }) && enabled) {
										 a_out.push_back(ShowSettings{ !a_pane.settings });
									 }
									 if (!enabled) {
										 ImGui::EndDisabled();
									 }
								 } };
		}

		// Both rows, then what the pick needs under them: Add output for an
		// empty slot, the light's cell for the light, the reason for a refused
		// one. Returns the picked cell.
		const Cell* DrawContext(const Snapshot& a_snapshot, const Board& a_board, const PieceRow& a_piece, const RecipeRow& a_recipe, const Selection& a_selection, Intents& a_out)
		{
			const Cell* picked = PickedCell(a_board, a_selection);
			DrawRecipeContext(a_snapshot, a_piece, a_recipe, a_out);
			Widgets::Rule();
			DrawEditContext(a_board, a_recipe, picked, a_selection, a_out);

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

		void DrawInspectorFields(const Inspector& a_inspector, const RecipeRow& a_recipe, const Layout& a_layout, const Names& a_names, Intents& a_out);
		void DrawFormWithSignals(const char* a_id, std::span<const FieldSpec> a_form, const RecipeRow& a_recipe, FormID a_actorID, float a_scale, const Names& a_names, Intents& a_out, std::size_t a_columns = 1);

		FormID g_modalActor = 0;  // the piece's actor, for a trigger fired from a modal; the body sets it

		void DrawScalars(const StackView& a_stack, const RecipeRow& a_recipe, float a_scale, const Names& a_names, Intents& a_out)
		{
			if (a_stack.scalars.empty()) {
				return;
			}
			DrawFormWithSignals("scalars", ScalarForm(a_stack), a_recipe, g_modalActor, a_scale, a_names, a_out);
		}

		[[nodiscard]] Widgets::Table BeginLayerTable()
		{
			const Width button = Width::Px(Widgets::RowButtonWidth());
			return Widgets::Table::Begin("layers", { { "#", Width::Fit() }, { "", button }, { "", button }, { "S", button }, { "M", button }, { "type", button }, { "blend", Width::Fit() }, { "source", Width::Fill() } }, kLayerStyle);
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
			if (Widgets::RemoveButton(0)) {
				Post(a_out, id, RemoveLayer{ output, index });
			}
			a_table.Cell();
			if (Widgets::DragHandle(kLayerPayload, index, "layer")) {
				a_out.push_back(PickLayer{ index });
			}
			if (const auto move = Widgets::DropTarget(kLayerPayload, index)) {
				Post(a_out, id, MoveLayer{ output, move->from, move->to });
			}
			a_table.Cell();
			bool solo = a_row.soloed;
			if (Widgets::SoloButton(solo)) {
				a_out.push_back(SoloLayer{ id, output, index, solo });
			}
			a_table.Cell();
			bool mute = a_row.muted;
			if (Widgets::MuteButton(mute)) {
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
		void DrawInspector(const StackView& a_stack, const std::optional<Inspector>& a_inspector, const RecipeRow& a_recipe, const Layout& a_layout, const Names& a_names, Intents& a_out)
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
			DrawInspectorFields(*a_inspector, a_recipe, a_layout, a_names, a_out);
			if (row != a_stack.rows.end() && row->layer.texture) {
				Widgets::Thumbnail(row->layer.texture, ShaderChannel::kRgb, false, a_layout.inspectorThumbnail * a_layout.widgetScale);
			}
			ImGui::PopID();
		}

		// The composite as rendered on the shape viewed; on a piece with
		// several shapes, clicking it views the next one.
		void DrawComposite(const StackView& a_stack, const PieceRow& a_piece, const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Layout& a_layout, Intents& a_out)
		{
			if (a_recipe.geometries.size() < 2) {
				Widgets::Thumbnail(a_stack.composite, ShaderChannel::kRgb, a_stack.animated, a_layout.compositeSize);
				return;
			}
			if (Widgets::ThumbnailButton("composite", a_stack.composite, ShaderChannel::kRgb, a_stack.animated, a_layout.compositeSize)) {
				if (const auto next = NextGeometry(a_recipe, a_geometry)) {
					a_out.push_back(*next);
				}
			}
			Widgets::Tooltip(std::format("viewed on {} (one of {} shapes; the recipe applies to all)\nclick: view the next shape\nraw name: {}", GeometryLabel(a_geometry.name, a_piece.armorName), a_recipe.geometries.size(), a_geometry.name));
		}

		void DrawStack(const std::optional<StackView>& a_stack, const std::optional<Inspector>& a_inspector, const PieceRow& a_piece, const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Selection& a_selection, Layout& a_layout, const Names& a_names, Intents& a_out)
		{
			if (!a_stack) {
				Widgets::Dim("choose a target and a slot");
				return;
			}
			const auto& stack = *a_stack;
			// The edit row above names the slot and carries its solo toggle.
			ImGui::PushID(static_cast<int>(stack.output));
			if (!stack.problem.empty()) {
				Widgets::Problem(stack.problem);
			}
			DrawComposite(stack, a_piece, a_recipe, a_geometry, a_layout, a_out);
			ImGui::SameLine();
			ImGui::BeginGroup();
			Widgets::Dim(std::format("composite {} px, {}", stack.size, stack.animated ? "animated" : "static"));
			DrawScalars(stack, a_recipe, a_layout.widgetScale, a_names, a_out);
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
				[&]() { DrawInspector(stack, a_inspector, a_recipe, a_layout, a_names, a_out); });
			ImGui::PopID();
		}

		// -------------------------------------------------------- signals

		// The value control for a signal a designer tunes: the same input the
		// inspector's fields use, from the signal's form. A trigger with an
		// event id gets Fire, which posts one firing through the manager's
		// queue. A row with no form otherwise reads as its kind. Drawn in
		// the signal's ID scope.
		// Fire, with its payload in a popup: the node (one of the shape's bones,
		// or none), an offset from it, a random scatter, and the value.
		void FirePopup(const SignalRow& a_signal, FormID a_actorID, std::span<const BoneRow> a_bones, Intents& a_out)
		{
			if (ImGui::SmallButton("Fire")) {
				ImGui::OpenPopup("fire");
			}
			if (!ImGui::BeginPopup("fire")) {
				return;
			}
			static std::string node;
			static float       offset[3]{};
			static float       random = 0.0f;
			static float       value = 1.0f;
			Widgets::NextItemWidth(Width::Px(220.0f));
			if (ImGui::BeginCombo("node", node.empty() ? "(none)" : node.c_str())) {
				if (ImGui::Selectable("(none)", node.empty())) {
					node.clear();
				}
				for (const auto& bone : a_bones) {
					if (ImGui::Selectable(bone.name.c_str(), bone.name == node)) {
						node = bone.name;
					}
				}
				ImGui::EndCombo();
			}
			Widgets::NextItemWidth(Width::Px(220.0f));
			ImGui::DragFloat3("offset", offset, 1.0f);
			Widgets::NextItemWidth(Width::Px(220.0f));
			ImGui::DragFloat("random", &random, 1.0f, 0.0f, 200.0f);
			Widgets::NextItemWidth(Width::Px(220.0f));
			ImGui::DragFloat("value", &value, 0.01f);
			if (ImGui::Button("Fire now")) {
				a_out.push_back(FireTrigger{ a_actorID, a_signal.event, node, Vec3{ offset[0], offset[1], offset[2] }, random, value });
			}
			ImGui::EndPopup();
		}

		std::span<const BoneRow> g_modalBones;  // the viewed shape's bones, for a firing's node; the body sets it

		void DrawSignalEditor(const std::string& a_id, const SignalRow& a_signal, FormID a_actorID, float a_scale, const Names& a_names, Intents& a_out)
		{
			const auto field = SignalForm(a_signal);
			if (!field) {
				if (!a_signal.event.empty()) {
					FirePopup(a_signal, a_actorID, g_modalBones, a_out);
					ImGui::SameLine();
				}
				Widgets::Dim(std::format("{}: edits in the file", SignalKindName(a_signal.kind)));
				return;
			}
			// The value's kind follows the text, so the check is the union of the
			// kinds rather than the field's current one.
			const Widgets::TextCheck check = [&](const std::string& a_text) { return CheckSignalValue(a_text, a_names); };
			if (const auto text = Widgets::ValueField("value", field->kind, field->text, {}, false, a_scale, check)) {
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
		void DrawSignalRow(Widgets::Table& a_table, const std::string& a_id, const SignalRow& a_signal, bool a_tunable, std::span<const std::string> a_curves, float a_curveWidth, FormID a_actorID, float a_scale, const Names& a_names, Intents& a_out)
		{
			ImGui::PushID(a_signal.name.c_str());
			a_table.Cell();
			if (Widgets::RemoveButton(a_signal.references)) {
				Post(a_out, a_id, RemoveSignal{ a_signal.name });
			}
			a_table.Cell();
			if (const auto renamed = Widgets::TextField("name", a_signal.name, Width::Fill(), a_scale)) {
				Post(a_out, a_id, RenameSignal{ a_signal.name, *renamed });
			}
			Widgets::Tooltip(SignalKindName(a_signal.kind));
			a_table.Cell();
			DrawSignalEditor(a_id, a_signal, a_actorID, a_scale, a_names, a_out);
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
		void DrawSignals(const PieceRow& a_piece, const RecipeRow& a_recipe, const Layout& a_layout, std::string_view a_filter, const Names& a_names, Intents& a_out)
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
					DrawSignalRow(signals, id, signal, true, curveNames, curveWidth, a_piece.actorID, scale, a_names, a_out);
				}
			}
			for (const auto& signal : list.developer) {
				if (NameMatches(signal.name, a_filter)) {
					DrawSignalRow(signals, id, signal, false, curveNames, curveWidth, a_piece.actorID, scale, a_names, a_out);
				}
			}
			signals.End();
		}

		// The declared curves, in their own pane, those the name filter on
		// its rule passes: each an expression in x, its name a field.
		void DrawCurves(const RecipeRow& a_recipe, const Layout& a_layout, std::string_view a_filter, const Names& a_names, Intents& a_out)
		{
			const Widgets::TextCheck check = [&](const std::string& a_text) { return CheckCurveText(a_text, a_names); };
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
				if (Widgets::RemoveButton(curve.references)) {
					Post(a_out, id, RemoveCurve{ curve.name });
				}
				curves.Cell();
				if (const auto renamed = Widgets::TextField("name", curve.name, Width::Fill(), scale)) {
					Post(a_out, id, RenameCurve{ curve.name, *renamed });
				}
				curves.Cell();
				Widgets::Badge(FieldKind::kCurve);
				if (const auto edited = Widgets::TextField("text", curve.text, Width::Fill(), scale, check)) {
					Post(a_out, id, SetCurve{ curve.name, *edited });
				}
				ImGui::PopID();
			}
			curves.End();
		}

		// The sources, in their own tab: remove, the name (a field), a detail
		// button opening the source's form in a modal, and the definition as
		// the file describes it.
		void DrawSources(const RecipeRow& a_recipe, const Layout& a_layout, std::string_view a_filter, const Names& a_names, Intents& a_out)
		{
			const auto& id = a_recipe.id;
			const float scale = a_layout.widgetScale;
			const auto  signalNames = SignalNamesOf(a_recipe);
			std::vector<std::string> names;
			for (const auto& source : a_recipe.sourceRows) {
				names.push_back(source.name);
			}
			const float nameWidth = Widgets::WidestOf(names) * scale;
			auto        sources = Widgets::Table::Begin("sources", { { "", Width::Fit() }, { "source", Width::Px(nameWidth) }, { "", Width::Px(ImGui::GetFrameHeight()) }, { "definition", Width::Fill() } }, kGridStyle);
			if (!sources.Open()) {
				return;
			}
			for (const auto& source : a_recipe.sourceRows) {
				if (!NameMatches(source.name, a_filter)) {
					continue;
				}
				ImGui::PushID(source.name.c_str());
				sources.Cell();
				if (Widgets::RemoveButton(source.references)) {
					Post(a_out, id, RemoveSource{ source.name });
				}
				sources.Cell();
				if (const auto renamed = Widgets::TextField("name", source.name, Width::Fill(), scale)) {
					Post(a_out, id, RenameSource{ source.name, *renamed });
				}
				sources.Cell();
				const auto title = std::format("source {}###source-detail", source.name);
				if (Widgets::DetailButton()) {
					ImGui::OpenPopup(title.c_str());
				}
				Widgets::DetailModal(title.c_str(), [&]() {
					[[maybe_unused]] const auto detail = DrawForm("form", SourceForm(source, signalNames), id, scale, a_names, a_out);
				});
				sources.Cell();
				ImGui::AlignTextToFramePadding();
				Widgets::Dim(DescribeSource(SourceKindOf(source).value_or(SourceKind{ MaterialSource{} })));
				ImGui::PopID();
			}
			sources.End();
		}

		// The masks, in their own tab: remove, the name (a field), the
		// expression per texel over sources, masks and signals.
		void DrawMasks(const RecipeRow& a_recipe, const Layout& a_layout, std::string_view a_filter, const Names& a_names, Intents& a_out)
		{
			const auto& id = a_recipe.id;
			const float scale = a_layout.widgetScale;
			const Widgets::TextCheck check = [&](const std::string& a_text) { return CheckMaskText(a_text, a_names); };
			std::vector<std::string> names;
			for (const auto& mask : a_recipe.maskRows) {
				names.push_back(mask.name);
			}
			const float nameWidth = Widgets::WidestOf(names) * scale;
			auto        masks = Widgets::Table::Begin("masks", { { "", Width::Fit() }, { "mask", Width::Px(nameWidth) }, { "expression", Width::Fill() } }, kGridStyle);
			if (!masks.Open()) {
				return;
			}
			for (const auto& mask : a_recipe.maskRows) {
				if (!NameMatches(mask.name, a_filter)) {
					continue;
				}
				ImGui::PushID(mask.name.c_str());
				masks.Cell();
				if (Widgets::RemoveButton(mask.references)) {
					Post(a_out, id, RemoveMask{ mask.name });
				}
				if (mask.name != kScratchMask) {
					ImGui::SameLine();
					if (ImGui::SmallButton("edit")) {
						EditMaskAsRegion(a_recipe, mask, a_out);
					}
				}
				masks.Cell();
				if (const auto renamed = Widgets::TextField("name", mask.name, Width::Fill(), scale)) {
					Post(a_out, id, RenameMask{ mask.name, *renamed });
				}
				masks.Cell();
				Widgets::Badge(FieldKind::kMask);
				if (const auto edited = Widgets::TextField("text", mask.text, Width::Fill(), scale, check)) {
					Post(a_out, id, SetMask{ mask.name, *edited });
				}
				ImGui::PopID();
			}
			masks.End();
		}

		// Add and the name filter for the open tab, at the right edge of the
		// resources rule, in the tab's own ID scope so each tab keeps its
		// filter and the two Add buttons never share an ID.
		[[nodiscard]] Widgets::RuleLine ResourcesRule(ResourceTab a_tab, const RecipeRow& a_recipe, float a_scale, std::string_view& a_filter, Intents& a_out)
		{
			const float addWidth = Widgets::ButtonWidth("Add");
			const float rightWidth = addWidth + Widgets::ItemSpacingX() + kFilterWidth * a_scale;
			return Widgets::RuleLine{ "Resources", rightWidth, [=, &a_recipe, &a_filter, &a_out]() {
									 ImGui::PushID(static_cast<int>(a_tab));
									 if (ImGui::Button("Add", ImVec2{ addWidth, 0.0f })) {
										 std::vector<std::string> names;
										 switch (a_tab) {
										 case ResourceTab::kSignals:
											 for (const auto& signal : a_recipe.signals) {
												 names.push_back(signal.name);
											 }
											 Post(a_out, a_recipe.id, AddSignal{ UniqueName("signal", names) });
											 break;
										 case ResourceTab::kCurves:
											 for (const auto& curve : a_recipe.curves) {
												 names.push_back(curve.name);
											 }
											 Post(a_out, a_recipe.id, AddCurve{ UniqueName("curve", names) });
											 break;
										 case ResourceTab::kSources:
											 for (const auto& source : a_recipe.sourceRows) {
												 names.push_back(source.name);
											 }
											 for (const auto& mask : a_recipe.masks) {
												 names.push_back(mask);
											 }
											 Post(a_out, a_recipe.id, AddSource{ UniqueName("source", names), MaterialSource{} });
											 break;
										 case ResourceTab::kMasks:
											 for (const auto& mask : a_recipe.masks) {
												 names.push_back(mask);
											 }
											 Post(a_out, a_recipe.id, AddMask{ UniqueName("mask", names) });
											 break;
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
		void DrawResources(const PieceRow& a_piece, const RecipeRow& a_recipe, const Layout& a_layout, ResourceTab a_open, std::string_view a_filter, const Names& a_names, Intents& a_out)
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
						DrawSignals(a_piece, a_recipe, a_layout, a_filter, a_names, a_out);
						break;
					case ResourceTab::kCurves:
						DrawCurves(a_recipe, a_layout, a_filter, a_names, a_out);
						break;
					case ResourceTab::kSources:
						DrawSources(a_recipe, a_layout, a_filter, a_names, a_out);
						break;
					case ResourceTab::kMasks:
						DrawMasks(a_recipe, a_layout, a_filter, a_names, a_out);
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
		void DrawImageRow(const std::string& a_id, const ImageRow& a_image, bool a_editable, const Layout& a_layout, const Names& a_names, Intents& a_out)
		{
			const float scale = a_layout.widgetScale;
			Widgets::Thumbnail(a_image.texture, a_image.channel, a_image.animated, a_layout.inspectorThumbnail * scale);
			ImGui::TextUnformatted((ReferenceText(a_image.name) + " =").c_str());
			ImGui::SameLine();
			if (a_editable) {
				Widgets::Badge(FieldKind::kMask);
				const Widgets::TextCheck check = [&](const std::string& a_text) { return CheckMaskText(a_text, a_names); };
				if (const auto edited = Widgets::TextField("text", a_image.kind, Width::Fill(), scale, check)) {
					Post(a_out, a_id, SetMask{ a_image.name, *edited });
				}
			} else {
				ImGui::TextWrapped("%s", a_image.kind.c_str());
			}
			if (!a_image.problem.empty()) {
				Widgets::Warn(a_image.problem);
			}
		}

		// The signal a parameter text names, drawn as its editor inside a
		// modal, with the signals its expression reads as buttons that open
		// their own modal, so a chain is followed to any depth (capped).
		constexpr int kMaxSignalModalDepth = 6;

		void DrawSignalModal(const RecipeRow& a_recipe, const std::string& a_name, FormID a_actorID, float a_scale, const Names& a_names, int a_depth, Intents& a_out);

		void DrawSignalDetail(const RecipeRow& a_recipe, const std::string& a_text, FormID a_actorID, float a_scale, const Names& a_names, int a_depth, Intents& a_out)
		{
			const auto name = ReferenceName(a_text);
			const auto it = std::ranges::find(a_recipe.signals, name, &SignalRow::name);
			if (a_text.empty() || !a_text.starts_with('@') || it == a_recipe.signals.end()) {
				Widgets::Dim("a literal; choose a @signal to tune it here");
				return;
			}
			ImGui::PushID(it->name.c_str());
			ImGui::Text("%s (%s)", ReferenceText(it->name).c_str(), it->kind.c_str());
			ImGui::SameLine();
			Widgets::ValueSwatch(it->value);
			DrawSignalEditor(a_recipe.id, *it, a_actorID, a_scale, a_names, a_out);
			if (it->inert) {
				Widgets::Problem(it->problem.empty() ? "inert" : "inert: " + it->problem);
			}
			// The signals this one reads, each a link into its own modal.
			if (!it->text.empty() && a_depth < kMaxSignalModalDepth) {
				if (const auto program = Program::Parse(it->text)) {
					bool any = false;
					for (const auto& read : program->References()) {
						if (std::ranges::find(a_recipe.signals, read, &SignalRow::name) == a_recipe.signals.end()) {
							continue;
						}
						if (!any) {
							Widgets::Dim("reads");
							any = true;
						}
						ImGui::SameLine();
						DrawSignalModal(a_recipe, read, a_actorID, a_scale, a_names, a_depth + 1, a_out);
					}
				}
			}
			ImGui::PopID();
		}

		// A button named for the signal that opens its modal, nested in the
		// current one.
		void DrawSignalModal(const RecipeRow& a_recipe, const std::string& a_name, FormID a_actorID, float a_scale, const Names& a_names, int a_depth, Intents& a_out)
		{
			ImGui::PushID(a_name.c_str());
			const auto title = std::format("{}###signal-modal-{}", ReferenceText(a_name), a_depth);
			if (ImGui::SmallButton(ReferenceText(a_name).c_str())) {
				ImGui::OpenPopup(title.c_str());
			}
			Widgets::DetailModal(title.c_str(), [&]() { DrawSignalDetail(a_recipe, ReferenceText(a_name), a_actorID, a_scale, a_names, a_depth, a_out); });
			ImGui::PopID();
		}

		// A form whose value fields may open their signal: the detail button
		// of a field naming a @signal opens that signal's modal.
		void DrawFormWithSignals(const char* a_id, std::span<const FieldSpec> a_form, const RecipeRow& a_recipe, FormID a_actorID, float a_scale, const Names& a_names, Intents& a_out, std::size_t a_columns)
		{
			const auto open = DrawForm(a_id, a_form, a_recipe.id, a_scale, a_names, a_out, a_columns);
			for (std::size_t i = 0; i < a_form.size(); ++i) {
				if (a_form[i].detail != FieldDetail::kSignal) {
					continue;
				}
				ImGui::PushID(static_cast<int>(i));
				const auto title = std::format("{}###signal-modal-0", a_form[i].text);
				if (open == i) {
					ImGui::OpenPopup(title.c_str());
				}
				Widgets::DetailModal(title.c_str(), [&]() { DrawSignalDetail(a_recipe, a_form[i].text, a_actorID, a_scale, a_names, 0, a_out); });
				ImGui::PopID();
			}
		}

		void DrawDetailModal(FieldDetail a_detail, const Inspector& a_in, const RecipeRow& a_recipe, FormID a_actorID, const Layout& a_layout, const Names& a_names, Intents& a_out)
		{
			const auto& id = a_recipe.id;
			const float scale = a_layout.widgetScale;
			switch (a_detail) {
			case FieldDetail::kSource:
				if (a_in.source) {
					const bool isMask = std::ranges::find(a_in.masks, a_in.source->name) != a_in.masks.end();
					DrawImageRow(id, *a_in.source, isMask, a_layout, a_names, a_out);
				} else {
					Widgets::Dim("a constant colour, or a name no source or mask has");
				}
				break;
			case FieldDetail::kCurve:
				if (a_in.curve) {
					ImGui::TextUnformatted((ReferenceText(a_in.curve->name) + " =").c_str());
					ImGui::SameLine();
					Widgets::Badge(FieldKind::kCurve);
					const Widgets::TextCheck check = [&](const std::string& a_text) { return CheckCurveText(a_text, a_names); };
					if (const auto edited = Widgets::TextField("text", a_in.curve->text, Width::Fill(), scale, check)) {
						Post(a_out, id, SetCurve{ a_in.curve->name, *edited });
					}
				} else {
					Widgets::Dim("no declared curve; the layer's curve is inline or empty");
				}
				break;
			case FieldDetail::kOpacity:
				DrawSignalDetail(a_recipe, a_in.row.opacityText, a_actorID, scale, a_names, 0, a_out);
				break;
			case FieldDetail::kColor:
				DrawSignalDetail(a_recipe, a_in.row.color, a_actorID, scale, a_names, 0, a_out);
				break;
			case FieldDetail::kSignal:
				break;
			case FieldDetail::kMask:
				if (a_in.mask) {
					DrawImageRow(id, *a_in.mask, true, a_layout, a_names, a_out);
				} else {
					Widgets::Dim("no mask");
				}
				break;
			}
		}

		// The detail modals live outside the table so their ids match the
		// buttons' scope; one per field, opened by its button.
		void DrawInspectorFields(const Inspector& a_inspector, const RecipeRow& a_recipe, const Layout& a_layout, const Names& a_names, Intents& a_out)
		{
			const auto                       form = InspectorForm(a_inspector);
			const std::optional<std::size_t> opened = DrawForm("fields", form, a_recipe.id, a_layout.widgetScale, a_names, a_out);
			const std::optional<FieldDetail> open = opened && *opened < form.size() ? form[*opened].detail : std::nullopt;
			for (const auto detail : { FieldDetail::kSource, FieldDetail::kCurve, FieldDetail::kOpacity, FieldDetail::kColor, FieldDetail::kMask }) {
				const auto title = std::format("{} of layer {}###detail{}", FieldDetailName(detail), a_inspector.layer, static_cast<int>(detail));
				if (open == detail) {
					ImGui::OpenPopup(title.c_str());
				}
				Widgets::DetailModal(title.c_str(), [&]() { DrawDetailModal(detail, a_inspector, a_recipe, g_modalActor, a_layout, a_names, a_out); });
			}
		}

		// --------------------------------------------------------- region
		// Paint mode: the region stack, terms with boolean ops, whose built
		// expression the paint recipe's scratch mask holds. The paint recipe
		// (a clone of the active one with a single masked emissive output)
		// is applied alone while Paint is open, so the armor shows the
		// region through the ordinary render path. Keep copies the region
		// into the active recipe; Discard drops the paint recipe.

		// The key a paint recipe takes from the piece: its armor, else the
		// first key it offers.
		[[nodiscard]] std::optional<RecipeKey> PaintKeyOf(const PieceRow& a_piece)
		{
			const KeyChoice* chosen = nullptr;
			for (const auto& key : a_piece.keys) {
				if (key.kind == KeyKind::kArmor) {
					chosen = &key;
					break;
				}
			}
			if (!chosen && !a_piece.keys.empty()) {
				chosen = &a_piece.keys.front();
			}
			if (!chosen) {
				return std::nullopt;
			}
			RecipeKey key;
			key.kind = chosen->kind;
			key.form.text = chosen->text;
			key.form.key = chosen->key;
			return key;
		}

		constexpr const char* kTermPayload = "WEPBR_TERM";

		constexpr TableStyle kChooserStyle{ .borders = TableStyle::Borders::kNone, .stretch = true, .headers = false, .rowBackground = false };
		// The groups whose sections open by default: what the analysis found.
		constexpr std::array<std::string_view, 2> kOpenOfferGroups{ "parts", "materials" };

		// A term recipe becomes the source edits it needs on the paint recipe
		// and one term of the stack, labelled by its recipe.
		void AddRecipeTerm(const TermRecipe& a_term, const RecipeRow& a_recipe, const GeometryRow& a_geometry, Intents& a_out)
		{
			const auto& presets = LoadedPresets();
			auto [edits, expression] = BuildTerm(a_term, presets, ExistingOf(a_recipe));
			for (auto& edit : edits) {
				Post(a_out, a_recipe.id, std::move(edit));
			}
			a_out.push_back(AddTerm{ Term{ TermOp::kAnd, std::move(expression), TermLabelOf(a_term, presets, a_geometry), a_term } });
		}

		// The offers the filter passes, of one group, as chooser rows;
		// choosing one adds its term. Nothing when the group has none.
		void DrawOfferGroup(std::string_view a_group, std::span<const TermOffer> a_offers, std::string_view a_filter, const RecipeRow& a_recipe, const GeometryRow& a_geometry, Intents& a_out)
		{
			std::vector<const TermOffer*> shown;
			for (const auto& offer : a_offers) {
				if (offer.group == a_group && (NameMatches(offer.name, a_filter) || NameMatches(offer.detail, a_filter))) {
					shown.push_back(&offer);
				}
			}
			if (shown.empty()) {
				return;
			}
			const std::string title = std::format("{} ({})", a_group, shown.size());
			const bool        openByDefault = std::ranges::find(kOpenOfferGroups, a_group) != kOpenOfferGroups.end();
			ImGui::PushID(title.c_str());
			if (Widgets::Section(title.c_str(), openByDefault)) {
				auto table = Widgets::Table::Begin("offers", { { "name", Width::Fit() }, { "detail", Width::Fill() }, { "%", Width::Fit("100%") } }, kChooserStyle);
				if (table.Open()) {
					for (std::size_t i = 0; i < shown.size(); ++i) {
						const TermOffer& offer = *shown[i];
						ImGui::PushID(static_cast<int>(i));
						if (Widgets::ChooserRow(table, offer.name, offer.detail, offer.coverage, offer.unavailable)) {
							AddRecipeTerm(offer.recipe, a_recipe, a_geometry, a_out);
						}
						ImGui::PopID();
					}
					table.End();
				}
			}
			ImGui::PopID();
		}

		[[nodiscard]] Widgets::Table BeginTermTable()
		{
			const Width button = Width::Px(Widgets::RowButtonWidth());
			return Widgets::Table::Begin("terms", { { "#", Width::Fit() }, { "", button }, { "", button }, { "S", button }, { "M", button }, { "op", Width::Fit("and") }, { "term", Width::Fit() }, { "detail", Width::Fill() }, { "", button } }, kLayerStyle);
		}

		// The ops a term past the first may take, as the op combo lists them.
		const std::vector<std::string> kTermOps{ std::string{ TermOpName(TermOp::kAnd) }, std::string{ TermOpName(TermOp::kOr) }, std::string{ TermOpName(TermOp::kNot) } };

		void DrawTermDetails(std::size_t a_index, const Term& a_term, const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Layout& a_layout, const Names& a_names, Intents& a_out);

		void DrawTermRow(Widgets::Table& a_table, const RegionStack& a_region, std::size_t a_index, std::span<const TermOffer> a_offers, const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Layout& a_layout, const Names& a_names, Intents& a_out)
		{
			const auto& term = a_region.terms[a_index];
			ImGui::PushID(static_cast<int>(a_index));
			a_table.Cell();
			ImGui::AlignTextToFramePadding();
			ImGui::Text("%zu", a_index);
			a_table.Cell();
			if (Widgets::RemoveButton(0)) {
				a_out.push_back(RemoveTerm{ a_index });
			}
			a_table.Cell();
			if (Widgets::DragHandle(kTermPayload, a_index, "term")) {
				a_out.push_back(PickTerm{ a_index });
			}
			if (const auto move = Widgets::DropTarget(kTermPayload, a_index)) {
				a_out.push_back(MoveTerm{ move->from, move->to });
			}
			a_table.Cell();
			bool solo = a_region.solo == a_index;
			if (Widgets::SoloButton(solo)) {
				a_out.push_back(SoloTerm{ a_index, solo });
			}
			a_table.Cell();
			bool mute = a_region.muted.contains(a_index);
			if (Widgets::MuteButton(mute)) {
				a_out.push_back(MuteTerm{ a_index, mute });
			}
			a_table.Cell();
			if (a_index == 0) {
				ImGui::AlignTextToFramePadding();
				Widgets::Dim("set");
			} else if (const auto chosen = Widgets::ChoiceCombo("op", std::string{ TermOpName(term.op) }, kTermOps, Width::Fit("and"), 1.0f)) {
				if (const auto op = ParseTermOp(*chosen)) {
					a_out.push_back(SetTermOp{ a_index, *op });
				}
			}
			a_table.Cell();
			const bool raw = std::holds_alternative<RawTerm>(term.recipe);
			const auto label = raw && term.text.empty() ? std::string{ "(empty)" } : term.label;
			if (ImGui::Selectable(label.c_str(), a_region.selected == a_index)) {
				a_out.push_back(PickTerm{ a_index });
			}
			a_table.Cell();
			ImGui::AlignTextToFramePadding();
			Widgets::Dim(TermDetailOf(term, a_offers));
			a_table.Cell();
			// Everything the row does not show (its settings, its expression,
			// what it reads) sits behind the details button.
			const auto title = std::format("term {}: {}###term-details", a_index, term.label);
			if (Widgets::DetailButton()) {
				ImGui::OpenPopup(title.c_str());
			}
			Widgets::DetailModal(title.c_str(), [&]() { DrawTermDetails(a_index, term, a_recipe, a_geometry, a_layout, a_names, a_out); });
			ImGui::PopID();
		}

		// The selected term's settings as a field table, one row per field
		// of its form: a committed text the field accepts is the term's new
		// recipe, rebuilt into its sources and text before it posts; a text
		// the field refuses is a log line. A raw term has no settings.
		void DrawTermSettings(std::size_t a_index, const Term& a_term, const RecipeRow& a_recipe, const GeometryRow& a_geometry, float a_scale, const Names& a_names, Intents& a_out)
		{
			const auto& presets = LoadedPresets();
			const auto  form = TermForm(a_term.recipe, presets, a_geometry);
			if (form.empty()) {
				return;
			}
			auto table = Widgets::Table::Begin("term-settings", { { "setting", Width::Fit() }, { "value", Width::Fill() } }, kFormStyle);
			if (!table.Open()) {
				return;
			}
			for (const auto& setting : form) {
				ImGui::PushID(setting.field.name.c_str());
				table.Cell();
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(setting.field.name.c_str());
				table.Cell();
				if (const auto text = FieldInput(setting.field, a_scale, a_names)) {
					const std::optional<TermRecipe> changed = setting.apply ? setting.apply(*text) : std::nullopt;
					if (changed) {
						auto [edits, expression] = BuildTerm(*changed, presets, ExistingOf(a_recipe));
						for (auto& edit : edits) {
							Post(a_out, a_recipe.id, std::move(edit));
						}
						a_out.push_back(SetTermRecipe{ a_index, *changed, std::move(expression), TermLabelOf(*changed, presets, a_geometry) });
					} else {
						Refuse(setting.field.name, *text);
					}
				}
				ImGui::PopID();
			}
			table.End();
		}

		// A term's details, in its modal: its settings, its op, its text
		// (typing there makes it raw), and the sources and masks it reads,
		// each with a detail button that opens the picture in a modal of its own.
		void DrawTermDetails(std::size_t a_index, const Term& a_term, const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Layout& a_layout, const Names& a_names, Intents& a_out)
		{
			const std::size_t index = a_index;
			const auto&       term = a_term;
			const float       scale = a_layout.widgetScale;
			ImGui::PushID("term-details");
			DrawTermSettings(index, term, a_recipe, a_geometry, scale, a_names, a_out);
			auto fields = Widgets::Table::Begin("term-fields", { { "field", Width::Fit() }, { "value", Width::Fill() } }, kFormStyle);
			if (fields.Open()) {
				fields.Cell();
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted("op");
				fields.Cell();
				if (index == 0) {
					ImGui::AlignTextToFramePadding();
					Widgets::Dim("set (the first term leads)");
				} else if (const auto chosen = Widgets::ChoiceCombo("op", std::string{ TermOpName(term.op) }, kTermOps, Width::Fit("and"), scale)) {
					if (const auto op = ParseTermOp(*chosen)) {
						a_out.push_back(SetTermOp{ index, *op });
					}
				}
				fields.Cell();
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted("expression");
				fields.Cell();
				Widgets::Badge(FieldKind::kMask);
				const Widgets::TextCheck check = [&](const std::string& a_text) { return CheckMaskText(a_text, a_names); };
				if (const auto edited = Widgets::TextField("text", term.text, Width::Fill(), scale, check)) {
					a_out.push_back(SetTermText{ index, *edited });
				}
				fields.End();
			}

			// What the term reads, one row each; the picture behind the button.
			const auto program = term.text.empty() ? std::nullopt : std::optional{ Program::Parse(term.text) };
			if (program && *program && !(*program)->References().empty()) {
				auto reads = Widgets::Table::Begin("term-reads", { { "reads", Width::Fit() }, { "", Width::Px(ImGui::GetFrameHeight()) }, { "definition", Width::Fill() } }, kFormStyle);
				if (reads.Open()) {
					for (const auto& name : (*program)->References()) {
						const ImageRow* image = nullptr;
						for (const auto* list : { &a_geometry.sources, &a_geometry.masks }) {
							const auto it = std::ranges::find(*list, name, &ImageRow::name);
							if (it != list->end()) {
								image = &*it;
							}
						}
						ImGui::PushID(name.c_str());
						reads.Cell();
						ImGui::AlignTextToFramePadding();
						ImGui::TextUnformatted(ReferenceText(name).c_str());
						reads.Cell();
						const auto title = std::format("{}###term-read", ReferenceText(name));
						if (image && Widgets::DetailButton()) {
							ImGui::OpenPopup(title.c_str());
						}
						reads.Cell();
						ImGui::AlignTextToFramePadding();
						if (image) {
							ImGui::TextUnformatted(image->kind.c_str());
						} else {
							Widgets::Warn("not a source or mask of the recipe");
						}
						Widgets::DetailModal(title.c_str(), [&]() {
							if (image) {
								DrawImageRow(a_recipe.id, *image, false, a_layout, a_names, a_out);
							}
						});
						ImGui::PopID();
					}
					reads.End();
				}
			}
			ImGui::PopID();
		}

		// Keep and Discard, at the right edge under the term table. Keep proposes a
		// name and hands the region to the manager, which copies it into the
		// active recipe and ends the session; Discard ends it.
		void DrawKeepDiscard(const MenuState& a_state, Intents& a_out)
		{
			const auto& region = a_state.region;
			const float keepWidth = Widgets::ButtonWidth("Keep");
			const float discardWidth = Widgets::ButtonWidth("Discard");
			const bool  painting = a_state.paint.has_value();
			const bool  something = painting && !BuildRegion(region.terms).empty();
			Widgets::RightAligned(keepWidth + discardWidth + Widgets::ItemSpacingX(), [&]() {
				Widgets::Disabled(!something, [&]() {
					if (ImGui::Button("Keep", ImVec2{ keepWidth, 0.0f })) {
						ImGui::OpenPopup("keep-region");
					}
				});
				if (ImGui::BeginPopup("keep-region")) {
					const auto        proposed = ProposedRegionName(region.terms, region.editing);
					const auto        typed = Widgets::LiveTextField("name", proposed.c_str(), Width::Px(200.0f), 1.0f);
					const std::string name = typed.empty() ? proposed : std::string{ typed };
					const bool        ready = painting && IsName(name) && name != kScratchMask;
					Widgets::Disabled(!ready, [&]() {
						if (ImGui::Button(std::format("Keep as {}", name).c_str()) && ready) {
							a_out.push_back(KeepPaint{ a_state.paint->recipe, name });
							ImGui::CloseCurrentPopup();
						}
					});
					ImGui::EndPopup();
				}
				ImGui::SameLine();
				Widgets::Disabled(!painting, [&]() {
					if (ImGui::Button("Discard", ImVec2{ discardWidth, 0.0f })) {
						a_out.push_back(EndPaint{});
					}
				});
			});
		}

		// The pane: the term table across the width, Keep and Discard, then
		// what the piece offers as tables in collapsible sections under a rule
		// that carries the filter.
		void DrawRegionStack(const RecipeRow& a_recipe, const GeometryRow& a_geometry, MenuState& a_state, const Names& a_names, Intents& a_out)
		{
			const auto& region = a_state.region;
			const auto  offers = OffersOf(LoadedPresets(), a_recipe, a_geometry, region.editing);
			auto        table = BeginTermTable();
			if (table.Open()) {
				for (std::size_t i = 0; i < region.terms.size(); ++i) {
					DrawTermRow(table, region, i, offers, a_recipe, a_geometry, a_state.layout, a_names, a_out);
				}
				table.End();
			}
			if (region.terms.empty()) {
				Widgets::Dim("no selection yet: choose a term below");
			}
			Widgets::HelpMarker("A region is terms combined in order: the first sets it, each next one is and (product), or (max) or not (times the complement). Drag the :: grip to reorder; S shows one term alone, M leaves one out; ... opens a term's settings; Keep writes every term.");
			DrawKeepDiscard(a_state, a_out);

			std::string_view filter;
			const float      filterWidth = kFilterWidth * a_state.layout.widgetScale;
			Widgets::Rule({}, Widgets::RuleLine{ "Add a term", filterWidth, [&]() { filter = Widgets::LiveTextField("offer-filter", "filter by name or measurement", Width::Px(kFilterWidth), a_state.layout.widgetScale); } });
			if (offers.empty()) {
				Widgets::Dim(a_geometry.meshRead ? "nothing to offer on this shape" : "reading the mesh");
			}
			for (const auto group : kOfferGroups) {
				DrawOfferGroup(group, offers, filter, a_recipe, a_geometry, a_out);
			}
		}

		// Paint's head: the active recipe's name, held while the session runs
		// (the piece's applied recipes are the paint recipe alone then), over
		// a rule; the context rows are Compose's.
		void DrawPaintHead(const PieceRow& a_piece, const RecipeRow& a_recipe, const MenuState& a_state, Intents& a_out)
		{
			if (a_state.paint) {
				Widgets::HeldLabel(a_state.paint->recipe.c_str());
				// Where the paint recipe previews, at the right edge of the line.
				ImGui::SameLine();
				const float labelWidth = Widgets::TextWidth("preview on");
				const float comboWidth = Widgets::FitWidth("material");
				Widgets::RightAligned(labelWidth + Widgets::ItemSpacingX() + comboWidth, [&]() {
					ImGui::AlignTextToFramePadding();
					Widgets::Dim("preview on");
					ImGui::SameLine();
					if (const auto chosen = Widgets::ChoiceCombo("surface", std::string{ SurfaceName(a_state.paint->surface) }, WordsOf(kSurfaces), Width::Px(comboWidth), 1.0f)) {
						if (const auto surface = ParseSurface(*chosen)) {
							a_out.push_back(SetPaintSurface{ *surface });
						}
					}
				});
			} else {
				Widgets::NextItemWidth(Width::Fit(a_recipe.id));
				RecipeCombo(a_piece, a_recipe, "##recipe", a_out);
			}
			ImGui::Separator();
		}

		// After the frame: a dirty stack rebuilds the paint recipe's scratch
		// mask. It waits until the paint recipe is the one applied, since the
		// session begins on the game thread a frame or more after Paint opens.
		void RebuildScratch(MenuState& a_state, const RecipeRow* a_recipe, const Snapshot& a_snapshot)
		{
			auto& region = a_state.region;
			if (!region.dirty || !a_state.paint || !a_recipe || a_recipe->id != kPaintRecipe) {
				return;
			}
			region.dirty = false;
			Intents intents;
			for (auto& edit : ScratchEdits(region.terms, region.solo, region.muted, ScratchOf(*a_recipe))) {
				Post(intents, a_recipe->id, std::move(edit));
			}
			Dispatch(intents, a_state, a_snapshot);
		}

		// A kept mask loaded into the stack for editing, in Paint mode.
		void EditMaskAsRegion(const RecipeRow& a_recipe, const TextRow& a_mask, Intents& a_out)
		{
			auto terms = TermsOfMask(a_mask.text, LoadedPresets(), ExistingOf(a_recipe));
			if (!terms) {
				return;
			}
			a_out.push_back(LoadRegion{ std::move(*terms), a_mask.name });
			a_out.push_back(SetMode{ Mode::kPaint });
		}

		// ---------------------------------------------------------- body

		// Everything under the mode bar is drawn in the recipe's ID scope, so a
		// field's key names the same field only while the same recipe is shown.
		// The context rows stay in view; under them two panes scroll on their
		// own, separated by rules: the stack and the resources. The resources
		// take a share of what is left under the rows; the stack takes the rest.
		void DrawBody(const Snapshot& a_snapshot, const PieceRow* a_piece, const RecipeRow* a_recipe, const GeometryRow* a_geometry, MenuState& a_state, Intents& a_out)
		{
			// Leaving Paint ends the session without keeping.
			if (a_state.paint && a_state.mode != Mode::kPaint) {
				a_out.push_back(EndPaint{});
			}
			if (!a_piece || !a_recipe) {
				Widgets::Dim(a_state.paint ? "starting the paint recipe" : "nothing applied; equip enchanted PBR armor or press Re-apply all on the Recipes page");
				return;
			}
			// A recipe bound to no geometry still gets the recipe row, so the
			// combo that leads away from it is always there.
			if (!a_geometry) {
				ImGui::PushID(a_recipe->id.c_str());
				if (a_state.layout.contextRows) {
					DrawRecipeContext(a_snapshot, *a_piece, *a_recipe, a_out);
				} else {
					DrawPaintHead(*a_piece, *a_recipe, a_state, a_out);
				}
				Widgets::Rule();
				Widgets::Dim(std::format("recipe {} is bound to no geometry of this piece: its keys or selectors match none of its shapes", a_recipe->id));
				ImGui::PopID();
				return;
			}
			if (a_state.mode == Mode::kDesign) {
				ImGui::Text("%s mode is not built yet", std::string{ ModeName(a_state.mode) }.c_str());
				return;
			}
			const auto& view = a_snapshot.view;
			const auto& selection = a_state.selection;
			Layout&     layout = a_state.layout;
			const float scale = layout.widgetScale;
			g_modalActor = a_piece->actorID;
			g_modalBones = a_geometry->bones;
			ImGui::PushID(a_recipe->id.c_str());

			const auto  board = BuildBoard(*a_recipe, *a_geometry, selection, view);
			const auto  names = NamesOf(*a_recipe, *a_geometry);
			const auto  pane = ChoosePane(selection.target, a_state.settings);
			const Cell* picked = layout.contextRows ? DrawContext(a_snapshot, board, *a_piece, *a_recipe, selection, a_out) : nullptr;
			// Paint: the head, then the session begins for the selected recipe
			// (the manager applies the paint recipe alone a frame or more
			// later) and the viewed shape's mesh is read for its offers; the
			// action row draws once the paint recipe is the one applied.
			const bool painting = layout.regionEditor;
			const bool painterReady = painting && a_state.paint && a_recipe->id == kPaintRecipe;
			if (!layout.contextRows) {
				DrawPaintHead(*a_piece, *a_recipe, a_state, a_out);
				if (!a_state.paint) {
					if (const auto key = PaintKeyOf(*a_piece)) {
						a_out.push_back(BeginPaint{ a_recipe->id, *key, Surface::kMaterial });
					} else {
						Widgets::Warn("the piece offers no key to paint on");
					}
				} else if (painterReady && !a_state.paint->readPosted) {
					// The shape's read (its mesh and its material's clusters) is asked
					// for once the paint recipe is the one applied; asked earlier it
					// would find nothing bound, since the session retires everything.
					a_out.push_back(ReadMesh{ a_piece->actorID, a_geometry->name });
				} else if (!painterReady) {
					Widgets::Dim("starting the paint recipe");
				}
			}
			const bool resources = layout.signals;
			// The pane's rule names what its switch shows: the target's settings
			// or the stack; in Paint mode the pane is the region stack.
			if (painting) {
				const std::string title = a_state.region.editing.empty() ? std::string{ "Region" } : std::format("Region: {}", a_state.region.editing);
				Widgets::Rule({}, Widgets::RuleLine::Text(title));
			} else {
				const std::string_view title = pane.settings ? (selection.target == Target::kLight ? "Light settings" : "Shell settings") : "Stack";
				Widgets::Rule({}, PaneRule(title, pane, board, *a_recipe, selection.target, a_out));
			}
			const float under = ImGui::GetContentRegionAvail().y;
			const float resourcesHeight = resources ? under * layout.resourcesShare : 0.0f;
			const float stackHeight = resources ? -(resourcesHeight + Widgets::RuleHeight()) : 0.0f;

			if (ImGui::BeginChild("stack-pane", ImVec2{ 0.0f, stackHeight }, 0, 0)) {
				// The pane shows the region editor in Paint mode; otherwise the
				// target's settings (the light's panel, the shell's settings) or
				// the picked slot's stack, as switched.
				if (painting) {
					if (painterReady) {
						DrawRegionStack(*a_recipe, *a_geometry, a_state, names, a_out);
					}
				} else if (pane.settings) {
					if (selection.target == Target::kLight) {
						if (a_recipe->lightRow.present) {
							DrawFormWithSignals("light", LightForm(a_recipe->lightRow, SignalNamesOf(*a_recipe)), *a_recipe, a_piece->actorID, scale, names, a_out, kSettingsColumns);
						} else {
							Widgets::Dim("the recipe has no light");
						}
					} else {
						DrawFormWithSignals("shell", ShellForm(a_recipe->shellRow, SignalNamesOf(*a_recipe)), *a_recipe, a_piece->actorID, scale, names, a_out, kSettingsColumns);
					}
				} else if (picked && picked->output) {
					const auto stack = BuildStackView(*a_piece, *a_recipe, *a_geometry, selection, view);
					const auto inspector = layout.inspector ? BuildInspector(*a_recipe, *a_geometry, selection) : std::nullopt;
					DrawStack(stack, inspector, *a_piece, *a_recipe, *a_geometry, selection, layout, names, a_out);
				}
			}
			ImGui::EndChild();

			if (resources) {
				std::string_view filter;
				Widgets::Rule({}, ResourcesRule(a_state.resource, *a_recipe, scale, filter, a_out));
				DrawResources(*a_piece, *a_recipe, layout, a_state.resource, filter, names, a_out);
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

		void DrawFooter(const RecipeRow* a_recipe, const View& a_view, Intents& a_out)
		{
			const auto& view = a_view;
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
			// Not while painting: the paint recipe has no history worth walking.
			const auto* io = ImGui::GetIO();
			if (!a_recipe || !io || !io->KeyCtrl || a_state.activeField != kNoField || a_state.paint) {
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
		auto* manager = Manager::GetSingleton();
		auto& state = State();
		manager->Watch(RequestOf(state.selection));
		const auto  held = manager->LatestSnapshot();
		const auto& snapshot = *held;
		Intents     intents;

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
		DrawFooter(recipe, snapshot.view, intents);
		HistoryKeys(recipe, state, intents);
		Dispatch(intents, state, snapshot);
		RebuildScratch(state, recipe, snapshot);
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
		const auto board = BuildBoard(*recipe, *geometry, state.selection, a_snapshot.view);
		Intents    intents;
		ImGui::PushID(recipe->id.c_str());
		DrawBoard(board, *recipe, *geometry, state.selection, state.layout, intents);
		ImGui::PopID();
		Dispatch(intents, state, a_snapshot);
	}
}
