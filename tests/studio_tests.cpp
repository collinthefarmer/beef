// The studio's view models: layouts per mode, selection defaults, the board
// from the canonical recipe, the stack view, the inspector, the signal split.
// The snapshot is built from schema/example-magicka.json as the manager
// would fill it, with no textures and no runtime verdicts.

#include "Signals.h"
#include "Forms.h"
#include "History.h"
#include "MenuState.h"
#include "Studio.h"
#include "test_support.h"

#include <algorithm>
#include <filesystem>
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
			geometry.sources.push_back({ source.name, DescribeSource(source.kind), nullptr, 4, false, "" });
		}
		for (const auto& mask : a_recipe.masks) {
			geometry.masks.push_back({ mask.name, mask.text, nullptr, 5, false, "" });
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
		Check(paint.stack && paint.inspector && paint.signals && paint.regionEditor && !paint.designPanel, "paint adds the region editor and keeps the inspector and the signals");
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
		Check(strength.name == "strength" && strength.kind == FieldKind::kScalar && strength.text == "@glowLevel" && strength.names == emissive->scalarSignals && !strength.allowEmpty && !strength.detail, "strength is a scalar field over the scalar signals");
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
		Reduce(state, CreateRecipe{ "fresh", kCuirass });
		Check(state.selection.recipeID == "fresh", "a created recipe is picked");
		Reduce(state, SoloRecipe{ "fresh", true });
		Reduce(state, SetFreeze{ true, 1.0f });
		Reduce(state, Undo{ "fresh" });
		Check(state.selection.recipeID == "fresh" && state.selection.slot == Slot::kCoat, "view and manager intents change no selection");
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

int main()
{
	Layouts();
	Colours();
	Reductions();
	Histories();
	UniqueNames();
	Filters();
	References();
	GeometryLabels();
	const auto recipe = Canonical();
	if (!recipe) {
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
	return test::Finish("studio");
}
