#include "ComposePage.h"

#include "EditCheck.h"
#include "Expression.h"
#include "Edits.h"
#include "Forms.h"
#include "Manager.h"
#include "Region.h"
#include "MenuState.h"
#include "MenuWidgets.h"
#include "RecipeStore.h"
#include "Paint.h"
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

namespace WornEnchantmentPBR::Studio
{
	namespace
	{
		using Widgets::TableStyle;
		using Widgets::Width;
		using Intents = std::vector<Intent>;

		constexpr float       kFilterWidth = 160.0f;
		constexpr const char* kLayerPayload = "WEPBR_LAYER";

		constexpr TableStyle kGridStyle{ .borders = TableStyle::Borders::kAll, .stretch = true, .headers = true, .rowBackground = true };
		constexpr TableStyle kContextStyle{ .borders = TableStyle::Borders::kAll, .stretch = false, .headers = true, .rowBackground = false };
		constexpr TableStyle kFormStyle{ .borders = TableStyle::Borders::kInnerHorizontal, .stretch = true, .headers = false, .rowBackground = false };
		constexpr std::size_t kSettingsColumns = 2;
		constexpr TableStyle  kColumnsStyle{ .borders = TableStyle::Borders::kNone, .stretch = true, .headers = false, .rowBackground = false };
		constexpr TableStyle kLayerStyle{ .borders = TableStyle::Borders::kInnerHorizontal, .stretch = true, .headers = true, .rowBackground = true };
		constexpr TableStyle kFooterStyle{ .borders = TableStyle::Borders::kAll, .stretch = true, .headers = true, .rowBackground = false };

		void Refuse(std::string_view a_field, const std::string& a_text)
		{
			logger::warn("{} not applied: '{}' does not parse", a_field, a_text);
		}

		void Perform(const Intent& a_intent, const MenuState& a_state, const View& a_view)
		{
			auto*       manager = Manager::GetSingleton();
			const auto& view = a_view;
			Match(
				a_intent,
				[&](const SetMode& i) {
					if (a_state.paint && i.mode != Mode::kPaint && a_state.mode == Mode::kPaint) {
						manager->EndPaint();
					}
				},
				[&](const EditRecipe& i) { manager->EditRecipe(i.recipeID, EditBatch{ i.edits }); },
				[&](const SoloRecipe& i) {
					manager->UpdateView([](View& a_live) { a_live.isolatedBySolo = false; });
					manager->Isolate(i.on ? i.recipeID : std::string{}, -1, -1);
				},
				[&](const SoloOutput& i) {
					if (i.on) {
						manager->UpdateView([began = !view.Isolating()](View& a_live) { a_live.isolatedBySolo = a_live.isolatedBySolo || began; });
						manager->Isolate(i.recipeID, static_cast<int>(i.output), -1);
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
						manager->Isolate(i.recipeID, static_cast<int>(i.output), static_cast<int>(i.layer));
					} else if (view.isolatedBySolo && view.isolateOutput < 0) {
						manager->UpdateView([](View& a_live) { a_live.isolatedBySolo = false; });
						manager->Isolate(std::string{}, -1, -1);
					} else {
						manager->Isolate(view.isolateRecipe, view.isolateOutput, -1);
					}
				},
				[&](const MuteLayer& i) {
					manager->UpdateView([key = LayerKey{ i.recipeID, i.output, i.layer }, on = i.on](View& a_live) {
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
							a_live.scrubSeconds = at;
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
				[&](const StepClock&) {
					manager->UpdateView([](View& a_live) {
						a_live.freeze = true;
						a_live.scrubSeconds += static_cast<float>(GetSettings().TickIntervalMS()) * 0.001f * a_live.speed;
					});
				},
				[&](const Undo& i) { manager->UndoRecipe(i.recipeID); },
				[&](const Redo& i) { manager->RedoRecipe(i.recipeID); },
				[&](const CreateRecipe& i) { manager->NewRecipe(i.recipeID, i.key, i.geometry); },
				[&](const RenameRecipe& i) { manager->RenameRecipe(i.from, i.to); },
				[&](const BeginPaint& i) { manager->BeginPaint(i.recipeID, i.key, i.surface); },
				[&](const SetPaintSurface& i) { manager->SetPaintSurface(i.surface); },
				[&](const KeepPaint& i) { manager->KeepPaint(i.recipeID, i.name); },
				[&](const EndPaint&) { manager->EndPaint(); },
				[&](const ReadMesh& i) { manager->RequestMesh(i.actorID, i.geometry); },
				[&](const FireTrigger& i) { manager->FireAt(i.actorID, i.event, i.node, i.offset, i.random, i.value); },
				[](const PickPiece&) {},
				[&](const PickRecipe& i) {
					if (view.pin && view.pin->piece == a_state.selection.piece && view.pin->recipeID != i.recipeID) {
						manager->PinRecipe(a_state.selection.piece, {});
					}
				},
				[&](const PinRecipe& i) { manager->PinRecipe(a_state.selection.piece, i.recipeID); },
				[](const PickTarget&) {},
				[](const PickSlot&) {},
				[](const PickCell&) {},
				[](const PickLayer&) {},
				[](const ViewGeometry&) {},
				[](const SetStackSplit&) {},
				[](const ShowSettings&) {},
				[](const ShowResource&) {},
				[](const AddTerm&) {},
				[](const SetTermOp&) {},
				[](const SetTermText&) {},
				[](const SetTermKind&) {},
				[](const RemoveTerm&) {},
				[](const MoveTerm&) {},
				[](const PickTerm&) {},
				[](const SoloTerm&) {},
				[](const MuteTerm&) {},
				[](const LoadRegion&) {},
				[](const ClearRegion&) {},
				[](const UndoRegion&) {},
				[](const RedoRegion&) {},
				[](const ScratchRebuilt&) {});
		}

		void Dispatch(Intents& a_intents, MenuState& a_state, const Snapshot& a_snapshot)
		{
			for (const auto& intent : a_intents) {
				Perform(intent, a_state, a_snapshot.view);
				Reduce(a_state, intent);
			}
			ResolveSelection(a_state.selection, a_snapshot);
			a_intents.clear();
		}

		void PostAll(Intents& a_out, const std::string& a_recipe, std::vector<RecipeEdit> a_edits)
		{
			if (!a_edits.empty()) {
				a_out.push_back(EditRecipe{ a_recipe, std::move(a_edits) });
			}
		}

		void Post(Intents& a_out, const std::string& a_recipe, RecipeEdit a_edit)
		{
			PostAll(a_out, a_recipe, { std::move(a_edit) });
		}

		void EditMaskAsRegion(const RecipeRow& a_recipe, const TextRow& a_mask, Intents& a_out);

		[[nodiscard]] std::optional<std::string> FieldInput(const FormField& a_field, float a_scale, const Names& a_names)
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
			case FieldInputKind::kPlain:
				return Widgets::TextField("value", a_field.text, Width::Fill(), a_scale, check);
			case FieldInputKind::kValue:
				return Widgets::ValueField("value", a_field.kind, a_field.text, a_field.names, a_field.allowEmpty, a_scale, check, a_field.creators);
			}
			return std::nullopt;
		}

		void PostField(const FormField& a_field, const std::string& a_text, const std::string& a_recipe, Intents& a_out)
		{
			if (std::ranges::find(a_field.creators, a_text) != a_field.creators.end()) {
				if (a_field.create) {
					PostAll(a_out, a_recipe, a_field.create(a_text));
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

		void DrawRowField(const char* a_key, const FormField& a_field, const std::string& a_recipe, float a_scale, const Names& a_names, Intents& a_out)
		{
			ImGui::PushID(a_key);
			if (const auto text = FieldInput(a_field, a_scale, a_names)) {
				PostField(a_field, *text, a_recipe, a_out);
			}
			ImGui::PopID();
		}

		[[nodiscard]] std::optional<std::size_t> DrawFieldTable(const char* a_id, std::span<const FormField> a_fields, const std::string& a_recipe, float a_scale, const Names& a_names, Intents& a_out)
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

		[[nodiscard]] std::optional<std::size_t> DrawForm(const char* a_id, std::span<const FormField> a_form, const std::string& a_recipe, float a_scale, const Names& a_names, Intents& a_out, std::size_t a_columns = 1)
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

		void SelectionCombo(const Snapshot& a_snapshot, const PieceRow* a_piece, const char* a_label, Intents& a_out)
		{
			const auto preview = a_piece ? std::format("{} / {} ({})", a_piece->actorName, a_piece->armorName, a_piece->ref.firstPerson ? "1st" : "3rd") : std::string{ "nothing applied" };
			if (ImGui::BeginCombo(a_label, preview.c_str())) {
				std::size_t i = 0;
				for (const auto& p : a_snapshot.pieces) {
					const auto label = std::format("{} / {} ({})##sel{}", p.actorName, p.armorName, p.ref.firstPerson ? "1st" : "3rd", i++);
					if (ImGui::Selectable(label.c_str(), &p == a_piece)) {
						a_out.push_back(PickPiece{ p.ref });
					}
				}
				ImGui::EndCombo();
			}
		}

		std::string RecipeLabel(const RecipeRow& a_recipe)
		{
			return a_recipe.pinned ? std::format("{} (pinned here)", a_recipe.id) : a_recipe.id;
		}

		void RecipeCombo(const PieceRow& a_piece, const RecipeRow& a_recipe, std::span<const std::string> a_loaded, const char* a_label, Intents& a_out)
		{
			if (ImGui::BeginCombo(a_label, RecipeLabel(a_recipe).c_str())) {
				for (const auto& r : a_piece.recipes) {
					const auto label = r.pinned ? RecipeLabel(r) : std::format("{} ({}, priority {})", r.id, r.key, r.priority);
					if (ImGui::Selectable(label.c_str(), &r == &a_recipe)) {
						a_out.push_back(PickRecipe{ r.id });
					}
				}
				bool divided = false;
				for (const auto& id : a_loaded) {
					if (std::ranges::find(a_piece.recipes, id, &RecipeRow::id) != a_piece.recipes.end()) {
						continue;
					}
					if (!divided) {
						ImGui::Separator();
						divided = true;
					}
					if (ImGui::Selectable(std::format("{} (not worn here)", id).c_str(), false)) {
						a_out.push_back(PinRecipe{ id });
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

		[[nodiscard]] std::span<const SlotRow> SlotRowsOf(const GeometryRow& a_geometry, Surface a_surface) noexcept
		{
			return a_surface == Surface::kMaterial ? a_geometry.materialSlots : a_geometry.shellSlots;
		}

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

		[[nodiscard]] PickCell PickOf(const Cell& a_cell)
		{
			return PickCell{ a_cell.surface, a_cell.slot, a_cell.layers > 0 ? std::optional{ a_cell.layers - 1 } : std::nullopt };
		}

		void DrawWrittenCell(const Cell& a_cell, const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Selection& a_selection, const Layout& a_layout, Intents& a_out)
		{
			const bool selected = a_selection.target != Target::kLight && SurfaceOf(a_selection.target) == a_cell.surface && a_selection.slot == a_cell.slot;
			if (Widgets::ThumbnailButton("cell", a_cell.composite, ShaderChannel::kRgb, a_cell.animated, a_layout.cellSize * a_layout.widgetScale)) {
				a_out.push_back(PickOf(a_cell));
			}
			Widgets::Tooltip(CellTooltip(a_cell, a_geometry));
			ImGui::SameLine();
			ImGui::BeginGroup();
			const auto count = std::format("{} layer{}", a_cell.layers, a_cell.layers == 1 ? "" : "s");
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

		void DrawCell(const Cell* a_cell, const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Selection& a_selection, const Layout& a_layout, Intents& a_out)
		{
			if (!a_cell) {
				return;
			}
			const float side = a_layout.cellSize * a_layout.widgetScale;
			switch (a_cell->state) {
			case CellState::kAbsent:
				return;
			case CellState::kWritten:
				DrawWrittenCell(*a_cell, a_recipe, a_geometry, a_selection, a_layout, a_out);
				return;
			case CellState::kEmpty:
				if (ImGui::Button("+", ImVec2{ side, side })) {
					Post(a_out, a_recipe.id, AddOutput{ a_cell->surface, a_cell->slot, {} });
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
				DrawCell(CellAt(a_board, Surface::kMaterial, slot), a_recipe, a_geometry, a_selection, a_layout, a_out);
				ImGui::PopID();
				table.Cell();
				ImGui::PushID(static_cast<int>(i * 2 + 1));
				DrawCell(CellAt(a_board, Surface::kShell, slot), a_recipe, a_geometry, a_selection, a_layout, a_out);
				ImGui::PopID();
			}
			table.Cell();
			ImGui::TextUnformatted("light");
			table.Cell();
			DrawLightCell(a_board.light, a_recipe, a_out);
			table.Cell();
			table.End();
		}

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

		[[nodiscard]] const Cell* PickedCell(const Board& a_board, const Selection& a_selection) noexcept
		{
			if (a_selection.target == Target::kLight || !a_selection.slot) {
				return nullptr;
			}
			return CellAt(a_board, SurfaceOf(a_selection.target), *a_selection.slot);
		}

		std::optional<RecipeKey> DefaultKeyOf(const PieceRow& a_piece);

		void KeysPopup(const PieceRow& a_piece, const RecipeRow& a_recipe, Intents& a_out)
		{
			if (!ImGui::BeginPopup("recipe-keys")) {
				return;
			}
			auto table = Widgets::Table::Begin("keys", { { "key", Width::Fit() }, { "", Width::Px(Widgets::RowButtonWidth()) } }, kFormStyle);
			if (table.Open()) {
				for (std::size_t i = 0; i < a_recipe.keys.size(); ++i) {
					const auto& key = a_recipe.keys[i];
					ImGui::PushID(static_cast<int>(i));
					table.Cell();
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted(key.ToString().c_str());
					table.Cell();
					Widgets::Disabled(a_recipe.keys.size() == 1, [&]() {
						if (Widgets::RemoveButton(0)) {
							Post(a_out, a_recipe.id, RemoveKey{ key });
						}
					});
					ImGui::PopID();
				}
				table.End();
			}
			const auto label = [](const KeyChoice& a_key) { return std::format("{}: {}", KeyKindName(a_key.key.kind), a_key.text); };
			const auto toKey = [](const KeyChoice& a_key) { return RecipeKeyOf(a_key.key, a_key.text); };
			Widgets::NextItemWidth(Width::Px(240.0f));
			if (ImGui::BeginCombo("##add-key", "add a key the piece carries")) {
				for (const auto& choice : a_piece.keys) {
					const auto key = toKey(choice);
					if (std::ranges::find(a_recipe.keys, key) != a_recipe.keys.end()) {
						continue;
					}
					if (ImGui::Selectable(label(choice).c_str(), false)) {
						Post(a_out, a_recipe.id, AddKey{ key });
					}
				}
				ImGui::EndCombo();
			}
			const auto keyword = Widgets::LiveTextField("keyword", "keyword editor id", Width::Px(240.0f), 1.0f);
			ImGui::SameLine();
			Widgets::Disabled(keyword.empty(), [&]() {
				if (ImGui::SmallButton("Add keyword")) {
					RecipeKey key;
					key.kind = KeyKind::kKeyword;
					key.operand = FormRef::From(keyword);
					Post(a_out, a_recipe.id, AddKey{ key });
				}
			});
			Widgets::Tooltip("a keyword by editor id, resolved against the loaded plugins; the recipe then applies to every piece carrying it");
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
			Widgets::NextItemWidth(Width::Fit(RecipeLabel(a_recipe)));
			RecipeCombo(a_piece, a_recipe, a_snapshot.loaded, "##recipe", a_out);
			table.End();
		}

		[[nodiscard]] Widgets::RuleLine RecipeRule(const PieceRow& a_piece, const RecipeRow& a_recipe, const GeometryRow& a_geometry, Intents& a_out)
		{
			const float newWidth = Widgets::ButtonWidth("New");
			const float renameWidth = Widgets::ButtonWidth("Rename");
			const float clearWidth = Widgets::ButtonWidth("Clear");
			const float keysWidth = Widgets::ButtonWidth("keys");
			const float undoWidth = Widgets::ButtonWidth("Undo");
			const float redoWidth = Widgets::ButtonWidth("Redo");
			const float rightWidth = newWidth + renameWidth + clearWidth + keysWidth + undoWidth + redoWidth + 5.0f * Widgets::ItemSpacingX();
			return Widgets::RuleLine{ "Recipe", rightWidth, [=, &a_piece, &a_recipe, &a_geometry, &a_out]() {
									 const auto key = DefaultKeyOf(a_piece);
									 Widgets::Disabled(!key, [&]() {
										 if (ImGui::Button("New", ImVec2{ newWidth, 0.0f }) && key) {
											 std::vector<std::string> ids;
											 for (const auto& recipe : a_piece.recipes) {
												 ids.push_back(recipe.id);
											 }
											 a_out.push_back(CreateRecipe{ UniqueName("recipe", ids), *key, a_geometry.name });
										 }
									 });
									 Widgets::Tooltip("a new recipe keyed to this armor, named recipe-N, with one empty emissive output on the material for the viewed geometry alone; rename it and edit its keys from keys");
									 ImGui::SameLine();
									 if (ImGui::Button("Rename", ImVec2{ renameWidth, 0.0f })) {
										 ImGui::OpenPopup("rename-recipe");
									 }
									 if (ImGui::BeginPopup("rename-recipe")) {
										 const auto typed = Widgets::LiveTextField("rename", a_recipe.id.c_str(), Width::Px(240.0f), 1.0f);
										 const bool ready = !typed.empty() && typed != a_recipe.id;
										 ImGui::SameLine();
										 Widgets::Disabled(!ready, [&]() {
											 if (ImGui::Button("Rename##do") && ready) {
												 a_out.push_back(RenameRecipe{ a_recipe.id, std::string{ typed } });
												 ImGui::CloseCurrentPopup();
											 }
										 });
										 ImGui::EndPopup();
									 }
									 Widgets::Tooltip("rename the recipe; its file follows when it is the user's");
									 ImGui::SameLine();
									 if (ImGui::Button("Clear", ImVec2{ clearWidth, 0.0f })) {
										 Post(a_out, a_recipe.id, ClearRecipe{});
									 }
									 Widgets::Tooltip("empty the recipe: every output, the shell settings, and every signal, curve, source, mask and variant go; its name and keys stay");
									 ImGui::SameLine();
									 if (ImGui::Button("keys", ImVec2{ keysWidth, 0.0f })) {
										 ImGui::OpenPopup("recipe-keys");
									 }
									 KeysPopup(a_piece, a_recipe, a_out);
									 Widgets::Tooltip("which pieces the recipe applies to");
									 ImGui::SameLine();
									 UndoRedoButtons(a_recipe, a_out);
								 } };
		}

		void ClearButton(const RecipeRow& a_recipe, std::optional<std::size_t> a_output, Intents& a_out);

		[[nodiscard]] Widgets::RuleLine OutputRule(const Board& a_board, const RecipeRow& a_recipe, const Cell* a_picked, const Selection& a_selection, Intents& a_out)
		{
			const float clearWidth = Widgets::ButtonWidth("Clear");
			return Widgets::RuleLine{ "Output", clearWidth, [=, &a_board, &a_recipe, &a_out]() {
									 const bool light = a_selection.target == Target::kLight;
									 ClearButton(a_recipe, light ? a_board.light.output : (a_picked ? a_picked->output : std::nullopt), a_out);
								 } };
		}

		void ClearButton(const RecipeRow& a_recipe, std::optional<std::size_t> a_output, Intents& a_out)
		{
			const std::optional<std::size_t> output = a_output;
			if (!output) {
				ImGui::BeginDisabled();
			}
			if (ImGui::Button("Clear", ImVec2{ Widgets::ButtonWidth("Clear"), 0.0f }) && output) {
				Post(a_out, a_recipe.id, RemoveOutput{ *output });
			}
			if (!output) {
				ImGui::EndDisabled();
			}
			Widgets::Tooltip("remove the picked slot's output and every layer in it");
		}

		struct PaneChoice
		{
			bool hasSettings = false;
			bool hasStack = false;
			bool settings = false;
		};

		[[nodiscard]] PaneChoice ChoosePane(Target a_target, bool a_settingsWanted) noexcept
		{
			PaneChoice choice;
			choice.hasSettings = a_target != Target::kMaterial;
			choice.hasStack = a_target != Target::kLight;
			choice.settings = choice.hasStack ? (a_settingsWanted && choice.hasSettings) : true;
			return choice;
		}

		void DrawEditContext(const Board& a_board, const RecipeRow& a_recipe, const Cell* a_picked, const Selection& a_selection, Intents& a_out)
		{
			const bool light = a_selection.target == Target::kLight;
			auto       table = Widgets::Table::Begin("context", { { "S", Width::Px(Widgets::RowButtonWidth()) }, { "target", Width::Fit() }, { "slot", Width::Fit() } }, kContextStyle);
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
			table.End();
		}

		[[nodiscard]] Widgets::RuleLine PaneRule(std::string_view a_title, const PaneChoice& a_pane, const Board& a_board, const RecipeRow& a_recipe, Target a_target, Intents& a_out)
		{
			const float switchWidth = (std::max)(Widgets::ButtonWidth("settings"), Widgets::ButtonWidth("stack"));
			const float defaultsWidth = Widgets::ButtonWidth("Apply Defaults");
			const float rightWidth = switchWidth + (a_pane.settings ? defaultsWidth + Widgets::ItemSpacingX() : 0.0f);
			return Widgets::RuleLine{ a_title, rightWidth, [=, &a_board, &a_recipe, &a_out]() {
									 if (a_pane.settings) {
										 const bool light = a_target == Target::kLight;
										 const bool present = !light || (a_board.light.present && a_board.light.output.has_value());
										 if (!present) {
											 ImGui::BeginDisabled();
										 }
										 if (ImGui::Button("Apply Defaults", ImVec2{ defaultsWidth, 0.0f }) && present) {
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

		const Cell* DrawContext(const Snapshot& a_snapshot, const Board& a_board, const PieceRow& a_piece, const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Selection& a_selection, Intents& a_out)
		{
			const Cell* picked = PickedCell(a_board, a_selection);
			Widgets::Rule({}, RecipeRule(a_piece, a_recipe, a_geometry, a_out));
			DrawRecipeContext(a_snapshot, a_piece, a_recipe, a_out);
			Widgets::Rule({}, OutputRule(a_board, a_recipe, picked, a_selection, a_out));
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
					Post(a_out, a_recipe.id, AddOutput{ picked->surface, picked->slot, {} });
				}
				Widgets::Tooltip(std::format("add an empty stack on {} of the {}", SlotName(picked->slot), SurfaceName(picked->surface)));
				return picked;
			}
			if (!picked->reason.empty()) {
				Widgets::Warn(picked->reason);
			}
			return picked;
		}

		void DrawInspectorFields(const Inspector& a_inspector, const RecipeRow& a_recipe, FormID a_actorID, std::span<const BoneRow> a_bones, const Layout& a_layout, const Names& a_names, Intents& a_out);
		void DrawFormWithSignals(const char* a_id, std::span<const FormField> a_form, const RecipeRow& a_recipe, FormID a_actorID, std::span<const BoneRow> a_bones, float a_scale, const Names& a_names, Intents& a_out, std::size_t a_columns = 1);

		void DrawScalars(const LayerStack& a_stack, const RecipeRow& a_recipe, FormID a_actorID, std::span<const BoneRow> a_bones, float a_scale, const Names& a_names, Intents& a_out)
		{
			if (a_stack.scalars.empty()) {
				return;
			}
			DrawFormWithSignals("scalars", ScalarForm(a_stack), a_recipe, a_actorID, a_bones, a_scale, a_names, a_out);
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
			Widgets::Dim(std::format("{}  ({}, priority {}: {} {})", a_row.layer.source, a_row.recipeID, a_row.priority, a_row.layer.opacityText, a_row.layer.mask));
		}

		void DrawStackRow(Widgets::Table& a_table, const LayerStack& a_stack, const LayerStackRow& a_row, const RecipeRow& a_recipe, Intents& a_out)
		{
			const auto&       id = a_recipe.id;
			const std::size_t output = a_stack.output;
			const std::size_t index = a_row.index;
			const bool        constant = !a_row.layer.source.starts_with('@');

			ImGui::PushID(static_cast<int>(index));
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
			ImGui::PopID();
		}

		void DrawAddLayer(const LayerStack& a_stack, const RecipeRow& a_recipe, Intents& a_out)
		{
			if (ImGui::SmallButton("Add layer")) {
				Post(a_out, a_recipe.id, AddLayer{ a_stack.output, DefaultLayer(), a_stack.rows.size() });
			}
		}

		void DrawLayers(const LayerStack& a_stack, const RecipeRow& a_recipe, Intents& a_out)
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
			DrawAddLayer(a_stack, a_recipe, a_out);
			Widgets::HelpMarker("Drag the :: grip onto another row to reorder; click the grip or the name to open the layer's fields beside the stack. S solos, M mutes. Enter commits a text field; a drag commits on release.");
		}

		void DrawInspector(const LayerStack& a_stack, const std::optional<Inspector>& a_inspector, const RecipeRow& a_recipe, FormID a_actorID, std::span<const BoneRow> a_bones, const Layout& a_layout, const Names& a_names, Intents& a_out)
		{
			if (!a_layout.inspector || !a_inspector) {
				Widgets::Dim("click a layer to inspect it");
				return;
			}
			const auto row = std::ranges::find(a_stack.rows, a_inspector->layer, &LayerStackRow::index);
			ImGui::PushID(static_cast<int>(a_inspector->layer));
			if (!a_inspector->row.problem.empty()) {
				Widgets::Warn(a_inspector->row.problem);
			}
			DrawInspectorFields(*a_inspector, a_recipe, a_actorID, a_bones, a_layout, a_names, a_out);
			if (row != a_stack.rows.end() && row->layer.texture) {
				Widgets::Thumbnail(row->layer.texture, ShaderChannel::kRgb, false, a_layout.inspectorThumbnail * a_layout.widgetScale);
			}
			ImGui::PopID();
		}

		void DrawComposite(const LayerStack& a_stack, const PieceRow& a_piece, const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Layout& a_layout, Intents& a_out)
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
			Widgets::Tooltip(std::format("viewed on {} (one of {} geometries; the recipe applies to all)\nclick: view the next geometry\nraw name: {}", GeometryLabel(a_geometry.name, a_piece.armorName), a_recipe.geometries.size(), a_geometry.name));
		}

		void DrawStack(const std::optional<LayerStack>& a_stack, const std::optional<Inspector>& a_inspector, const PieceRow& a_piece, const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Layout& a_layout, const Names& a_names, Intents& a_out)
		{
			if (!a_stack) {
				Widgets::Dim("choose a target and a slot");
				return;
			}
			const auto& stack = *a_stack;
			ImGui::PushID(static_cast<int>(stack.output));
			if (!stack.problem.empty()) {
				Widgets::Problem(stack.problem);
			}
			DrawComposite(stack, a_piece, a_recipe, a_geometry, a_layout, a_out);
			ImGui::SameLine();
			ImGui::BeginGroup();
			Widgets::Dim(std::format("composite {} px, {}", stack.size, stack.animated ? "animated" : "static"));
			DrawScalars(stack, a_recipe, a_piece.ref.actorID, a_geometry.bones, a_layout.widgetScale, a_names, a_out);
			ImGui::EndGroup();

			if (stack.rows.empty()) {
				DrawAddLayer(stack, a_recipe, a_out);
				ImGui::PopID();
				return;
			}
			const auto dragged = Widgets::Split(
				"stack-split", a_layout.stackSplit,
				[&]() { DrawLayers(stack, a_recipe, a_out); },
				[&]() { DrawInspector(stack, a_inspector, a_recipe, a_piece.ref.actorID, a_geometry.bones, a_layout, a_names, a_out); });
			if (dragged) {
				a_out.push_back(SetStackSplit{ *dragged });
			}
			ImGui::PopID();
		}

		void FirePopup(const SignalRow& a_signal, FormID a_actorID, std::span<const BoneRow> a_bones, Intents& a_out)
		{
			if (ImGui::SmallButton("Fire")) {
				ImGui::OpenPopup("fire");
			}
			if (!ImGui::BeginPopup("fire")) {
				return;
			}
			auto& draft = State().firing;
			Widgets::NextItemWidth(Width::Px(220.0f));
			if (ImGui::BeginCombo("node", draft.node.empty() ? "(none)" : draft.node.c_str())) {
				if (ImGui::Selectable("(none)", draft.node.empty())) {
					draft.node.clear();
				}
				for (const auto& bone : a_bones) {
					if (ImGui::Selectable(bone.name.c_str(), bone.name == draft.node)) {
						draft.node = bone.name;
					}
				}
				ImGui::EndCombo();
			}
			float offset[3]{ draft.offset.x, draft.offset.y, draft.offset.z };
			Widgets::NextItemWidth(Width::Px(220.0f));
			if (ImGui::DragFloat3("offset", offset, 1.0f)) {
				draft.offset = Vec3{ offset[0], offset[1], offset[2] };
			}
			Widgets::NextItemWidth(Width::Px(220.0f));
			ImGui::DragFloat("random", &draft.random, 1.0f, 0.0f, 200.0f);
			Widgets::NextItemWidth(Width::Px(220.0f));
			ImGui::DragFloat("value", &draft.value, 0.01f);
			if (ImGui::Button("Fire now")) {
				a_out.push_back(FireTrigger{ a_actorID, a_signal.event, draft.node, draft.offset, draft.random, draft.value });
			}
			ImGui::EndPopup();
		}

		void DrawSignalEditor(const std::string& a_id, const SignalRow& a_signal, const SignalNames& a_signalNames, FormID a_actorID, std::span<const BoneRow> a_bones, float a_scale, const Names& a_names, Intents& a_out)
		{
			const auto form = SignalForm(a_signal, a_signalNames);
			const auto value = a_signal.kind == SignalKindId::kConstant || a_signal.kind == SignalKindId::kExpr ? std::ranges::find(form, "value", &FormField::name) : form.end();
			const auto title = std::format("signal {}###signal-settings", a_signal.name);
			if (Widgets::DetailButton()) {
				ImGui::OpenPopup(title.c_str());
			}
			Widgets::DetailModal(title.c_str(), [&]() { [[maybe_unused]] const auto detail = DrawForm("form", form, a_id, a_scale, a_names, a_out); });
			ImGui::SameLine();
			if (value != form.end()) {
				DrawRowField("value", *value, a_id, a_scale, a_names, a_out);
				return;
			}
			if (!a_signal.event.empty()) {
				FirePopup(a_signal, a_actorID, a_bones, a_out);
				ImGui::SameLine();
			}
			ImGui::AlignTextToFramePadding();
			Widgets::Dim(SignalKindName(a_signal.kind));
		}

		void DrawSignalCurve(const std::string& a_id, const SignalRow& a_signal, std::span<const std::string> a_curves, float a_width, float a_scale, Intents& a_out)
		{
			if (const auto chosen = Widgets::ReferenceCombo("curve", a_signal.curve, a_curves, true, Width::Px(a_width), a_scale)) {
				Post(a_out, a_id, SetSignalCurve{ a_signal.name, chosen->empty() ? std::nullopt : std::optional{ CurveRef{ *chosen } } });
			}
			Widgets::Tooltip("a declared curve applied to the signal's value; none passes it through");
		}

		void DrawSignalRow(Widgets::Table& a_table, const std::string& a_id, const SignalRow& a_signal, const SignalNames& a_signalNames, bool a_tunable, std::span<const std::string> a_curves, float a_curveWidth, FormID a_actorID, std::span<const BoneRow> a_bones, float a_scale, const Names& a_names, Intents& a_out)
		{
			ImGui::PushID(a_signal.name.c_str());
			a_table.Cell();
			if (Widgets::RemoveButton(a_signal.references)) {
				Post(a_out, a_id, RemoveSignal{ a_signal.name });
			}
			a_table.Cell();
			DrawRowField("name", RowNameField(RowKind::kSignal, a_signal.name, TakenNames(RowKind::kSignal, a_names)), a_id, a_scale, a_names, a_out);
			Widgets::Tooltip(SignalKindName(a_signal.kind));
			a_table.Cell();
			DrawSignalEditor(a_id, a_signal, a_signalNames, a_actorID, a_bones, a_scale, a_names, a_out);
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

		void DrawSignals(const PieceRow& a_piece, const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Layout& a_layout, std::string_view a_filter, const Names& a_names, Intents& a_out)
		{
			const auto  list = BuildSignalList(a_recipe, a_layout);
			const auto& id = a_recipe.id;
			const float scale = a_layout.widgetScale;

			const auto               signalNames = SignalNamesOf(a_recipe);
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
					DrawSignalRow(signals, id, signal, signalNames, true, curveNames, curveWidth, a_piece.ref.actorID, a_geometry.bones, scale, a_names, a_out);
				}
			}
			for (const auto& signal : list.developer) {
				if (NameMatches(signal.name, a_filter)) {
					DrawSignalRow(signals, id, signal, signalNames, false, curveNames, curveWidth, a_piece.ref.actorID, a_geometry.bones, scale, a_names, a_out);
				}
			}
			signals.End();
		}

		void DrawCurves(const RecipeRow& a_recipe, const Layout& a_layout, std::string_view a_filter, const Names& a_names, Intents& a_out)
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
				if (Widgets::RemoveButton(curve.references)) {
					Post(a_out, id, RemoveCurve{ curve.name });
				}
				curves.Cell();
				DrawRowField("name", RowNameField(RowKind::kCurve, curve.name, TakenNames(RowKind::kCurve, a_names)), id, scale, a_names, a_out);
				curves.Cell();
				DrawRowField("text", CurveTextField(curve.name, curve.text), id, scale, a_names, a_out);
				ImGui::PopID();
			}
			curves.End();
		}

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
				DrawRowField("name", RowNameField(RowKind::kSource, source.name, TakenNames(RowKind::kSource, a_names)), id, scale, a_names, a_out);
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

		void DrawMasks(const RecipeRow& a_recipe, const Layout& a_layout, std::string_view a_filter, const Names& a_names, Intents& a_out)
		{
			const auto& id = a_recipe.id;
			const float scale = a_layout.widgetScale;
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
				DrawRowField("name", RowNameField(RowKind::kMask, mask.name, TakenNames(RowKind::kMask, a_names)), id, scale, a_names, a_out);
				masks.Cell();
				DrawRowField("text", MaskTextField(mask.name, mask.text), id, scale, a_names, a_out);
				ImGui::PopID();
			}
			masks.End();
		}

		[[nodiscard]] Widgets::RuleLine ResourcesRule(ResourceTab a_tab, const RecipeRow& a_recipe, float a_scale, std::string_view& a_filter, Intents& a_out)
		{
			const float clearWidth = Widgets::ButtonWidth("Clear");
			const float addWidth = Widgets::ButtonWidth("Add");
			const float rightWidth = clearWidth + addWidth + 2.0f * Widgets::ItemSpacingX() + kFilterWidth * a_scale;
			return Widgets::RuleLine{ "Resources", rightWidth, [=, &a_recipe, &a_filter, &a_out]() {
									 if (ImGui::Button("Clear", ImVec2{ clearWidth, 0.0f })) {
										 Post(a_out, a_recipe.id, ClearResources{});
									 }
									 Widgets::Tooltip("remove every signal, curve, source, mask and variant, and what read them: layers on a source go, the masks and curves of the layers that stay are dropped, and a parameter that named a signal returns to its default");
									 ImGui::SameLine();
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

		void DrawResources(const PieceRow& a_piece, const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Layout& a_layout, ResourceTab a_open, std::string_view a_filter, const Names& a_names, Intents& a_out)
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
						DrawSignals(a_piece, a_recipe, a_geometry, a_layout, a_filter, a_names, a_out);
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

		void DrawImageRow(const std::string& a_id, const PictureRow& a_image, bool a_editable, const Layout& a_layout, const Names& a_names, Intents& a_out)
		{
			const float scale = a_layout.widgetScale;
			Widgets::Thumbnail(a_image.texture, a_image.channel, a_image.animated, a_layout.inspectorThumbnail * scale);
			ImGui::TextUnformatted((ReferenceText(a_image.name) + " =").c_str());
			ImGui::SameLine();
			if (a_editable) {
				DrawRowField("text", MaskTextField(a_image.name, a_image.description), a_id, scale, a_names, a_out);
			} else {
				ImGui::TextWrapped("%s", a_image.description.c_str());
			}
			if (!a_image.problem.empty()) {
				Widgets::Warn(a_image.problem);
			}
		}

		constexpr int kMaxSignalModalDepth = 6;

		void DrawSignalModal(const RecipeRow& a_recipe, const std::string& a_name, FormID a_actorID, std::span<const BoneRow> a_bones, float a_scale, const Names& a_names, int a_depth, Intents& a_out);

		void DrawSignalDetail(const RecipeRow& a_recipe, const std::string& a_text, FormID a_actorID, std::span<const BoneRow> a_bones, float a_scale, const Names& a_names, int a_depth, Intents& a_out)
		{
			const auto name = ReferenceName(a_text);
			const auto it = std::ranges::find(a_recipe.signals, name, &SignalRow::name);
			if (a_text.empty() || !a_text.starts_with('@') || it == a_recipe.signals.end()) {
				Widgets::Dim("a literal; choose a @signal to tune it here");
				return;
			}
			ImGui::PushID(it->name.c_str());
			ImGui::Text("%s (%s)", ReferenceText(it->name).c_str(), std::string{ SignalKindName(it->kind) }.c_str());
			ImGui::SameLine();
			Widgets::ValueSwatch(it->value);
			DrawSignalEditor(a_recipe.id, *it, SignalNamesOf(a_recipe), a_actorID, a_bones, a_scale, a_names, a_out);
			if (it->inert) {
				Widgets::Problem(it->problem.empty() ? "inert" : "inert: " + it->problem);
			}
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
						DrawSignalModal(a_recipe, read, a_actorID, a_bones, a_scale, a_names, a_depth + 1, a_out);
					}
				}
			}
			ImGui::PopID();
		}

		void DrawSignalModal(const RecipeRow& a_recipe, const std::string& a_name, FormID a_actorID, std::span<const BoneRow> a_bones, float a_scale, const Names& a_names, int a_depth, Intents& a_out)
		{
			ImGui::PushID(a_name.c_str());
			const auto title = std::format("{}###signal-modal-{}", ReferenceText(a_name), a_depth);
			if (ImGui::SmallButton(ReferenceText(a_name).c_str())) {
				ImGui::OpenPopup(title.c_str());
			}
			Widgets::DetailModal(title.c_str(), [&]() { DrawSignalDetail(a_recipe, ReferenceText(a_name), a_actorID, a_bones, a_scale, a_names, a_depth, a_out); });
			ImGui::PopID();
		}

		void DrawFormWithSignals(const char* a_id, std::span<const FormField> a_form, const RecipeRow& a_recipe, FormID a_actorID, std::span<const BoneRow> a_bones, float a_scale, const Names& a_names, Intents& a_out, std::size_t a_columns)
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
				Widgets::DetailModal(title.c_str(), [&]() { DrawSignalDetail(a_recipe, a_form[i].text, a_actorID, a_bones, a_scale, a_names, 0, a_out); });
				ImGui::PopID();
			}
		}

		void DrawDetailModal(FieldDetail a_detail, const Inspector& a_in, const RecipeRow& a_recipe, FormID a_actorID, std::span<const BoneRow> a_bones, const Layout& a_layout, const Names& a_names, Intents& a_out)
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
					DrawRowField("text", CurveTextField(a_in.curve->name, a_in.curve->text), id, scale, a_names, a_out);
				} else {
					Widgets::Dim("no declared curve; the layer's curve is inline or empty");
				}
				break;
			case FieldDetail::kOpacity:
				DrawSignalDetail(a_recipe, a_in.row.opacityText, a_actorID, a_bones, scale, a_names, 0, a_out);
				break;
			case FieldDetail::kColor:
				DrawSignalDetail(a_recipe, a_in.row.color, a_actorID, a_bones, scale, a_names, 0, a_out);
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

		void DrawInspectorFields(const Inspector& a_inspector, const RecipeRow& a_recipe, FormID a_actorID, std::span<const BoneRow> a_bones, const Layout& a_layout, const Names& a_names, Intents& a_out)
		{
			const auto                       form = InspectorForm(a_inspector);
			const std::optional<std::size_t> opened = DrawForm("fields", form, a_recipe.id, a_layout.widgetScale, a_names, a_out);
			const std::optional<FieldDetail> open = opened && *opened < form.size() ? form[*opened].detail : std::nullopt;
			for (const auto detail : { FieldDetail::kSource, FieldDetail::kCurve, FieldDetail::kOpacity, FieldDetail::kColor, FieldDetail::kMask }) {
				const auto title = std::format("{} of layer {}###detail{}", FieldDetailName(detail), a_inspector.layer, static_cast<int>(detail));
				if (open == detail) {
					ImGui::OpenPopup(title.c_str());
				}
				Widgets::DetailModal(title.c_str(), [&]() { DrawDetailModal(detail, a_inspector, a_recipe, a_actorID, a_bones, a_layout, a_names, a_out); });
			}
		}

		std::optional<RecipeKey> DefaultKeyOf(const PieceRow& a_piece)
		{
			const KeyChoice* chosen = nullptr;
			for (const auto& key : a_piece.keys) {
				if (key.key.kind == KeyKind::kArmor) {
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
			return RecipeKeyOf(chosen->key, chosen->text);
		}

		constexpr const char* kTermPayload = "WEPBR_TERM";

		constexpr TableStyle kOffersStyle{ .borders = TableStyle::Borders::kNone, .stretch = true, .headers = true, .rowBackground = false };

		void AddTermOfKind(const TermKind& a_term, const RecipeRow& a_recipe, const GeometryRow& a_geometry, bool a_full, Intents& a_out)
		{
			if (a_full) {
				return;
			}
			const auto& presets = LoadedPresets();
			auto [edits, expression] = BuildTerm(a_term, presets, ExistingOf(a_recipe));
			PostAll(a_out, a_recipe.id, std::move(edits));
			a_out.push_back(AddTerm{ Term{ TermOp::kAnd, std::move(expression), TermLabelOf(a_term, presets, a_geometry), a_term } });
		}

		void DrawOffers(std::span<const TermOffer> a_offers, std::string_view a_filter, const PieceRow& a_piece, const RecipeRow& a_recipe, const GeometryRow& a_geometry, bool a_full, Intents& a_out)
		{
			auto table = Widgets::Table::Begin("offers", { { "geometry", Width::Fit() }, { "kind", Width::Fit() }, { "name", Width::Fit() }, { "description", Width::Fill() }, { "coverage", Width::Fit("coverage") }, { "", Width::Fit("edit") } }, kOffersStyle);
			if (!table.Open()) {
				return;
			}
			int shown = 0;
			for (const auto& offer : a_offers) {
				const auto*            row = RowOf(kOfferGroups, offer.group);
				const std::string_view kind = row ? row->word : std::string_view{ "?" };
				if (!NameMatches(offer.name, a_filter) && !NameMatches(offer.detail, a_filter) && !NameMatches(kind, a_filter)) {
					continue;
				}
				ImGui::PushID(shown++);
				const bool             mask = offer.group == OfferGroup::kMasks;
				const std::string      geometry = offer.geometry.empty() ? std::string{} : GeometryLabel(offer.geometry, a_piece.armorName);
				const std::string_view leading[]{ geometry, kind };
				switch (Widgets::ChooserRow(table, leading, offer.name, offer.detail, offer.coverage, offer.unavailable, mask ? "edit" : nullptr)) {
				case Widgets::ChooserPick::kChosen: {
					const auto from = std::ranges::find(a_recipe.geometries, offer.geometry, &GeometryRow::name);
					AddTermOfKind(offer.kind, a_recipe, from != a_recipe.geometries.end() ? *from : a_geometry, a_full, a_out);
					break;
				}
				case Widgets::ChooserPick::kAction:
					if (const auto it = std::ranges::find(a_recipe.maskRows, offer.name, &TextRow::name); it != a_recipe.maskRows.end()) {
						EditMaskAsRegion(a_recipe, *it, a_out);
					}
					break;
				case Widgets::ChooserPick::kNone:
					break;
				}
				ImGui::PopID();
			}
			table.End();
		}

		[[nodiscard]] Widgets::Table BeginTermTable()
		{
			const Width button = Width::Px(Widgets::RowButtonWidth());
			return Widgets::Table::Begin("terms", { { "#", Width::Fit() }, { "", button }, { "", button }, { "S", button }, { "M", button }, { "op", Width::Fit("and") }, { "term", Width::Fit() }, { "detail", Width::Fill() }, { "", button } }, kLayerStyle);
		}

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
			const bool raw = std::holds_alternative<RawTerm>(term.kind);
			const auto label = raw && term.text.empty() ? std::string{ "(empty)" } : term.label;
			if (ImGui::Selectable(label.c_str(), a_region.selected == a_index)) {
				a_out.push_back(PickTerm{ a_index });
			}
			a_table.Cell();
			ImGui::AlignTextToFramePadding();
			Widgets::Dim(TermDetailOf(term, a_offers));
			a_table.Cell();
			const auto title = std::format("term {}: {}###term-details", a_index, term.label);
			if (Widgets::DetailButton()) {
				ImGui::OpenPopup(title.c_str());
			}
			Widgets::DetailModal(title.c_str(), [&]() { DrawTermDetails(a_index, term, a_recipe, a_geometry, a_layout, a_names, a_out); });
			ImGui::PopID();
		}

		void DrawTermSettings(std::size_t a_index, const Term& a_term, const RecipeRow& a_recipe, const GeometryRow& a_geometry, float a_scale, const Names& a_names, Intents& a_out)
		{
			const auto& presets = LoadedPresets();
			const auto  form = TermForm(a_term.kind, presets, a_geometry);
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
					const std::optional<TermKind> changed = setting.apply ? setting.apply(*text) : std::nullopt;
					if (changed) {
						auto [edits, expression] = BuildTerm(*changed, presets, ExistingOf(a_recipe));
						PostAll(a_out, a_recipe.id, std::move(edits));
						a_out.push_back(SetTermKind{ a_index, *changed, std::move(expression), TermLabelOf(*changed, presets, a_geometry) });
					} else {
						Refuse(setting.field.name, *text);
					}
				}
				ImGui::PopID();
			}
			table.End();
		}

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

			const auto program = term.text.empty() ? std::nullopt : std::optional{ Program::Parse(term.text) };
			if (program && *program && !(*program)->References().empty()) {
				auto reads = Widgets::Table::Begin("term-reads", { { "reads", Width::Fit() }, { "", Width::Px(ImGui::GetFrameHeight()) }, { "definition", Width::Fill() } }, kFormStyle);
				if (reads.Open()) {
					for (const auto& name : (*program)->References()) {
						const PictureRow* image = nullptr;
						for (const auto* list : { &a_geometry.sources, &a_geometry.masks }) {
							const auto it = std::ranges::find(*list, name, &PictureRow::name);
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
							ImGui::TextUnformatted(image->description.c_str());
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

		[[nodiscard]] Widgets::RuleLine RegionRule(std::string_view a_title, const MenuState& a_state, Intents& a_out)
		{
			const float undoWidth = Widgets::ButtonWidth("Undo");
			const float redoWidth = Widgets::ButtonWidth("Redo");
			const float clearWidth = Widgets::ButtonWidth("Clear");
			const float keepWidth = Widgets::ButtonWidth("Keep");
			const float discardWidth = Widgets::ButtonWidth("Discard");
			const float rightWidth = undoWidth + redoWidth + clearWidth + keepWidth + discardWidth + 4.0f * Widgets::ItemSpacingX();
			return Widgets::RuleLine{ a_title, rightWidth, [=, &a_state, &a_out]() {
									 const auto& region = a_state.region;
									 const bool  painting = a_state.paint.has_value();
									 const bool  something = painting && !BuildRegion(region.terms).empty();
									 Widgets::Disabled(a_state.regionHistory.UndoDepth() == 0, [&]() {
										 if (ImGui::Button("Undo", ImVec2{ undoWidth, 0.0f })) {
											 a_out.push_back(UndoRegion{});
										 }
									 });
									 Widgets::Tooltip("the stack as it was before the last change (Ctrl+Z)");
									 ImGui::SameLine();
									 Widgets::Disabled(a_state.regionHistory.RedoDepth() == 0, [&]() {
										 if (ImGui::Button("Redo", ImVec2{ redoWidth, 0.0f })) {
											 a_out.push_back(RedoRegion{});
										 }
									 });
									 Widgets::Tooltip("the change undone (Ctrl+Y)");
									 ImGui::SameLine();
									 Widgets::Disabled(region.terms.empty(), [&]() {
										 if (ImGui::Button("Clear", ImVec2{ clearWidth, 0.0f })) {
											 a_out.push_back(ClearRegion{});
										 }
									 });
									 ImGui::SameLine();
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
												 Post(a_out, std::string{ kPaintRecipe }, SetMask{ std::string{ kScratchMask }, BuildRegion(a_state.region.terms) });
												 a_out.push_back(KeepPaint{ a_state.paint->recipeID, name });
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
								 } };
		}

		void DrawRegionPicture(const PieceRow& a_piece, const RecipeRow& a_recipe, const GeometryRow& a_geometry, const RegionStack& a_region, const Layout& a_layout, Intents& a_out)
		{
			const auto scratch = std::ranges::find(a_geometry.masks, kScratchMask, &PictureRow::name);
			if (scratch == a_geometry.masks.end()) {
				return;
			}
			if (!scratch->problem.empty()) {
				Widgets::Problem(scratch->problem);
			}
			if (a_recipe.geometries.size() < 2) {
				Widgets::Thumbnail(scratch->texture, scratch->channel, scratch->animated, a_layout.compositeSize);
			} else {
				if (Widgets::ThumbnailButton("region", scratch->texture, scratch->channel, scratch->animated, a_layout.compositeSize)) {
					if (const auto next = NextGeometry(a_recipe, a_geometry)) {
						a_out.push_back(*next);
					}
				}
				Widgets::Tooltip(std::format("viewed on {} (one of {} geometries; the region applies to all)\nclick: view the next geometry", GeometryLabel(a_geometry.name, a_piece.armorName), a_recipe.geometries.size()));
			}
			ImGui::SameLine();
			ImGui::BeginGroup();
			const std::size_t shown = a_region.solo ? 1 : a_region.terms.size() - a_region.muted.size();
			Widgets::Dim(std::format("region of {} term{}, {} shown, {}", a_region.terms.size(), a_region.terms.size() == 1 ? "" : "s", shown, scratch->animated ? "animated" : "static"));
			ImGui::EndGroup();
		}

		void DrawRegionStack(const PieceRow& a_piece, const RecipeRow& a_recipe, const GeometryRow& a_geometry, MenuState& a_state, const Names& a_names, Intents& a_out)
		{
			const auto& region = a_state.region;
			const auto  offers = OffersOfRecipe(LoadedPresets(), a_recipe, region.editing);
			DrawRegionPicture(a_piece, a_recipe, a_geometry, region, a_state.layout, a_out);
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

			std::string_view filter;
			const char*      hint = "filter by kind, name or measurement";
			const float      filterWidth = Widgets::FitWidth(hint) * a_state.layout.widgetScale;
			Widgets::Rule({}, Widgets::RuleLine{ "Terms", filterWidth, [&]() { filter = Widgets::LiveTextField("offer-filter", hint, Width::Px(Widgets::FitWidth(hint)), a_state.layout.widgetScale); } });
			if (offers.empty()) {
				Widgets::Dim(a_geometry.meshRead ? "nothing to offer on this geometry" : "reading the mesh");
			}
			DrawOffers(offers, filter, a_piece, a_recipe, a_geometry, region.terms.size() >= kMaxTerms, a_out);
		}

		void DrawPaintHead(const PieceRow& a_piece, const RecipeRow& a_recipe, const MenuState& a_state, Intents& a_out)
		{
			if (a_state.paint) {
				Widgets::HeldLabel(a_state.paint->recipeID.c_str());
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
				Widgets::NextItemWidth(Width::Fit(RecipeLabel(a_recipe)));
				RecipeCombo(a_piece, a_recipe, {}, "##recipe", a_out);
			}
		}

		void RebuildScratch(MenuState& a_state, const RecipeRow* a_recipe, const Snapshot& a_snapshot)
		{
			auto& region = a_state.region;
			if (!region.dirty || !a_state.paint || !a_recipe || a_recipe->id != kPaintRecipe) {
				return;
			}
			Intents intents;
			for (auto& edit : ScratchEdits(region.terms, region.solo, region.muted, ScratchOf(*a_recipe))) {
				Post(intents, a_recipe->id, std::move(edit));
			}
			intents.push_back(ScratchRebuilt{});
			Dispatch(intents, a_state, a_snapshot);
		}

		void EditMaskAsRegion(const RecipeRow& a_recipe, const TextRow& a_mask, Intents& a_out)
		{
			const std::string label = TermLabel(a_mask.text, LoadedPresets(), ExistingOf(a_recipe));
			a_out.push_back(LoadRegion{ { Term{ TermOp::kSet, a_mask.text, label, RawTerm{} } }, a_mask.name });
			a_out.push_back(SetMode{ Mode::kPaint });
		}

		void DrawBody(const Snapshot& a_snapshot, const PieceRow* a_piece, const RecipeRow* a_recipe, const GeometryRow* a_geometry, MenuState& a_state, Intents& a_out)
		{
			if (!a_piece || !a_recipe) {
				Widgets::Dim(a_state.paint ? "starting the paint recipe" : "nothing applied; equip enchanted PBR armor or press Re-apply all on the Recipes page");
				return;
			}
			if (!a_geometry) {
				ImGui::PushID(a_recipe->id.c_str());
				if (a_state.layout.contextRows) {
					DrawRecipeContext(a_snapshot, *a_piece, *a_recipe, a_out);
				} else {
					DrawPaintHead(*a_piece, *a_recipe, a_state, a_out);
				}
				Widgets::Rule();
				Widgets::Dim(std::format("recipe {} is bound to no geometry of this piece: its keys or selectors match none of its geometries", a_recipe->id));
				ImGui::PopID();
				return;
			}
			if (!a_state.layout.implemented) {
				ImGui::Text("%s mode is not built yet", std::string{ ModeName(a_state.mode) }.c_str());
				return;
			}
			const auto& view = a_snapshot.view;
			const auto& selection = a_state.selection;
			Layout&     layout = a_state.layout;
			const float scale = layout.widgetScale;
			ImGui::PushID(a_recipe->id.c_str());

			const auto  board = BuildBoard(*a_recipe, *a_geometry, selection, view);
			const auto  names = NamesOf(*a_recipe, *a_geometry);
			const auto  pane = ChoosePane(selection.target, a_state.settings);
			const Cell* picked = layout.contextRows ? DrawContext(a_snapshot, board, *a_piece, *a_recipe, *a_geometry, selection, a_out) : nullptr;
			const bool painting = layout.regionEditor;
			const bool painterReady = painting && a_state.paint && a_recipe->id == kPaintRecipe;
			if (!layout.contextRows) {
				DrawPaintHead(*a_piece, *a_recipe, a_state, a_out);
				Widgets::Rule();
				if (!a_state.paint) {
					const RecipeRow* active = a_recipe;
					if (view.Isolating() && view.isolateRecipe != kPaintRecipe) {
						const auto it = std::ranges::find(a_piece->recipes, view.isolateRecipe, &RecipeRow::id);
						if (it != a_piece->recipes.end() && it->id != a_recipe->id) {
							active = &*it;
							a_out.push_back(PickRecipe{ it->id });
						}
					}
					if (const auto key = DefaultKeyOf(*a_piece)) {
						a_out.push_back(BeginPaint{ active->id, *key, Surface::kMaterial });
					} else {
						Widgets::Warn("the piece offers no key to paint on");
					}
				} else if (painterReady) {
					for (const auto& geometry : a_recipe->geometries) {
						if (!a_state.paint->readGeometries.contains(geometry.name)) {
							a_out.push_back(ReadMesh{ a_piece->ref.actorID, geometry.name });
						}
					}
				} else {
					Widgets::Dim("starting the paint recipe");
				}
			}
			const bool resources = layout.signals;
			if (painting) {
				const std::string title = a_state.region.editing.empty() ? std::string{ "Region" } : std::format("Region: {}", a_state.region.editing);
				Widgets::Rule({}, RegionRule(title, a_state, a_out));
			} else {
				const std::string_view title = pane.settings ? (selection.target == Target::kLight ? "Light settings" : "Shell settings") : "Stack";
				Widgets::Rule({}, PaneRule(title, pane, board, *a_recipe, selection.target, a_out));
			}
			const float under = ImGui::GetContentRegionAvail().y;
			const float resourcesHeight = resources ? under * layout.resourcesShare : 0.0f;
			const float stackHeight = resources ? -(resourcesHeight + Widgets::RuleHeight()) : 0.0f;

			if (ImGui::BeginChild("stack-pane", ImVec2{ 0.0f, stackHeight }, 0, 0)) {
				if (painting) {
					if (painterReady) {
						DrawRegionStack(*a_piece, *a_recipe, *a_geometry, a_state, names, a_out);
					}
				} else if (pane.settings) {
					if (selection.target == Target::kLight) {
						if (a_recipe->lightRow.present) {
							DrawFormWithSignals("light", LightForm(a_recipe->lightRow, SignalNamesOf(*a_recipe)), *a_recipe, a_piece->ref.actorID, a_geometry->bones, scale, names, a_out, kSettingsColumns);
						} else {
							Widgets::Dim("the recipe has no light");
						}
					} else {
						DrawFormWithSignals("shell", ShellForm(a_recipe->shellRow, SignalNamesOf(*a_recipe)), *a_recipe, a_piece->ref.actorID, a_geometry->bones, scale, names, a_out, kSettingsColumns);
					}
				} else if (picked && picked->output) {
					const auto stack = BuildStackView(*a_piece, *a_recipe, *a_geometry, selection, view);
					const auto inspector = layout.inspector ? BuildInspector(*a_recipe, *a_geometry, selection) : std::nullopt;
					DrawStack(stack, inspector, *a_piece, *a_recipe, *a_geometry, layout, names, a_out);
				}
			}
			ImGui::EndChild();

			if (resources) {
				std::string_view filter;
				Widgets::Rule({}, ResourcesRule(a_state.resource, *a_recipe, scale, filter, a_out));
				DrawResources(*a_piece, *a_recipe, *a_geometry, layout, a_state.resource, filter, names, a_out);
			}
			ImGui::PopID();
		}

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

		void HistoryKeys(const RecipeRow* a_recipe, const MenuState& a_state, Intents& a_out)
		{
			const auto* io = ImGui::GetIO();
			if (!a_recipe || !io || !io->KeyCtrl || a_state.activeField != kNoField) {
				return;
			}
			const bool undo = ImGui::IsKeyPressed(ImGuiMCP::ImGuiKey_Z, false);
			const bool redo = ImGui::IsKeyPressed(ImGuiMCP::ImGuiKey_Y, false);
			if (a_state.paint) {
				if (undo) {
					a_out.push_back(UndoRegion{});
				}
				if (redo) {
					a_out.push_back(RedoRegion{});
				}
				return;
			}
			if (undo) {
				a_out.push_back(Undo{ a_recipe->id });
			}
			if (redo) {
				a_out.push_back(Redo{ a_recipe->id });
			}
		}
	}

	void __stdcall RenderStudio()
	{
		auto* manager = Manager::GetSingleton();
		auto& state = State();
		manager->Watch(RequestOf(state.selection));
		const auto  held = manager->LatestSnapshot();
		const auto& snapshot = *held;
		ResolveSelection(state.selection, snapshot);
		Intents     intents;

		Mode mode = state.mode;
		if (Widgets::ModeBar(mode, state.modeDrawn)) {
			intents.push_back(SetMode{ mode });
		}

		const auto* piece = SelectedPiece(snapshot, state.selection);
		const auto* recipe = SelectedRecipe(piece, state.selection);
		const auto* geometry = SelectedGeometry(recipe, state.selection);

		const float footer = Widgets::RuleHeight() + ImGui::GetFrameHeightWithSpacing() * 2.0f + 8.0f;
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
