#include "Studio.h"

#include "Expression.h"
#include "Forms.h"

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

		// ------------------------------------------------------------ forms
		// How a field's text reads as a value. Each binding yields the edit,
		// or nothing when the text does not parse; an empty text is a value
		// only where the field allows none.

		[[nodiscard]] std::optional<CurveRef> CurveRefOf(const std::string& a_text)
		{
			return a_text.empty() ? std::nullopt : std::optional{ CurveRef{ a_text } };
		}

		[[nodiscard]] std::optional<Ref> MaskRefOf(const std::string& a_text)
		{
			const auto name = ReferenceName(a_text);
			return name.empty() ? std::nullopt : std::optional{ Ref{ name } };
		}

		[[nodiscard]] FieldBinding BindLayerSource(std::size_t a_output, std::size_t a_layer)
		{
			return [=](const std::string& a_text) -> std::optional<RecipeEdit> {
				const auto source = ParseLayerSource(a_text);
				if (!source) {
					return std::nullopt;
				}
				return SetLayerSource{ a_output, a_layer, *source };
			};
		}

		[[nodiscard]] FieldBinding BindLayerCurve(std::size_t a_output, std::size_t a_layer)
		{
			return [=](const std::string& a_text) -> std::optional<RecipeEdit> {
				return SetLayerCurve{ a_output, a_layer, CurveRefOf(a_text) };
			};
		}

		[[nodiscard]] FieldBinding BindLayerOpacity(std::size_t a_output, std::size_t a_layer)
		{
			return [=](const std::string& a_text) -> std::optional<RecipeEdit> {
				const auto opacity = ParseParam(a_text);
				if (!opacity) {
					return std::nullopt;
				}
				return SetLayerOpacity{ a_output, a_layer, *opacity };
			};
		}

		[[nodiscard]] FieldBinding BindLayerColor(std::size_t a_output, std::size_t a_layer)
		{
			return [=](const std::string& a_text) -> std::optional<RecipeEdit> {
				if (a_text.empty()) {
					return SetLayerColor{ a_output, a_layer, std::nullopt };
				}
				const auto color = ParseVec3Param(a_text);
				if (!color) {
					return std::nullopt;
				}
				return SetLayerColor{ a_output, a_layer, *color };
			};
		}

		[[nodiscard]] FieldBinding BindLayerMask(std::size_t a_output, std::size_t a_layer)
		{
			return [=](const std::string& a_text) -> std::optional<RecipeEdit> {
				return SetLayerMask{ a_output, a_layer, MaskRefOf(a_text) };
			};
		}

		[[nodiscard]] FieldBinding BindLayerChannels(std::size_t a_output, std::size_t a_layer)
		{
			return [=](const std::string& a_text) -> std::optional<RecipeEdit> {
				const auto channels = ChannelSet::Parse(a_text);
				if (!channels) {
					return std::nullopt;
				}
				return SetLayerChannels{ a_output, a_layer, *channels };
			};
		}

		// A colour scalar reads as a colour parameter, every other scalar as a
		// number or @signal. A name that is no slot field binds to nothing.
		[[nodiscard]] FieldBinding BindScalar(std::size_t a_output, std::optional<ScalarField> a_field)
		{
			return [=](const std::string& a_text) -> std::optional<RecipeEdit> {
				if (!a_field) {
					return std::nullopt;
				}
				if (*a_field == ScalarField::kColor) {
					const auto color = ParseVec3Param(a_text);
					if (!color) {
						return std::nullopt;
					}
					return SetColorScalar{ a_output, *color };
				}
				const auto value = ParseParam(a_text);
				if (!value) {
					return std::nullopt;
				}
				return SetScalar{ a_output, *a_field, *value };
			};
		}

		// A parameter text has a detail when it names a signal the inspector found.
		[[nodiscard]] bool NamesSignal(const Inspector& a_inspector, const std::string& a_text)
		{
			return a_text.starts_with('@') && std::ranges::find(a_inspector.signals, ReferenceName(a_text), &SignalRow::name) != a_inspector.signals.end();
		}

		[[nodiscard]] std::optional<FieldDetail> DetailWhen(bool a_present, FieldDetail a_detail) noexcept
		{
			return a_present ? std::optional{ a_detail } : std::nullopt;
		}
	}

	// ------------------------------------------------------------------ modes

	std::string_view ModeName(Mode a_mode) noexcept
	{
		switch (a_mode) {
		case Mode::kCompose:
			return "Compose";
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
		case Mode::kPaint:
			layout.regionEditor = true;
			break;
		case Mode::kDesign:
			layout.stack = false;
			layout.inspector = false;
			layout.signals = false;
			layout.designPanel = true;
			layout.widgetScale = 1.6f;
			layout.compositeSize = 128.0f;
			layout.developerSignals = false;
			break;
		}
		return layout;
	}

	// -------------------------------------------------------------- selection

	std::string_view TargetName(Target a_target) noexcept
	{
		switch (a_target) {
		case Target::kMaterial:
			return "material";
		case Target::kShell:
			return "shell";
		case Target::kLight:
			return "light";
		}
		return "?";
	}

	Surface SurfaceOf(Target a_target) noexcept
	{
		return a_target == Target::kShell ? Surface::kShell : Surface::kMaterial;
	}

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
		if (!a_geometry || a_selection.target == Target::kLight || !a_selection.slot) {
			return nullptr;
		}
		const Surface surface = SurfaceOf(a_selection.target);
		for (const auto& output : a_geometry->outputs) {
			if (WritesCell(output, surface, *a_selection.slot)) {
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

	// ------------------------------------------------------------- panels

	LightRow LightRowOf(const Recipe& a_recipe)
	{
		LightRow row;
		for (std::size_t i = 0; i < a_recipe.outputs.size(); ++i) {
			const auto* light = Get<LightOutput>(a_recipe.outputs[i]);
			if (!light) {
				continue;
			}
			row.present = true;
			row.output = i;
			row.color = Vec3ParamText(light->color);
			row.intensity = ParamText(light->intensity);
			row.size = ParamText(light->size);
			row.cutoff = ParamText(light->cutoff);
			row.offset = Vec3ParamText(light->offset);
			row.shadow = light->shadow;
			Match(
				light->bones,
				[&](const SkinnedBones& b) {
					row.bones = "skinned";
					row.bonesMax = std::to_string(b.max);
					row.bonesMinShare = ParamText(b.minShare);
				},
				[&](const NamedBones& b) {
					row.bones = "named";
					for (const auto& bone : b.bones) {
						row.bonesNames += (row.bonesNames.empty() ? "" : ", ") + bone;
					}
				});
			break;
		}
		return row;
	}

	ShellRow ShellRowOf(const Recipe& a_recipe)
	{
		const auto& shell = a_recipe.shell;
		ShellRow    row;
		row.material = shell.material;
		row.blend = shell.blend;
		row.depthBias = shell.depthBias;
		row.alphaTest = shell.alphaTest;
		row.alpha = ParamText(shell.alpha);
		row.rimPower = ParamText(shell.rimPower);
		row.emissive = ParamText(shell.emissive);
		row.inflate = Vec3ParamText(shell.pose.inflate);
		row.offset = Vec3ParamText(shell.pose.offset);
		row.scale = ParamText(shell.pose.scale);
		row.spin = ParamText(shell.pose.spin);
		row.scalePoint = shell.pose.scalePoint;
		row.spinAxis = shell.pose.spinAxis;
		return row;
	}

	SignalNames SignalNamesOf(const RecipeRow& a_recipe)
	{
		SignalNames names;
		for (const auto& signal : a_recipe.signals) {
			if (signal.type == ValueType::kScalar) {
				names.scalar.push_back(signal.name);
			} else if (signal.type == ValueType::kVec3) {
				names.color.push_back(signal.name);
			}
		}
		return names;
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

	// ------------------------------------------------------------------ forms

	std::string_view FieldDetailName(FieldDetail a_detail) noexcept
	{
		switch (a_detail) {
		case FieldDetail::kSource:
			return "source";
		case FieldDetail::kCurve:
			return "curve";
		case FieldDetail::kOpacity:
			return "opacity";
		case FieldDetail::kColor:
			return "colour";
		case FieldDetail::kMask:
			return "mask";
		}
		return "?";
	}

	std::vector<FieldSpec> InspectorForm(const Inspector& a_inspector)
	{
		const auto&       in = a_inspector;
		const std::size_t output = in.output;
		const std::size_t layer = in.layer;

		// A layer's source may be a source or a mask row, so its combo lists both.
		std::vector<std::string> sourceNames = in.sources;
		sourceNames.insert(sourceNames.end(), in.masks.begin(), in.masks.end());

		std::vector<FieldSpec> form;
		form.push_back(FieldSpec{ "source", FieldKind::kColor, in.row.source, std::move(sourceNames), false, DetailWhen(in.source.has_value(), FieldDetail::kSource), std::nullopt, BindLayerSource(output, layer) });
		form.push_back(FieldSpec{ "curve", FieldKind::kCurve, in.row.curve, in.curves, true, DetailWhen(in.curve.has_value(), FieldDetail::kCurve), std::nullopt, BindLayerCurve(output, layer) });
		form.push_back(FieldSpec{ "opacity", FieldKind::kScalar, in.row.opacityText, in.scalarSignals, false, DetailWhen(NamesSignal(in, in.row.opacityText), FieldDetail::kOpacity), std::nullopt, BindLayerOpacity(output, layer) });
		form.push_back(FieldSpec{ "colour", FieldKind::kColor, in.row.color, in.colorSignals, true, DetailWhen(NamesSignal(in, in.row.color), FieldDetail::kColor), std::nullopt, BindLayerColor(output, layer) });
		form.push_back(FieldSpec{ "mask", FieldKind::kReference, in.row.mask, in.masks, true, DetailWhen(in.mask.has_value(), FieldDetail::kMask), std::nullopt, BindLayerMask(output, layer) });
		form.push_back(FieldSpec{ "channels", FieldKind::kChannels, in.row.channels, {}, false, std::nullopt, std::nullopt, BindLayerChannels(output, layer) });
		return form;
	}

	std::vector<FieldSpec> ScalarForm(const StackView& a_stack)
	{
		std::vector<FieldSpec> form;
		form.reserve(a_stack.scalars.size());
		for (const auto& scalar : a_stack.scalars) {
			const auto field = ParseScalarField(scalar.name);
			const bool colour = field == std::optional{ ScalarField::kColor };
			form.push_back(FieldSpec{ scalar.name, colour ? FieldKind::kColor : FieldKind::kScalar, scalar.text, colour ? a_stack.colorSignals : a_stack.scalarSignals, false, std::nullopt, scalar.value, BindScalar(a_stack.output, field) });
		}
		return form;
	}

	std::optional<RecipeEdit> SignalValueEdit(const std::string& a_signal, const std::string& a_text)
	{
		if (const auto param = ParseParam(a_text)) {
			if (const auto* number = Get<float>(*param)) {
				return SetConstant{ a_signal, *number };
			}
		}
		if (const auto colour = LiteralColor(a_text)) {
			return SetConstant{ a_signal, *colour };
		}
		if (!a_text.empty() && Program::Parse(a_text)) {
			return SetExpression{ a_signal, a_text };
		}
		return std::nullopt;
	}

	std::optional<FieldSpec> SignalForm(const SignalRow& a_signal)
	{
		const std::string& name = a_signal.name;
		const FieldBinding bind = [name](const std::string& a_text) { return SignalValueEdit(name, a_text); };
		if (a_signal.kind == "expr") {
			return FieldSpec{ name, FieldKind::kExpression, a_signal.text, {}, false, std::nullopt, std::nullopt, bind };
		}
		if (!a_signal.constant) {
			return std::nullopt;
		}
		if (const auto* number = Get<float>(*a_signal.constant)) {
			return FieldSpec{ name, FieldKind::kScalar, ParamText(*number), {}, false, std::nullopt, std::nullopt, bind };
		}
		if (const auto* colour = Get<Vec3>(*a_signal.constant)) {
			return FieldSpec{ name, FieldKind::kColor, LiteralColorText(*colour), {}, false, std::nullopt, std::nullopt, bind };
		}
		return std::nullopt;  // a vec2 constant edits in the file
	}

	namespace
	{
		[[nodiscard]] std::vector<std::string> SplitNames(std::string_view a_text)
		{
			std::vector<std::string> names;
			std::size_t              at = 0;
			while (at <= a_text.size()) {
				const auto comma = a_text.find(',', at);
				auto       part = a_text.substr(at, comma == std::string_view::npos ? std::string_view::npos : comma - at);
				while (!part.empty() && part.front() == ' ') {
					part.remove_prefix(1);
				}
				while (!part.empty() && part.back() == ' ') {
					part.remove_suffix(1);
				}
				if (!part.empty()) {
					names.emplace_back(part);
				}
				if (comma == std::string_view::npos) {
					break;
				}
				at = comma + 1;
			}
			return names;
		}

		[[nodiscard]] std::optional<std::uint32_t> ParseCount(std::string_view a_text)
		{
			const auto param = ParseParam(a_text);
			const auto* number = param ? Get<float>(*param) : nullptr;
			if (!number || *number < 1.0f || *number > 64.0f) {
				return std::nullopt;
			}
			return static_cast<std::uint32_t>(*number);
		}

		[[nodiscard]] FieldBinding BindLightParam(std::size_t a_output, LightParam a_field)
		{
			return [=](const std::string& a_text) -> std::optional<RecipeEdit> {
				const auto value = ParseParam(a_text);
				return value ? std::optional<RecipeEdit>{ SetLightParam{ a_output, a_field, *value } } : std::nullopt;
			};
		}

		[[nodiscard]] FieldBinding BindLightVector(std::size_t a_output, LightVector a_field)
		{
			return [=](const std::string& a_text) -> std::optional<RecipeEdit> {
				const auto value = ParseVec3Param(a_text);
				return value ? std::optional<RecipeEdit>{ SetLightVector{ a_output, a_field, *value } } : std::nullopt;
			};
		}

		[[nodiscard]] FieldBinding BindShellParam(ShellParam a_field)
		{
			return [=](const std::string& a_text) -> std::optional<RecipeEdit> {
				const auto value = ParseParam(a_text);
				return value ? std::optional<RecipeEdit>{ SetShellParam{ a_field, *value } } : std::nullopt;
			};
		}

		[[nodiscard]] FieldBinding BindShellVector(ShellVector a_field)
		{
			return [=](const std::string& a_text) -> std::optional<RecipeEdit> {
				const auto value = ParseVec3Param(a_text);
				return value ? std::optional<RecipeEdit>{ SetShellVector{ a_field, *value } } : std::nullopt;
			};
		}

		[[nodiscard]] FieldBinding BindShellPoint(ShellPoint a_field)
		{
			return [=](const std::string& a_text) -> std::optional<RecipeEdit> {
				const auto value = LiteralColor(a_text);
				return value ? std::optional<RecipeEdit>{ SetShellPoint{ a_field, *value } } : std::nullopt;
			};
		}

		[[nodiscard]] FieldSpec Field(std::string a_name, FieldKind a_kind, std::string a_text, std::vector<std::string> a_names, FieldBinding a_bind)
		{
			return FieldSpec{ std::move(a_name), a_kind, std::move(a_text), std::move(a_names), false, std::nullopt, std::nullopt, std::move(a_bind) };
		}
	}

	std::vector<FieldSpec> LightForm(const LightRow& a_light, const SignalNames& a_names)
	{
		std::vector<FieldSpec> form;
		if (!a_light.present) {
			return form;
		}
		const std::size_t output = a_light.output;
		form.push_back(Field("color", FieldKind::kColor, a_light.color, a_names.color, BindLightVector(output, LightVector::kColor)));
		form.push_back(Field("intensity", FieldKind::kScalar, a_light.intensity, a_names.scalar, BindLightParam(output, LightParam::kIntensity)));
		form.push_back(Field("size", FieldKind::kScalar, a_light.size, a_names.scalar, BindLightParam(output, LightParam::kSize)));
		form.push_back(Field("cutoff", FieldKind::kScalar, a_light.cutoff, a_names.scalar, BindLightParam(output, LightParam::kCutoff)));
		form.push_back(Field("offset", FieldKind::kVector, a_light.offset, a_names.color, BindLightVector(output, LightVector::kOffset)));
		form.push_back(Field("shadow", FieldKind::kToggle, a_light.shadow ? "on" : "off", {}, [=](const std::string& a_text) -> std::optional<RecipeEdit> {
			return SetLightShadow{ output, a_text == "on" };
		}));
		// Bones: the kind, then its settings. A kind change starts from the
		// format's defaults; a setting change keeps the rest as shown.
		const bool skinned = a_light.bones != "named";
		form.push_back(Field("bones", FieldKind::kChoice, skinned ? "skinned" : "named", { "skinned", "named" }, [=](const std::string& a_text) -> std::optional<RecipeEdit> {
			if (a_text == "skinned") {
				return SetLightBones{ output, SkinnedBones{} };
			}
			if (a_text == "named") {
				return SetLightBones{ output, NamedBones{ { "NPC Spine2 [Spn2]" } } };
			}
			return std::nullopt;
		}));
		if (skinned) {
			const std::string minShare = a_light.bonesMinShare;
			const std::string max = a_light.bonesMax;
			form.push_back(Field("max", FieldKind::kScalar, max, {}, [=](const std::string& a_text) -> std::optional<RecipeEdit> {
				const auto count = ParseCount(a_text);
				const auto share = ParseParam(minShare);
				const auto* shareValue = share ? Get<float>(*share) : nullptr;
				if (!count || !shareValue) {
					return std::nullopt;
				}
				return SetLightBones{ output, SkinnedBones{ *count, *shareValue } };
			}));
			form.push_back(Field("minShare", FieldKind::kScalar, minShare, {}, [=](const std::string& a_text) -> std::optional<RecipeEdit> {
				const auto count = ParseCount(max);
				const auto share = ParseParam(a_text);
				const auto* shareValue = share ? Get<float>(*share) : nullptr;
				if (!count || !shareValue || *shareValue < 0.0f || *shareValue > 1.0f) {
					return std::nullopt;
				}
				return SetLightBones{ output, SkinnedBones{ *count, *shareValue } };
			}));
		} else {
			form.push_back(Field("names", FieldKind::kText, a_light.bonesNames, {}, [=](const std::string& a_text) -> std::optional<RecipeEdit> {
				auto names = SplitNames(a_text);
				if (names.empty()) {
					return std::nullopt;
				}
				return SetLightBones{ output, NamedBones{ std::move(names) } };
			}));
		}
		return form;
	}

	std::vector<FieldSpec> ShellForm(const ShellRow& a_shell, const SignalNames& a_names)
	{
		std::vector<FieldSpec> form;
		form.push_back(Field("material", FieldKind::kChoice, std::string{ ShellMaterialName(a_shell.material) }, { "pbrCopy", "vanilla" }, [](const std::string& a_text) -> std::optional<RecipeEdit> {
			const auto material = ParseShellMaterial(a_text);
			return material ? std::optional<RecipeEdit>{ SetShellMaterial{ *material } } : std::nullopt;
		}));
		form.push_back(Field("blend", FieldKind::kChoice, std::string{ ShellBlendName(a_shell.blend) }, { "additive", "alpha" }, [](const std::string& a_text) -> std::optional<RecipeEdit> {
			const auto blend = ParseShellBlend(a_text);
			return blend ? std::optional<RecipeEdit>{ SetShellBlend{ *blend } } : std::nullopt;
		}));
		form.push_back(Field("depthBias", FieldKind::kToggle, a_shell.depthBias ? "on" : "off", {}, [](const std::string& a_text) -> std::optional<RecipeEdit> {
			return SetShellDepthBias{ a_text == "on" };
		}));
		form.push_back(Field("alphaTest", FieldKind::kScalar, ParamText(a_shell.alphaTest), {}, [](const std::string& a_text) -> std::optional<RecipeEdit> {
			const auto value = ParseParam(a_text);
			const auto* number = value ? Get<float>(*value) : nullptr;
			return number ? std::optional<RecipeEdit>{ SetShellAlphaTest{ *number } } : std::nullopt;
		}));
		form.push_back(Field("alpha", FieldKind::kScalar, a_shell.alpha, a_names.scalar, BindShellParam(ShellParam::kAlpha)));
		form.push_back(Field("rimPower", FieldKind::kScalar, a_shell.rimPower, a_names.scalar, BindShellParam(ShellParam::kRimPower)));
		form.push_back(Field("emissive", FieldKind::kScalar, a_shell.emissive, a_names.scalar, BindShellParam(ShellParam::kEmissive)));
		form.push_back(Field("inflate", FieldKind::kVector, a_shell.inflate, a_names.color, BindShellVector(ShellVector::kInflate)));
		form.push_back(Field("offset", FieldKind::kVector, a_shell.offset, a_names.color, BindShellVector(ShellVector::kOffset)));
		form.push_back(Field("scale", FieldKind::kScalar, a_shell.scale, a_names.scalar, BindShellParam(ShellParam::kScale)));
		form.push_back(Field("scalePoint", FieldKind::kVector, LiteralColorText(a_shell.scalePoint), {}, BindShellPoint(ShellPoint::kScalePoint)));
		form.push_back(Field("spin", FieldKind::kScalar, a_shell.spin, a_names.scalar, BindShellParam(ShellParam::kSpin)));
		form.push_back(Field("spinAxis", FieldKind::kVector, LiteralColorText(a_shell.spinAxis), {}, BindShellPoint(ShellPoint::kSpinAxis)));
		return form;
	}

	// ---------------------------------------------------------------- colours

	std::optional<Vec3> LiteralColor(std::string_view a_text)
	{
		const auto  param = ParseVec3Param(a_text);
		const auto* parts = param ? Get<std::array<Param, 3>>(*param) : nullptr;
		if (!parts) {
			return std::nullopt;
		}
		const auto* x = Get<float>((*parts)[0]);
		const auto* y = Get<float>((*parts)[1]);
		const auto* z = Get<float>((*parts)[2]);
		if (!x || !y || !z) {
			return std::nullopt;
		}
		return Vec3{ *x, *y, *z };
	}

	std::string LiteralColorText(const Vec3& a_color)
	{
		return Vec3ParamText(std::array<Param, 3>{ a_color.x, a_color.y, a_color.z });
	}

	// ---------------------------------------------------------------- filters

	bool NameMatches(std::string_view a_name, std::string_view a_filter) noexcept
	{
		if (a_filter.empty()) {
			return true;
		}
		if (a_filter.size() > a_name.size()) {
			return false;
		}
		const auto lower = [](char a_ch) { return (a_ch >= 'A' && a_ch <= 'Z') ? static_cast<char>(a_ch - 'A' + 'a') : a_ch; };
		for (std::size_t at = 0; at + a_filter.size() <= a_name.size(); ++at) {
			bool same = true;
			for (std::size_t i = 0; i < a_filter.size() && same; ++i) {
				same = lower(a_name[at + i]) == lower(a_filter[i]);
			}
			if (same) {
				return true;
			}
		}
		return false;
	}

	// ------------------------------------------------------------------ names

	std::string UniqueName(std::string_view a_stem, std::span<const std::string> a_taken)
	{
		const auto taken = [&](const std::string& a_name) { return std::ranges::find(a_taken, a_name) != a_taken.end(); };
		std::string name{ a_stem };
		for (std::size_t n = 2; taken(name); ++n) {
			name = std::string{ a_stem } + std::to_string(n);
		}
		return name;
	}

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
