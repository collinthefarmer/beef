#include "Edits.h"

#include <algorithm>
#include <format>

// Every edit follows one shape: find the row the edit names (or refuse with
// the row in `where`), check the value against the slot rules and the
// recipe's names, then write. Nothing is written before every check has
// passed, so a refused edit leaves the recipe equal to what it was.

namespace WornEnchantmentPBR::Studio
{
	namespace
	{
		using Refusal = std::optional<Diagnostic>;

		// ------------------------------------------------------- row names

		std::string OutputWhere(std::size_t a_output)
		{
			return std::format("output {}", a_output);
		}

		std::string LayerWhere(std::size_t a_output, std::size_t a_layer)
		{
			return std::format("output {} layer {}", a_output, a_layer);
		}

		std::string SignalWhere(const std::string& a_signal)
		{
			return std::format("signal {}", a_signal);
		}

		Diagnostic Refuse(std::string a_where, std::string a_message)
		{
			return Diagnostic{ Severity::kError, std::move(a_where), std::move(a_message) };
		}

		// --------------------------------------------------------- lookups
		// A lookup is the row, or the reason there is none; exactly one of
		// the two is set.

		struct FoundOutput
		{
			MaterialOutput* output = nullptr;
			Refusal         problem;
		};

		struct FoundLayer
		{
			MaterialOutput* output = nullptr;
			Layer*          layer = nullptr;
			Refusal         problem;
		};

		FoundOutput FindMaterialOutput(Recipe& a_recipe, std::size_t a_index)
		{
			if (a_index >= a_recipe.outputs.size()) {
				return { nullptr, Refuse(OutputWhere(a_index), std::format("there are {} outputs", a_recipe.outputs.size())) };
			}
			auto* material = Get<MaterialOutput>(a_recipe.outputs[a_index]);
			if (!material) {
				return { nullptr, Refuse(OutputWhere(a_index), std::format("output {} is a light", a_index)) };
			}
			return { material, std::nullopt };
		}

		FoundLayer FindLayer(Recipe& a_recipe, std::size_t a_output, std::size_t a_layer)
		{
			auto found = FindMaterialOutput(a_recipe, a_output);
			if (found.problem) {
				return { nullptr, nullptr, found.problem };
			}
			if (a_layer >= found.output->stack.size()) {
				return { nullptr, nullptr, Refuse(LayerWhere(a_output, a_layer), std::format("the stack has {} layers", found.output->stack.size())) };
			}
			return { found.output, &found.output->stack[a_layer], std::nullopt };
		}

		Signal* FindSignalRow(Recipe& a_recipe, std::string_view a_name)
		{
			const auto it = std::ranges::find(a_recipe.signals, a_name, &Signal::name);
			return it == a_recipe.signals.end() ? nullptr : &*it;
		}

		Curve* FindCurveRow(Recipe& a_recipe, std::string_view a_name)
		{
			const auto it = std::ranges::find(a_recipe.curves, a_name, &Curve::name);
			return it == a_recipe.curves.end() ? nullptr : &*it;
		}

		Mask* FindMaskRow(Recipe& a_recipe, std::string_view a_name)
		{
			const auto it = std::ranges::find(a_recipe.masks, a_name, &Mask::name);
			return it == a_recipe.masks.end() ? nullptr : &*it;
		}

		// ---------------------------------------------------- value checks
		// A reference in a value must name a row the recipe has. Types and
		// expression text are Validate's to judge once the edit is in.

		Refusal CheckParam(const Recipe& a_recipe, const std::string& a_where, std::string_view a_field, const Param& a_param)
		{
			const auto* ref = Get<Ref>(a_param);
			if (ref && !a_recipe.FindSignal(ref->name)) {
				return Refuse(a_where, std::format("'{}' reads unknown signal '@{}'", a_field, ref->name));
			}
			return std::nullopt;
		}

		Refusal CheckVec3(const Recipe& a_recipe, const std::string& a_where, std::string_view a_field, const Vec3Param& a_param)
		{
			return Match(
				a_param,
				[&](const Ref& ref) -> Refusal {
					if (!a_recipe.FindSignal(ref.name)) {
						return Refuse(a_where, std::format("'{}' reads unknown signal '@{}'", a_field, ref.name));
					}
					return std::nullopt;
				},
				[&](const std::array<Param, 3>& parts) -> Refusal {
					for (const auto& part : parts) {
						if (auto problem = CheckParam(a_recipe, a_where, a_field, part)) {
							return problem;
						}
					}
					return std::nullopt;
				});
		}

		Refusal CheckCurve(const Recipe& a_recipe, const std::string& a_where, const std::optional<CurveRef>& a_curve)
		{
			if (!a_curve) {
				return std::nullopt;
			}
			const auto name = a_curve->Named();
			if (name && !a_recipe.FindCurve(*name)) {
				return Refuse(a_where, std::format("'curve' names unknown curve '@{}'", *name));
			}
			if (!name && a_curve->text.empty()) {
				return Refuse(a_where, "'curve' is empty");
			}
			return std::nullopt;
		}

		Refusal CheckLayerSource(const Recipe& a_recipe, const std::string& a_where, const LayerSource& a_source)
		{
			const auto* ref = Get<Ref>(a_source);
			if (ref && !a_recipe.FindSource(ref->name) && !a_recipe.FindMask(ref->name)) {
				return Refuse(a_where, std::format("'source' names unknown source or mask '@{}'", ref->name));
			}
			return std::nullopt;
		}

		Refusal CheckMask(const Recipe& a_recipe, const std::string& a_where, const std::optional<Ref>& a_mask)
		{
			if (a_mask && !a_recipe.FindMask(a_mask->name)) {
				return Refuse(a_where, std::format("'mask' names unknown mask '@{}'", a_mask->name));
			}
			return std::nullopt;
		}

		Refusal CheckBlend(const std::string& a_where, Slot a_slot, Blend a_blend)
		{
			if (!BlendAllowed(a_slot, a_blend)) {
				return Refuse(a_where, std::format("blend '{}' is only for the normal slot", BlendName(a_blend)));
			}
			return std::nullopt;
		}

		Refusal CheckChannels(const std::string& a_where, const ChannelSet& a_channels)
		{
			if (!a_channels.r && !a_channels.g && !a_channels.b && !a_channels.a) {
				return Refuse(a_where, "'channels' selects nothing");
			}
			return std::nullopt;
		}

		Refusal CheckLayer(const Recipe& a_recipe, const std::string& a_where, Slot a_slot, const Layer& a_layer)
		{
			if (auto problem = CheckLayerSource(a_recipe, a_where, a_layer.source)) return problem;
			if (auto problem = CheckCurve(a_recipe, a_where, a_layer.curve)) return problem;
			if (auto problem = CheckBlend(a_where, a_slot, a_layer.blend)) return problem;
			if (auto problem = CheckParam(a_recipe, a_where, "opacity", a_layer.opacity)) return problem;
			if (a_layer.color) {
				if (auto problem = CheckVec3(a_recipe, a_where, "color", *a_layer.color)) return problem;
			}
			if (auto problem = CheckMask(a_recipe, a_where, a_layer.mask)) return problem;
			return CheckChannels(a_where, a_layer.channels);
		}

		// The output of the recipe on the same surface whose slot excludes
		// this one, if any.
		std::optional<std::size_t> ExcludingOutput(const Recipe& a_recipe, Surface a_surface, Slot a_slot)
		{
			for (std::size_t i = 0; i < a_recipe.outputs.size(); ++i) {
				const auto* other = Get<MaterialOutput>(a_recipe.outputs[i]);
				if (other && other->surface == a_surface && SlotsExclude(other->slot, a_slot)) {
					return i;
				}
			}
			return std::nullopt;
		}

		// ----------------------------------------------------------- text

		std::string_view SurfaceName(Surface a_surface)
		{
			return a_surface == Surface::kShell ? "shell" : "material";
		}

		std::string ValueText(const Value& a_value)
		{
			return Match(
				a_value,
				[](float f) { return ParamText(Param{ f }); },
				[](const Vec2& v) { return ParamText(Param{ v.x }) + ", " + ParamText(Param{ v.y }); },
				[](const Vec3& v) { return Vec3ParamText(Vec3Param{ std::array<Param, 3>{ v.x, v.y, v.z } }); });
		}

		std::string CurveText(const std::optional<CurveRef>& a_curve)
		{
			return a_curve ? a_curve->text : "none";
		}

		// ------------------------------------------------- layer edits

		Refusal Edit(Recipe& a_recipe, const SetLayerSource& a_edit)
		{
			auto found = FindLayer(a_recipe, a_edit.output, a_edit.layer);
			if (found.problem) return found.problem;
			if (auto problem = CheckLayerSource(a_recipe, LayerWhere(a_edit.output, a_edit.layer), a_edit.source)) return problem;
			found.layer->source = a_edit.source;
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const SetLayerCurve& a_edit)
		{
			auto found = FindLayer(a_recipe, a_edit.output, a_edit.layer);
			if (found.problem) return found.problem;
			if (auto problem = CheckCurve(a_recipe, LayerWhere(a_edit.output, a_edit.layer), a_edit.curve)) return problem;
			found.layer->curve = a_edit.curve;
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const SetLayerBlend& a_edit)
		{
			auto found = FindLayer(a_recipe, a_edit.output, a_edit.layer);
			if (found.problem) return found.problem;
			if (auto problem = CheckBlend(LayerWhere(a_edit.output, a_edit.layer), found.output->slot, a_edit.blend)) return problem;
			found.layer->blend = a_edit.blend;
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const SetLayerOpacity& a_edit)
		{
			auto found = FindLayer(a_recipe, a_edit.output, a_edit.layer);
			if (found.problem) return found.problem;
			if (auto problem = CheckParam(a_recipe, LayerWhere(a_edit.output, a_edit.layer), "opacity", a_edit.opacity)) return problem;
			found.layer->opacity = a_edit.opacity;
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const SetLayerColor& a_edit)
		{
			auto found = FindLayer(a_recipe, a_edit.output, a_edit.layer);
			if (found.problem) return found.problem;
			if (a_edit.color) {
				if (auto problem = CheckVec3(a_recipe, LayerWhere(a_edit.output, a_edit.layer), "color", *a_edit.color)) return problem;
			}
			found.layer->color = a_edit.color;
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const SetLayerMask& a_edit)
		{
			auto found = FindLayer(a_recipe, a_edit.output, a_edit.layer);
			if (found.problem) return found.problem;
			if (auto problem = CheckMask(a_recipe, LayerWhere(a_edit.output, a_edit.layer), a_edit.mask)) return problem;
			found.layer->mask = a_edit.mask;
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const SetLayerChannels& a_edit)
		{
			auto found = FindLayer(a_recipe, a_edit.output, a_edit.layer);
			if (found.problem) return found.problem;
			if (auto problem = CheckChannels(LayerWhere(a_edit.output, a_edit.layer), a_edit.channels)) return problem;
			found.layer->channels = a_edit.channels;
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const AddLayer& a_edit)
		{
			auto found = FindMaterialOutput(a_recipe, a_edit.output);
			if (found.problem) return found.problem;
			auto&             stack = found.output->stack;
			const std::size_t at = a_edit.at.value_or(stack.size());
			if (at > stack.size()) {
				return Refuse(LayerWhere(a_edit.output, at), std::format("the stack has {} layers", stack.size()));
			}
			if (auto problem = CheckLayer(a_recipe, LayerWhere(a_edit.output, at), found.output->slot, a_edit.layer)) return problem;
			stack.insert(stack.begin() + static_cast<std::ptrdiff_t>(at), a_edit.layer);
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const RemoveLayer& a_edit)
		{
			auto found = FindLayer(a_recipe, a_edit.output, a_edit.layer);
			if (found.problem) return found.problem;
			auto& stack = found.output->stack;
			stack.erase(stack.begin() + static_cast<std::ptrdiff_t>(a_edit.layer));
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const MoveLayer& a_edit)
		{
			auto found = FindLayer(a_recipe, a_edit.output, a_edit.from);
			if (found.problem) return found.problem;
			auto& stack = found.output->stack;
			if (a_edit.to >= stack.size()) {
				return Refuse(LayerWhere(a_edit.output, a_edit.from), std::format("cannot move to {}: the stack has {} layers", a_edit.to, stack.size()));
			}
			const auto from = stack.begin() + static_cast<std::ptrdiff_t>(a_edit.from);
			const auto to = stack.begin() + static_cast<std::ptrdiff_t>(a_edit.to);
			// Rotating the range between the two positions by one slides the
			// layers in between and lands `from` at `to`.
			if (a_edit.from < a_edit.to) {
				std::rotate(from, from + 1, to + 1);
			} else if (a_edit.to < a_edit.from) {
				std::rotate(to, from, from + 1);
			}
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const ClearLayers& a_edit)
		{
			auto found = FindMaterialOutput(a_recipe, a_edit.output);
			if (found.problem) return found.problem;
			found.output->stack.clear();
			return std::nullopt;
		}

		// ------------------------------------------------ output edits

		Refusal Edit(Recipe& a_recipe, const AddOutput& a_edit)
		{
			const auto where = OutputWhere(a_recipe.outputs.size());
			if (!SurfaceHasSlot(a_edit.surface, a_recipe.shell.material, a_edit.slot)) {
				return Refuse(where, std::format("a {} shell offers no '{}' slot", a_recipe.shell.material == ShellMaterial::kVanilla ? "vanilla" : "PBR-copy", SlotName(a_edit.slot)));
			}
			if (const auto other = ExcludingOutput(a_recipe, a_edit.surface, a_edit.slot)) {
				const auto* row = Get<MaterialOutput>(a_recipe.outputs[*other]);
				return Refuse(where, std::format("output {} on '{}' excludes '{}' on the same {}", *other, row ? SlotName(row->slot) : "?", SlotName(a_edit.slot), SurfaceName(a_edit.surface)));
			}
			a_recipe.outputs.emplace_back(DefaultOutput(a_edit.surface, a_edit.slot));
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const RemoveOutput& a_edit)
		{
			if (a_edit.output >= a_recipe.outputs.size()) {
				return Refuse(OutputWhere(a_edit.output), std::format("there are {} outputs", a_recipe.outputs.size()));
			}
			a_recipe.outputs.erase(a_recipe.outputs.begin() + static_cast<std::ptrdiff_t>(a_edit.output));
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const SetScalar& a_edit)
		{
			auto found = FindMaterialOutput(a_recipe, a_edit.output);
			if (found.problem) return found.problem;
			const auto where = OutputWhere(a_edit.output);
			const auto field = ScalarFieldName(a_edit.field);
			if (!std::ranges::contains(ScalarsOf(found.output->slot), a_edit.field)) {
				return Refuse(where, std::format("slot '{}' has no '{}'", SlotName(found.output->slot), field));
			}
			auto* scalar = ScalarOf(found.output->scalars, a_edit.field);
			if (!scalar) {
				return Refuse(where, std::format("'{}' is a colour", field));
			}
			if (auto problem = CheckParam(a_recipe, where, field, a_edit.value)) return problem;
			*scalar = a_edit.value;
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const SetColorScalar& a_edit)
		{
			auto found = FindMaterialOutput(a_recipe, a_edit.output);
			if (found.problem) return found.problem;
			const auto where = OutputWhere(a_edit.output);
			if (!std::ranges::contains(ScalarsOf(found.output->slot), ScalarField::kColor)) {
				return Refuse(where, std::format("slot '{}' has no 'color'", SlotName(found.output->slot)));
			}
			if (auto problem = CheckVec3(a_recipe, where, "color", a_edit.color)) return problem;
			found.output->scalars.color = a_edit.color;
			return std::nullopt;
		}

		// ------------------------------------------------ signal edits

		Refusal Edit(Recipe& a_recipe, const SetConstant& a_edit)
		{
			auto* signal = FindSignalRow(a_recipe, a_edit.signal);
			if (!signal) {
				return Refuse(SignalWhere(a_edit.signal), "no such signal");
			}
			signal->kind = ConstantSignal{ a_edit.value };
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const SetExpression& a_edit)
		{
			auto* signal = FindSignalRow(a_recipe, a_edit.signal);
			if (!signal) {
				return Refuse(SignalWhere(a_edit.signal), "no such signal");
			}
			if (a_edit.text.empty()) {
				return Refuse(SignalWhere(a_edit.signal), "the expression is empty");
			}
			signal->kind = ExprSignal{ a_edit.text };
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const SetSignalCurve& a_edit)
		{
			auto* signal = FindSignalRow(a_recipe, a_edit.signal);
			if (!signal) {
				return Refuse(SignalWhere(a_edit.signal), "no such signal");
			}
			if (auto problem = CheckCurve(a_recipe, SignalWhere(a_edit.signal), a_edit.curve)) return problem;
			signal->curve = a_edit.curve;
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const SetCurve& a_edit)
		{
			auto* curve = FindCurveRow(a_recipe, a_edit.curve);
			if (!curve) {
				return Refuse(std::format("curve {}", a_edit.curve), "no such curve");
			}
			if (a_edit.text.empty()) {
				return Refuse(std::format("curve {}", a_edit.curve), "the expression is empty");
			}
			curve->text = a_edit.text;
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const SetMask& a_edit)
		{
			auto* mask = FindMaskRow(a_recipe, a_edit.mask);
			if (!mask) {
				return Refuse(std::format("mask {}", a_edit.mask), "no such mask");
			}
			if (a_edit.text.empty()) {
				return Refuse(std::format("mask {}", a_edit.mask), "the expression is empty");
			}
			mask->text = a_edit.text;
			return std::nullopt;
		}

		// ---------------------------------------------------- defaults

		// What a required scalar starts at when the menu adds an output.
		std::optional<Param> DefaultScalar(ScalarField a_field)
		{
			switch (a_field) {
			case ScalarField::kStrength:
			case ScalarField::kScale:
			case ScalarField::kWeight:
			case ScalarField::kThickness:
				return Param{ 1.0f };
			case ScalarField::kRoughness:
				return Param{ 0.15f };
			case ScalarField::kLevel:
				return Param{ 0.6f };
			default:
				return std::nullopt;
			}
		}
	}

	std::optional<Diagnostic> Apply(Recipe& a_recipe, const RecipeEdit& a_edit)
	{
		return Match(a_edit, [&](const auto& edit) { return Edit(a_recipe, edit); });
	}

	std::string Describe(const RecipeEdit& a_edit)
	{
		return Match(
			a_edit,
			[](const SetLayerSource& e) { return std::format("{}: source {}", LayerWhere(e.output, e.layer), LayerSourceText(e.source)); },
			[](const SetLayerCurve& e) { return std::format("{}: curve {}", LayerWhere(e.output, e.layer), CurveText(e.curve)); },
			[](const SetLayerBlend& e) { return std::format("{}: blend {}", LayerWhere(e.output, e.layer), BlendName(e.blend)); },
			[](const SetLayerOpacity& e) { return std::format("{}: opacity {}", LayerWhere(e.output, e.layer), ParamText(e.opacity)); },
			[](const SetLayerColor& e) { return std::format("{}: color {}", LayerWhere(e.output, e.layer), e.color ? Vec3ParamText(*e.color) : "none"); },
			[](const SetLayerMask& e) { return std::format("{}: mask {}", LayerWhere(e.output, e.layer), e.mask ? "@" + e.mask->name : "none"); },
			[](const SetLayerChannels& e) { return std::format("{}: channels {}", LayerWhere(e.output, e.layer), e.channels.ToString()); },
			[](const AddLayer& e) { return e.at ? std::format("{}: add layer", LayerWhere(e.output, *e.at)) : std::format("{}: add layer on top", OutputWhere(e.output)); },
			[](const RemoveLayer& e) { return std::format("{}: remove", LayerWhere(e.output, e.layer)); },
			[](const MoveLayer& e) { return std::format("{}: move to {}", LayerWhere(e.output, e.from), e.to); },
			[](const ClearLayers& e) { return std::format("{}: clear layers", OutputWhere(e.output)); },
			[](const AddOutput& e) { return std::format("outputs: add {} {}", SurfaceName(e.surface), SlotName(e.slot)); },
			[](const RemoveOutput& e) { return std::format("{}: remove", OutputWhere(e.output)); },
			[](const SetScalar& e) { return std::format("{}: {} {}", OutputWhere(e.output), ScalarFieldName(e.field), ParamText(e.value)); },
			[](const SetColorScalar& e) { return std::format("{}: color {}", OutputWhere(e.output), Vec3ParamText(e.color)); },
			[](const SetConstant& e) { return std::format("{}: constant {}", SignalWhere(e.signal), ValueText(e.value)); },
			[](const SetExpression& e) { return std::format("{}: expr {}", SignalWhere(e.signal), e.text); },
			[](const SetSignalCurve& e) { return std::format("{}: curve {}", SignalWhere(e.signal), CurveText(e.curve)); },
			[](const SetCurve& e) { return std::format("curve {}: {}", e.curve, e.text); },
			[](const SetMask& e) { return std::format("mask {}: {}", e.mask, e.text); });
	}

	Layer DefaultLayer()
	{
		Layer layer;
		layer.source = Vec3{ 1.0f, 1.0f, 1.0f };
		layer.blend = Blend::kReplace;
		layer.opacity = 1.0f;
		return layer;
	}

	MaterialOutput DefaultOutput(Surface a_surface, Slot a_slot)
	{
		MaterialOutput output;
		output.surface = a_surface;
		output.slot = a_slot;
		for (const auto field : ScalarsOf(a_slot)) {
			if (!ScalarRequired(a_slot, field)) {
				continue;
			}
			if (field == ScalarField::kColor) {
				output.scalars.color = std::array<Param, 3>{ 1.0f, 1.0f, 1.0f };
			} else if (auto* scalar = ScalarOf(output.scalars, field)) {
				*scalar = DefaultScalar(field);
			}
		}
		return output;
	}
}
