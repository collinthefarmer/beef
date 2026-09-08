// The studio's view models: layouts per mode, selection defaults, the board
// from the canonical recipe, the stack view, the inspector, the signal split.
// The snapshot is built from schema/example-magicka.json as the manager
// would fill it, with no textures and no runtime verdicts.

#include "Signals.h"
#include "EditCheck.h"
#include "Expression.h"
#include "Forms.h"
#include "History.h"
#include "MenuState.h"
#include "MaskStack.h"
#include "Regions.h"
#include "Studio.h"
#include "test_support.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <string>

using namespace WornEnchantmentPBR;
using namespace WornEnchantmentPBR::Studio;
using test::Check;

namespace
{
	constexpr FormID kPlayer = 0x14;
	constexpr FormID kCuirass = 0x1001;
	constexpr const char* kGeometry = "Cuirass";
	constexpr const char* kRecipeID = "example-magicka";

	std::string KindName(const Signal& a_signal)
	{
		return Match(
			a_signal.kind,
			[](const ConstantSignal&) { return "constant"; }, [](const PulseSignal&) { return "pulse"; }, [](const RampSignal&) { return "ramp"; },
			[](const EfshSignal&) { return "efsh"; }, [](const ActorValueSignal&) { return "av"; }, [](const ActorStateSignal&) { return "actorState"; },
			[](const EnchantmentSignal&) { return "enchantment"; }, [](const TriggerSignal&) { return "trigger"; }, [](const PayloadSignal&) { return "payload"; },
			[](const CounterSignal&) { return "counter"; }, [](const AccumulateSignal&) { return "accumulate"; }, [](const NoiseSignal&) { return "noise"; },
			[](const GradientSignal&) { return "gradient"; }, [](const DeltaSignal&) { return "delta"; }, [](const SmoothSignal&) { return "smooth"; },
			[](const ExprSignal&) { return "expr"; });
	}

	std::vector<SignalRow> SignalRows(const Recipe& a_recipe)
	{
		const auto             graph = SignalGraph::Compile(a_recipe.signals, a_recipe.curves);
		const auto             counts = CountReferences(a_recipe);
		std::vector<SignalRow> rows;
		for (const auto& signal : a_recipe.signals) {
			SignalRow row;
			row.name = signal.name;
			row.kind = KindName(signal);
			row.type = graph.TypeOf(signal.name).value_or(ValueType::kScalar);
			if (const auto* constant = Get<ConstantSignal>(signal.kind)) {
				row.constant = constant->value;
				row.value = constant->value;
			}
			if (const auto* expr = Get<ExprSignal>(signal.kind)) {
				row.text = expr->text;
			}
			row.curve = signal.curve ? signal.curve->text : "";
			if (const auto count = counts.signals.find(signal.name); count != counts.signals.end()) {
				row.references = count->second;
			}
			rows.push_back(std::move(row));
		}
		return rows;
	}

	std::vector<ScalarRow> ScalarRows(const MaterialOutput& a_output)
	{
		std::vector<ScalarRow> rows;
		for (const auto field : ScalarsOf(a_output.slot)) {
			if (field == ScalarField::kColor) {
				if (a_output.scalars.color) {
					rows.push_back({ std::string{ ScalarFieldName(field) }, Vec3{ 1.0f, 1.0f, 1.0f }, Vec3ParamText(*a_output.scalars.color) });
				}
				continue;
			}
			const auto* param = ScalarOf(a_output.scalars, field);
			if (param && *param) {
				rows.push_back({ std::string{ ScalarFieldName(field) }, 1.0f, ParamText(**param) });
			}
		}
		return rows;
	}

	LayerRow ToRow(const Layer& a_layer)
	{
		LayerRow row;
		row.source = LayerSourceText(a_layer.source);
		row.mask = a_layer.mask ? ReferenceText(a_layer.mask->name) : "";
		row.blend = std::string{ BlendName(a_layer.blend) };
		row.opacity = Get<float>(a_layer.opacity) ? *Get<float>(a_layer.opacity) : 1.0f;
		row.opacityText = ParamText(a_layer.opacity);
		row.color = a_layer.color ? Vec3ParamText(*a_layer.color) : "";
		row.curve = a_layer.curve ? a_layer.curve->text : "";
		row.channels = a_layer.channels.ToString();
		return row;
	}

	OutputRow ToRow(std::size_t a_index, const Output& a_output)
	{
		OutputRow row;
		row.index = a_index;
		if (const auto* material = Get<MaterialOutput>(a_output)) {
			row.target = material->surface == Surface::kShell ? "shell" : "material";
			row.surface = material->surface;
			row.slot = material->slot;
			row.slotName = std::string{ SlotName(material->slot) };
			row.replace = material->replace;
			row.scalars = ScalarRows(*material);
			for (const auto& layer : material->stack) {
				row.layers.push_back(ToRow(layer));
			}
		} else {
			row.target = "light";
			row.light = true;
		}
		return row;
	}

	GeometryRow GeometryOf(const Recipe& a_recipe, const char* a_name)
	{
		GeometryRow geometry;
		geometry.name = a_name;
		geometry.shell = a_recipe.shell.material == ShellMaterial::kVanilla ? "vanilla shell" : "pbr copy shell";
		for (const auto& source : a_recipe.sources) {
			geometry.sources.push_back({ source.name, DescribeSource(source.kind), SourceType(source), nullptr, ShaderChannel::kRgb, false, "" });
		}
		for (const auto& mask : a_recipe.masks) {
			geometry.masks.push_back({ mask.name, mask.text, ValueType::kScalar, nullptr, ShaderChannel::kLuma, false, "" });
		}
		for (std::size_t i = 0; i < a_recipe.outputs.size(); ++i) {
			geometry.outputs.push_back(ToRow(i, a_recipe.outputs[i]));
		}
		return geometry;
	}

	RecipeRow RecipeOf(const Recipe& a_recipe, int a_priority)
	{
		RecipeRow row;
		row.id = a_recipe.id;
		row.key = a_recipe.keys.empty() ? "" : a_recipe.keys.front().ToString();
		row.priority = a_priority;
		row.shellMaterial = a_recipe.shell.material;
		row.signals = SignalRows(a_recipe);
		for (const auto& curve : a_recipe.curves) {
			row.curves.push_back({ curve.name, curve.text });
		}
		for (const auto& mask : a_recipe.masks) {
			row.masks.push_back(mask.name);
		}
		row.geometries.push_back(GeometryOf(a_recipe, kGeometry));
		row.lightRow = LightRowOf(a_recipe);
		row.shellRow = ShellRowOf(a_recipe);
		const auto counts = CountReferences(a_recipe);
		for (const auto& mask : a_recipe.masks) {
			const auto count = counts.images.find(mask.name);
			row.maskRows.push_back({ mask.name, mask.text, count != counts.images.end() ? count->second : 0 });
		}
		for (const auto& source : a_recipe.sources) {
			const auto count = counts.images.find(source.name);
			row.sourceRows.push_back(SourceRowOf(source, count != counts.images.end() ? count->second : 0));
		}
		for (std::size_t i = 0; i < a_recipe.outputs.size(); ++i) {
			if (Is<LightOutput>(a_recipe.outputs[i])) {
				row.lightOutput = i;
				row.light = "point light on 2 bones";
			}
		}
		return row;
	}

	// A neighbour recipe writing one layer on the shell's emissive slot, so
	// the stack shows foreign rows.
	RecipeRow Neighbour(const char* a_id, int a_priority)
	{
		Recipe recipe;
		recipe.id = a_id;
		MaterialOutput output;
		output.surface = Surface::kShell;
		output.slot = Slot::kEmissive;
		output.scalars.strength = 1.0f;
		Layer layer;
		layer.source = Vec3{ 1.0f, 0.0f, 0.0f };
		output.stack.push_back(layer);
		recipe.outputs.push_back(output);
		return RecipeOf(recipe, a_priority);
	}

	std::optional<Recipe> Canonical()
	{
		const auto path = std::filesystem::path{ WEPBR_FIXTURES_DIR }.parent_path().parent_path() / "schema" / "example-magicka.json";
		const auto text = test::ReadFile(path);
		Check(!text.empty(), "schema/example-magicka.json is readable at " + path.string());
		auto loaded = ParseRecipe(text, kRecipeID);
		Check(loaded.recipe.has_value(), "the canonical file parses");
		return loaded.recipe;
	}

	// The player's cuirass: a lower neighbour, the canonical recipe, a higher neighbour.
	Snapshot SnapshotOf(const Recipe& a_recipe)
	{
		PieceRow piece;
		piece.actorID = kPlayer;
		piece.actorName = "Player";
		piece.armorID = kCuirass;
		piece.armorName = "Ebony Cuirass";
		piece.recipes.push_back(Neighbour("lower", 20));
		piece.recipes.push_back(RecipeOf(a_recipe, 40));
		piece.recipes.push_back(Neighbour("higher", 60));
		Snapshot snapshot;
		snapshot.pieces.push_back(std::move(piece));
		return snapshot;
	}

	void Pick(Selection& a_selection, Target a_target, std::optional<Slot> a_slot = std::nullopt, std::optional<std::size_t> a_layer = std::nullopt)
	{
		a_selection.target = a_target;
		a_selection.slot = a_slot;
		a_selection.layer = a_layer;
	}

	Selection SelectCanonical()
	{
		Selection selection;
		selection.actorID = kPlayer;
		selection.armorID = kCuirass;
		selection.recipeID = kRecipeID;
		selection.geometry = kGeometry;
		return selection;
	}

	void Layouts()
	{
		const auto compose = LayoutFor(Mode::kCompose);
		Check(compose.mode == Mode::kCompose && compose.stack && compose.inspector && compose.signals && !compose.regionEditor && !compose.designPanel, "compose shows stack, inspector and the signal table");
		Check(compose.widgetScale == 1.0f && compose.compositeSize == 160.0f && compose.cellSize == 40.0f && compose.rowThumbnail == 32.0f && compose.inspectorThumbnail == 96.0f, "compose at scale 1 with pictures sized for one column");
		Check(compose.developerSignals && compose.stackSplit == 0.5f, "compose shows developer signals and splits the stack evenly");
		const auto paint = LayoutFor(Mode::kPaint);
		Check(paint.stack && paint.inspector && !paint.signals && !paint.contextRows && paint.regionEditor && !paint.designPanel, "paint adds the region editor, drops the context rows and the resources pane (they would edit the paint recipe)");
		const auto design = LayoutFor(Mode::kDesign);
		Check(!design.stack && !design.inspector && !design.signals && !design.regionEditor && design.designPanel, "design shows the design panel only");
		Check(design.widgetScale == 1.6f && design.compositeSize == 128.0f && !design.developerSignals, "design at scale 1.6, 128 px composite, developer signals off");
		Check(kModes.size() == 3 && kModes[0] == Mode::kCompose && kModes[1] == Mode::kPaint && kModes[2] == Mode::kDesign, "modes are Compose, Paint, Design");
		Check(ModeName(Mode::kCompose) == "Compose" && ModeName(Mode::kPaint) == "Paint" && ModeName(Mode::kDesign) == "Design", "mode names");
	}

	void Selections(const Snapshot& a_snapshot)
	{
		const Selection none;
		const auto*     piece = SelectedPiece(a_snapshot, none);
		Check(piece && piece->armorID == kCuirass, "an unset selection yields the first piece");
		const auto* recipe = SelectedRecipe(piece, none);
		Check(recipe && recipe->id == "higher", "an unset recipe yields the last (highest priority)");
		const auto* geometry = SelectedGeometry(recipe, none);
		Check(geometry && geometry->name == kGeometry, "an unset geometry yields the first");
		Check(SelectedOutput(geometry, none) == nullptr, "an unset output yields none");
		Check(TargetName(Target::kMaterial) == "material" && TargetName(Target::kShell) == "shell" && TargetName(Target::kLight) == "light", "a target's word is its surface's, or light");
		Check(TargetOf(Surface::kShell) == Target::kShell && TargetOf(Surface::kMaterial) == Target::kMaterial && SurfaceOf(Target::kLight) == Surface::kMaterial, "surface and target convert both ways; the light reads as the material");

		// The snapshot request names the selected piece; none when nothing is selected.
		Check(!RequestOf(none), "no piece selected: nothing to watch");
		const auto request = RequestOf(SelectCanonical());
		Check(request && request->actorID == kPlayer && request->armorID == kCuirass && !request->firstPerson, "the selected piece is what the tick builds full rows for");

		// The layer selection is clamped to the picked stack after every dispatch.
		{
			Selection stale = SelectCanonical();
			Pick(stale, Target::kShell, Slot::kEmissive, 99);
			ClampSelection(stale, a_snapshot);
			Check(!stale.layer, "a layer past the stack's end is dropped");
			Selection fine = SelectCanonical();
			Pick(fine, Target::kShell, Slot::kEmissive, 0);
			ClampSelection(fine, a_snapshot);
			Check(fine.layer == 0, "a layer within the stack stays");
			Selection light = SelectCanonical();
			Pick(light, Target::kLight, std::nullopt, 5);
			ClampSelection(light, a_snapshot);
			Check(light.layer == 5, "the light target has no stack to clamp against");
			Selection empty = SelectCanonical();
			Pick(empty, Target::kMaterial, Slot::kDiffuse, 0);
			ClampSelection(empty, a_snapshot);
			Check(!empty.layer, "a slot with no output drops the layer");
			Selection light_rows = SelectCanonical();
			Pick(light_rows, Target::kShell, Slot::kEmissive, 99);
			Snapshot bare;
			ClampSelection(light_rows, bare);
			Check(light_rows.layer == 99, "an empty snapshot (light rows) leaves the selection alone");
		}

		auto chosen = SelectCanonical();
		Pick(chosen, Target::kShell, Slot::kFuzz);
		piece = SelectedPiece(a_snapshot, chosen);
		recipe = SelectedRecipe(piece, chosen);
		Check(recipe && recipe->id == kRecipeID, "a recipe is chosen by id");
		geometry = SelectedGeometry(recipe, chosen);
		const auto* output = SelectedOutput(geometry, chosen);
		Check(output && output->index == 1 && output->slot == Slot::kFuzz, "an output is found by its target and slot");
		Pick(chosen, Target::kMaterial, Slot::kFuzz);
		Check(SelectedOutput(geometry, chosen) == nullptr, "the same slot on the other surface yields none");
		Pick(chosen, Target::kShell, Slot::kGlint);
		Check(SelectedOutput(geometry, chosen) == nullptr, "a slot nothing writes yields none");
		Pick(chosen, Target::kLight, Slot::kEmissive);
		Check(SelectedOutput(geometry, chosen) == nullptr, "the light target names no stack");
		Pick(chosen, Target::kShell);
		Check(SelectedOutput(geometry, chosen) == nullptr, "no slot picked yields none");
		chosen.recipeID = "missing";
		chosen.geometry = "missing";
		recipe = SelectedRecipe(piece, chosen);
		Check(recipe && recipe->id == "higher" && SelectedGeometry(recipe, chosen) && SelectedGeometry(recipe, chosen)->name == kGeometry, "unknown names fall back to the defaults");

		const Snapshot empty;
		Check(SelectedPiece(empty, none) == nullptr && SelectedRecipe(nullptr, none) == nullptr && SelectedGeometry(nullptr, none) == nullptr && SelectedOutput(nullptr, none) == nullptr, "an empty snapshot yields nulls");
	}

	void BoardCells(const RecipeRow& a_recipe, const GeometryRow& a_geometry)
	{
		const View view;
		auto       selection = SelectCanonical();
		const auto board = BuildBoard(a_recipe, a_geometry, selection, view);
		Check(board.cells.size() == kSlotCount * 2, "the board has a material and a shell cell per slot");
		bool slotMajor = true;
		for (std::size_t i = 0; i < board.cells.size(); ++i) {
			const auto& cell = board.cells[i];
			slotMajor = slotMajor && cell.slot == static_cast<Slot>(i / 2) && cell.surface == (i % 2 == 0 ? Surface::kMaterial : Surface::kShell);
		}
		Check(slotMajor, "cells are slot-major, material before shell");

		const auto* emissive = CellAt(board, Surface::kShell, Slot::kEmissive);
		Check(emissive && emissive->state == CellState::kWritten && emissive->output == 0 && emissive->layers == 3, "shell emissive is written by output 0 with three layers");
		Check(emissive && emissive->badges == std::vector<std::string>{ "metal" }, "its badge is the metal mask");
		Check(emissive && emissive->layersInRegion == 3, "with no region every layer is in");
		Check(emissive && emissive->scalars.size() == 1 && emissive->scalars[0].name == "strength" && emissive->scalars[0].text == "@glowLevel", "the emissive cell carries its strength scalar");
		Check(emissive && !emissive->isolated && !emissive->replace, "not isolated, not replacing");
		const auto* fuzz = CellAt(board, Surface::kShell, Slot::kFuzz);
		Check(fuzz && fuzz->state == CellState::kWritten && fuzz->output == 1 && fuzz->layers == 2 && fuzz->badges.empty(), "shell fuzz is written by output 1 without badges");
		const auto* height = CellAt(board, Surface::kMaterial, Slot::kHeight);
		const auto* rmaos = CellAt(board, Surface::kMaterial, Slot::kRmaos);
		Check(height && height->state == CellState::kWritten && height->output == 2 && rmaos && rmaos->state == CellState::kWritten && rmaos->output == 3, "material height and rmaos are written");
		const auto* materialEmissive = CellAt(board, Surface::kMaterial, Slot::kEmissive);
		Check(materialEmissive && materialEmissive->state == CellState::kEmpty && !materialEmissive->output, "material emissive is empty");
		const auto* shellCoat = CellAt(board, Surface::kShell, Slot::kCoat);
		const auto* shellSubsurface = CellAt(board, Surface::kShell, Slot::kSubsurface);
		const auto* shellGlint = CellAt(board, Surface::kShell, Slot::kGlint);
		Check(shellCoat && shellCoat->state == CellState::kExcluded && shellSubsurface && shellSubsurface->state == CellState::kExcluded && shellGlint && shellGlint->state == CellState::kExcluded, "shell coat, subsurface and glint are excluded by fuzz");
		Check(shellCoat && shellCoat->reason.find("fuzz") != std::string::npos && shellCoat->reason.find("output 1") != std::string::npos, "the exclusion names the slot and output");
		const auto* materialCoat = CellAt(board, Surface::kMaterial, Slot::kCoat);
		Check(materialCoat && materialCoat->state == CellState::kEmpty, "an exclusion on the shell does not reach the material");
		Check(CellAt(board, Surface::kShell, Slot::kDiffuse) && CellAt(board, Surface::kShell, Slot::kDiffuse)->state == CellState::kEmpty, "a PBR-copy shell offers diffuse");

		Check(board.light.present && board.light.output == 4 && board.light.description == "point light on 2 bones" && !board.light.isolated, "the light cell comes from the recipe's light output");
		Check(board.regions == std::vector<std::string>{ "metal" } && board.region.empty(), "regions are the recipe's masks; whole piece selected");
		Check(board.shell == a_geometry.shell, "the board carries the shell description");

		selection.region = "metal";
		const auto lens = BuildBoard(a_recipe, a_geometry, selection, view);
		const auto* lensed = CellAt(board, Surface::kShell, Slot::kEmissive);
		const auto* lensedEmissive = CellAt(lens, Surface::kShell, Slot::kEmissive);
		Check(lensed && lensedEmissive && lensedEmissive->layersInRegion == 1 && lensedEmissive->layers == 3 && lens.region == "metal", "with the metal region one of three emissive layers is in");
		const auto* lensedFuzz = CellAt(lens, Surface::kShell, Slot::kFuzz);
		Check(lensedFuzz && lensedFuzz->layersInRegion == 0, "no fuzz layer is in the metal region");
	}

	void BoardStates(const RecipeRow& a_recipe, const GeometryRow& a_geometry)
	{
		const auto selection = SelectCanonical();

		// A vanilla shell offers emissive only; adding an output is what makes
		// a shell, so a geometry without one is still offered the column.
		auto vanilla = a_recipe;
		vanilla.shellMaterial = ShellMaterial::kVanilla;
		auto bare = a_geometry;
		bare.shell.clear();
		const View view;
		const auto board = BuildBoard(vanilla, bare, selection, view);
		const auto* shellDiffuse = CellAt(board, Surface::kShell, Slot::kDiffuse);
		const auto* shellEmissive = CellAt(board, Surface::kShell, Slot::kEmissive);
		const auto* shellFuzz = CellAt(board, Surface::kShell, Slot::kFuzz);
		Check(shellDiffuse && shellDiffuse->state == CellState::kAbsent && shellDiffuse->reason.empty(), "a vanilla shell has no diffuse cell");
		Check(shellEmissive && shellEmissive->state == CellState::kWritten, "a vanilla shell still offers emissive, written here");
		Check(shellFuzz && shellFuzz->state == CellState::kAbsent, "a vanilla shell has no fuzz cell even with an output on it");
		Check(CellAt(board, Surface::kMaterial, Slot::kDiffuse) && CellAt(board, Surface::kMaterial, Slot::kDiffuse)->state == CellState::kEmpty, "the material still offers every slot");
		Check(board.shell.empty(), "no shell description when the geometry has none");

		// The binding's refusal makes the cell refused with its reason.
		auto refused = a_geometry;
		refused.outputs[1].problem = "material already has coat";
		const auto refusedBoard = BuildBoard(a_recipe, refused, selection, view);
		const auto* fuzz = CellAt(refusedBoard, Surface::kShell, Slot::kFuzz);
		Check(fuzz && fuzz->state == CellState::kRefused && fuzz->reason == "material already has coat" && fuzz->output == 1 && fuzz->layers == 2, "a refused output shows its problem and still its stack");

		// A second output on the same cell: the first wins and the duplicate is noted.
		auto duplicate = a_geometry;
		auto twin = a_geometry.outputs[0];
		twin.index = 5;
		twin.layers.clear();
		duplicate.outputs.push_back(twin);
		const auto duplicateBoard = BuildBoard(a_recipe, duplicate, selection, view);
		const auto* emissive = CellAt(duplicateBoard, Surface::kShell, Slot::kEmissive);
		Check(emissive && emissive->state == CellState::kWritten && emissive->output == 0 && emissive->layers == 3 && emissive->reason.find("output 5") != std::string::npos, "a duplicate output is noted; the first one is shown");

		// Isolate follows the view.
		View isolate;
		isolate.isolateRecipe = a_recipe.id;
		isolate.isolateOutput = 2;
		const auto isolatedBoard = BuildBoard(a_recipe, a_geometry, selection, isolate);
		const auto* height = CellAt(isolatedBoard, Surface::kMaterial, Slot::kHeight);
		const auto* rmaos = CellAt(isolatedBoard, Surface::kMaterial, Slot::kRmaos);
		Check(height && height->isolated && rmaos && !rmaos->isolated, "the isolated output's cell is marked");
		isolate.isolateOutput = 4;
		Check(BuildBoard(a_recipe, a_geometry, selection, isolate).light.isolated, "the light cell is marked when isolated");
		isolate.isolateRecipe = "other";
		Check(!BuildBoard(a_recipe, a_geometry, selection, isolate).light.isolated, "isolating another recipe marks nothing here");

		Check(CellAt(board, Surface::kShell, static_cast<Slot>(99)) == nullptr, "an unknown slot has no cell");
		Check(CellAt(Board{}, Surface::kMaterial, Slot::kEmissive) == nullptr, "an empty board has no cell");

		// An output without layers or scalars, on a geometry with no outputs at all.
		GeometryRow empty;
		empty.name = kGeometry;
		const auto emptyBoard = BuildBoard(a_recipe, empty, selection, view);
		Check(std::ranges::all_of(emptyBoard.cells, [](const Cell& a_cell) { return a_cell.state == CellState::kEmpty; }), "a geometry without outputs offers every cell");
		Check(emptyBoard.light.present && emptyBoard.light.output == 4, "the light cell still comes from the recipe row");
	}

	void Stacks(const PieceRow& a_piece, const RecipeRow& a_recipe, const GeometryRow& a_geometry)
	{
		const View view;
		auto       selection = SelectCanonical();
		Check(!BuildStackView(a_piece, a_recipe, a_geometry, selection, view), "no selected output, no stack");
		Pick(selection, Target::kLight);
		Check(!BuildStackView(a_piece, a_recipe, a_geometry, selection, view), "a light has no stack");
		Pick(selection, Target::kMaterial, Slot::kGlint);
		Check(!BuildStackView(a_piece, a_recipe, a_geometry, selection, view), "an unwritten slot has no stack");

		Pick(selection, Target::kShell, Slot::kEmissive, 1);
		const auto stack = BuildStackView(a_piece, a_recipe, a_geometry, selection, view);
		Check(stack.has_value(), "the emissive stack builds");
		if (!stack) {
			return;
		}
		Check(stack->output == 0 && stack->surface == Surface::kShell && stack->slot == Slot::kEmissive, "the stack names its cell");
		Check(stack->rows.size() == 3 && stack->rows[0].index == 0 && stack->rows[2].index == 2 && stack->rows[0].layer.source == "@fill" && stack->rows[2].layer.source == "@stepRing", "rows are in file order, base first");
		Check(stack->rows[1].selected && !stack->rows[0].selected && !stack->rows[2].selected, "the selected layer is marked");
		Check(std::ranges::all_of(stack->rows, [](const StackRow& a_row) { return a_row.inRegion && !a_row.muted && !a_row.soloed; }), "with no region every row is in, none muted or soloed");
		Check(stack->below.size() == 1 && stack->below[0].recipe == "lower" && stack->below[0].priority == 20 && stack->below[0].layer.source == "1, 0, 0", "the lower neighbour's layer sits below");
		Check(stack->above.size() == 1 && stack->above[0].recipe == "higher" && stack->above[0].priority == 60, "the higher neighbour's layer sits above");
		Check(stack->blends.size() == 6 && std::ranges::find(stack->blends, Blend::kNormal) == stack->blends.end(), "emissive takes every blend but normal");
		Check(stack->scalars.size() == 1 && stack->scalars[0].name == "strength", "the stack carries the slot's scalars");
		Check(!stack->isolated && stack->problem.empty() && stack->composite == nullptr, "not isolated, no problem, no texture");

		selection.region = "metal";
		const auto lensed = BuildStackView(a_piece, a_recipe, a_geometry, selection, view);
		Check(lensed && lensed->rows.size() == 3 && lensed->rows[0].inRegion && !lensed->rows[1].inRegion && !lensed->rows[2].inRegion, "the metal region keeps the fill layer in and the rings out");

		View filtered;
		filtered.muted.insert(LayerKey{ a_recipe.id, 0, 2 });
		filtered.isolateRecipe = a_recipe.id;
		filtered.isolateOutput = 0;
		filtered.isolateLayer = 1;
		selection.region.clear();
		const auto soloed = BuildStackView(a_piece, a_recipe, a_geometry, selection, filtered);
		Check(soloed && soloed->rows[2].muted && !soloed->rows[1].muted && soloed->rows[1].soloed && !soloed->rows[0].soloed && soloed->isolated, "mute and solo follow the view");

		Pick(selection, Target::kMaterial, Slot::kRmaos);
		const auto rmaos = BuildStackView(a_piece, a_recipe, a_geometry, selection, view);
		Check(rmaos && rmaos->rows.size() == 1 && rmaos->below.empty() && rmaos->above.empty() && rmaos->scalars.empty(), "rmaos on the material has no neighbours and no scalars");
		Check(rmaos && rmaos->masks == a_recipe.masks, "the stack carries the recipe's mask names for its rows");
		Check(rmaos && std::ranges::none_of(rmaos->rows, [](const StackRow& a_row) { return a_row.selected; }), "no layer selected, none marked");

		// A recipe the piece does not list: neighbours split by priority.
		auto stray = a_recipe;
		stray.id = "stray";
		stray.priority = 50;
		Pick(selection, Target::kShell, Slot::kEmissive);
		const auto strayStack = BuildStackView(a_piece, stray, a_geometry, selection, view);
		// lower (1 layer) and the canonical recipe (3 layers) merge before priority 50; higher after.
		Check(strayStack && strayStack->below.size() == 4 && strayStack->above.size() == 1, "a recipe outside the merge order splits neighbours by priority");
	}

	void NormalBlends(const PieceRow& a_piece, const RecipeRow& a_recipe, const GeometryRow& a_geometry)
	{
		auto geometry = a_geometry;
		auto normal = a_geometry.outputs[3];
		normal.slot = Slot::kNormal;
		normal.slotName = "normal";
		normal.index = 5;
		geometry.outputs.push_back(normal);
		auto selection = SelectCanonical();
		Pick(selection, Target::kMaterial, Slot::kNormal);
		const View view;
		const auto stack = BuildStackView(a_piece, a_recipe, geometry, selection, view);
		Check(stack && stack->blends.size() == 7 && stack->blends.back() == Blend::kNormal, "the normal stack takes every blend including normal");
	}

	void Inspectors(const RecipeRow& a_recipe, const GeometryRow& a_geometry)
	{
		auto selection = SelectCanonical();
		Check(!BuildInspector(a_recipe, a_geometry, selection), "no output, no inspector");
		Pick(selection, Target::kShell, Slot::kEmissive);
		Check(!BuildInspector(a_recipe, a_geometry, selection), "no layer, no inspector");
		selection.layer = 3;
		Check(!BuildInspector(a_recipe, a_geometry, selection), "a layer past the end, no inspector");
		Pick(selection, Target::kLight, std::nullopt, 0);
		Check(!BuildInspector(a_recipe, a_geometry, selection), "a light has no inspector");

		Pick(selection, Target::kShell, Slot::kEmissive, 0);
		const auto fill = BuildInspector(a_recipe, a_geometry, selection);
		Check(fill.has_value(), "the fill layer inspects");
		if (!fill) {
			return;
		}
		Check(fill->output == 0 && fill->layer == 0 && fill->slot == Slot::kEmissive && fill->row.source == "@fill" && fill->row.mask == "@metal", "the inspector names its layer");
		Check(fill->source && fill->source->name == "fill" && fill->source->kind.find("DarkSwirls") != std::string::npos, "the source row is found by name");
		Check(fill->mask && fill->mask->name == "metal" && fill->mask->kind == "@metallic", "the mask row is found by name");
		Check(fill->signals.size() == 1 && fill->signals[0].name == "glowHue" && fill->signals[0].type == ValueType::kVec3, "a literal opacity names no signal; the colour names glowHue");
		Check(!fill->curve, "the fill layer has no curve");
		Check(fill->blends.size() == 6, "the emissive layer's blends exclude normal");
		Check(fill->sources.size() == 8 && fill->sources[0] == "fill" && fill->sources[7] == "stepRing", "every source name, in file order");
		Check(fill->masks == std::vector<std::string>{ "metal" }, "every mask name");
		Check(fill->curves.size() == 5 && fill->curves[0] == "rest" && fill->curves[4] == "punchy", "every curve name");
		Check(fill->scalarSignals.size() == 15 && std::ranges::find(fill->scalarSignals, std::string_view{ "glowLevel" }) != fill->scalarSignals.end() && std::ranges::find(fill->scalarSignals, std::string_view{ "glowHue" }) == fill->scalarSignals.end(), "scalar signals for opacity");
		Check(fill->colorSignals.size() == 2 && fill->colorSignals[0] == "edgeColor" && fill->colorSignals[1] == "glowHue", "vec3 signals for colour");

		selection.layer = 1;
		const auto ring = BuildInspector(a_recipe, a_geometry, selection);
		Check(ring && ring->signals.size() == 2 && ring->signals[0].name == "struck" && ring->signals[1].name == "glowHue", "opacity's signal comes before the colour's");
		Check(ring && ring->source && ring->source->name == "ring" && !ring->mask, "a ripple source, no mask");

		Pick(selection, Target::kMaterial, Slot::kHeight, 0);
		const auto relief = BuildInspector(a_recipe, a_geometry, selection);
		Check(relief && relief->curve && relief->curve->name == "crisp" && relief->curve->text == "(x - mean) * 3 + 0.5", "a declared curve is found by its reference");
		Check(relief && relief->signals.empty() && relief->slot == Slot::kHeight, "a literal opacity and no colour name no signal");

		Pick(selection, Target::kShell, Slot::kFuzz, 0);
		const auto white = BuildInspector(a_recipe, a_geometry, selection);
		Check(white && !white->source && white->row.source == "1, 1, 1", "a constant colour source has no source row");

		// A layer whose source is a mask, whose curve is inline and whose
		// references do not resolve: nothing is found, nothing crashes.
		auto geometry = a_geometry;
		auto& layer = geometry.outputs[0].layers[0];
		layer.source = "@metal";
		layer.curve = "x * 2";
		layer.opacityText = "@missing";
		layer.color = "@glowHue, 1, 1";
		layer.mask = "@nowhere";
		Pick(selection, Target::kShell, Slot::kEmissive, 0);
		const auto maskSource = BuildInspector(a_recipe, geometry, selection);
		Check(maskSource && maskSource->source && maskSource->source->name == "metal", "a source naming a mask finds the mask row");
		Check(maskSource && !maskSource->mask && maskSource->signals.empty() && !maskSource->curve, "unknown references and an inline curve resolve to nothing");
	}

	// The edit a field's binding makes of a text, as the alternative T, or
	// null when the text is refused or makes another kind of edit.
	template <class T>
	const T* Bound(const FieldSpec& a_field, const char* a_text, std::optional<RecipeEdit>& a_edit)
	{
		a_edit = a_field.bind ? a_field.bind(a_text) : std::nullopt;
		return a_edit ? Get<T>(*a_edit) : nullptr;
	}

	void InspectorForms(const RecipeRow& a_recipe, const GeometryRow& a_geometry)
	{
		auto selection = SelectCanonical();
		Pick(selection, Target::kShell, Slot::kEmissive, 0);
		const auto inspector = BuildInspector(a_recipe, a_geometry, selection);
		Check(inspector.has_value(), "the fill layer inspects for its form");
		if (!inspector) {
			return;
		}
		const auto form = InspectorForm(*inspector);
		Check(form.size() == 6, "the inspector form has six fields");
		if (form.size() != 6) {
			return;
		}
		const auto& source = form[0];
		const auto& curve = form[1];
		const auto& opacity = form[2];
		const auto& colour = form[3];
		const auto& mask = form[4];
		const auto& channels = form[5];
		Check(source.name == "source" && curve.name == "curve" && opacity.name == "opacity" && colour.name == "colour" && mask.name == "mask" && channels.name == "channels", "fields are source, curve, opacity, colour, mask, channels");
		Check(source.kind == FieldKind::kColor && curve.kind == FieldKind::kCurve && opacity.kind == FieldKind::kScalar && colour.kind == FieldKind::kColor && mask.kind == FieldKind::kReference && channels.kind == FieldKind::kChannels, "field kinds");
		Check(source.text == "@fill" && curve.text.empty() && opacity.text == inspector->row.opacityText && colour.text == "@glowHue" && mask.text == "@metal" && channels.text == inspector->row.channels, "field texts are the layer's");
		Check(source.names.size() == 9 && source.names[0] == "fill" && source.names[8] == "metal", "the source combo lists sources then masks");
		Check(curve.names == inspector->curves && opacity.names == inspector->scalarSignals && colour.names == inspector->colorSignals && mask.names == inspector->masks && channels.names.empty(), "combo names per field");
		Check(!source.allowEmpty && curve.allowEmpty && !opacity.allowEmpty && colour.allowEmpty && mask.allowEmpty && !channels.allowEmpty, "curve, colour and mask may be empty");
		Check(source.detail == FieldDetail::kSource && !curve.detail && !opacity.detail && colour.detail == FieldDetail::kColor && mask.detail == FieldDetail::kMask && !channels.detail, "details only where the modal has content: the source row, the colour's signal, the mask row");
		Check(std::ranges::none_of(form, [](const FieldSpec& a_field) { return a_field.value.has_value(); }), "layer fields show no swatch");
		Check(std::ranges::all_of(form, [](const FieldSpec& a_field) { return static_cast<bool>(a_field.bind); }), "every field binds");

		std::optional<RecipeEdit> edit;
		const auto*               sourceRef = Bound<SetLayerSource>(source, "@ring", edit);
		Check(sourceRef && sourceRef->output == 0 && sourceRef->layer == 0 && Get<Ref>(sourceRef->source) && Get<Ref>(sourceRef->source)->name == "ring", "a @name source binds to a reference");
		const auto* sourceColour = Bound<SetLayerSource>(source, "1, 0, 0", edit);
		Check(sourceColour && Get<Vec3>(sourceColour->source) && *Get<Vec3>(sourceColour->source) == Vec3{ 1.0f, 0.0f, 0.0f }, "a colour source binds to a constant");
		Check(!Bound<SetLayerSource>(source, "nonsense", edit) && !edit, "a source that does not parse is refused");
		Check(!Bound<SetLayerSource>(source, "", edit) && !edit, "an empty source is refused");

		const auto* named = Bound<SetLayerCurve>(curve, "@crisp", edit);
		Check(named && named->output == 0 && named->layer == 0 && named->curve == CurveRef{ "@crisp" }, "a @curve binds by name");
		const auto* inlineCurve = Bound<SetLayerCurve>(curve, "x * 2", edit);
		Check(inlineCurve && inlineCurve->curve == CurveRef{ "x * 2" }, "an inline curve binds as text");
		const auto* noCurve = Bound<SetLayerCurve>(curve, "", edit);
		Check(noCurve && !noCurve->curve, "an empty curve clears it");

		const auto* number = Bound<SetLayerOpacity>(opacity, "0.5", edit);
		Check(number && number->output == 0 && number->layer == 0 && Get<float>(number->opacity) && *Get<float>(number->opacity) == 0.5f, "a number binds as opacity");
		const auto* signal = Bound<SetLayerOpacity>(opacity, "@glowLevel", edit);
		Check(signal && Get<Ref>(signal->opacity) && Get<Ref>(signal->opacity)->name == "glowLevel", "a @signal binds as opacity");
		Check(!Bound<SetLayerOpacity>(opacity, "abc", edit) && !edit, "an opacity that does not parse is refused");

		const auto* noColour = Bound<SetLayerColor>(colour, "", edit);
		Check(noColour && noColour->output == 0 && noColour->layer == 0 && !noColour->color, "an empty colour clears it");
		const auto* parts = Bound<SetLayerColor>(colour, "1, 0.5, 0", edit);
		Check(parts && parts->color && Get<std::array<Param, 3>>(*parts->color), "r, g, b binds as a colour");
		const auto* colourSignal = Bound<SetLayerColor>(colour, "@glowHue", edit);
		Check(colourSignal && colourSignal->color && Get<Ref>(*colourSignal->color) && Get<Ref>(*colourSignal->color)->name == "glowHue", "a @signal binds as the colour");
		Check(!Bound<SetLayerColor>(colour, "bad", edit) && !edit, "a colour that does not parse is refused");

		const auto* maskRef = Bound<SetLayerMask>(mask, "@metal", edit);
		Check(maskRef && maskRef->output == 0 && maskRef->layer == 0 && maskRef->mask && maskRef->mask->name == "metal", "a @mask binds by name");
		const auto* noMask = Bound<SetLayerMask>(mask, "", edit);
		Check(noMask && !noMask->mask, "an empty mask clears it");

		const auto* set = Bound<SetLayerChannels>(channels, "rg", edit);
		Check(set && set->output == 0 && set->layer == 0 && set->channels == ChannelSet{ true, true, false, false }, "a channel set binds");
		Check(!Bound<SetLayerChannels>(channels, "xyz", edit) && !edit, "channels that do not parse are refused");

		// The relief layer: a declared curve has a detail, a literal opacity none;
		// the bindings carry its own indices.
		Pick(selection, Target::kMaterial, Slot::kHeight, 0);
		const auto relief = BuildInspector(a_recipe, a_geometry, selection);
		Check(relief.has_value(), "the relief layer inspects for its form");
		if (!relief) {
			return;
		}
		const auto reliefForm = InspectorForm(*relief);
		Check(reliefForm.size() == 6 && reliefForm[1].detail == FieldDetail::kCurve && reliefForm[1].text == "@crisp" && !reliefForm[2].detail && !reliefForm[3].detail && !reliefForm[4].detail, "the relief form has a curve detail and no others");
		const auto* reliefOpacity = reliefForm.size() == 6 ? Bound<SetLayerOpacity>(reliefForm[2], "0.25", edit) : nullptr;
		Check(reliefOpacity && reliefOpacity->output == 2 && reliefOpacity->layer == 0, "a binding names its own output and layer");
	}

	void ScalarForms(const PieceRow& a_piece, const RecipeRow& a_recipe, const GeometryRow& a_geometry)
	{
		const View view;
		auto       selection = SelectCanonical();
		Pick(selection, Target::kShell, Slot::kEmissive);
		const auto emissive = BuildStackView(a_piece, a_recipe, a_geometry, selection, view);
		Check(emissive.has_value(), "the emissive stack builds for its form");
		if (!emissive) {
			return;
		}
		const auto form = ScalarForm(*emissive);
		Check(form.size() == 1, "the emissive form has one scalar");
		if (form.size() != 1) {
			return;
		}
		const auto& strength = form[0];
		Check(strength.name == "strength" && strength.kind == FieldKind::kScalar && strength.text == "@glowLevel" && strength.names == emissive->scalarSignals && !strength.allowEmpty && strength.detail == FieldDetail::kSignal, "strength is a scalar field over the scalar signals, opening its signal");
		Check(strength.value.has_value(), "a scalar field shows its live value");
		std::optional<RecipeEdit> edit;
		const auto*               number = Bound<SetScalar>(strength, "2", edit);
		Check(number && number->output == 0 && number->field == ScalarField::kStrength && Get<float>(number->value) && *Get<float>(number->value) == 2.0f, "a number binds as the scalar");
		const auto* signal = Bound<SetScalar>(strength, "@glowLevel", edit);
		Check(signal && Get<Ref>(signal->value) && Get<Ref>(signal->value)->name == "glowLevel", "a @signal binds as the scalar");
		Check(!Bound<SetScalar>(strength, "zzz", edit) && !edit, "a scalar that does not parse is refused");

		Pick(selection, Target::kShell, Slot::kFuzz);
		const auto fuzz = BuildStackView(a_piece, a_recipe, a_geometry, selection, view);
		Check(fuzz.has_value(), "the fuzz stack builds for its form");
		if (!fuzz) {
			return;
		}
		const auto fuzzForm = ScalarForm(*fuzz);
		const auto colour = std::ranges::find(fuzzForm, "color", &FieldSpec::name);
		const auto weight = std::ranges::find(fuzzForm, "weight", &FieldSpec::name);
		Check(fuzzForm.size() == 2 && colour != fuzzForm.end() && weight != fuzzForm.end(), "the fuzz form has colour and weight");
		if (colour == fuzzForm.end() || weight == fuzzForm.end()) {
			return;
		}
		Check(colour->kind == FieldKind::kColor && colour->names == fuzz->colorSignals && colour->text == "@edgeColor", "the colour scalar is a colour field over the colour signals");
		Check(weight->kind == FieldKind::kScalar && weight->names == fuzz->scalarSignals && weight->text == "@sheenWeight", "weight is a scalar field");
		const auto* parts = Bound<SetColorScalar>(*colour, "1, 0, 0", edit);
		Check(parts && parts->output == 1 && Get<std::array<Param, 3>>(parts->color), "r, g, b binds as the colour scalar");
		const auto* colourSignal = Bound<SetColorScalar>(*colour, "@edgeColor", edit);
		Check(colourSignal && Get<Ref>(colourSignal->color) && Get<Ref>(colourSignal->color)->name == "edgeColor", "a @signal binds as the colour scalar");
		Check(!Bound<SetColorScalar>(*colour, "bad", edit) && !edit, "a colour scalar that does not parse is refused");
		const auto* weightValue = Bound<SetScalar>(*weight, "0.3", edit);
		Check(weightValue && weightValue->output == 1 && weightValue->field == ScalarField::kWeight, "weight binds to its field");

		Pick(selection, Target::kMaterial, Slot::kRmaos);
		const auto rmaos = BuildStackView(a_piece, a_recipe, a_geometry, selection, view);
		Check(rmaos && ScalarForm(*rmaos).empty(), "a stack without scalars has an empty form");
	}

	void PanelForms(const RecipeRow& a_recipe)
	{
		const auto names = SignalNamesOf(a_recipe);
		Check(names.scalar.size() == 15 && names.color.size() == 2, "signal names split by type");
		std::optional<RecipeEdit> edit;

		const auto& light = a_recipe.lightRow;
		Check(light.present && light.output == 4 && light.color == "@glowHue" && light.intensity == "@lightLevel" && light.size == "1.4" && light.cutoff == "0.05" && light.offset == "0, 0, 0" && !light.shadow && light.bones == "skinned" && light.bonesMax == "2" && light.bonesMinShare == "0", "the light row reads the canonical light");
		const auto lightForm = LightForm(light, names);
		Check(lightForm.size() == 9 && lightForm[0].name == "color" && lightForm[4].name == "offset" && lightForm[5].name == "shadow" && lightForm[6].name == "bones" && lightForm[7].name == "max" && lightForm[8].name == "minShare", "the light form: colour, intensity, size, cutoff, offset, shadow, bones, max, minShare");
		Check(lightForm[0].kind == FieldKind::kColor && lightForm[0].names == names.color && lightForm[1].kind == FieldKind::kScalar && lightForm[1].names == names.scalar && lightForm[4].kind == FieldKind::kVector && lightForm[5].kind == FieldKind::kToggle && lightForm[6].kind == FieldKind::kChoice && lightForm[7].names.empty(), "light field kinds and combos");
		const auto* colour = Bound<SetLightVector>(lightForm[0], "1, 0.5, 0", edit);
		Check(colour && colour->output == 4 && colour->field == LightVector::kColor && Get<std::array<Param, 3>>(colour->value), "the light's colour binds");
		const auto* intensity = Bound<SetLightParam>(lightForm[1], "@glowLevel", edit);
		Check(intensity && intensity->field == LightParam::kIntensity && Get<Ref>(intensity->value), "the light's intensity binds a signal");
		const auto* shadow = Bound<SetLightShadow>(lightForm[5], "on", edit);
		Check(shadow && shadow->shadow, "shadow binds a toggle");
		const auto* named = Bound<SetLightBones>(lightForm[6], "named", edit);
		Check(named && Get<NamedBones>(named->bones) && Get<NamedBones>(named->bones)->bones.size() == 1, "choosing named bones starts with one bone");
		const auto* max = Bound<SetLightBones>(lightForm[7], "3", edit);
		Check(max && Get<SkinnedBones>(max->bones) && Get<SkinnedBones>(max->bones)->max == 3 && Get<SkinnedBones>(max->bones)->minShare == 0.0f, "max keeps the share");
		Check(!Bound<SetLightBones>(lightForm[7], "0", edit) && !Bound<SetLightBones>(lightForm[7], "x", edit), "a bad count is refused");
		Check(!Bound<SetLightParam>(lightForm[1], "nonsense", edit) && !edit, "a light value that does not parse is refused");

		LightRow namedRow = light;
		namedRow.bones = "named";
		namedRow.bonesNames = "NPC Head [Head], NPC Spine2 [Spn2]";
		const auto namedForm = LightForm(namedRow, names);
		Check(namedForm.size() == 8 && namedForm[7].name == "names" && namedForm[7].kind == FieldKind::kText, "named bones show a names field");
		const auto* renamed = Bound<SetLightBones>(namedForm[7], "NPC Head [Head] , NPC L Hand [LHnd]", edit);
		Check(renamed && Get<NamedBones>(renamed->bones) && Get<NamedBones>(renamed->bones)->bones == std::vector<std::string>{ "NPC Head [Head]", "NPC L Hand [LHnd]" }, "names split on commas, trimmed");
		Check(!Bound<SetLightBones>(namedForm[7], " , ", edit), "no names is refused");
		Check(LightForm(LightRow{}, names).empty(), "no light, no form");

		const auto& shell = a_recipe.shellRow;
		Check(shell.material == ShellMaterial::kPbrCopy && shell.blend == ShellBlend::kAdditive && shell.depthBias && shell.alphaTest == 0.0f && shell.alpha == "@shellOpacity" && shell.inflate == "0, @inflate, @inflate" && shell.scale == "1" && shell.spinAxis == Vec3{ 0.0f, 0.0f, 1.0f }, "the shell row reads the canonical shell");
		const auto shellForm = ShellForm(shell, names);
		Check(shellForm.size() == 13 && shellForm[0].name == "material" && shellForm[1].name == "blend" && shellForm[2].name == "depthBias" && shellForm[3].name == "alphaTest" && shellForm[4].name == "alpha" && shellForm[7].name == "inflate" && shellForm[10].name == "scalePoint" && shellForm[12].name == "spinAxis", "the shell form's fields");
		Check(shellForm[0].kind == FieldKind::kChoice && shellForm[0].text == "pbrCopy" && shellForm[0].names.size() == 2 && shellForm[2].kind == FieldKind::kToggle && shellForm[2].text == "on" && shellForm[3].names.empty() && shellForm[4].names == names.scalar && shellForm[7].kind == FieldKind::kVector && shellForm[10].names.empty(), "shell field kinds and combos");
		const auto* material = Bound<SetShellMaterial>(shellForm[0], "vanilla", edit);
		Check(material && material->material == ShellMaterial::kVanilla, "the material kind binds");
		Check(!Bound<SetShellMaterial>(shellForm[0], "glass", edit), "an unknown kind is refused");
		const auto* blend = Bound<SetShellBlend>(shellForm[1], "alpha", edit);
		Check(blend && blend->blend == ShellBlend::kAlpha, "the blend binds");
		const auto* bias = Bound<SetShellDepthBias>(shellForm[2], "off", edit);
		Check(bias && !bias->on, "depth bias binds a toggle");
		const auto* alphaTest = Bound<SetShellAlphaTest>(shellForm[3], "0.5", edit);
		Check(alphaTest && alphaTest->value == 0.5f, "alpha test binds a number");
		Check(!Bound<SetShellAlphaTest>(shellForm[3], "@shellOpacity", edit), "alpha test takes no signal");
		const auto* alpha = Bound<SetShellParam>(shellForm[4], "0.5", edit);
		Check(alpha && alpha->field == ShellParam::kAlpha && Get<float>(alpha->value), "alpha binds");
		const auto* inflate = Bound<SetShellVector>(shellForm[7], "@inflate", edit);
		Check(inflate && inflate->field == ShellVector::kInflate && Get<Ref>(inflate->value), "inflate binds a signal");
		const auto* point = Bound<SetShellPoint>(shellForm[10], "0, 0, 10", edit);
		Check(point && point->field == ShellPoint::kScalePoint && point->value == Vec3{ 0.0f, 0.0f, 10.0f }, "the scale point binds three numbers");
		Check(!Bound<SetShellPoint>(shellForm[10], "@inflate", edit), "a point takes no signal");
	}

	void Checks(const RecipeRow& a_recipe, const GeometryRow& a_geometry)
	{
		const auto names = NamesOf(a_recipe, a_geometry);
		Check(names.signals.size() == 20 && names.curves.size() == 5 && names.sources.size() == 8 && names.masks.size() == 1, "the names carry every row");
		Check(std::ranges::find(names.sources, std::pair{ std::string{ "fill" }, ValueType::kVec3 }) != names.sources.end() && std::ranges::find(names.sources, std::pair{ std::string{ "metallic" }, ValueType::kScalar }) != names.sources.end(), "sources carry their texel type");
		Check(a_recipe.maskRows.size() == 1 && a_recipe.maskRows[0].name == "metal" && a_recipe.maskRows[0].text == "@metallic" && a_recipe.maskRows[0].references == 1, "the mask row carries its text and one reference (the fill layer)");

		const auto ok = [](const std::optional<std::string>& a_problem) { return !a_problem.has_value(); };
		FieldSpec scalar{ "opacity", FieldKind::kScalar, "", {}, false, std::nullopt, std::nullopt, {} };
		for (const auto& [name, type] : names.signals) {
			if (type == ValueType::kScalar) {
				scalar.names.push_back(name);
			}
		}
		Check(ok(CheckField(scalar, "0.5", names)) && ok(CheckField(scalar, "@glowLevel", names)), "a number and a listed signal pass a scalar field");
		Check(!ok(CheckField(scalar, "@glowHue", names)) && !ok(CheckField(scalar, "@nothing", names)) && !ok(CheckField(scalar, "abc", names)) && !ok(CheckField(scalar, "", names)), "an unlisted signal, an unknown name, text and emptiness fail a scalar field");
		FieldSpec colour{ "colour", FieldKind::kColor, "", { "glowHue", "edgeColor" }, true, std::nullopt, std::nullopt, {} };
		Check(ok(CheckField(colour, "1, 0, 0", names)) && ok(CheckField(colour, "0.5", names)) && ok(CheckField(colour, "@glowHue", names)) && ok(CheckField(colour, "", names)), "numbers, a listed colour signal and emptiness pass a colour field");
		Check(ok(CheckField(colour, "1, @glowLevel, 0", names)) && !ok(CheckField(colour, "1, @glowHue, 0", names)) && !ok(CheckField(colour, "1, @nothing, 0", names)), "a component reference must be a scalar signal");
		FieldSpec mask{ "mask", FieldKind::kReference, "", { "metal" }, true, std::nullopt, std::nullopt, {} };
		Check(ok(CheckField(mask, "@metal", names)) && !ok(CheckField(mask, "@fill", names)) && !ok(CheckField(mask, "metal", names)), "a reference field takes only its listed rows as @name");
		FieldSpec expression{ "value", FieldKind::kExpression, "", {}, false, std::nullopt, std::nullopt, {} };
		Check(ok(CheckField(expression, "@glowStrength * 2", names)) && !ok(CheckField(expression, "@fill * 2", names)) && !ok(CheckField(expression, "@nothing", names)) && !ok(CheckField(expression, "1 +", names)) && !ok(CheckField(expression, "x", names)), "an expression reads signals, parses, and has no x");
		Check(ok(CheckField(expression, "@glowHue + @glowLevel", names)) && !ok(CheckField(expression, "@scroll + @glowHue", names)), "a scalar broadcasts against a colour; a vec2 against a vec3 fails");
		FieldSpec maskText{ "mask", FieldKind::kMask, "", {}, false, std::nullopt, std::nullopt, {} };
		Check(ok(CheckField(maskText, "@metallic * @glowLevel + @metal", names)) && !ok(CheckField(maskText, "@nothing", names)), "a mask reads sources, masks and signals");
		FieldSpec curve{ "curve", FieldKind::kCurve, "", { "crisp", "flash" }, true, std::nullopt, std::nullopt, {} };
		Check(ok(CheckField(curve, "@crisp", names)) && ok(CheckField(curve, "x * 2", names)) && ok(CheckField(curve, "", names)) && !ok(CheckField(curve, "@rest", names)) && !ok(CheckField(curve, "@nothing", names)), "a curve field takes a listed curve or an expression in x");
		FieldSpec channels{ "channels", FieldKind::kChannels, "", {}, false, std::nullopt, std::nullopt, {} };
		Check(ok(CheckField(channels, "rg", names)) && !ok(CheckField(channels, "xyz", names)), "channels parse");
		FieldSpec choice{ "material", FieldKind::kChoice, "", { "pbrCopy", "vanilla" }, false, std::nullopt, std::nullopt, {} };
		Check(ok(CheckField(choice, "vanilla", names)) && !ok(CheckField(choice, "glass", names)), "a choice is one of its names");
		Check(ok(CheckSignalValue("1", names)) && ok(CheckSignalValue("1, 0, 0", names)) && ok(CheckSignalValue("@glowLevel * 2", names)) && !ok(CheckSignalValue("@fill", names)) && !ok(CheckSignalValue("", names)), "a signal value is a number, a colour or an expression over signals");
		Check(ok(CheckCurveText("x * @glowStrength", names)) && !ok(CheckCurveText("@fill", names)) && !ok(CheckCurveText("", names)), "a curve text is an expression in x over signals");
		Check(ok(CheckMaskText("@metallic > 0.5", names)) && !ok(CheckMaskText("x", names)) && !ok(CheckMaskText("", names)), "a mask text is per texel and has no x");
	}

	void SourceForms(const RecipeRow& a_recipe)
	{
		const auto names = SignalNamesOf(a_recipe);
		Check(names.vec2.size() == 3 && names.vec2[0] == "scroll" && names.vec2[2] == "shimmerScroll" && names.triggers.size() == 2 && names.triggers[0] == "struck", "signal names carry vec2 signals (scroll and the expressions over it) and triggers");
		Check(a_recipe.sourceRows.size() == 8, "a source row per source");
		if (a_recipe.sourceRows.size() != 8) {
			return;
		}
		const auto& fill = a_recipe.sourceRows[0];
		Check(fill.name == "fill" && fill.kind == "image" && fill.path == "Effects\\DarkSwirls.dds" && fill.channel == "rgb" && fill.space == "tiled" && fill.scroll == "@scroll" && fill.tile == "3, 3" && fill.mirrorU == "off" && fill.transpose == "off" && fill.mip == "0" && fill.references == 1, "the fill row reads the image");
		const auto& sheen = a_recipe.sourceRows[1];
		Check(sheen.channel == "luma" && sheen.mirrorV == "on", "luma and a mirror read back");
		const auto& relief = a_recipe.sourceRows[4];
		Check(relief.kind == "material" && relief.material == "relief", "a material row");
		const auto& ring = a_recipe.sourceRows[6];
		Check(ring.kind == "ripple" && ring.trigger == "@struck" && ring.speed == "90" && ring.width == "8" && ring.decay == "1.2" && ring.shape == "ring", "a ripple row");

		// Every row reads back as the source it came from.
		const auto canonical = Canonical();
		if (canonical) {
			for (std::size_t i = 0; i < a_recipe.sourceRows.size(); ++i) {
				const auto kind = SourceKindOf(a_recipe.sourceRows[i]);
				Check(kind && *kind == canonical->sources[i].kind, std::format("source row '{}' reads back as its source", a_recipe.sourceRows[i].name));
			}
		}
		SourceRow bad = fill;
		bad.channel = "purple";
		Check(!SourceKindOf(bad), "an unknown channel reads back as nothing");
		bad = fill;
		bad.scroll = "1, 2, 3";
		Check(!SourceKindOf(bad), "a three-part scroll reads back as nothing");

		std::optional<RecipeEdit> edit;
		const auto                imageForm = SourceForm(fill, names);
		Check(imageForm.size() == 10 && imageForm[0].name == "kind" && imageForm[1].name == "path" && imageForm[4].name == "scroll" && imageForm[4].kind == FieldKind::kVec2 && imageForm[4].names == names.vec2 && imageForm[4].allowEmpty && imageForm[6].kind == FieldKind::kToggle, "the image form's fields");
		const auto* setKind = Bound<SetSource>(imageForm[0], "ripple", edit);
		Check(setKind && setKind->name == "fill" && Get<RippleSource>(setKind->kind), "the kind field starts another kind at its defaults");
		const auto* setPath = Bound<SetSource>(imageForm[1], "Effects\\Other.dds", edit);
		const auto* image = setPath ? Get<ImageSource>(setPath->kind) : nullptr;
		Check(image && image->path == "Effects\\Other.dds" && image->channel == ImageChannel::kRgb && image->tile && Get<std::array<Param, 2>>(*image->tile), "a path change keeps the rest of the image");
		const auto* setScroll = Bound<SetSource>(imageForm[4], "", edit);
		Check(setScroll && Get<ImageSource>(setScroll->kind) && !Get<ImageSource>(setScroll->kind)->scroll, "an empty scroll clears it");
		Check(!Bound<SetSource>(imageForm[2], "purple", edit) && !edit, "a bad channel is refused");
		const auto* setMirror = Bound<SetSource>(imageForm[6], "on", edit);
		Check(setMirror && Get<ImageSource>(setMirror->kind)->mirror[0], "a toggle sets the mirror");

		const auto rippleForm = SourceForm(ring, names);
		Check(rippleForm.size() == 6 && rippleForm[1].name == "trigger" && rippleForm[1].kind == FieldKind::kReference && rippleForm[1].names == names.triggers && rippleForm[5].name == "shape", "the ripple form's fields");
		const auto* setSpeed = Bound<SetSource>(rippleForm[2], "@glowLevel", edit);
		Check(setSpeed && Get<RippleSource>(setSpeed->kind) && Get<Ref>(Get<RippleSource>(setSpeed->kind)->speed), "a ripple's speed binds a signal");

		SourceRow bake;
		bake.name = "b";
		bake.kind = "bake";
		bake.bake = "partition";
		bake.partition = "hands";
		const auto bakeForm = SourceForm(bake, names);
		Check(bakeForm.size() == 3 && bakeForm[2].name == "partition" && bakeForm[2].kind == FieldKind::kChoice && bakeForm[2].names.size() == 11 && bakeForm[2].names[2] == "body", "a partition bake offers the named biped slots");
		const auto* setSlot = Bound<SetSource>(bakeForm[2], "body", edit);
		Check(setSlot && Get<BakeSource>(setSlot->kind) && Get<PartitionBake>(Get<BakeSource>(setSlot->kind)->bake) && Get<PartitionBake>(Get<BakeSource>(setSlot->kind)->bake)->slot == 32, "a slot name sets the partition");
		bake.bake = "boneWeight";
		bake.bones = "NPC L Hand [LHnd], NPC R Hand [RHnd]";
		const auto boneForm = SourceForm(bake, names);
		Check(boneForm.size() == 3 && boneForm[2].name == "bones" && boneForm[2].kind == FieldKind::kText, "a boneWeight bake takes names");
		const auto* setBones = Bound<SetSource>(boneForm[2], "NPC Head [Head]", edit);
		Check(setBones && Get<BoneWeightBake>(Get<BakeSource>(setBones->kind)->bake)->bones == std::vector<std::string>{ "NPC Head [Head]" }, "bones split on commas");
	}

	void Creators(const RecipeRow& a_recipe, const GeometryRow& a_geometry)
	{
		auto selection = SelectCanonical();
		Pick(selection, Target::kShell, Slot::kEmissive, 0);
		const auto inspector = BuildInspector(a_recipe, a_geometry, selection);
		if (!inspector) {
			Check(false, "the fill layer inspects for its creators");
			return;
		}
		const auto form = InspectorForm(*inspector);
		Check(form[0].creators.size() == 7 && form[0].creators[0] == "new image" && form[0].creators[6] == "new mask" && form[1].creators == std::vector<std::string>{ "new curve" } && form[2].creators.size() == 3 && form[4].creators == std::vector<std::string>{ "new mask" } && form[5].creators.empty(), "creators per field");
		const auto image = form[0].create("new image");
		Check(image.size() == 2 && Get<AddSource>(image[0]) && Get<AddSource>(image[0])->name == "image" && Get<ImageSource>(Get<AddSource>(image[0])->kind) && Get<SetLayerSource>(image[1]) && Get<Ref>(Get<SetLayerSource>(image[1])->source) && Get<Ref>(Get<SetLayerSource>(image[1])->source)->name == "image", "new image adds an image and binds the layer to it");
		const auto ripple = form[0].create("new ripple");
		Check(ripple.size() == 2 && Get<AddSource>(ripple[0]) && Get<AddSource>(ripple[0])->name == "ripple", "new ripple names the row after its kind");
		const auto mask = form[4].create("new mask");
		Check(mask.size() == 2 && Get<AddMask>(mask[0]) && Get<AddMask>(mask[0])->name == "mask" && Get<SetLayerMask>(mask[1]) && Get<SetLayerMask>(mask[1])->mask && Get<SetLayerMask>(mask[1])->mask->name == "mask", "new mask adds a mask and binds the layer to it");
		const auto curve = form[1].create("new curve");
		Check(curve.size() == 2 && Get<AddCurve>(curve[0]) && Get<AddCurve>(curve[0])->name == "curve" && Get<SetLayerCurve>(curve[1]) && Get<SetLayerCurve>(curve[1])->curve && Get<SetLayerCurve>(curve[1])->curve->text == "@curve", "new curve adds a curve and binds the layer to it");
		const auto promoted = form[2].create("promote to signal");
		Check(promoted.size() == 3 && Get<AddSignal>(promoted[0]) && Get<AddSignal>(promoted[0])->name == "opacity" && Get<SetConstant>(promoted[1]) && Get<float>(Get<SetConstant>(promoted[1])->value) && *Get<float>(Get<SetConstant>(promoted[1])->value) == 1.0f && Get<SetLayerOpacity>(promoted[2]) && Get<Ref>(Get<SetLayerOpacity>(promoted[2])->opacity), "promote makes a constant of the literal and binds it");
		const auto colourPromote = form[3].create("promote to signal");
		Check(colourPromote.empty(), "a colour that is already a signal has nothing to promote");
		const auto expression = form[3].create("new expression");
		Check(expression.size() == 3 && Get<AddSignal>(expression[0]) && Get<AddSignal>(expression[0])->name == "signal" && Get<SetExpression>(expression[1]) && Get<SetExpression>(expression[1])->text == "[1, 1, 1]" && Get<SetLayerColor>(expression[2]), "a new colour expression starts white and binds");
		const auto constant = form[2].create("new constant");
		Check(constant.size() == 2 && Get<AddSignal>(constant[0]) && Get<SetLayerOpacity>(constant[1]), "a new scalar constant is the format's 0 and binds");
	}

	MeshData SkinnedMesh()
	{
		MeshData      mesh;
		MeshPartition body;
		body.slot = 32;
		body.boneNames = { "NPC Spine2 [Spn2]", "NPC L UpperArm [LUar]" };
		for (int i = 0; i < 4; ++i) {
			MeshVertex v;
			v.bones = { 0, 1, 0, 0 };
			v.weights = { i < 2 ? 1.0f : 0.5f, i < 2 ? 0.0f : 0.5f, 0.0f, 0.0f };
			body.vertices.push_back(v);
		}
		body.triangles = { { 0, 1, 2 }, { 0, 2, 3 } };
		mesh.partitions.push_back(body);
		MeshPartition hands;
		hands.slot = 33;
		hands.boneNames = { "NPC L Hand [LHnd]" };
		MeshVertex hv;
		hv.bones = { 0, 0, 0, 0 };
		hv.weights = { 1.0f, 0.0f, 0.0f, 0.0f };
		hands.vertices = { hv, hv, hv, hv };
		hands.triangles = { { 0, 1, 2 } };
		mesh.partitions.push_back(hands);
		MeshPartition unskinned;
		unskinned.slot = MeshPartition::kNoSlot;
		mesh.partitions.push_back(unskinned);
		return mesh;
	}

	void Regions(const RecipeRow& a_recipe, const GeometryRow& a_geometry)
	{
		const auto mesh = SkinnedMesh();
		const auto partitions = PartitionsOf(mesh);
		Check(partitions.size() == 2 && partitions[0].slot == 32 && partitions[0].name == "body" && partitions[0].triangles == 2 && partitions[1].slot == 33 && partitions[1].name == "hands", "partitions by slot with their names and triangle counts; the unskinned part is skipped");
		const auto bones = BonesOf(mesh);
		Check(bones.size() == 3 && bones[0].name == "NPC L Hand [LHnd]" && bones[0].coverage == 0.5f && bones[1].name == "NPC Spine2 [Spn2]" && bones[1].coverage == 0.375f && bones[2].coverage == 0.125f, "bones by coverage, the share of all vertices each moves");

		const auto file = test::ReadFile(std::filesystem::path{ WEPBR_FIXTURES_DIR } / ".." / ".." / "presets" / "regions.json");
		const auto presets = ParsePresets(file);
		Check(presets.has_value(), presets ? "the shipped preset file parses" : "the shipped preset file parses: " + presets.error());
		if (!presets) {
			test::Skip("the region preset checks need presets/regions.json");
			return;
		}
		Check(presets->where.size() == 13 && presets->what.size() == 8, "the file ships thirteen where presets and eight what presets");
		Check(presets->where[0].name == "leftPauldron" && presets->where[0].partition == 32 && presets->where[0].bones.size() == 2, "the first where preset is the left pauldron on the body");
		Check(presets->what[0].name == "leather" && presets->what[0].sources.size() == 2 && Get<MaterialSource>(presets->what[0].sources[0].second), "the first what preset is leather over two material channels");
		Check(PlainBoneName(*presets, "NPC L Hand [LHnd]") == "left hand" && PlainBoneName(*presets, "Unknown") == "Unknown" && PlainPartitionName(*presets, 33) == "hands" && PlainPartitionName(*presets, 61) == "61", "plain names, with the engine's name as the fallback");
		Check(!ParsePresets("nonsense") && !ParsePresets("{ \"where\": [ { \"name\": \"x\" } ] }") && !ParsePresets("{ \"what\": [ { \"name\": \"x\" } ] }"), "garbage, a where preset without a partition or bones, and a what preset without an expression are refused");
		const auto unparseable = ParsePresets(R"({ "what": [ { "name": "bad", "expression": "@a *" } ] })");
		Check(!unparseable && unparseable.error().starts_with("what preset bad: expression:"), "a what preset whose expression does not parse is refused, naming the preset");
		const auto overlong = ParsePresets(std::format(R"({{ "what": [ {{ "name": "long", "expression": "{}" }} ] }})", std::string(kMaxExpressionLength + 1, '1')));
		Check(!overlong && overlong.error() == std::format("what preset long: expression longer than {} characters", kMaxExpressionLength), "a what preset expression past the length cap is refused");
		std::string boneList;
		for (std::size_t i = 0; i <= kMaxPresetBones; ++i) {
			boneList += std::format("{}\"bone{}\"", i == 0 ? "" : ", ", i);
		}
		const auto manyBones = ParsePresets(std::format(R"({{ "where": [ {{ "name": "many", "bones": [ {} ] }} ] }})", boneList));
		Check(!manyBones && manyBones.error() == std::format("where preset many: more than {} bones", kMaxPresetBones), "a where preset with more bones than the cap is refused, naming the cap");
		std::string sourceList;
		for (std::size_t i = 0; i <= kMaxPresetSources; ++i) {
			sourceList += std::format("{}\"s{}\": {{ \"uv\": \"u\" }}", i == 0 ? "" : ", ", i);
		}
		const auto manySources = ParsePresets(std::format(R"({{ "what": [ {{ "name": "wide", "expression": "1", "sources": {{ {} }} }} ] }})", sourceList));
		Check(!manySources && manySources.error() == std::format("what preset wide: more than {} sources", kMaxPresetSources), "a what preset with more sources than the cap is refused, naming the cap");
		std::string presetList;
		for (std::size_t i = 0; i <= kMaxPresets; ++i) {
			presetList += std::format("{}{{ \"name\": \"p{}\", \"partition\": 32 }}", i == 0 ? "" : ", ", i);
		}
		const auto manyPresets = ParsePresets(std::format(R"({{ "where": [ {} ] }})", presetList));
		Check(!manyPresets && manyPresets.error() == std::format("more than {} where presets", kMaxPresets), "a list past the preset cap is refused, naming the cap");

		GeometryRow geometry = a_geometry;
		Check(Unresolvable(presets->where[0], geometry) == "the mesh has not been read yet", "a where preset waits for the mesh");
		geometry.meshRead = true;
		geometry.partitions = partitions;
		geometry.bones = bones;
		Check(Unresolvable(presets->where[2], geometry) == std::nullopt, "chest resolves on a body skinned to the spine");
		Check(Unresolvable(presets->where[0], geometry) && Unresolvable(presets->where[0], geometry)->find("NPC L Clavicle") != std::string::npos, "a preset naming a bone the shape lacks says which");
		Check(Unresolvable(presets->where[6], geometry) && Unresolvable(presets->where[6], geometry)->find("head") != std::string::npos, "a preset on a partition the shape lacks says which");
		Check(Unresolvable(presets->what[0], geometry) == std::nullopt && Unresolvable(presets->what[0], a_geometry) == std::nullopt, "a what preset resolves anywhere");

		// Materialising: sources reused by definition, the scratch mask made and set.
		std::vector<std::pair<std::string, SourceKind>> existing;
		std::vector<std::string>                        taken;
		for (const auto& source : a_recipe.sourceRows) {
			if (const auto kind = SourceKindOf(source)) {
				existing.emplace_back(source.name, *kind);
			}
			taken.push_back(source.name);
		}
		for (const auto& mask : a_recipe.masks) {
			taken.push_back(mask);
		}
		const Existing have{ existing, taken };
		const auto     chest = MaterialiseTerm(presets->where[2], have);
		Check(chest.edits.size() == 2 && Get<AddSource>(chest.edits[0]) && Get<AddSource>(chest.edits[0])->name == "partition" && Get<BakeSource>(Get<AddSource>(chest.edits[0])->kind) && Get<AddSource>(chest.edits[1]) && Get<AddSource>(chest.edits[1])->name == "bones", "a where preset adds its two bakes");
		Check(chest.expression == "@partition * @bones", "a where preset reads as the product of its bakes");
		// metallic exists as a material source in the canonical recipe; roughness does not.
		const auto leather = MaterialiseTerm(presets->what[0], have);
		Check(leather.edits.size() == 1 && Get<AddSource>(leather.edits[0]) && Get<AddSource>(leather.edits[0])->name == "roughness", "a what preset reuses the metallic source and adds roughness");
		Check(leather.expression == "(1 - @metallic) * smoothstep(0.35, 0.6, @roughness)", "a what preset reads its expression over the recipe's names");
		Existing withRoughness = have;
		withRoughness.sources.emplace_back("roughness", MaterialSource{ MaterialChannel::kRoughness });
		withRoughness.taken.push_back("roughness");
		Check(MaterialiseTerm(presets->what[0], withRoughness).edits.empty(), "a preset whose sources all exist adds nothing");
		Existing partitionTaken = have;
		partitionTaken.taken.push_back("partition");
		const auto second = MaterialiseTerm(presets->where[2], partitionTaken);
		Check(second.edits.size() == 2 && Get<AddSource>(second.edits[0]) && Get<AddSource>(second.edits[0])->name == "partition2" && second.expression == "@partition2 * @bones", "a taken name is made unique and the expression reads it");

		// The scratch mask follows the stack: added when absent, set when the text differs, "0" when nothing shows.
		const std::vector stackTerms{ Term{ TermOp::kSet, "@a", "a" }, Term{ TermOp::kAnd, "@b", "b" } };
		const auto        fresh = ScratchEdits(stackTerms, std::nullopt, {}, ScratchState{});
		Check(fresh.size() == 2 && Get<AddMask>(fresh[0]) && Get<SetMask>(fresh[1]) && Get<SetMask>(fresh[1])->text == "(@a) * (@b)", "an absent scratch is added and set");
		Check(ScratchEdits(stackTerms, std::nullopt, {}, ScratchState{ true, "(@a) * (@b)" }).empty(), "a scratch that already holds the text needs no edit");
		const auto muted = ScratchEdits(stackTerms, std::nullopt, { 0, 1 }, ScratchState{ true, "(@a) * (@b)" });
		Check(muted.size() == 1 && Get<SetMask>(muted[0]) && Get<SetMask>(muted[0])->text == "0", "everything muted writes 0");

		// The paint recipe: a clone with one masked emissive output; Keep
		// copies the region and the sources it reads back into the active
		// recipe, reusing what is there.
		const auto active = Canonical();
		Check(active.has_value(), "the canonical recipe parses for the paint tests");
		if (!active) {
			test::Skip("the paint and keep checks need the canonical recipe");
			return;
		}
		RecipeKey key;
		key.kind = KeyKind::kArmor;
		key.form = FormRef::From("0x12E49~Skyrim.esm");
		Recipe paint = PaintRecipe(*active, key, Surface::kShell);
		Check(paint.id == kPaintRecipe && paint.keys.size() == 1 && paint.keys[0].kind == KeyKind::kArmor && paint.priority == kPaintPriority && paint.variants.empty(), "the paint recipe is keyed alone at the paint priority");
		Check(paint.signals == active->signals && paint.sources == active->sources && paint.shell == active->shell, "signals, sources and the shell settings are cloned");
		Check(paint.masks.size() == active->masks.size() + 1 && paint.FindMask(kScratchMask) && paint.FindMask(kScratchMask)->text == "0", "the scratch mask is added, empty");
		const auto* output = paint.outputs.size() == 1 ? Get<MaterialOutput>(paint.outputs[0]) : nullptr;
		Check(output && output->surface == Surface::kShell && output->slot == Slot::kEmissive && output->stack.size() == 1 && output->stack[0].mask && output->stack[0].mask->name == kScratchMask, "one emissive output on the chosen surface, its layer masked by the scratch");
		Check(Validate(paint).empty() || std::ranges::none_of(Validate(paint), [](const Diagnostic& d) { return d.severity == Severity::kError; }), "the paint recipe validates");

		// A leftPauldron round: the where preset's bakes are new to the active recipe, the metallic twin is not.
		auto term = MaterialiseTerm(presets->where[0], have);
		for (const auto& edit : term.edits) {
			Check(!Apply(paint, edit), "a paint source edit applies");
		}
		auto leatherTerm = MaterialiseTerm(presets->what[0], have);
		Check(!Apply(paint, SetMask{ std::string{ kScratchMask }, std::format("({}) * ({})", term.expression, leatherTerm.expression) }), "the scratch takes the built expression");
		for (const auto& edit : leatherTerm.edits) {
			Check(!Apply(paint, edit), "a paint what-source edit applies");
		}
		const auto keep = KeepEdits(paint, *active, "chestLeather");
		std::size_t adds = 0;
		for (const auto& e : keep) {
			adds += Get<AddSource>(e) ? 1 : 0;
		}
		Check(adds == 3 && Get<AddMask>(keep[keep.size() - 2]) && Get<SetMask>(keep.back()) && Get<SetMask>(keep.back())->mask == "chestLeather", "keep adds partition, bones and roughness, then the mask");
		Recipe kept = *active;
		for (const auto& e : keep) {
			Check(!Apply(kept, e), "a keep edit applies to the active recipe");
		}
		Check(kept.FindMask("chestLeather") && kept.FindMask("chestLeather")->text == paint.FindMask(kScratchMask)->text && kept.FindSource("partition") && kept.FindSource("roughness") && !kept.FindMask(kScratchMask), "the kept mask reads as painted and the scratch never reaches the active recipe");
		const auto again = KeepEdits(paint, kept, "chestLeather");
		Check(again.size() == 1 && Get<SetMask>(again[0]), "keeping again over the same recipe only sets the mask");
		Recipe renamed = *active;
		Check(!Apply(renamed, AddSource{ "partition", BakeSource{ PartitionBake{ 30 } } }), "a same-named source of another definition");
		const auto clash = KeepEdits(paint, renamed, "region");
		const auto* clashMask = Get<SetMask>(clash.back());
		Check(clashMask && clashMask->text.find("@partition2") != std::string::npos && std::ranges::any_of(clash, [](const RecipeEdit& e) { return Get<AddSource>(e) && Get<AddSource>(e)->name == "partition2"; }), "a taken name is made unique and the text repointed");
		Check(KeepEdits(*active, *active, "x").empty(), "no scratch, nothing to keep");
		Recipe broken = paint;
		for (auto& mask : broken.masks) {
			if (mask.name == kScratchMask) {
				mask.text = "@partition *";
			}
		}
		Check(KeepEdits(broken, *active, "x").empty(), "unparseable scratch text keeps nothing");
		Recipe overMask = paint;
		for (auto& mask : overMask.masks) {
			if (mask.name == kScratchMask) {
				mask.text = "@metal";
			}
		}
		const auto viaMask = KeepEdits(overMask, *active, "onlyMetal");
		Check(active->FindMask("metal") && viaMask.size() == 2 && Get<AddMask>(viaMask[0]) && Get<SetMask>(viaMask[1]) && Get<SetMask>(viaMask[1])->text == "@metal", "a scratch reading a mask of the active recipe adds no source, only the mask");

		Existing paintExisting;
		for (const auto& source : paint.sources) {
			paintExisting.sources.emplace_back(source.name, source.kind);
			paintExisting.taken.push_back(source.name);
		}
		const auto terms = TermsOfMask(paint.FindMask(kScratchMask)->text, *presets, paintExisting);
		Check(terms && terms->size() == 2 && (*terms)[0].label == "leftPauldron" && (*terms)[1].label == "leather", "a kept expression comes back as its labelled terms");
		Check(ProposedRegionName(*terms, "") == "leftPauldronLeather" && ProposedRegionName(*terms, "metal") == "metal" && ProposedRegionName({}, "") == "region", "the proposed name");
		const std::vector<Term> unlabelled{ Term{ TermOp::kSet, "@a + 1", "expression" }, Term{ TermOp::kAnd, "0.5", "expression" } };
		Check(ProposedRegionName(unlabelled, "") == "region", "terms that are all expressions propose 'region'");
		Check(TermLabel("@", *presets, paintExisting) == "expression", "a bare '@' labels as an expression");
		Check(TermLabel("@nothingKnown", *presets, paintExisting) == "nothingKnown", "a lone reference labels by its name even when nothing defines it");
	}

	void SignalLists(const RecipeRow& a_recipe)
	{
		const auto edit = BuildSignalList(a_recipe, LayoutFor(Mode::kCompose));
		Check(edit.tunable.size() == 14 && edit.developer.size() == 6, "constants and expressions are tunable; the rest developer");
		Check(std::ranges::all_of(edit.tunable, [](const SignalRow& a_row) { return a_row.kind == "constant" || a_row.kind == "expr"; }), "tunable rows are constants or expressions");
		Check(edit.developer[0].name == "fillLevel" && edit.developer[0].kind == "efsh" && edit.developer[5].name == "step" && edit.developer[5].kind == "trigger", "developer rows keep file order");
		Check(edit.tunable[0].name == "glowHue" && edit.tunable[0].constant && Get<Vec3>(*edit.tunable[0].constant), "a colour constant keeps its value");
		const auto design = BuildSignalList(a_recipe, LayoutFor(Mode::kDesign));
		Check(design.tunable.size() == 14 && design.developer.empty(), "design mode hides developer rows");
		Check(BuildSignalList(RecipeRow{}, LayoutFor(Mode::kCompose)).tunable.empty(), "a recipe without signals lists nothing");
	}

	void SignalForms(const RecipeRow& a_recipe)
	{
		const auto find = [&](std::string_view a_name) -> const SignalRow* {
			const auto it = std::ranges::find(a_recipe.signals, a_name, &SignalRow::name);
			return it == a_recipe.signals.end() ? nullptr : &*it;
		};
		const auto* strength = find("glowStrength");
		const auto* hue = find("glowHue");
		const auto* scroll = find("shimmerScroll");
		const auto* level = find("fillLevel");
		Check(strength && hue && scroll && level, "the canonical signals for the forms are present");
		if (!strength || !hue || !scroll || !level) {
			return;
		}
		std::optional<RecipeEdit> edit;

		const auto number = SignalForm(*strength);
		Check(number && number->kind == FieldKind::kScalar && number->text == "1" && number->names.empty() && !number->allowEmpty && !number->detail, "a scalar constant is a literal scalar field");
		const auto* setNumber = number ? Bound<SetConstant>(*number, "0.5", edit) : nullptr;
		Check(setNumber && setNumber->signal == "glowStrength" && Get<float>(setNumber->value) && *Get<float>(setNumber->value) == 0.5f, "a number sets the constant");
		const auto* toExpression = number ? Bound<SetExpression>(*number, "@fillLevel * 2", edit) : nullptr;
		Check(toExpression && toExpression->signal == "glowStrength" && toExpression->text == "@fillLevel * 2", "an expression typed into a constant makes it an expression");
		const auto* toColour = number ? Bound<SetConstant>(*number, "1, 0, 0", edit) : nullptr;
		Check(toColour && Get<Vec3>(toColour->value), "three numbers typed into a constant make it a colour");
		Check(number && !Bound<SetConstant>(*number, "nonsense +", edit) && !edit, "text that parses as nothing is refused");
		Check(number && !Bound<SetConstant>(*number, "", edit) && !edit, "an empty text is refused");

		const auto colour = SignalForm(*hue);
		Check(colour && colour->kind == FieldKind::kColor && colour->names.empty(), "a colour constant is a literal colour field");
		const auto* setColour = colour ? Bound<SetConstant>(*colour, "0.6, 0.2, 1", edit) : nullptr;
		const auto* value = setColour ? Get<Vec3>(setColour->value) : nullptr;
		Check(value && value->x == 0.6f && value->y == 0.2f && value->z == 1.0f, "three numbers set the colour");
		const auto* colourExpression = colour ? Bound<SetExpression>(*colour, "@edgeColor", edit) : nullptr;
		Check(colourExpression && colourExpression->text == "@edgeColor", "a reference typed into a colour constant makes it an expression");

		const auto expression = SignalForm(*scroll);
		Check(expression && expression->kind == FieldKind::kExpression && expression->text == "@scroll + 0.25", "an expr signal is an expression field");
		const auto* setExpression = expression ? Bound<SetExpression>(*expression, "@scroll * 2", edit) : nullptr;
		Check(setExpression && setExpression->signal == "shimmerScroll" && setExpression->text == "@scroll * 2", "the text sets the expression");
		const auto* toConstant = expression ? Bound<SetConstant>(*expression, "0.25", edit) : nullptr;
		Check(toConstant && Get<float>(toConstant->value) && *Get<float>(toConstant->value) == 0.25f, "a number typed into an expression makes it a constant");
		const auto* hueRow = find("glowHue");
		const auto* sheenScale = find("sheenScale");
		Check(hueRow && hueRow->references == 5 && sheenScale && sheenScale->references == 1, "reference counts: glowHue in three layers, the light and a variant; sheenScale in one expression");

		Check(!SignalForm(*level), "an efsh row has no form");
	}

	void Colours()
	{
		const auto three = LiteralColor("0.6, 0.2, 1");
		Check(three && three->x == 0.6f && three->y == 0.2f && three->z == 1.0f, "three numbers are a colour");
		const auto one = LiteralColor("0.25");
		Check(one && one->x == 0.25f && one->y == 0.25f && one->z == 0.25f, "one number stands for all three");
		Check(!LiteralColor("@glowHue") && !LiteralColor("1, @a, 0") && !LiteralColor("nonsense") && !LiteralColor(""), "references and non-numbers are not literal colours");
		Check(LiteralColorText(Vec3{ 0.6f, 0.2f, 1.0f }) == "0.6, 0.2, 1", "a colour's text is three numbers");
		const auto back = LiteralColor(LiteralColorText(Vec3{ 0.155f, 0.388f, 1.0f }));
		Check(back && back->x == 0.155f && back->y == 0.388f && back->z == 1.0f, "a colour's text reads back");
	}

	void UniqueNames()
	{
		const std::vector<std::string> taken{ "signal", "signal2", "curve" };
		Check(UniqueName("glow", taken) == "glow", "a free stem is the name");
		Check(UniqueName("signal", taken) == "signal3", "a taken stem takes the first free number from 2");
		Check(UniqueName("curve", taken) == "curve2", "the first number is 2");
		Check(UniqueName("x", {}) == "x", "nothing taken");
	}

	void Filters()
	{
		Check(NameMatches("glowHue", "") && NameMatches("", ""), "an empty filter passes every name");
		Check(NameMatches("glowHue", "hue") && NameMatches("glowHue", "GLOW") && NameMatches("glowHue", "glowHue"), "a filter matches anywhere, case ignored");
		Check(!NameMatches("glowHue", "ring") && !NameMatches("", "a") && !NameMatches("hue", "glowHue"), "a name without the filter fails");
	}

	void Reductions()
	{
		MenuState state;
		Reduce(state, SetMode{ Mode::kDesign });
		Check(state.mode == Mode::kDesign && state.layout.designPanel, "set mode takes the mode's layout");
		state.layout.stackSplit = 0.3f;
		Reduce(state, SetMode{ Mode::kDesign });
		Check(state.layout.stackSplit == 0.3f, "the same mode again keeps the layout as dragged");
		Reduce(state, SetMode{ Mode::kCompose });

		Reduce(state, PickPiece{ kPlayer, kCuirass, false });
		Check(state.selection.actorID == kPlayer && state.selection.armorID == kCuirass && state.selection.recipeID.empty(), "a piece pick starts the selection over");
		Reduce(state, PickRecipe{ kRecipeID });
		Reduce(state, PickCell{ Surface::kShell, Slot::kEmissive, 2 });
		Check(state.selection.recipeID == kRecipeID && state.selection.target == Target::kShell && state.selection.slot == Slot::kEmissive && state.selection.layer == 2, "a cell pick sets target, slot and top layer");
		Reduce(state, PickLayer{ 1 });
		Check(state.selection.layer == 1, "a layer pick");
		Reduce(state, PickRegion{ "metal" });
		Check(state.selection.region == "metal", "a region pick");
		Reduce(state, ViewGeometry{ "other" });
		Check(state.selection.geometry == "other", "viewing a geometry");
		Reduce(state, ShowSettings{ true });
		Check(state.settings, "the settings switch");
		Reduce(state, ShowSettings{ false });
		Check(!state.settings, "the stack switch");
		Reduce(state, ShowResource{ ResourceTab::kCurves });
		Check(state.resource == ResourceTab::kCurves && ResourceTabName(state.resource) == "Curves", "the resources tab");
		Reduce(state, PickTarget{ Target::kShell });
		Check(state.selection.slot == Slot::kEmissive && state.selection.layer == 1, "picking the same target keeps the slot and layer");
		Reduce(state, PickTarget{ Target::kLight });
		Check(state.selection.target == Target::kLight && !state.selection.slot && !state.selection.layer, "picking another target clears the slot and layer");
		Reduce(state, PickSlot{ Slot::kHeight });
		Check(state.selection.slot == Slot::kHeight && !state.selection.layer, "a slot pick clears the layer");
		Reduce(state, PickRecipe{ "other" });
		Check(state.selection.recipeID == "other" && state.selection.slot == Slot::kHeight, "a recipe pick keeps the cell");

		// The region stack: terms land selected, the first leads, indices follow moves and removals.
		Reduce(state, SetMode{ Mode::kPaint });
		Check(state.resource == ResourceTab::kMasks && !state.layout.contextRows, "paint opens the masks tab and drops the context rows");
		Reduce(state, AddTerm{ Term{ TermOp::kAnd, "@a", "a" } });
		Check(state.region.terms.size() == 1 && state.region.terms[0].op == TermOp::kSet && state.region.selected == 0 && state.region.dirty, "the first term is set, selected and dirty");
		state.region.dirty = false;
		Reduce(state, AddTerm{ Term{ TermOp::kSet, "@b", "b" } });
		Reduce(state, AddTerm{ Term{ TermOp::kOr, "@c", "c" } });
		Check(state.region.terms.size() == 3 && state.region.terms[1].op == TermOp::kAnd && state.region.terms[2].op == TermOp::kOr && state.region.selected == 2, "later terms take and unless they say otherwise");
		Reduce(state, SetTermOp{ 0, TermOp::kNot });
		Check(state.region.terms[0].op == TermOp::kSet, "the first term's op cannot change");
		Reduce(state, SetTermOp{ 1, TermOp::kNot });
		Check(state.region.terms[1].op == TermOp::kNot, "a later term's op changes");
		Reduce(state, SetTermRecipe{ 1, ReferenceTerm{ "b" }, "@b", "@b" });
		Check(state.region.terms[1].recipe == TermRecipe{ ReferenceTerm{ "b" } } && state.region.terms[1].text == "@b" && state.region.terms[1].label == "@b" && state.region.dirty, "a settings change carries the rebuilt text and label");
		Reduce(state, SetTermRecipe{ 9, ReferenceTerm{ "z" }, "@z", "@z" });
		Check(state.region.terms.size() == 3 && state.region.terms[1].text == "@b", "a settings change past the stack is dropped");
		Reduce(state, SetTermText{ 1, "@b * 2" });
		Check(state.region.terms[1].text == "@b * 2" && state.region.terms[1].label == "expression" && state.region.terms[1].recipe == TermRecipe{ RawTerm{} }, "typed text makes the term a raw expression");
		Reduce(state, SoloTerm{ 1, true });
		Reduce(state, MuteTerm{ 2, true });
		Check(state.region.solo == 1 && state.region.muted.contains(2), "solo and mute are stack state");
		Reduce(state, MoveTerm{ 2, 0 });
		Check(state.region.terms[0].text == "@c" && state.region.terms[0].op == TermOp::kSet && state.region.terms[1].op == TermOp::kAnd && state.region.solo == 2 && state.region.muted.contains(0) && state.region.selected == 0, "a move re-leads the stack and carries solo, mute and selection along");
		Reduce(state, RemoveTerm{ 0 });
		Check(state.region.terms.size() == 2 && state.region.terms[0].text == "@a" && state.region.terms[0].op == TermOp::kSet && state.region.solo == 1 && state.region.muted.empty() && !state.region.selected, "a removal closes up and drops what pointed at the row");
		Reduce(state, SoloTerm{ 1, false });
		Check(!state.region.solo, "solo off");
		Reduce(state, LoadRegion{ { Term{ TermOp::kAnd, "@m", "m" }, Term{ TermOp::kOr, "@n", "n" } }, "metal" });
		Check(state.region.terms.size() == 2 && state.region.terms[0].op == TermOp::kSet && state.region.editing == "metal" && state.region.selected == 0 && state.region.dirty, "a loaded region replaces the stack and names its mask");
		Reduce(state, PickTerm{ 1 });
		Check(state.region.selected == 1, "a term pick");
		Reduce(state, PickRecipe{ "other-recipe" });
		Check(state.region.terms.empty() && state.region.editing.empty(), "another recipe starts the stack over");
		Reduce(state, PickRecipe{ kRecipeID });
		Reduce(state, AddTerm{ Term{ TermOp::kSet, "@a", "a" } });
		Reduce(state, ClearRegion{});
		Check(state.region.terms.empty() && !state.region.dirty, "clear empties the stack");
		for (std::size_t i = 0; i < kMaxTerms + 2; ++i) {
			Reduce(state, AddTerm{ Term{ TermOp::kAnd, "@a", "a" } });
		}
		Check(state.region.terms.size() == kMaxTerms, "the stack stops at the cap");
		Reduce(state, ClearRegion{});
		RecipeKey armor;
		armor.kind = KeyKind::kArmor;
		Reduce(state, BeginPaint{ kRecipeID, armor, Surface::kShell });
		Check(state.paint && state.paint->recipe == kRecipeID && state.paint->surface == Surface::kShell, "a paint session names the active recipe and its surface");
		Reduce(state, SetPaintSurface{ Surface::kMaterial });
		Check(state.paint && state.paint->surface == Surface::kMaterial, "the preview surface changes");
		Check(state.paint && !state.paint->readPosted, "a fresh session has not asked for the shape's read");
		Reduce(state, ReadMesh{ kPlayer, "Cuirass" });
		Check(state.paint && state.paint->readPosted, "posting the read marks the session, so it is asked once");
		Reduce(state, AddTerm{ Term{ TermOp::kSet, "@a", "a" } });
		Reduce(state, KeepPaint{ kRecipeID, "chest" });
		Check(!state.paint && state.region.terms.empty(), "keep ends the session and empties the stack");
		Reduce(state, BeginPaint{ kRecipeID, armor, Surface::kMaterial });
		Reduce(state, EndPaint{});
		Check(!state.paint, "end drops the session");
		Reduce(state, SetMode{ Mode::kCompose });

		// Edits move the layer selection with the rows.
		Reduce(state, PickCell{ Surface::kShell, Slot::kEmissive, 1 });
		Reduce(state, EditRecipe{ kRecipeID, AddLayer{ 0, DefaultLayer(), 3 } });
		Check(state.selection.layer == 3, "an added layer is selected");
		Reduce(state, EditRecipe{ kRecipeID, RemoveLayer{ 0, 1 } });
		Check(state.selection.layer == 2, "removing a row above moves the selection up");
		Reduce(state, EditRecipe{ kRecipeID, RemoveLayer{ 0, 2 } });
		Check(!state.selection.layer, "removing the selected row clears the selection");
		Reduce(state, PickLayer{ 2 });
		Reduce(state, EditRecipe{ kRecipeID, MoveLayer{ 0, 2, 0 } });
		Check(state.selection.layer == 0, "the selection follows a moved row");
		Reduce(state, EditRecipe{ kRecipeID, MoveLayer{ 0, 2, 0 } });
		Check(state.selection.layer == 1, "a row moved above the selection pushes it down");
		Reduce(state, EditRecipe{ kRecipeID, MoveLayer{ 0, 0, 2 } });
		Check(state.selection.layer == 0, "a row moved from above to below pulls it up");
		Reduce(state, EditRecipe{ kRecipeID, ClearLayers{ 0 } });
		Check(!state.selection.layer, "clearing the layers clears the selection");
		Reduce(state, EditRecipe{ kRecipeID, AddOutput{ Surface::kMaterial, Slot::kCoat } });
		Check(state.selection.target == Target::kMaterial && state.selection.slot == Slot::kCoat, "an added output is picked");
		Reduce(state, EditRecipe{ kRecipeID, SetConstant{ "glowStrength", 1.0f } });
		Check(state.selection.slot == Slot::kCoat, "a value edit changes no selection");
		Reduce(state, CreateRecipe{ "fresh", RecipeKey{} });
		Check(state.selection.recipeID == "fresh", "a created recipe is picked");
		Reduce(state, SoloRecipe{ "fresh", true });
		Reduce(state, SetFreeze{ true, 1.0f });
		Reduce(state, Undo{ "fresh" });
		Check(state.selection.recipeID == "fresh" && state.selection.slot == Slot::kCoat, "view and manager intents change no selection");

		// Paint edge cases: a surface without a session, a piece pick during one.
		Reduce(state, SetPaintSurface{ Surface::kShell });
		Check(!state.paint, "a surface pick with no session opens none");
		Reduce(state, BeginPaint{ kRecipeID, armor, Surface::kShell });
		Reduce(state, AddTerm{ Term{ TermOp::kSet, "@a", "a" } });
		Reduce(state, PickPiece{ kPlayer, kCuirass, false });
		Check(state.paint && state.paint->recipe == kRecipeID && state.region.terms.empty(), "a piece pick during a session keeps the session and starts the stack over");
		Reduce(state, EndPaint{});
	}

	void Histories()
	{
		EditHistory history;
		Recipe      recipe = Canonical().value_or(Recipe{});
		Check(history.UndoDepth() == 0 && history.RedoDepth() == 0 && !history.Undo(recipe) && !history.Redo(recipe), "an empty history undoes and redoes nothing");
		Recipe before = recipe;
		recipe.metadata.name = "edited";
		history.Push(before);
		Check(history.UndoDepth() == 1 && history.RedoDepth() == 0, "an edit pushes one step");
		const auto undone = history.Undo(recipe);
		Check(undone && undone->metadata.name == before.metadata.name && history.UndoDepth() == 0 && history.RedoDepth() == 1, "undo restores the recipe before the edit and keeps the edited one for redo");
		const auto redone = history.Redo(*undone);
		Check(redone && redone->metadata.name == "edited" && history.UndoDepth() == 1 && history.RedoDepth() == 0, "redo restores the edit");
		history.Push(*redone);
		Check(history.UndoDepth() == 2 && history.RedoDepth() == 0, "a new edit after redo stacks on it");
		[[maybe_unused]] const auto back = history.Undo(recipe);
		history.Push(recipe);
		Check(history.RedoDepth() == 0, "an edit after undo forgets the redo");
		for (std::size_t i = 0; i < EditHistory::kCap + 10; ++i) {
			history.Push(recipe);
		}
		Check(history.UndoDepth() == EditHistory::kCap, "the history is capped");
		history.Clear();
		Check(history.UndoDepth() == 0 && history.RedoDepth() == 0, "clear empties both");
	}

	void References()
	{
		Check(ReferenceText("metal") == "@metal", "reference text adds the sigil");
		Check(ReferenceName("@metal") == "metal" && ReferenceName("metal") == "metal", "reference name strips one sigil");
		Check(ReferenceName("") == "" && ReferenceName("@") == "" && ReferenceText("") == "@", "empty texts");
	}

	void GeometryLabels()
	{
		Check(GeometryLabel("Armor003", "Iron Cuirass") == "Armor003", "an authored shape keeps its name");
		Check(GeometryLabel(" (FE034935)[0]/ (2500097A) [100%]", "Northern Iron Boots") == "Northern Iron Boots shape 0 (addon FE034935)", "an engine-built shape reads as armor, index and addon");
		Check(GeometryLabel(" (0008E840)[12]/ (00100E29) [50%]", "") == "armor 00100E29 shape 12 (addon 0008E840)", "no armor name falls back to the armor id");
		Check(GeometryLabel(" (FE03493)[0]/ (2500097A) [100%]", "Boots") == " (FE03493)[0]/ (2500097A) [100%]", "a short id is not the pattern");
		Check(GeometryLabel(" (FE034935)[x]/ (2500097A) [100%]", "Boots") == " (FE034935)[x]/ (2500097A) [100%]", "a non-numeric index is not the pattern");
		Check(GeometryLabel(" (FE034935)[0]", "Boots") == " (FE034935)[0]", "a truncated name is not the pattern");
		Check(GeometryLabel("", "Boots").empty(), "an empty name stays empty");
	}
}

namespace
{
	// The cluster map's row and form, and the id-map bakes' rows.
	void MaterialClusterRows()
	{
		MaterialClustersSource tuned;
		tuned.clusters = 3;
		tuned.luma = 2.0f;
		tuned.seed = 7;
		tuned.iterations = 64;
		const Source source{ "materials", tuned };
		const auto   row = SourceRowOf(source, 0);
		Check(row.kind == "materialClusters" && row.clusters == "3" && row.weights == "1, 1, 0.5, 0.5, 2" && row.seed == "7" && row.iterations == "64", "a materialClusters row carries its settings as text");
		const auto back = SourceKindOf(row);
		Check(back && *back == SourceKind{ tuned }, "the row reads back as its source");
		SourceRow bad = row;
		bad.clusters = "9";
		Check(!SourceKindOf(bad), "clusters past the cap read back as nothing");
		bad = row;
		bad.weights = "1, 2, 3";
		Check(!SourceKindOf(bad), "three weights read back as nothing");
		bad = row;
		bad.iterations = "0";
		Check(!SourceKindOf(bad), "zero iterations read back as nothing");
		bad = row;
		bad.seed = "-1";
		Check(!SourceKindOf(bad), "a negative seed reads back as nothing");

		SignalNames               names;
		std::optional<RecipeEdit> edit;
		const auto                form = SourceForm(row, names);
		Check(form.size() == 5 && form[1].name == "clusters" && form[1].kind == FieldKind::kScalar && form[2].name == "weights" && form[2].kind == FieldKind::kText && form[3].name == "seed" && form[4].name == "iterations", "the materialClusters form's fields");
		const auto* setClusters = Bound<SetSource>(form[1], "5", edit);
		const auto* changed = setClusters ? Get<MaterialClustersSource>(setClusters->kind) : nullptr;
		Check(changed && changed->clusters == 5 && changed->luma == 2.0f && changed->seed == 7, "a cluster count change keeps the rest");
		const auto* setWeights = Bound<SetSource>(form[2], "0, 0, 0, 0, 1", edit);
		const auto* weighted = setWeights ? Get<MaterialClustersSource>(setWeights->kind) : nullptr;
		Check(weighted && weighted->roughness == 0.0f && weighted->luma == 1.0f && weighted->clusters == 3, "the weights field sets all five");
		Check(!Bound<SetSource>(form[1], "many", edit), "a word is not a cluster count");
		const auto* setKind = Bound<SetSource>(form[0], "materialClusters", edit);
		Check(setKind && Get<MaterialClustersSource>(setKind->kind) && *Get<MaterialClustersSource>(setKind->kind) == MaterialClustersSource{}, "the kind field starts the cluster map at its defaults");

		SourceRow bake;
		bake.name = "b";
		bake.kind = "bake";
		bake.bake = "componentId";
		const auto kind = SourceKindOf(bake);
		Check(kind && Get<BakeSource>(*kind) && Get<ComponentIdBake>(Get<BakeSource>(*kind)->bake), "a componentId row reads back as the bake");
		const auto bakeForm = SourceForm(bake, names);
		Check(bakeForm.size() == 2 && bakeForm[1].names.size() == 7 && bakeForm[1].names[6] == "chartId", "an id-map bake has no further field and the choice offers both");
		Check(SourceRowOf(Source{ "c", BakeSource{ ChartIdBake{} } }, 0).bake == "chartId", "a chartId row names its bake");
	}
}

namespace
{
	// The recipe row with a term's new sources added, as the snapshot would
	// show it after the edits apply.
	RecipeRow WithSources(const RecipeRow& a_recipe, const std::vector<RecipeEdit>& a_edits)
	{
		RecipeRow row = a_recipe;
		for (const auto& edit : a_edits) {
			if (const auto* add = Get<AddSource>(edit)) {
				row.sourceRows.push_back(SourceRowOf(Source{ add->name, add->kind }, 0));
			}
		}
		return row;
	}

	// A geometry read and analysed: two parts, one chart, two material clusters.
	GeometryRow Analysed(const GeometryRow& a_geometry)
	{
		GeometryRow geometry = a_geometry;
		geometry.meshRead = true;
		geometry.partitions = PartitionsOf(SkinnedMesh());
		geometry.bones = BonesOf(SkinnedMesh());
		MeshRegion body;
		body.source = RegionSource::kComponent;
		body.id = 0;
		body.share = 0.6f;
		body.dominantBone = "NPC Spine2 [Spn2]";
		body.dominantShare = 0.9f;
		MeshRegion hand = body;
		hand.id = 1;
		hand.share = 0.4f;
		hand.dominantBone = "NPC L Hand [LHnd]";
		hand.dominantShare = 1.0f;
		MeshRegion chart;
		chart.source = RegionSource::kChart;
		chart.id = 0;
		chart.share = 1.0f;
		geometry.regions = { body, hand, chart };
		MaterialCluster leather;
		leather.id = 0;
		leather.share = 0.7f;
		leather.description = "rough dark non-metal";
		MaterialCluster steel;
		steel.id = 1;
		steel.share = 0.3f;
		steel.description = "polished bright metal";
		geometry.clusters = { leather, steel };
		return geometry;
	}

	// The template's text applied as a term, and a copy of the recipe applied
	// to a field's text; nothing when the field refuses it.
	std::optional<TermRecipe> Applied(const std::vector<TermField>& a_form, const char* a_field, const char* a_text)
	{
		const auto it = std::ranges::find(a_form, a_field, [](const TermField& f) { return f.field.name; });
		return it == a_form.end() || !it->apply ? std::nullopt : it->apply(a_text);
	}

	void TermTemplates(const RecipeRow& a_recipe, const GeometryRow& a_geometry)
	{
		const auto file = test::ReadFile(std::filesystem::path{ WEPBR_FIXTURES_DIR } / ".." / ".." / "presets" / "regions.json");
		const auto presets = ParsePresets(file);
		if (!presets) {
			test::Skip("the term template checks need presets/regions.json");
			return;
		}
		const auto existing = ExistingOf(a_recipe);
		const auto geometry = Analysed(a_geometry);

		// Every template round-trips: built over the canonical row, read back
		// over the row with the built sources added.
		ThresholdTerm roughness;
		roughness.channel = MaterialChannel::kRoughness;
		roughness.low = 0.35f;
		roughness.high = 0.6f;
		ThresholdTerm posterized = roughness;
		posterized.posterize = 4;
		posterized.invert = true;
		ThresholdTerm lowOnly = roughness;
		lowOnly.high = 1.0f;
		ThresholdTerm highOnly = roughness;
		highOnly.low = 0.0f;
		ThresholdTerm everything = roughness;
		everything.low = 0.0f;
		everything.high = 1.0f;
		ThresholdTerm metallic;
		metallic.channel = MaterialChannel::kMetallic;
		metallic.low = 0.5f;
		metallic.softness = 0.1f;
		ClusterSettings tuned;
		tuned.clusters = 3;
		tuned.weights.luma = 2.0f;
		tuned.seed = 7;
		const std::vector<TermRecipe> all{
			ReferenceTerm{ "metal" }, ReferenceTerm{ "fill" }, roughness, posterized, lowOnly, highOnly, everything, metallic, WhatPresetTerm{ "leather" }, WhatPresetTerm{ "dark" },
			PartitionTerm{ 33 }, BoneTerm{ { "NPC Spine2 [Spn2]", "NPC L UpperArm [LUar]" } }, ComponentTerm{ RegionSource::kComponent, 1 }, ComponentTerm{ RegionSource::kChart, 0 },
			ClusterTerm{ ClusterSettings{}, 1 }, ClusterTerm{ tuned, 2 }
		};
		for (const auto& recipe : all) {
			const auto built = BuildTerm(recipe, *presets, existing);
			const auto row = WithSources(a_recipe, built.edits);
			const auto back = ReadTerm(built.expression, *presets, row);
			Check(back == recipe, std::format("{} round-trips through BuildTerm and ReadTerm: {}", TermRecipeName(recipe), built.expression));
			Check(Program::Parse(built.expression).has_value(), std::format("{} builds an expression that parses: {}", TermRecipeName(recipe), built.expression));
		}

		// The spellings.
		const auto builtRoughness = BuildTerm(roughness, *presets, existing);
		Check(builtRoughness.expression == "smoothstep(0.35 - 0.05, 0.35 + 0.05, @roughness) * (1 - smoothstep(0.6 - 0.05, 0.6 + 0.05, @roughness))", "a threshold spells both edges over the channel: " + builtRoughness.expression);
		Check(builtRoughness.edits.size() == 1 && Get<AddSource>(builtRoughness.edits[0]) && Get<AddSource>(builtRoughness.edits[0])->name == "roughness", "a threshold adds its channel as a material source named after it");
		Check(BuildTerm(metallic, *presets, existing).edits.empty() && BuildTerm(metallic, *presets, existing).expression == "smoothstep(0.5 - 0.1, 0.5 + 0.1, @metallic)", "a threshold reuses the recipe's metallic source; low alone spells one edge");
		Check(BuildTerm(posterized, *presets, existing).expression == "1 - (smoothstep(0.35 - 0.05, 0.35 + 0.05, floor(@roughness * 4) / 4) * (1 - smoothstep(0.6 - 0.05, 0.6 + 0.05, floor(@roughness * 4) / 4)))", "posterize quantises the operand and invert wraps: " + BuildTerm(posterized, *presets, existing).expression);
		Check(BuildTerm(highOnly, *presets, existing).expression == "1 - smoothstep(0.6 - 0.05, 0.6 + 0.05, @roughness)", "high alone spells the complement of one edge");
		Check(BuildTerm(everything, *presets, existing).expression == "step(0, @roughness)", "0..1 keeps the channel readable");
		Check(BuildTerm(ReferenceTerm{ "metal" }, *presets, existing).expression == "@metal" && BuildTerm(ReferenceTerm{ "metal" }, *presets, existing).edits.empty(), "a reference is its name");
		const auto leather = BuildTerm(WhatPresetTerm{ "leather" }, *presets, existing);
		Check(leather.expression == "(1 - @metallic) * smoothstep(0.35, 0.6, @roughness)" && leather.edits.size() == 1, "a what preset materialises as the preset does");
		Check(BuildTerm(WhatPresetTerm{ "unknown" }, *presets, existing).expression == "0" && BuildTerm(WhatPresetTerm{ "unknown" }, *presets, existing).edits.empty(), "an unknown preset is 0 with no sources");
		const auto partition = BuildTerm(PartitionTerm{ 33 }, *presets, existing);
		Check(partition.expression == "@partition" && partition.edits.size() == 1 && Get<AddSource>(partition.edits[0])->name == "partition", "a partition adds its bake");
		const auto bones = BuildTerm(BoneTerm{ { "NPC Spine2 [Spn2]" } }, *presets, existing);
		Check(bones.expression == "@bones" && bones.edits.size() == 1 && Get<AddSource>(bones.edits[0])->name == "bones", "bones add their bake");
		const auto component = BuildTerm(ComponentTerm{ RegionSource::kComponent, 1 }, *presets, existing);
		Check(component.expression == "abs(@components * 255 - 1) < 0.5" && component.edits.size() == 1 && Get<AddSource>(component.edits[0])->name == "components" && Get<ComponentIdBake>(Get<BakeSource>(Get<AddSource>(component.edits[0])->kind)->bake), "a component tests the componentId bake");
		const auto chart = BuildTerm(ComponentTerm{ RegionSource::kChart, 0 }, *presets, existing);
		Check(chart.expression == "abs(@charts * 255 - 0) < 0.5" && chart.edits.size() == 1 && Get<AddSource>(chart.edits[0])->name == "charts", "a chart tests the chartId bake");
		const auto cluster = BuildTerm(ClusterTerm{ tuned, 2 }, *presets, existing);
		const auto* clusterSource = cluster.edits.size() == 1 ? Get<AddSource>(cluster.edits[0]) : nullptr;
		const auto* clusterKind = clusterSource ? Get<MaterialClustersSource>(clusterSource->kind) : nullptr;
		Check(cluster.expression == "abs(@clusters * 255 - 2) < 0.5" && clusterKind && clusterSource->name == "clusters" && clusterKind->clusters == 3 && clusterKind->luma == 2.0f && clusterKind->seed == 7, "a cluster tests a materialClusters source carrying its settings");
		Check(BuildTerm(RawTerm{}, *presets, existing).expression.empty() && BuildTerm(RawTerm{}, *presets, existing).edits.empty(), "a raw term builds nothing");
		Existing componentsTaken = existing;
		componentsTaken.taken.push_back("components");
		Check(BuildTerm(ComponentTerm{}, *presets, componentsTaken).expression == "abs(@components2 * 255 - 0) < 0.5", "a taken source name is made unique");

		// Reading: hand-written text and near misses are raw; a lone name of
		// a mask or an unknown row is a reference.
		Check(Is<RawTerm>(ReadTerm("@metallic * 2", *presets, a_recipe)), "hand-written text reads as raw");
		Check(Is<RawTerm>(ReadTerm("smoothstep(0.35 - 0.05, 0.35 + 0.06, @metallic)", *presets, a_recipe)), "an edge whose softness differs between its bounds is raw");
		Check(Is<RawTerm>(ReadTerm("smoothstep(0.35 - 0.05, 0.35 + 0.05, @fill)", *presets, a_recipe)), "a threshold over an image source is raw");
		Check(Is<RawTerm>(ReadTerm("abs(@metallic * 255 - 1) < 0.5", *presets, a_recipe)), "the region form over a material source is raw");
		Check(Is<RawTerm>(ReadTerm("", *presets, a_recipe)) && Is<RawTerm>(ReadTerm("1 - (", *presets, a_recipe)) && Is<RawTerm>(ReadTerm("smoothstep(", *presets, a_recipe)), "empty and truncated texts are raw");
		Check(ReadTerm("@metal", *presets, a_recipe) == TermRecipe{ ReferenceTerm{ "metal" } } && ReadTerm("@nothingKnown", *presets, a_recipe) == TermRecipe{ ReferenceTerm{ "nothingKnown" } }, "a lone name reads as a reference");
		Check(ReadTerm("(1 - @metallic) * smoothstep(0.35, 0.6, @roughness)", *presets, WithSources(a_recipe, leather.edits)) == TermRecipe{ WhatPresetTerm{ "leather" } }, "a what preset's expression over the recipe's names reads as the preset");
		Check(Is<RawTerm>(ReadTerm("(1 - @metallic) * smoothstep(0.35, 0.6, @roughness)", *presets, a_recipe)), "the same text without the roughness source is raw");

		// Labels.
		Check(TermLabelOf(roughness, *presets, geometry) == "roughness 0.35..0.6", "a threshold labels as its channel and range");
		Check(TermLabelOf(ReferenceTerm{ "metal" }, *presets, geometry) == "@metal" && TermLabelOf(WhatPresetTerm{ "leather" }, *presets, geometry) == "leather" && TermLabelOf(RawTerm{}, *presets, geometry) == "expression", "reference, preset and raw labels");
		Check(TermLabelOf(PartitionTerm{ 33 }, *presets, geometry) == "hands" && TermLabelOf(BoneTerm{ { "NPC Spine2 [Spn2]", "NPC L Hand [LHnd]" } }, *presets, geometry) == "chest, left hand", "partitions and bones label by their plain names");
		Check(TermLabelOf(ComponentTerm{ RegionSource::kComponent, 0 }, *presets, geometry) == "part 0: chest, 60%" && TermLabelOf(ComponentTerm{ RegionSource::kChart, 0 }, *presets, geometry) == "chart 0: 100%", "a part labels with its dominant bone and share; an unskinned chart with its share");
		Check(TermLabelOf(ComponentTerm{ RegionSource::kComponent, 7 }, *presets, geometry) == "part 7" && TermLabelOf(ComponentTerm{ RegionSource::kComponent, 0 }, *presets, a_geometry) == "part 0", "a part the geometry lacks labels by number alone");
		Check(TermLabelOf(ClusterTerm{ ClusterSettings{}, 1 }, *presets, geometry) == "material 1: polished bright metal, 30%" && TermLabelOf(ClusterTerm{ ClusterSettings{}, 5 }, *presets, geometry) == "material 5", "a cluster labels with its description and share");

		// A kept mask comes back with its recipes and labels.
		const auto keptText = std::format("({}) * ({})", builtRoughness.expression, "@metal");
		const auto terms = TermsOfMask(keptText, *presets, ExistingOf(WithSources(a_recipe, builtRoughness.edits)));
		Check(terms && terms->size() == 2 && (*terms)[0].recipe == TermRecipe{ roughness } && (*terms)[0].label == "roughness 0.35..0.6" && (*terms)[1].recipe == TermRecipe{ ReferenceTerm{ "metal" } } && (*terms)[1].label == "@metal", "a kept mask's terms carry their recipes and labels");
		Check(terms && ProposedRegionName(*terms, "") == "region" && ProposedRegionName(std::vector<Term>{ (*terms)[1] }, "") == "metal", "a reference label proposes its name");

		// Forms: one field per setting; a bad text applies to nothing.
		const auto thresholdForm = TermForm(roughness, *presets, geometry);
		Check(thresholdForm.size() == 6 && thresholdForm[0].field.name == "channel" && thresholdForm[0].field.kind == FieldKind::kChoice && thresholdForm[0].field.names.size() == 8 && thresholdForm[1].field.name == "low" && thresholdForm[1].field.text == "0.35" && thresholdForm[4].field.name == "posterize" && thresholdForm[5].field.name == "invert" && thresholdForm[5].field.kind == FieldKind::kToggle && thresholdForm[5].field.text == "off", "the threshold form's fields");
		const auto lowered = Applied(thresholdForm, "low", "0.2");
		Check(lowered && Get<ThresholdTerm>(*lowered) && Get<ThresholdTerm>(*lowered)->low == 0.2f && Get<ThresholdTerm>(*lowered)->high == 0.6f, "a low change keeps the rest");
		Check(!Applied(thresholdForm, "low", "much") && !Applied(thresholdForm, "low", "") && !Applied(thresholdForm, "posterize", "-1") && !Applied(thresholdForm, "posterize", "1.5") && !Applied(thresholdForm, "invert", "maybe") && !Applied(thresholdForm, "channel", "diffuseRgb"), "bad numbers, a fraction for posterize, a word for a toggle and a colour channel apply to nothing");
		const auto channelled = Applied(thresholdForm, "channel", "metallic");
		Check(channelled && Get<ThresholdTerm>(*channelled) && Get<ThresholdTerm>(*channelled)->channel == MaterialChannel::kMetallic, "the channel choice applies");
		const auto inverted = Applied(thresholdForm, "invert", "on");
		Check(inverted && Get<ThresholdTerm>(*inverted) && Get<ThresholdTerm>(*inverted)->invert, "the toggle applies");
		const auto clusterForm = TermForm(ClusterTerm{ tuned, 2 }, *presets, geometry);
		Check(clusterForm.size() == 7 && clusterForm[0].field.name == "clusters" && clusterForm[0].field.text == "3" && clusterForm[1].field.name == "roughness" && clusterForm[5].field.name == "luma" && clusterForm[5].field.text == "2" && clusterForm[6].field.name == "seed" && clusterForm[6].field.text == "7", "the cluster form's fields");
		const auto recounted = Applied(clusterForm, "clusters", "5");
		Check(recounted && Get<ClusterTerm>(*recounted) && Get<ClusterTerm>(*recounted)->settings.clusters == 5 && Get<ClusterTerm>(*recounted)->id == 2, "a cluster count change keeps the id");
		Check(!Applied(clusterForm, "clusters", "9") && !Applied(clusterForm, "clusters", "0") && !Applied(clusterForm, "luma", "-1") && !Applied(clusterForm, "seed", "x"), "counts past the cap, a negative weight and a word for a seed apply to nothing");
		const auto componentForm = TermForm(ComponentTerm{ RegionSource::kComponent, 0 }, *presets, geometry);
		Check(componentForm.size() == 1 && componentForm[0].field.kind == FieldKind::kChoice && componentForm[0].field.names.size() == 2 && componentForm[0].field.names[1] == "part 1: left hand, 40%" && componentForm[0].field.text == "part 0: chest, 60%", "the component form offers the parts of its source by label");
		const auto picked = Applied(componentForm, "id", "part 1: left hand, 40%");
		Check(picked && Get<ComponentTerm>(*picked) && Get<ComponentTerm>(*picked)->id == 1 && Get<ComponentTerm>(*picked)->source == RegionSource::kComponent, "a label picks its part");
		Check(!Applied(componentForm, "id", "part 9: nothing") && !Applied(componentForm, "id", "300"), "an unknown label and an id past the cap apply to nothing");
		const auto boneForm = TermForm(BoneTerm{ { "NPC Spine2 [Spn2]", "NPC L Hand [LHnd]" } }, *presets, geometry);
		Check(boneForm.size() == 1 && boneForm[0].field.kind == FieldKind::kText && boneForm[0].field.text == "NPC Spine2 [Spn2], NPC L Hand [LHnd]", "the bone form's list");
		const auto relisted = Applied(boneForm, "bones", " NPC Pelvis [Pelv] ,, NPC Neck [Neck]");
		Check(relisted && Get<BoneTerm>(*relisted) && Get<BoneTerm>(*relisted)->bones == std::vector<std::string>{ "NPC Pelvis [Pelv]", "NPC Neck [Neck]" }, "the list is split and trimmed");
		Check(!Applied(boneForm, "bones", " , "), "an empty list applies to nothing");
		const auto partitionForm = TermForm(PartitionTerm{ 33 }, *presets, geometry);
		Check(partitionForm.size() == 1 && partitionForm[0].field.names == std::vector<std::string>{ "body", "hands" } && partitionForm[0].field.text == "hands", "the partition form offers the geometry's partitions");
		const auto reslotted = Applied(partitionForm, "slot", "body");
		Check(reslotted && Get<PartitionTerm>(*reslotted) && Get<PartitionTerm>(*reslotted)->slot == 32 && Applied(partitionForm, "slot", "36") && !Applied(partitionForm, "slot", "wings"), "a plain name or a slot applies; an unknown name applies to nothing");
		Check(TermForm(RawTerm{}, *presets, geometry).empty() && TermForm(ReferenceTerm{ "x" }, *presets, geometry).empty() && TermForm(WhatPresetTerm{ "leather" }, *presets, geometry).empty(), "raw, reference and preset terms have no fields");

		// Offers: groups in order, the unread mesh saying why.
		const auto offers = OffersOf(*presets, a_recipe, geometry, "");
		std::size_t group = 0;
		bool        ordered = true;
		for (const auto& offer : offers) {
			const auto it = std::ranges::find(kOfferGroups, offer.group);
			const auto index = it == kOfferGroups.end() ? kOfferGroups.size() : static_cast<std::size_t>(it - kOfferGroups.begin());
			ordered = ordered && index >= group && index < kOfferGroups.size();
			group = std::max(group, index);
		}
		Check(ordered && !offers.empty(), "offers come in group order");
		const auto count = [&](std::string_view a_group) { return std::ranges::count(offers, a_group, &TermOffer::group); };
		Check(count("parts") == 3 && count("materials") == 2 && count("bones") == 3 && count("partitions") == 2 && count("channels") == 8 && count("presets") == 8 && count("masks") == 1 && count("sources") == 8, "one offer per part, cluster, bone, partition, channel, what preset, mask and source");
		Check(offers[0].name == "part 0" && offers[0].detail == "60% of the mesh, chest 90%" && !offers[0].unavailable && !offers[0].coverage && Get<ComponentTerm>(offers[0].recipe) && Get<ComponentTerm>(offers[0].recipe)->id == 0, "a part's offer carries its measurements, no coverage yet");
		Check(offers[3].name == "material 0" && offers[3].detail == "rough dark non-metal, 70%" && Get<ClusterTerm>(offers[3].recipe) && Get<ClusterTerm>(offers[3].recipe)->id == 0 && Get<ClusterTerm>(offers[3].recipe)->settings == ClusterSettings{}, "a cluster's offer at the default settings");
		const auto* channelOffer = Get<ThresholdTerm>(offers[static_cast<std::size_t>(count("parts") + count("materials") + count("bones") + count("partitions"))].recipe);
		Check(channelOffer && channelOffer->low == 0.5f && channelOffer->high == 1.0f && channelOffer->channel == MaterialChannel::kDiffuseLuma, "a channel's offer is its upper half");
		Check(std::ranges::any_of(offers, [](const TermOffer& o) { return o.group == "masks" && o.name == "metal" && o.detail == "@metallic"; }) && std::ranges::none_of(offers, [](const TermOffer& o) { return o.name == kScratchMask; }), "masks are offered with their expressions");
		Check(std::ranges::none_of(OffersOf(*presets, a_recipe, geometry, "metal"), [](const TermOffer& o) { return o.group == "masks"; }), "the mask being edited is not offered");
		const auto unread = OffersOf(*presets, a_recipe, a_geometry, "");
		Check(unread.size() >= 2 && unread[0].group == "parts" && unread[0].unavailable == "the mesh has not been read yet" && unread[1].group == "materials" && unread[1].unavailable.has_value(), "an unread mesh offers a reason in place of its parts and materials");
		Check(std::ranges::none_of(unread, [](const TermOffer& o) { return o.group == "bones" || o.group == "partitions"; }) && std::ranges::count(unread, "channels", &TermOffer::group) == 8, "an unread mesh has no bones or partitions to offer; the channels stay");
	}
}

int main()
{
	Layouts();
	MaterialClusterRows();
	Colours();
	Reductions();
	Histories();
	UniqueNames();
	Filters();
	References();
	GeometryLabels();
	const auto recipe = Canonical();
	if (!recipe) {
		test::Skip("the snapshot checks need schema/example-magicka.json");
		return test::Finish("studio");
	}
	const auto snapshot = SnapshotOf(*recipe);
	Selections(snapshot);
	const auto& piece = snapshot.pieces[0];
	const auto& row = piece.recipes[1];
	const auto& geometry = row.geometries[0];
	Check(row.id == kRecipeID && row.signals.size() == 20 && geometry.outputs.size() == 5, "the snapshot carries the canonical recipe");
	BoardCells(row, geometry);
	BoardStates(row, geometry);
	Stacks(piece, row, geometry);
	NormalBlends(piece, row, geometry);
	Inspectors(row, geometry);
	InspectorForms(row, geometry);
	ScalarForms(piece, row, geometry);
	SignalLists(row);
	SignalForms(row);
	PanelForms(row);
	Checks(row, geometry);
	SourceForms(row);
	Creators(row, geometry);
	Regions(row, geometry);
	TermTemplates(row, geometry);
	return test::Finish("studio");
}
