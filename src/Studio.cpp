#include "Studio.h"

#include "Expression.h"
#include "Forms.h"
#include "Vocabulary.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <limits>

namespace WornEnchantmentPBR::Studio
{
	namespace
	{
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
			for (const auto& row : kBlends) {
				if (BlendAllowed(a_slot, row.value)) {
					blends.push_back(row.value);
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
			layout.contextRows = false;
			layout.regionEditor = true;
			layout.signals = false;  // the resources would edit the paint recipe, which is dropped
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
		return a_target == Target::kLight ? "light" : SurfaceName(SurfaceOf(a_target));
	}

	Surface SurfaceOf(Target a_target) noexcept
	{
		return a_target == Target::kShell ? Surface::kShell : Surface::kMaterial;
	}

	Target TargetOf(Surface a_surface) noexcept
	{
		return a_surface == Surface::kShell ? Target::kShell : Target::kMaterial;
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

	std::optional<SnapshotRequest> RequestOf(const Selection& a_selection) noexcept
	{
		if (a_selection.actorID == 0) {
			return std::nullopt;
		}
		return SnapshotRequest{ a_selection.actorID, a_selection.armorID, a_selection.firstPerson };
	}

	void ClampSelection(Selection& a_selection, const Snapshot& a_snapshot) noexcept
	{
		if (!a_selection.layer || a_selection.target == Target::kLight || !a_selection.slot) {
			return;
		}
		const auto* geometry = SelectedGeometry(SelectedRecipe(SelectedPiece(a_snapshot, a_selection), a_selection), a_selection);
		if (!geometry) {
			return;
		}
		const Surface surface = SurfaceOf(a_selection.target);
		for (const auto& output : geometry->outputs) {
			if (!output.light && output.surface == surface && output.slot == *a_selection.slot) {
				if (*a_selection.layer >= output.layers.size()) {
					a_selection.layer.reset();
				}
				return;
			}
		}
		a_selection.layer.reset();
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
			} else if (signal.type == ValueType::kVec2) {
				names.vec2.push_back(signal.name);
			}
			if (signal.kind == "trigger") {
				names.triggers.push_back(signal.name);
			}
		}
		return names;
	}

	// ------------------------------------------------------------- sources

	namespace
	{
		[[nodiscard]] std::string OnOff(bool a_on)
		{
			return a_on ? "on" : "off";
		}

		[[nodiscard]] std::string PointText(const Vec3& a_point)
		{
			return LiteralColorText(a_point);
		}
	}

	SourceRow SourceRowOf(const Source& a_source, std::size_t a_references)
	{
		SourceRow row;
		row.name = a_source.name;
		row.kind = std::string{ SourceKindName(a_source.kind) };
		row.references = a_references;
		Match(
			a_source.kind,
			[&](const ImageSource& s) {
				row.path = s.path;
				row.channel = std::string{ ImageChannelName(s.channel) };
				row.space = std::string{ ImageSpaceName(s.space) };
				row.scroll = s.scroll ? Vec2ParamText(*s.scroll) : "";
				row.tile = s.tile ? Vec2ParamText(*s.tile) : "";
				row.mirrorU = OnOff(s.mirror[0]);
				row.mirrorV = OnOff(s.mirror[1]);
				row.transpose = OnOff(s.transpose);
				row.mip = ParamText(s.mip);
			},
			[&](const MaterialSource& s) { row.material = std::string{ MaterialChannelName(s.channel) }; },
			[&](const BakeSource& s) {
				row.bake = std::string{ BakeKindName(s.bake) };
				if (const auto* partition = Get<PartitionBake>(s.bake)) {
					const auto name = BipedSlotName(partition->slot);
					row.partition = name ? std::string{ *name } : std::to_string(partition->slot);
				}
				if (const auto* bones = Get<BoneWeightBake>(s.bake)) {
					for (const auto& bone : bones->bones) {
						row.bones += (row.bones.empty() ? "" : ", ") + bone;
					}
				}
			},
			[&](const UvSource& s) { row.axis = std::string{ UvAxisName(s.axis) }; },
			[&](const DistanceSource& s) {
				row.from = Match(
					s.from,
					[](const std::string& node) { return node; },
					[](const Vec3& point) { return PointText(point); });
			},
			[&](const RippleSource& s) {
				row.trigger = "@" + s.trigger.name;
				row.speed = ParamText(s.speed);
				row.width = ParamText(s.width);
				row.decay = ParamText(s.decay);
				row.shape = std::string{ RippleShapeName(s.shape) };
			},
			[&](const MaterialClustersSource& s) {
				row.clusters = std::to_string(s.clusters);
				row.weights = std::format("{}, {}, {}, {}, {}", ParamText(s.roughness), ParamText(s.metallic), ParamText(s.occlusion), ParamText(s.reflectance), ParamText(s.luma));
				row.seed = std::to_string(s.seed);
				row.iterations = std::to_string(s.iterations);
			});
		return row;
	}

	namespace
	{
		// A whole number in 0..a_max, the whole text; nothing otherwise.
		std::optional<std::uint32_t> WholeNumber(std::string_view a_text, std::uint32_t a_max)
		{
			while (!a_text.empty() && a_text.front() == ' ') {
				a_text.remove_prefix(1);
			}
			while (!a_text.empty() && a_text.back() == ' ') {
				a_text.remove_suffix(1);
			}
			std::uint32_t value = 0;
			const auto    result = std::from_chars(a_text.data(), a_text.data() + a_text.size(), value);
			if (result.ec != std::errc{} || result.ptr != a_text.data() + a_text.size() || value > a_max) {
				return std::nullopt;
			}
			return value;
		}

		// "a, b, c, d, e": five plain numbers; nothing for any other shape.
		std::optional<std::array<float, 5>> FiveNumbers(std::string_view a_text)
		{
			std::array<float, 5> out{};
			std::size_t          count = 0;
			std::size_t          at = 0;
			while (at <= a_text.size()) {
				const auto comma = a_text.find(',', at);
				const auto part = a_text.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
				const auto param = ParseParam(part);
				const auto* number = param ? Get<float>(*param) : nullptr;
				if (!number || count >= out.size()) {
					return std::nullopt;
				}
				out[count++] = *number;
				if (comma == std::string::npos) {
					break;
				}
				at = comma + 1;
			}
			return count == out.size() ? std::optional{ out } : std::nullopt;
		}
	}

	std::optional<SourceKind> SourceKindOf(const SourceRow& a_row)
	{
		if (a_row.kind == "image") {
			ImageSource s;
			s.path = a_row.path;
			const auto channel = ParseImageChannel(a_row.channel);
			const auto space = ParseImageSpace(a_row.space);
			const auto mip = ParseParam(a_row.mip);
			const auto* mipValue = mip ? Get<float>(*mip) : nullptr;
			if (!channel || !space || !mipValue) {
				return std::nullopt;
			}
			s.channel = *channel;
			s.space = *space;
			s.mip = *mipValue;
			if (!a_row.scroll.empty()) {
				const auto scroll = ParseVec2Param(a_row.scroll);
				if (!scroll) {
					return std::nullopt;
				}
				s.scroll = *scroll;
			}
			if (!a_row.tile.empty()) {
				const auto tile = ParseVec2Param(a_row.tile);
				if (!tile) {
					return std::nullopt;
				}
				s.tile = *tile;
			}
			s.mirror = { a_row.mirrorU == "on", a_row.mirrorV == "on" };
			s.transpose = a_row.transpose == "on";
			return SourceKind{ s };
		}
		if (a_row.kind == "material") {
			const auto channel = ParseMaterialChannel(a_row.material);
			return channel ? std::optional<SourceKind>{ MaterialSource{ *channel } } : std::nullopt;
		}
		if (a_row.kind == "bake") {
			auto bake = DefaultBakeKind(a_row.bake);
			if (!bake) {
				return std::nullopt;
			}
			if (auto* partition = Get<PartitionBake>(*bake)) {
				const auto slot = BipedSlotFromName(a_row.partition);
				if (!slot) {
					return std::nullopt;
				}
				partition->slot = *slot;
			}
			if (auto* bones = Get<BoneWeightBake>(*bake)) {
				std::size_t at = 0;
				while (at <= a_row.bones.size()) {
					const auto comma = a_row.bones.find(',', at);
					auto       part = std::string_view{ a_row.bones }.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
					while (!part.empty() && part.front() == ' ') {
						part.remove_prefix(1);
					}
					while (!part.empty() && part.back() == ' ') {
						part.remove_suffix(1);
					}
					if (!part.empty()) {
						bones->bones.emplace_back(part);
					}
					if (comma == std::string::npos) {
						break;
					}
					at = comma + 1;
				}
			}
			return SourceKind{ BakeSource{ *bake } };
		}
		if (a_row.kind == "uv") {
			const auto axis = ParseUvAxis(a_row.axis);
			return axis ? std::optional<SourceKind>{ UvSource{ *axis } } : std::nullopt;
		}
		if (a_row.kind == "distance") {
			DistanceSource s;
			if (const auto point = LiteralColor(a_row.from)) {
				s.from = *point;
			} else {
				s.from = a_row.from;
			}
			return SourceKind{ s };
		}
		if (a_row.kind == "ripple") {
			RippleSource s;
			if (!a_row.trigger.starts_with('@') || a_row.trigger.size() < 2) {
				return std::nullopt;
			}
			s.trigger = Ref{ a_row.trigger.substr(1) };
			const auto speed = ParseParam(a_row.speed);
			const auto width = ParseParam(a_row.width);
			const auto decay = ParseParam(a_row.decay);
			const auto shape = ParseRippleShape(a_row.shape);
			if (!speed || !width || !decay || !shape) {
				return std::nullopt;
			}
			s.speed = *speed;
			s.width = *width;
			s.decay = *decay;
			s.shape = *shape;
			return SourceKind{ s };
		}
		if (a_row.kind == "materialClusters") {
			MaterialClustersSource s;
			const auto             clusters = WholeNumber(a_row.clusters, kMaxMaterialClusters);
			const auto             weights = FiveNumbers(a_row.weights);
			const auto             seed = WholeNumber(a_row.seed, std::numeric_limits<std::uint32_t>::max());
			const auto             iterations = WholeNumber(a_row.iterations, kMaxClusterIterations);
			if (!clusters || *clusters < 1 || !weights || !seed || !iterations || *iterations < 1) {
				return std::nullopt;
			}
			s.clusters = static_cast<std::uint8_t>(*clusters);
			s.roughness = (*weights)[0];
			s.metallic = (*weights)[1];
			s.occlusion = (*weights)[2];
			s.reflectance = (*weights)[3];
			s.luma = (*weights)[4];
			s.seed = *seed;
			s.iterations = *iterations;
			return SourceKind{ s };
		}
		return std::nullopt;
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
		case FieldDetail::kSignal:
			return "signal";
		}
		return "?";
	}

	namespace
	{
		// The image kinds a source field can make in place, and a mask.
		constexpr const char* kImageCreators[]{ "new image", "new material", "new bake", "new uv", "new distance", "new ripple", "new mask" };
		// What a value field can make: a constant of the literal it holds
		// (promote), a fresh constant, an expression.
		constexpr const char* kValueCreators[]{ "promote to signal", "new constant", "new expression" };

		[[nodiscard]] std::vector<std::string> Creators(std::span<const char* const> a_names)
		{
			return std::vector<std::string>(a_names.begin(), a_names.end());
		}

		// The row a creator makes, named after its kind and unique among the
		// taken names, and the edit that binds the field to it. `a_bind` turns
		// the "@name" text into the field's own edit.
		[[nodiscard]] std::vector<RecipeEdit> CreateImage(const std::string& a_creator, std::span<const std::string> a_taken, const FieldBinding& a_bind)
		{
			std::vector<RecipeEdit> edits;
			const auto              make = [&](const std::string& a_name, RecipeEdit a_add) {
				edits.push_back(std::move(a_add));
				if (const auto bound = a_bind(ReferenceText(a_name))) {
					edits.push_back(*bound);
				}
			};
			if (a_creator == "new mask") {
				const auto name = UniqueName("mask", a_taken);
				make(name, AddMask{ name });
				return edits;
			}
			const std::string_view word = a_creator.starts_with("new ") ? std::string_view{ a_creator }.substr(4) : std::string_view{ a_creator };
			if (const auto kind = DefaultSourceKind(word)) {
				const auto name = UniqueName(word, a_taken);
				make(name, AddSource{ name, *kind });
			}
			return edits;
		}

		// A value field's creators. Promote needs the literal the field holds
		// now; a fresh constant starts at 0 (a scalar) or white (a colour).
		[[nodiscard]] std::vector<RecipeEdit> CreateValue(const std::string& a_creator, std::string_view a_field, const std::string& a_current, bool a_colour, std::span<const std::string> a_taken, const FieldBinding& a_bind)
		{
			std::vector<RecipeEdit> edits;
			const auto              bindTo = [&](const std::string& a_name) {
				if (const auto bound = a_bind(ReferenceText(a_name))) {
					edits.push_back(*bound);
				}
			};
			if (a_creator == "promote to signal") {
				const auto name = UniqueName(a_field, a_taken);
				Value      value = 0.0f;
				if (a_colour) {
					const auto colour = LiteralColor(a_current);
					if (!colour) {
						return edits;
					}
					value = *colour;
				} else {
					const auto param = ParseParam(a_current);
					const auto* number = param ? Get<float>(*param) : nullptr;
					if (!number) {
						return edits;
					}
					value = *number;
				}
				edits.push_back(AddSignal{ name });
				edits.push_back(SetConstant{ name, value });
				bindTo(name);
				return edits;
			}
			if (a_creator == "new constant") {
				const auto name = UniqueName("signal", a_taken);
				edits.push_back(AddSignal{ name });
				if (a_colour) {
					edits.push_back(SetConstant{ name, Vec3{ 1.0f, 1.0f, 1.0f } });
				}
				bindTo(name);
				return edits;
			}
			if (a_creator == "new expression") {
				const auto name = UniqueName("signal", a_taken);
				edits.push_back(AddSignal{ name });
				edits.push_back(SetExpression{ name, a_colour ? "[1, 1, 1]" : "1" });
				bindTo(name);
				return edits;
			}
			return edits;
		}

		[[nodiscard]] std::vector<std::string> Joined(std::span<const std::string> a_first, std::span<const std::string> a_second)
		{
			std::vector<std::string> names(a_first.begin(), a_first.end());
			names.insert(names.end(), a_second.begin(), a_second.end());
			return names;
		}
	}

	std::vector<FieldSpec> InspectorForm(const Inspector& a_inspector)
	{
		const auto&       in = a_inspector;
		const std::size_t output = in.output;
		const std::size_t layer = in.layer;

		// A layer's source may be a source or a mask row, so its combo lists
		// both; a new signal's name must be unique among every signal.
		const auto sourceNames = Joined(in.sources, in.masks);
		const auto signalNames = Joined(in.scalarSignals, in.colorSignals);

		std::vector<FieldSpec> form;
		form.push_back(FieldSpec{ "source", FieldKind::kColor, in.row.source, sourceNames, false, DetailWhen(in.source.has_value(), FieldDetail::kSource), std::nullopt, BindLayerSource(output, layer) });
		form.back().creators = Creators(kImageCreators);
		form.back().create = [sourceNames, bind = form.back().bind](const std::string& a_creator) { return CreateImage(a_creator, sourceNames, bind); };
		form.push_back(FieldSpec{ "curve", FieldKind::kCurve, in.row.curve, in.curves, true, DetailWhen(in.curve.has_value(), FieldDetail::kCurve), std::nullopt, BindLayerCurve(output, layer) });
		form.back().creators = { "new curve" };
		form.back().create = [curves = in.curves, bind = form.back().bind](const std::string&) {
			std::vector<RecipeEdit> edits;
			const auto              name = UniqueName("curve", curves);
			edits.push_back(AddCurve{ name });
			if (const auto bound = bind(ReferenceText(name))) {
				edits.push_back(*bound);
			}
			return edits;
		};
		form.push_back(FieldSpec{ "opacity", FieldKind::kScalar, in.row.opacityText, in.scalarSignals, false, DetailWhen(NamesSignal(in, in.row.opacityText), FieldDetail::kOpacity), std::nullopt, BindLayerOpacity(output, layer) });
		form.back().creators = Creators(kValueCreators);
		form.back().create = [current = in.row.opacityText, signalNames, bind = form.back().bind](const std::string& a_creator) { return CreateValue(a_creator, "opacity", current, false, signalNames, bind); };
		form.push_back(FieldSpec{ "colour", FieldKind::kColor, in.row.color, in.colorSignals, true, DetailWhen(NamesSignal(in, in.row.color), FieldDetail::kColor), std::nullopt, BindLayerColor(output, layer) });
		form.back().creators = Creators(kValueCreators);
		form.back().create = [current = in.row.color, signalNames, bind = form.back().bind](const std::string& a_creator) { return CreateValue(a_creator, "colour", current, true, signalNames, bind); };
		form.push_back(FieldSpec{ "mask", FieldKind::kReference, in.row.mask, in.masks, true, DetailWhen(in.mask.has_value(), FieldDetail::kMask), std::nullopt, BindLayerMask(output, layer) });
		form.back().creators = { "new mask" };
		form.back().create = [sourceNames, bind = form.back().bind](const std::string& a_creator) { return CreateImage(a_creator, sourceNames, bind); };
		form.push_back(FieldSpec{ "channels", FieldKind::kChannels, in.row.channels, {}, false, std::nullopt, std::nullopt, BindLayerChannels(output, layer) });
		return form;
	}

	std::vector<FieldSpec> ScalarForm(const StackView& a_stack)
	{
		std::vector<FieldSpec> form;
		form.reserve(a_stack.scalars.size());
		const auto signalNames = Joined(a_stack.scalarSignals, a_stack.colorSignals);
		for (const auto& scalar : a_stack.scalars) {
			const auto field = ParseScalarField(scalar.name);
			const bool colour = field == std::optional{ ScalarField::kColor };
			const auto& names = colour ? a_stack.colorSignals : a_stack.scalarSignals;
			const bool  signal = IsWholeReference(scalar.text) && std::ranges::find(names, ReferenceName(scalar.text)) != names.end();
			form.push_back(FieldSpec{ scalar.name, colour ? FieldKind::kColor : FieldKind::kScalar, scalar.text, names, false, signal ? std::optional{ FieldDetail::kSignal } : std::nullopt, scalar.value, BindScalar(a_stack.output, field) });
			form.back().creators = Creators(kValueCreators);
			form.back().create = [name = scalar.name, current = scalar.text, colour, signalNames, bind = form.back().bind](const std::string& a_creator) { return CreateValue(a_creator, name, current, colour, signalNames, bind); };
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

		// A value field whose text is a whole @signal opens that signal.
		[[nodiscard]] FieldSpec Field(std::string a_name, FieldKind a_kind, std::string a_text, std::vector<std::string> a_names, FieldBinding a_bind)
		{
			const bool signal = (a_kind == FieldKind::kScalar || a_kind == FieldKind::kColor || a_kind == FieldKind::kVector || a_kind == FieldKind::kVec2) && IsWholeReference(a_text) && std::ranges::find(a_names, ReferenceName(a_text)) != a_names.end();
			return FieldSpec{ std::move(a_name), a_kind, std::move(a_text), std::move(a_names), false, signal ? std::optional{ FieldDetail::kSignal } : std::nullopt, std::nullopt, std::move(a_bind) };
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
		form.push_back(Field("material", FieldKind::kChoice, std::string{ ShellMaterialName(a_shell.material) }, WordsOf(kShellMaterials), [](const std::string& a_text) -> std::optional<RecipeEdit> {
			const auto material = ParseShellMaterial(a_text);
			return material ? std::optional<RecipeEdit>{ SetShellMaterial{ *material } } : std::nullopt;
		}));
		form.push_back(Field("blend", FieldKind::kChoice, std::string{ ShellBlendName(a_shell.blend) }, WordsOf(kShellBlends), [](const std::string& a_text) -> std::optional<RecipeEdit> {
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

	namespace
	{
		// A field of the source form: the row with one text replaced, read
		// back as the kind, set whole.
		[[nodiscard]] FieldBinding BindSourceText(const SourceRow& a_row, std::string SourceRow::*a_member)
		{
			return [a_row, a_member](const std::string& a_text) -> std::optional<RecipeEdit> {
				SourceRow edited = a_row;
				edited.*a_member = a_text;
				const auto kind = SourceKindOf(edited);
				return kind ? std::optional<RecipeEdit>{ SetSource{ a_row.name, *kind } } : std::nullopt;
			};
		}

		[[nodiscard]] std::vector<std::string> BipedSlotNames()
		{
			std::vector<std::string> names;
			for (std::uint32_t slot = 30; slot <= 61; ++slot) {
				if (const auto name = BipedSlotName(slot)) {
					names.emplace_back(*name);
				}
			}
			return names;
		}
	}

	std::vector<FieldSpec> SourceForm(const SourceRow& a_source, const SignalNames& a_names)
	{
		std::vector<FieldSpec> form;
		const std::string&     name = a_source.name;
		form.push_back(Field("kind", FieldKind::kChoice, a_source.kind, { "image", "material", "bake", "uv", "distance", "ripple", "materialClusters" }, [name](const std::string& a_text) -> std::optional<RecipeEdit> {
			const auto kind = DefaultSourceKind(a_text);
			return kind ? std::optional<RecipeEdit>{ SetSource{ name, *kind } } : std::nullopt;
		}));
		if (a_source.kind == "image") {
			form.push_back(Field("path", FieldKind::kText, a_source.path, {}, BindSourceText(a_source, &SourceRow::path)));
			form.push_back(Field("channel", FieldKind::kChoice, a_source.channel, WordsOf(kImageChannels), BindSourceText(a_source, &SourceRow::channel)));
			form.push_back(Field("space", FieldKind::kChoice, a_source.space, { "tiled", "mesh" }, BindSourceText(a_source, &SourceRow::space)));
			FieldSpec scroll = Field("scroll", FieldKind::kVec2, a_source.scroll, a_names.vec2, BindSourceText(a_source, &SourceRow::scroll));
			scroll.allowEmpty = true;
			form.push_back(std::move(scroll));
			FieldSpec tile = Field("tile", FieldKind::kVec2, a_source.tile, a_names.vec2, BindSourceText(a_source, &SourceRow::tile));
			tile.allowEmpty = true;
			form.push_back(std::move(tile));
			form.push_back(Field("mirrorU", FieldKind::kToggle, a_source.mirrorU, {}, BindSourceText(a_source, &SourceRow::mirrorU)));
			form.push_back(Field("mirrorV", FieldKind::kToggle, a_source.mirrorV, {}, BindSourceText(a_source, &SourceRow::mirrorV)));
			form.push_back(Field("transpose", FieldKind::kToggle, a_source.transpose, {}, BindSourceText(a_source, &SourceRow::transpose)));
			form.push_back(Field("mip", FieldKind::kScalar, a_source.mip, {}, BindSourceText(a_source, &SourceRow::mip)));
		} else if (a_source.kind == "material") {
			form.push_back(Field("channel", FieldKind::kChoice, a_source.material, WordsOf(kMaterialChannels), BindSourceText(a_source, &SourceRow::material)));
		} else if (a_source.kind == "bake") {
			form.push_back(Field("bake", FieldKind::kChoice, a_source.bake, { "position", "localPosition", "worldUp", "partition", "boneWeight", "componentId", "chartId" }, BindSourceText(a_source, &SourceRow::bake)));
			if (a_source.bake == "partition") {
				form.push_back(Field("partition", FieldKind::kChoice, a_source.partition, BipedSlotNames(), BindSourceText(a_source, &SourceRow::partition)));
			} else if (a_source.bake == "boneWeight") {
				form.push_back(Field("bones", FieldKind::kText, a_source.bones, {}, BindSourceText(a_source, &SourceRow::bones)));
			}
		} else if (a_source.kind == "uv") {
			form.push_back(Field("axis", FieldKind::kChoice, a_source.axis, { "u", "v" }, BindSourceText(a_source, &SourceRow::axis)));
		} else if (a_source.kind == "distance") {
			form.push_back(Field("from", FieldKind::kText, a_source.from, {}, BindSourceText(a_source, &SourceRow::from)));
		} else if (a_source.kind == "ripple") {
			form.push_back(Field("trigger", FieldKind::kReference, a_source.trigger, a_names.triggers, BindSourceText(a_source, &SourceRow::trigger)));
			form.push_back(Field("speed", FieldKind::kScalar, a_source.speed, a_names.scalar, BindSourceText(a_source, &SourceRow::speed)));
			form.push_back(Field("width", FieldKind::kScalar, a_source.width, a_names.scalar, BindSourceText(a_source, &SourceRow::width)));
			form.push_back(Field("decay", FieldKind::kScalar, a_source.decay, a_names.scalar, BindSourceText(a_source, &SourceRow::decay)));
			form.push_back(Field("shape", FieldKind::kChoice, a_source.shape, { "ring", "disc" }, BindSourceText(a_source, &SourceRow::shape)));
		} else if (a_source.kind == "materialClusters") {
			form.push_back(Field("clusters", FieldKind::kScalar, a_source.clusters, {}, BindSourceText(a_source, &SourceRow::clusters)));
			form.push_back(Field("weights", FieldKind::kText, a_source.weights, {}, BindSourceText(a_source, &SourceRow::weights)));
			form.push_back(Field("seed", FieldKind::kScalar, a_source.seed, {}, BindSourceText(a_source, &SourceRow::seed)));
			form.push_back(Field("iterations", FieldKind::kScalar, a_source.iterations, {}, BindSourceText(a_source, &SourceRow::iterations)));
		}
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
