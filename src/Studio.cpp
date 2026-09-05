#include "Studio.h"

#include <algorithm>
#include <format>

namespace WornEnchantmentPBR::Studio
{
	namespace
	{
		constexpr Blend kEveryBlend[]{ Blend::kReplace, Blend::kMultiply, Blend::kAdd, Blend::kSubtract, Blend::kScreen, Blend::kLerp, Blend::kNormal };

		// A parameter text names one signal when it is "@name" and nothing else:
		// a colour written "1, 0.5, @blue" references per component, which the
		// inspector does not follow.
		[[nodiscard]] bool IsWholeReference(std::string_view a_text) noexcept
		{
			return a_text.size() > 1 && a_text.starts_with('@') && a_text.find(',') == std::string_view::npos;
		}

		[[nodiscard]] bool IsMaterialOutput(const OutputRow& a_output) noexcept
		{
			return !a_output.light;
		}

		[[nodiscard]] bool WritesCell(const OutputRow& a_output, Surface a_surface, Slot a_slot) noexcept
		{
			return IsMaterialOutput(a_output) && a_output.surface == a_surface && a_output.slot == a_slot;
		}

		// A layer belongs to the selected region when its mask is that region;
		// with no region selected, every layer is in.
		[[nodiscard]] bool LayerInRegion(const LayerRow& a_layer, const std::string& a_region)
		{
			return a_region.empty() || a_layer.mask == ReferenceText(a_region);
		}

		[[nodiscard]] bool OutputIsolated(const View& a_view, const std::string& a_recipe, std::size_t a_output) noexcept
		{
			return a_view.isolateRecipe == a_recipe && a_view.isolateOutput >= 0 && static_cast<std::size_t>(a_view.isolateOutput) == a_output;
		}

		[[nodiscard]] bool LayerSoloed(const View& a_view, const std::string& a_recipe, std::size_t a_output, std::size_t a_layer) noexcept
		{
			return OutputIsolated(a_view, a_recipe, a_output) && a_view.isolateLayer >= 0 && static_cast<std::size_t>(a_view.isolateLayer) == a_layer;
		}

		[[nodiscard]] std::vector<Blend> BlendsFor(Slot a_slot)
		{
			std::vector<Blend> blends;
			for (const auto blend : kEveryBlend) {
				if (BlendAllowed(a_slot, blend)) {
					blends.push_back(blend);
				}
			}
			return blends;
		}

		[[nodiscard]] const ImageRow* FindImage(const std::vector<ImageRow>& a_rows, std::string_view a_name) noexcept
		{
			const auto it = std::ranges::find(a_rows, a_name, &ImageRow::name);
			return it == a_rows.end() ? nullptr : &*it;
		}

		[[nodiscard]] const SignalRow* FindSignal(const RecipeRow& a_recipe, std::string_view a_name) noexcept
		{
			const auto it = std::ranges::find(a_recipe.signals, a_name, &SignalRow::name);
			return it == a_recipe.signals.end() ? nullptr : &*it;
		}

		[[nodiscard]] const TextRow* FindCurve(const RecipeRow& a_recipe, std::string_view a_name) noexcept
		{
			const auto it = std::ranges::find(a_recipe.curves, a_name, &TextRow::name);
			return it == a_recipe.curves.end() ? nullptr : &*it;
		}

		[[nodiscard]] const GeometryRow* FindGeometry(const RecipeRow& a_recipe, std::string_view a_name) noexcept
		{
			const auto it = std::ranges::find(a_recipe.geometries, a_name, &GeometryRow::name);
			return it == a_recipe.geometries.end() ? nullptr : &*it;
		}

		// The masks the layers read, each once, in the order first used.
		[[nodiscard]] std::vector<std::string> BadgesOf(const std::vector<LayerRow>& a_layers)
		{
			std::vector<std::string> badges;
			for (const auto& layer : a_layers) {
				if (layer.mask.empty()) {
					continue;
				}
				const auto name = ReferenceName(layer.mask);
				if (std::ranges::find(badges, name) == badges.end()) {
					badges.push_back(name);
				}
			}
			return badges;
		}

		[[nodiscard]] std::size_t CountInRegion(const std::vector<LayerRow>& a_layers, const std::string& a_region)
		{
			return static_cast<std::size_t>(std::ranges::count_if(a_layers, [&](const LayerRow& a_layer) { return LayerInRegion(a_layer, a_region); }));
		}

		// Cell state for a slot the surface offers: the first output on it wins
		// (written, or refused when the binding would not take it); otherwise
		// the slot is excluded by a feature another output on the surface
		// already takes, or empty.
		void FillCell(Cell& a_cell, const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Selection& a_selection, const View& a_view)
		{
			const OutputRow* first = nullptr;
			for (const auto& output : a_geometry.outputs) {
				if (!WritesCell(output, a_cell.surface, a_cell.slot)) {
					continue;
				}
				if (!first) {
					first = &output;
				} else {
					a_cell.reason = std::format("output {} also writes this slot; the first one is shown", output.index);
				}
			}
			if (first) {
				a_cell.state = first->problem.empty() ? CellState::kWritten : CellState::kRefused;
				if (!first->problem.empty()) {
					a_cell.reason = first->problem;
				}
				a_cell.output = first->index;
				a_cell.layers = first->layers.size();
				a_cell.layersInRegion = CountInRegion(first->layers, a_selection.region);
				a_cell.animated = first->animated;
				a_cell.replace = first->replace;
				a_cell.composite = first->texture;
				a_cell.scalars = first->scalars;
				a_cell.badges = BadgesOf(first->layers);
				a_cell.isolated = OutputIsolated(a_view, a_recipe.id, first->index);
				return;
			}
			for (const auto& output : a_geometry.outputs) {
				if (IsMaterialOutput(output) && output.surface == a_cell.surface && SlotsExclude(a_cell.slot, output.slot)) {
					a_cell.state = CellState::kExcluded;
					a_cell.reason = std::format("excluded by {} (output {})", output.slotName, output.index);
					return;
				}
			}
			a_cell.state = CellState::kEmpty;
		}

		[[nodiscard]] LightCell BuildLightCell(const RecipeRow& a_recipe, const GeometryRow& a_geometry, const View& a_view)
		{
			LightCell light;
			light.output = a_recipe.lightOutput;
			if (!light.output) {
				const auto it = std::ranges::find_if(a_geometry.outputs, [](const OutputRow& a_output) { return a_output.light; });
				if (it != a_geometry.outputs.end()) {
					light.output = it->index;
				}
			}
			light.present = light.output.has_value();
			light.description = a_recipe.light;
			light.isolated = light.output && OutputIsolated(a_view, a_recipe.id, *light.output);
			return light;
		}

		// Where the recipe sits in the piece's merge order; by priority when
		// the piece does not list it (a stale selection).
		[[nodiscard]] bool MergesBefore(const PieceRow& a_piece, const RecipeRow& a_other, const RecipeRow& a_recipe) noexcept
		{
			const auto mine = std::ranges::find(a_piece.recipes, a_recipe.id, &RecipeRow::id);
			const auto theirs = std::ranges::find(a_piece.recipes, a_other.id, &RecipeRow::id);
			if (mine == a_piece.recipes.end() || theirs == a_piece.recipes.end()) {
				return a_other.priority <= a_recipe.priority;
			}
			return theirs < mine;
		}

		// Other recipes' layers on the same slot of a geometry of the same
		// name, split by merge order around this recipe.
		void FillForeignRows(StackView& a_stack, const PieceRow& a_piece, const RecipeRow& a_recipe, const GeometryRow& a_geometry)
		{
			for (const auto& other : a_piece.recipes) {
				if (other.id == a_recipe.id) {
					continue;
				}
				const auto* geometry = FindGeometry(other, a_geometry.name);
				if (!geometry) {
					continue;
				}
				auto& rows = MergesBefore(a_piece, other, a_recipe) ? a_stack.below : a_stack.above;
				for (const auto& output : geometry->outputs) {
					if (!WritesCell(output, a_stack.surface, a_stack.slot)) {
						continue;
					}
					for (const auto& layer : output.layers) {
						rows.push_back(ForeignRow{ other.id, other.priority, layer });
					}
				}
			}
		}

		void AddSignalNamed(std::vector<SignalRow>& a_signals, const RecipeRow& a_recipe, std::string_view a_text)
		{
			if (!IsWholeReference(a_text)) {
				return;
			}
			const auto* signal = FindSignal(a_recipe, ReferenceName(a_text));
			if (signal && std::ranges::find(a_signals, signal->name, &SignalRow::name) == a_signals.end()) {
				a_signals.push_back(*signal);
			}
		}
	}

	// ------------------------------------------------------------------ modes

	std::string_view ModeName(Mode a_mode) noexcept
	{
		switch (a_mode) {
		case Mode::kCompose:
			return "Compose";
		case Mode::kSignals:
			return "Signals";
		case Mode::kPaint:
			return "Paint";
		case Mode::kDesign:
			return "Design";
		}
		return "?";
	}

	Layout LayoutFor(Mode a_mode) noexcept
	{
		Layout layout;
		layout.mode = a_mode;
		switch (a_mode) {
		case Mode::kCompose:
			break;
		case Mode::kSignals:
			layout.stack = false;
			layout.inspector = false;
			layout.signals = true;
			break;
		case Mode::kPaint:
			layout.regionEditor = true;
			break;
		case Mode::kDesign:
			layout.stack = false;
			layout.inspector = false;
			layout.designPanel = true;
			layout.widgetScale = 1.6f;
			layout.compositeSize = 128.0f;
			layout.developerSignals = false;
			break;
		}
		return layout;
	}

	// -------------------------------------------------------------- selection

	const PieceRow* SelectedPiece(const Snapshot& a_snapshot, const Selection& a_selection) noexcept
	{
		for (const auto& piece : a_snapshot.pieces) {
			if (piece.actorID == a_selection.actorID && piece.armorID == a_selection.armorID && piece.firstPerson == a_selection.firstPerson) {
				return &piece;
			}
		}
		return a_snapshot.pieces.empty() ? nullptr : &a_snapshot.pieces.front();
	}

	const RecipeRow* SelectedRecipe(const PieceRow* a_piece, const Selection& a_selection) noexcept
	{
		if (!a_piece || a_piece->recipes.empty()) {
			return nullptr;
		}
		for (const auto& recipe : a_piece->recipes) {
			if (recipe.id == a_selection.recipeID) {
				return &recipe;
			}
		}
		return &a_piece->recipes.back();
	}

	const GeometryRow* SelectedGeometry(const RecipeRow* a_recipe, const Selection& a_selection) noexcept
	{
		if (!a_recipe || a_recipe->geometries.empty()) {
			return nullptr;
		}
		const auto* named = FindGeometry(*a_recipe, a_selection.geometry);
		return named ? named : &a_recipe->geometries.front();
	}

	const OutputRow* SelectedOutput(const GeometryRow* a_geometry, const Selection& a_selection) noexcept
	{
		if (!a_geometry || !a_selection.output) {
			return nullptr;
		}
		for (const auto& output : a_geometry->outputs) {
			if (output.index == *a_selection.output) {
				return &output;
			}
		}
		return nullptr;
	}

	// ------------------------------------------------------------------ board

	const Cell* CellAt(const Board& a_board, Surface a_surface, Slot a_slot) noexcept
	{
		for (const auto& cell : a_board.cells) {
			if (cell.surface == a_surface && cell.slot == a_slot) {
				return &cell;
			}
		}
		return nullptr;
	}

	Board BuildBoard(const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Selection& a_selection, const View& a_view)
	{
		Board board;
		board.cells.reserve(kSlotCount * 2);
		for (std::size_t i = 0; i < kSlotCount; ++i) {
			const auto slot = static_cast<Slot>(i);
			for (const auto surface : { Surface::kMaterial, Surface::kShell }) {
				Cell cell;
				cell.surface = surface;
				cell.slot = slot;
				// A shell column with no shell yet is still offered: adding an
				// output there is what makes the shell.
				if (SurfaceHasSlot(surface, a_recipe.shellMaterial, slot)) {
					FillCell(cell, a_recipe, a_geometry, a_selection, a_view);
				}
				board.cells.push_back(std::move(cell));
			}
		}
		board.light = BuildLightCell(a_recipe, a_geometry, a_view);
		board.regions = a_recipe.masks;
		board.region = a_selection.region;
		board.shell = a_geometry.shell;
		return board;
	}

	// ------------------------------------------------------------------ stack

	std::optional<StackView> BuildStackView(const PieceRow& a_piece, const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Selection& a_selection, const View& a_view)
	{
		const auto* output = SelectedOutput(&a_geometry, a_selection);
		if (!output || !IsMaterialOutput(*output)) {
			return std::nullopt;
		}
		StackView stack;
		stack.output = output->index;
		stack.surface = output->surface;
		stack.slot = output->slot;
		stack.title = std::format("{} on {}", output->slotName, output->target);
		stack.rows.reserve(output->layers.size());
		for (std::size_t i = 0; i < output->layers.size(); ++i) {
			StackRow row;
			row.index = i;
			row.layer = output->layers[i];
			row.inRegion = LayerInRegion(row.layer, a_selection.region);
			row.muted = a_view.LayerMuted(a_recipe.id, output->index, i);
			row.soloed = LayerSoloed(a_view, a_recipe.id, output->index, i);
			row.selected = a_selection.layer && *a_selection.layer == i;
			stack.rows.push_back(std::move(row));
		}
		FillForeignRows(stack, a_piece, a_recipe, a_geometry);
		stack.composite = output->texture;
		stack.animated = output->animated;
		stack.size = output->size;
		stack.problem = output->problem;
		stack.scalars = output->scalars;
		stack.blends = BlendsFor(output->slot);
		stack.masks = a_recipe.masks;
		for (const auto& signal : a_recipe.signals) {
			if (signal.type == ValueType::kScalar) {
				stack.scalarSignals.push_back(signal.name);
			} else if (signal.type == ValueType::kVec3) {
				stack.colorSignals.push_back(signal.name);
			}
		}
		stack.isolated = OutputIsolated(a_view, a_recipe.id, output->index);
		return stack;
	}

	// -------------------------------------------------------------- inspector

	std::optional<Inspector> BuildInspector(const RecipeRow& a_recipe, const GeometryRow& a_geometry, const Selection& a_selection)
	{
		const auto* output = SelectedOutput(&a_geometry, a_selection);
		if (!output || !IsMaterialOutput(*output) || !a_selection.layer || *a_selection.layer >= output->layers.size()) {
			return std::nullopt;
		}
		Inspector inspector;
		inspector.output = output->index;
		inspector.layer = *a_selection.layer;
		inspector.slot = output->slot;
		inspector.row = output->layers[inspector.layer];
		const auto& row = inspector.row;

		// A layer's source may name a source or a mask; its mask only a mask.
		if (IsWholeReference(row.source)) {
			const auto name = ReferenceName(row.source);
			const auto* image = FindImage(a_geometry.sources, name);
			if (!image) {
				image = FindImage(a_geometry.masks, name);
			}
			if (image) {
				inspector.source = *image;
			}
		}
		if (!row.mask.empty()) {
			if (const auto* image = FindImage(a_geometry.masks, ReferenceName(row.mask))) {
				inspector.mask = *image;
			}
		}
		AddSignalNamed(inspector.signals, a_recipe, row.opacityText);
		AddSignalNamed(inspector.signals, a_recipe, row.color);
		if (IsWholeReference(row.curve)) {
			if (const auto* curve = FindCurve(a_recipe, ReferenceName(row.curve))) {
				inspector.curve = *curve;
			}
		}
		inspector.blends = BlendsFor(output->slot);
		for (const auto& source : a_geometry.sources) {
			inspector.sources.push_back(source.name);
		}
		for (const auto& mask : a_geometry.masks) {
			inspector.masks.push_back(mask.name);
		}
		for (const auto& curve : a_recipe.curves) {
			inspector.curves.push_back(curve.name);
		}
		for (const auto& signal : a_recipe.signals) {
			if (signal.type == ValueType::kScalar) {
				inspector.scalarSignals.push_back(signal.name);
			} else if (signal.type == ValueType::kVec3) {
				inspector.colorSignals.push_back(signal.name);
			}
		}
		return inspector;
	}

	// ---------------------------------------------------------------- signals

	SignalList BuildSignalList(const RecipeRow& a_recipe, const Layout& a_layout)
	{
		SignalList list;
		for (const auto& signal : a_recipe.signals) {
			const bool tunable = signal.kind == "constant" || signal.kind == "expr";
			if (tunable) {
				list.tunable.push_back(signal);
			} else if (a_layout.developerSignals) {
				list.developer.push_back(signal);
			}
		}
		return list;
	}

	// ------------------------------------------------------------------ names

	std::string ReferenceText(std::string_view a_name)
	{
		return "@" + std::string{ a_name };
	}

	std::string GeometryLabel(std::string_view a_name, std::string_view a_armorName)
	{
		// " (FE034935)[0]/ (2500097A) [100%]": eight hex digits in each pair of
		// parentheses, an index in brackets, a weight in brackets.
		const auto hex = [](std::string_view a_text, std::size_t a_at) {
			if (a_at + 8 > a_text.size()) {
				return false;
			}
			for (std::size_t i = 0; i < 8; ++i) {
				const char ch = a_text[a_at + i];
				const bool digit = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f');
				if (!digit) {
					return false;
				}
			}
			return true;
		};
		const auto number = [](std::string_view a_text, std::size_t a_at, char a_close) -> std::optional<std::pair<std::string_view, std::size_t>> {
			std::size_t end = a_at;
			while (end < a_text.size() && a_text[end] >= '0' && a_text[end] <= '9') {
				++end;
			}
			if (end == a_at || end >= a_text.size() || a_text[end] != a_close) {
				return std::nullopt;
			}
			return std::pair{ a_text.substr(a_at, end - a_at), end + 1 };
		};
		if (!a_name.starts_with(" (") || !hex(a_name, 2) || a_name.substr(10, 2) != ")[") {
			return std::string{ a_name };
		}
		const auto addon = a_name.substr(2, 8);
		const auto index = number(a_name, 12, ']');
		if (!index || a_name.substr(index->second, 3) != "/ (" || !hex(a_name, index->second + 3)) {
			return std::string{ a_name };
		}
		const auto armor = a_armorName.empty() ? std::string{ "armor " } + std::string{ a_name.substr(index->second + 3, 8) } : std::string{ a_armorName };
		return std::format("{} shape {} (addon {})", armor, index->first, addon);
	}

	std::string ReferenceName(std::string_view a_text)
	{
		return std::string{ a_text.starts_with('@') ? a_text.substr(1) : a_text };
	}
}
