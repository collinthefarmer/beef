#include "Edits.h"

#include "Expression.h"

#include <algorithm>
#include <cctype>
#include <format>

namespace WornEnchantmentPBR::Studio
{
	namespace
	{
		using Refusal = std::optional<Diagnostic>;

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
		std::string CurveWhere(const std::string& a_curve);
		std::string MaskWhere(const std::string& a_mask);

		Diagnostic Refuse(std::string a_where, std::string a_message)
		{
			return Diagnostic{ Severity::kError, std::move(a_where), std::move(a_message) };
		}

		struct FoundOutput
		{
			SurfaceOutput* output = nullptr;
			Refusal         problem;
		};

		struct FoundLayer
		{
			SurfaceOutput* output = nullptr;
			Layer*          layer = nullptr;
			Refusal         problem;
		};

		FoundOutput FindMaterialOutput(Recipe& a_recipe, std::size_t a_index)
		{
			if (a_index >= a_recipe.outputs.size()) {
				return { nullptr, Refuse(OutputWhere(a_index), std::format("there are {} outputs", a_recipe.outputs.size())) };
			}
			auto* material = Get<SurfaceOutput>(a_recipe.outputs[a_index]);
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

		Source* FindSourceRow(Recipe& a_recipe, std::string_view a_name)
		{
			const auto it = std::ranges::find(a_recipe.sources, a_name, &Source::name);
			return it == a_recipe.sources.end() ? nullptr : &*it;
		}

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

		std::optional<std::size_t> ExcludingOutput(const Recipe& a_recipe, Surface a_surface, Slot a_slot)
		{
			for (std::size_t i = 0; i < a_recipe.outputs.size(); ++i) {
				const auto* other = Get<SurfaceOutput>(a_recipe.outputs[i]);
				if (other && other->surface == a_surface && SlotsExclude(other->slot, a_slot)) {
					return i;
				}
			}
			return std::nullopt;
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

		std::string KeyWhere(const RecipeKey& a_key)
		{
			return std::format("key {}", a_key.ToString());
		}

		Refusal Edit(Recipe& a_recipe, const AddKey& a_edit)
		{
			const auto where = KeyWhere(a_edit.key);
			if (std::ranges::find(a_recipe.keys, a_edit.key) != a_recipe.keys.end()) {
				return Refuse(where, "the recipe has that key");
			}
			switch (KeyOperandOf(a_edit.key.kind)) {
			case KeyOperand::kForm:
				if (a_edit.key.form.text.empty()) {
					return Refuse(where, "a form key names a form");
				}
				break;
			case KeyOperand::kGlob:
				if (a_edit.key.glob.empty()) {
					return Refuse(where, "a material key needs a glob");
				}
				break;
			case KeyOperand::kNone:
				break;
			}
			a_recipe.keys.push_back(a_edit.key);
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const RemoveKey& a_edit)
		{
			const auto where = KeyWhere(a_edit.key);
			const auto it = std::ranges::find(a_recipe.keys, a_edit.key);
			if (it == a_recipe.keys.end()) {
				return Refuse(where, "no such key");
			}
			if (a_recipe.keys.size() == 1) {
				return Refuse(where, "a recipe keeps at least one key");
			}
			a_recipe.keys.erase(it);
			return std::nullopt;
		}

		Selector SharedSelector(const Recipe& a_recipe)
		{
			std::optional<Selector> shared;
			for (const auto& output : a_recipe.outputs) {
				const auto* surface = Get<SurfaceOutput>(output);
				if (!surface) {
					continue;
				}
				if (shared && !(*shared == surface->selector)) {
					return Selector{};
				}
				shared = surface->selector;
			}
			return shared.value_or(Selector{});
		}

		Refusal Edit(Recipe& a_recipe, const AddOutput& a_edit)
		{
			const auto where = OutputWhere(a_recipe.outputs.size());
			if (!SurfaceHasSlot(a_edit.surface, a_recipe.shell.material, a_edit.slot)) {
				return Refuse(where, std::format("a {} shell offers no '{}' slot", a_recipe.shell.material == ShellMaterial::kVanilla ? "vanilla" : "PBR-copy", SlotName(a_edit.slot)));
			}
			if (const auto other = ExcludingOutput(a_recipe, a_edit.surface, a_edit.slot)) {
				const auto* row = Get<SurfaceOutput>(a_recipe.outputs[*other]);
				return Refuse(where, std::format("output {} on '{}' excludes '{}' on the same {}", *other, row ? SlotName(row->slot) : "?", SlotName(a_edit.slot), SurfaceName(a_edit.surface)));
			}
			SurfaceOutput output = DefaultOutput(a_edit.surface, a_edit.slot);
			output.selector = a_edit.selector.All() ? SharedSelector(a_recipe) : a_edit.selector;
			a_recipe.outputs.emplace_back(std::move(output));
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
				return Refuse(CurveWhere(a_edit.curve), "no such curve");
			}
			if (a_edit.text.empty()) {
				return Refuse(CurveWhere(a_edit.curve), "the expression is empty");
			}
			curve->text = a_edit.text;
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const SetMask& a_edit)
		{
			auto* mask = FindMaskRow(a_recipe, a_edit.mask);
			if (!mask) {
				return Refuse(MaskWhere(a_edit.mask), "no such mask");
			}
			if (a_edit.text.empty()) {
				return Refuse(MaskWhere(a_edit.mask), "the expression is empty");
			}
			if (a_edit.text.size() > kMaxExpressionLength) {
				return Refuse(std::format("mask {}", a_edit.mask), std::format("longer than {} characters", kMaxExpressionLength));
			}
			mask->text = a_edit.text;
			return std::nullopt;
		}

		std::string CurveWhere(const std::string& a_curve)
		{
			return std::format("curve {}", a_curve);
		}

		Refusal Edit(Recipe& a_recipe, const AddSignal& a_edit)
		{
			if (!IsName(a_edit.name)) {
				return Refuse(SignalWhere(a_edit.name), "names are letters, digits and underscores, not starting with a digit");
			}
			if (a_recipe.FindSignal(a_edit.name)) {
				return Refuse(SignalWhere(a_edit.name), "a signal has that name");
			}
			a_recipe.signals.push_back(Signal{ a_edit.name, ConstantSignal{ 0.0f }, std::nullopt });
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const AddCurve& a_edit)
		{
			if (!IsName(a_edit.name)) {
				return Refuse(CurveWhere(a_edit.name), "names are letters, digits and underscores, not starting with a digit");
			}
			if (a_recipe.FindCurve(a_edit.name)) {
				return Refuse(CurveWhere(a_edit.name), "a curve has that name");
			}
			a_recipe.curves.push_back(Curve{ a_edit.name, "x" });
			return std::nullopt;
		}

		template <class F>
		void VisitRef(Ref& a_ref, F& a_visit)
		{
			a_visit(a_ref);
		}

		template <class F>
		void VisitRef(std::optional<Ref>& a_ref, F& a_visit)
		{
			if (a_ref) {
				a_visit(*a_ref);
			}
		}

		template <class F>
		void VisitParam(Param& a_param, F& a_visit)
		{
			if (auto* ref = Get<Ref>(a_param)) {
				a_visit(*ref);
			}
		}

		template <class F>
		void VisitParam(std::optional<Param>& a_param, F& a_visit)
		{
			if (a_param) {
				VisitParam(*a_param, a_visit);
			}
		}

		template <std::size_t N, class F>
		void VisitVector(std::variant<std::array<Param, N>, Ref>& a_param, F& a_visit)
		{
			if (auto* ref = Get<Ref>(a_param)) {
				a_visit(*ref);
			} else if (auto* parts = Get<std::array<Param, N>>(a_param)) {
				for (auto& part : *parts) {
					VisitParam(part, a_visit);
				}
			}
		}

		template <std::size_t N, class F>
		void VisitVector(std::optional<std::variant<std::array<Param, N>, Ref>>& a_param, F& a_visit)
		{
			if (a_param) {
				VisitVector(*a_param, a_visit);
			}
		}

		template <class F>
		void ForEachSignalRef(Recipe& a_recipe, F a_visit)
		{
			for (auto& signal : a_recipe.signals) {
				Match(
					signal.kind,
					[&](PulseSignal& s) { VisitParam(s.base, a_visit); VisitParam(s.amplitude, a_visit); VisitParam(s.period, a_visit); VisitParam(s.phase, a_visit); },
					[&](RampSignal& s) { VisitParam(s.from, a_visit); VisitParam(s.to, a_visit); VisitParam(s.seconds, a_visit); },
					[&](TriggerSignal& s) {
						VisitParam(s.lifetime, a_visit);
						if (auto* when = Get<WhenOrigin>(s.origin)) {
							VisitRef(when->when, a_visit);
							VisitRef(when->value, a_visit);
						}
					},
					[&](PayloadSignal& s) { VisitRef(s.trigger, a_visit); },
					[&](CounterSignal& s) { VisitRef(s.trigger, a_visit); VisitRef(s.reset, a_visit); VisitParam(s.cap, a_visit); },
					[&](AccumulateSignal& s) { VisitRef(s.trigger, a_visit); VisitParam(s.decay, a_visit); },
					[&](NoiseSignal& s) { VisitParam(s.frequency, a_visit); VisitParam(s.amplitude, a_visit); },
					[&](GradientSignal& s) {
						VisitParam(s.t, a_visit);
						for (auto& stop : s.stops) {
							VisitVector(stop.color, a_visit);
						}
					},
					[&](DeltaSignal& s) { VisitRef(s.of, a_visit); },
					[&](SmoothSignal& s) { VisitRef(s.of, a_visit); VisitParam(s.seconds, a_visit); },
					[](auto&) {});
			}
			for (auto& source : a_recipe.sources) {
				Match(
					source.kind,
					[&](ImageSource& s) { VisitVector(s.scroll, a_visit); VisitVector(s.tile, a_visit); },
					[&](RippleSource& s) { VisitRef(s.trigger, a_visit); VisitParam(s.speed, a_visit); VisitParam(s.width, a_visit); VisitParam(s.decay, a_visit); },
					[](auto&) {});
			}
			for (auto& output : a_recipe.outputs) {
				Match(
					output,
					[&](SurfaceOutput& o) {
						auto& sc = o.scalars;
						VisitParam(sc.strength, a_visit);
						VisitParam(sc.scale, a_visit);
						VisitVector(sc.color, a_visit);
						VisitParam(sc.weight, a_visit);
						VisitParam(sc.screenSpaceScale, a_visit);
						VisitParam(sc.logMicrofacetDensity, a_visit);
						VisitParam(sc.microfacetRoughness, a_visit);
						VisitParam(sc.densityRandomization, a_visit);
						VisitParam(sc.roughness, a_visit);
						VisitParam(sc.level, a_visit);
						VisitParam(sc.thickness, a_visit);
						for (auto& layer : o.stack) {
							VisitParam(layer.opacity, a_visit);
							VisitVector(layer.color, a_visit);
						}
					},
					[&](LightOutput& o) {
						VisitVector(o.offset, a_visit);
						VisitVector(o.color, a_visit);
						VisitParam(o.intensity, a_visit);
						VisitParam(o.size, a_visit);
						VisitParam(o.cutoff, a_visit);
					});
			}
			auto& shell = a_recipe.shell;
			VisitParam(shell.alpha, a_visit);
			VisitParam(shell.rimPower, a_visit);
			VisitParam(shell.emissive, a_visit);
			VisitVector(shell.pose.inflate, a_visit);
			VisitVector(shell.pose.offset, a_visit);
			VisitParam(shell.pose.scale, a_visit);
			VisitParam(shell.pose.spin, a_visit);
		}

		template <class F>
		void ForEachText(Recipe& a_recipe, F a_visit)
		{
			for (auto& signal : a_recipe.signals) {
				if (auto* expr = Get<ExprSignal>(signal.kind)) {
					a_visit(expr->text, false);
				}
				if (signal.curve && !signal.curve->Named()) {
					a_visit(signal.curve->text, false);
				}
			}
			for (auto& curve : a_recipe.curves) {
				a_visit(curve.text, false);
			}
			for (auto& mask : a_recipe.masks) {
				a_visit(mask.text, true);
			}
			for (auto& output : a_recipe.outputs) {
				if (auto* material = Get<SurfaceOutput>(output)) {
					for (auto& layer : material->stack) {
						if (layer.curve && !layer.curve->Named()) {
							a_visit(layer.curve->text, false);
						}
					}
				}
			}
		}

		template <class F>
		void ForEachImageRef(Recipe& a_recipe, F a_visit)
		{
			for (auto& output : a_recipe.outputs) {
				if (auto* material = Get<SurfaceOutput>(output)) {
					for (auto& layer : material->stack) {
						if (auto* ref = Get<Ref>(layer.source)) {
							a_visit(*ref);
						}
						if (layer.mask) {
							a_visit(*layer.mask);
						}
					}
				}
			}
		}

		void RenameImageRefs(Recipe& a_recipe, std::string_view a_from, std::string_view a_to)
		{
			ForEachImageRef(a_recipe, [&](Ref& a_ref) {
				if (a_ref.name == a_from) {
					a_ref.name = std::string{ a_to };
				}
			});
			for (auto& mask : a_recipe.masks) {
				mask.text = RenameInExpression(mask.text, a_from, a_to, false);
			}
		}

		template <class F>
		void ForEachCurveRef(Recipe& a_recipe, F a_visit)
		{
			for (auto& signal : a_recipe.signals) {
				if (signal.curve && signal.curve->Named()) {
					a_visit(*signal.curve);
				}
			}
			for (auto& output : a_recipe.outputs) {
				if (auto* material = Get<SurfaceOutput>(output)) {
					for (auto& layer : material->stack) {
						if (layer.curve && layer.curve->Named()) {
							a_visit(*layer.curve);
						}
					}
				}
			}
		}

		Refusal Edit(Recipe& a_recipe, const RenameSignal& a_edit)
		{
			auto* signal = FindSignalRow(a_recipe, a_edit.from);
			if (!signal) {
				return Refuse(SignalWhere(a_edit.from), "no such signal");
			}
			if (!IsName(a_edit.to)) {
				return Refuse(SignalWhere(a_edit.from), "names are letters, digits and underscores, not starting with a digit");
			}
			if (a_edit.to == a_edit.from) {
				return std::nullopt;
			}
			if (a_recipe.FindSignal(a_edit.to)) {
				return Refuse(SignalWhere(a_edit.from), std::format("a signal is already named '{}'", a_edit.to));
			}
			const bool imageShares = a_recipe.FindSource(a_edit.from) || a_recipe.FindMask(a_edit.from);
			signal->name = a_edit.to;
			ForEachSignalRef(a_recipe, [&](Ref& a_ref) {
				if (a_ref.name == a_edit.from) {
					a_ref.name = a_edit.to;
				}
			});
			ForEachText(a_recipe, [&](std::string& a_text, bool a_mask) {
				if (!(a_mask && imageShares)) {
					a_text = RenameInExpression(a_text, a_edit.from, a_edit.to, false);
				}
			});
			for (auto& variant : a_recipe.variants) {
				const auto it = variant.overrides.find(a_edit.from);
				if (it != variant.overrides.end()) {
					Value value = it->second;
					variant.overrides.erase(it);
					variant.overrides.emplace(a_edit.to, value);
				}
			}
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const RemoveSignal& a_edit)
		{
			const auto it = std::ranges::find(a_recipe.signals, a_edit.name, &Signal::name);
			if (it == a_recipe.signals.end()) {
				return Refuse(SignalWhere(a_edit.name), "no such signal");
			}
			const auto counts = CountReferences(a_recipe);
			if (const auto found = counts.signals.find(a_edit.name); found != counts.signals.end() && found->second > 0) {
				return Refuse(SignalWhere(a_edit.name), std::format("referenced in {} place(s)", found->second));
			}
			a_recipe.signals.erase(it);
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const RemoveCurve& a_edit)
		{
			const auto it = std::ranges::find(a_recipe.curves, a_edit.name, &Curve::name);
			if (it == a_recipe.curves.end()) {
				return Refuse(CurveWhere(a_edit.name), "no such curve");
			}
			const auto counts = CountReferences(a_recipe);
			if (const auto found = counts.curves.find(a_edit.name); found != counts.curves.end() && found->second > 0) {
				return Refuse(CurveWhere(a_edit.name), std::format("referenced in {} place(s)", found->second));
			}
			a_recipe.curves.erase(it);
			return std::nullopt;
		}

		std::string MaskWhere(const std::string& a_mask)
		{
			return std::format("mask {}", a_mask);
		}

		Refusal Edit(Recipe& a_recipe, const AddMask& a_edit)
		{
			if (!IsName(a_edit.name)) {
				return Refuse(MaskWhere(a_edit.name), "names are letters, digits and underscores, not starting with a digit");
			}
			if (a_recipe.FindMask(a_edit.name) || a_recipe.FindSource(a_edit.name)) {
				return Refuse(MaskWhere(a_edit.name), "a mask or source has that name");
			}
			a_recipe.masks.push_back(Mask{ a_edit.name, "1" });
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const RenameMask& a_edit)
		{
			auto* mask = FindMaskRow(a_recipe, a_edit.from);
			if (!mask) {
				return Refuse(MaskWhere(a_edit.from), "no such mask");
			}
			if (!IsName(a_edit.to)) {
				return Refuse(MaskWhere(a_edit.from), "names are letters, digits and underscores, not starting with a digit");
			}
			if (a_edit.to == a_edit.from) {
				return std::nullopt;
			}
			if (a_recipe.FindMask(a_edit.to) || a_recipe.FindSource(a_edit.to)) {
				return Refuse(MaskWhere(a_edit.from), std::format("a mask or source is already named '{}'", a_edit.to));
			}
			mask->name = a_edit.to;
			RenameImageRefs(a_recipe, a_edit.from, a_edit.to);
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const RemoveMask& a_edit)
		{
			const auto it = std::ranges::find(a_recipe.masks, a_edit.name, &Mask::name);
			if (it == a_recipe.masks.end()) {
				return Refuse(MaskWhere(a_edit.name), "no such mask");
			}
			const auto counts = CountReferences(a_recipe);
			if (const auto found = counts.images.find(a_edit.name); found != counts.images.end() && found->second > 0) {
				return Refuse(MaskWhere(a_edit.name), std::format("referenced in {} place(s)", found->second));
			}
			a_recipe.masks.erase(it);
			return std::nullopt;
		}

		std::string SourceWhere(const std::string& a_source)
		{
			return std::format("source {}", a_source);
		}

		Refusal CheckSourceKind(const Recipe& a_recipe, const std::string& a_where, const SourceKind& a_kind)
		{
			return Match(
				a_kind,
				[&](const ImageSource& s) -> Refusal {
					if (s.path.empty()) {
						return Refuse(a_where, "'path' is empty");
					}
					if (s.scroll) {
						if (const auto* ref = Get<Ref>(*s.scroll); ref && !a_recipe.FindSignal(ref->name)) {
							return Refuse(a_where, std::format("'scroll' reads unknown signal '@{}'", ref->name));
						}
					}
					if (s.tile) {
						if (const auto* ref = Get<Ref>(*s.tile); ref && !a_recipe.FindSignal(ref->name)) {
							return Refuse(a_where, std::format("'tile' reads unknown signal '@{}'", ref->name));
						}
					}
					return std::nullopt;
				},
				[&](const BakeSource& s) -> Refusal {
					if (const auto* bones = Get<BoneWeightBake>(s.bake); bones && bones->bones.empty()) {
						return Refuse(a_where, "boneWeight needs at least one bone");
					}
					return std::nullopt;
				},
				[&](const DistanceSource& s) -> Refusal {
					if (const auto* node = Get<std::string>(s.from); node && node->empty()) {
						return Refuse(a_where, "'distance' needs a node name or a point");
					}
					return std::nullopt;
				},
				[&](const RippleSource& s) -> Refusal {
					if (!a_recipe.FindSignal(s.trigger.name)) {
						return Refuse(a_where, std::format("'trigger' reads unknown signal '@{}'", s.trigger.name));
					}
					for (const auto& [param, field] : { std::pair{ &s.speed, "speed" }, std::pair{ &s.width, "width" }, std::pair{ &s.decay, "decay" } }) {
						if (auto problem = CheckParam(a_recipe, a_where, field, *param)) {
							return problem;
						}
					}
					return std::nullopt;
				},
				[](const auto&) -> Refusal { return std::nullopt; });
		}

		Refusal Edit(Recipe& a_recipe, const AddSource& a_edit)
		{
			if (!IsName(a_edit.name)) {
				return Refuse(SourceWhere(a_edit.name), "names are letters, digits and underscores, not starting with a digit");
			}
			if (a_recipe.FindSource(a_edit.name) || a_recipe.FindMask(a_edit.name)) {
				return Refuse(SourceWhere(a_edit.name), "a source or mask has that name");
			}
			a_recipe.sources.push_back(Source{ a_edit.name, a_edit.kind });
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const SetSource& a_edit)
		{
			auto* source = FindSourceRow(a_recipe, a_edit.name);
			if (!source) {
				return Refuse(SourceWhere(a_edit.name), "no such source");
			}
			if (auto problem = CheckSourceKind(a_recipe, SourceWhere(a_edit.name), a_edit.kind)) {
				return problem;
			}
			source->kind = a_edit.kind;
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const RenameSource& a_edit)
		{
			auto* source = FindSourceRow(a_recipe, a_edit.from);
			if (!source) {
				return Refuse(SourceWhere(a_edit.from), "no such source");
			}
			if (!IsName(a_edit.to)) {
				return Refuse(SourceWhere(a_edit.from), "names are letters, digits and underscores, not starting with a digit");
			}
			if (a_edit.to == a_edit.from) {
				return std::nullopt;
			}
			if (a_recipe.FindSource(a_edit.to) || a_recipe.FindMask(a_edit.to)) {
				return Refuse(SourceWhere(a_edit.from), std::format("a source or mask is already named '{}'", a_edit.to));
			}
			source->name = a_edit.to;
			RenameImageRefs(a_recipe, a_edit.from, a_edit.to);
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const RemoveSource& a_edit)
		{
			const auto it = std::ranges::find(a_recipe.sources, a_edit.name, &Source::name);
			if (it == a_recipe.sources.end()) {
				return Refuse(SourceWhere(a_edit.name), "no such source");
			}
			const auto counts = CountReferences(a_recipe);
			if (const auto found = counts.images.find(a_edit.name); found != counts.images.end() && found->second > 0) {
				return Refuse(SourceWhere(a_edit.name), std::format("referenced in {} place(s)", found->second));
			}
			a_recipe.sources.erase(it);
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const RenameCurve& a_edit)
		{
			auto* curve = FindCurveRow(a_recipe, a_edit.from);
			if (!curve) {
				return Refuse(CurveWhere(a_edit.from), "no such curve");
			}
			if (!IsName(a_edit.to)) {
				return Refuse(CurveWhere(a_edit.from), "names are letters, digits and underscores, not starting with a digit");
			}
			if (a_edit.to == a_edit.from) {
				return std::nullopt;
			}
			if (a_recipe.FindCurve(a_edit.to)) {
				return Refuse(CurveWhere(a_edit.from), std::format("a curve is already named '{}'", a_edit.to));
			}
			curve->name = a_edit.to;
			ForEachCurveRef(a_recipe, [&](CurveRef& a_ref) {
				if (a_ref.Named() == a_edit.from) {
					a_ref.text = "@" + a_edit.to;
				}
			});
			ForEachText(a_recipe, [&](std::string& a_text, bool) {
				a_text = RenameInExpression(a_text, a_edit.from, a_edit.to, true);
			});
			return std::nullopt;
		}

		struct FoundLight
		{
			LightOutput* light = nullptr;
			Refusal      problem;
		};

		FoundLight FindLight(Recipe& a_recipe, std::size_t a_index)
		{
			if (a_index >= a_recipe.outputs.size()) {
				return { nullptr, Refuse(OutputWhere(a_index), std::format("there are {} outputs", a_recipe.outputs.size())) };
			}
			auto* light = Get<LightOutput>(a_recipe.outputs[a_index]);
			if (!light) {
				return { nullptr, Refuse(OutputWhere(a_index), "not a light output") };
			}
			return { light, std::nullopt };
		}

		Refusal Edit(Recipe& a_recipe, const AddLight&)
		{
			for (std::size_t i = 0; i < a_recipe.outputs.size(); ++i) {
				if (Get<LightOutput>(a_recipe.outputs[i])) {
					return Refuse("outputs", std::format("output {} is already the light", i));
				}
			}
			a_recipe.outputs.push_back(LightOutput{});
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const SetLightParam& a_edit)
		{
			auto found = FindLight(a_recipe, a_edit.output);
			if (found.problem) return found.problem;
			if (auto problem = CheckParam(a_recipe, OutputWhere(a_edit.output), LightParamName(a_edit.field), a_edit.value)) return problem;
			switch (a_edit.field) {
			case LightParam::kIntensity:
				found.light->intensity = a_edit.value;
				break;
			case LightParam::kSize:
				found.light->size = a_edit.value;
				break;
			case LightParam::kCutoff:
				found.light->cutoff = a_edit.value;
				break;
			}
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const SetLightVector& a_edit)
		{
			auto found = FindLight(a_recipe, a_edit.output);
			if (found.problem) return found.problem;
			if (auto problem = CheckVec3(a_recipe, OutputWhere(a_edit.output), LightVectorName(a_edit.field), a_edit.value)) return problem;
			if (a_edit.field == LightVector::kColor) {
				found.light->color = a_edit.value;
			} else {
				found.light->offset = a_edit.value;
			}
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const SetLightShadow& a_edit)
		{
			auto found = FindLight(a_recipe, a_edit.output);
			if (found.problem) return found.problem;
			found.light->shadow = a_edit.shadow;
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const SetLightBones& a_edit)
		{
			auto found = FindLight(a_recipe, a_edit.output);
			if (found.problem) return found.problem;
			if (const auto* named = Get<NamedBones>(a_edit.bones); named && named->bones.empty()) {
				return Refuse(OutputWhere(a_edit.output), "named bones need at least one name");
			}
			if (const auto* skinned = Get<SkinnedBones>(a_edit.bones); skinned && skinned->max == 0) {
				return Refuse(OutputWhere(a_edit.output), "skinned bones need max of at least 1");
			}
			found.light->bones = a_edit.bones;
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const ResetLight& a_edit)
		{
			auto found = FindLight(a_recipe, a_edit.output);
			if (found.problem) return found.problem;
			LightOutput reset;
			reset.bulb = found.light->bulb;
			reset.selector = found.light->selector;
			reset.replace = found.light->replace;
			*found.light = reset;
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const SetShellParam& a_edit)
		{
			if (auto problem = CheckParam(a_recipe, "shell", ShellParamName(a_edit.field), a_edit.value)) return problem;
			auto& shell = a_recipe.shell;
			switch (a_edit.field) {
			case ShellParam::kAlpha:
				shell.alpha = a_edit.value;
				break;
			case ShellParam::kRimPower:
				shell.rimPower = a_edit.value;
				break;
			case ShellParam::kEmissive:
				shell.emissive = a_edit.value;
				break;
			case ShellParam::kScale:
				shell.pose.scale = a_edit.value;
				break;
			case ShellParam::kSpin:
				shell.pose.spin = a_edit.value;
				break;
			}
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const SetShellVector& a_edit)
		{
			if (auto problem = CheckVec3(a_recipe, "shell", ShellVectorName(a_edit.field), a_edit.value)) return problem;
			if (a_edit.field == ShellVector::kInflate) {
				a_recipe.shell.pose.inflate = a_edit.value;
			} else {
				a_recipe.shell.pose.offset = a_edit.value;
			}
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const SetShellPoint& a_edit)
		{
			if (a_edit.field == ShellPoint::kScalePoint) {
				a_recipe.shell.pose.scalePoint = a_edit.value;
			} else {
				if (a_edit.value == Vec3{}) {
					return Refuse("shell", "the spin axis cannot be zero");
				}
				a_recipe.shell.pose.spinAxis = a_edit.value;
			}
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const SetShellMaterial& a_edit)
		{
			for (std::size_t i = 0; i < a_recipe.outputs.size(); ++i) {
				const auto* material = Get<SurfaceOutput>(a_recipe.outputs[i]);
				if (material && material->surface == Surface::kShell && !SurfaceHasSlot(Surface::kShell, a_edit.material, material->slot)) {
					return Refuse("shell", std::format("output {} writes '{}' on the shell, which a {} shell lacks", i, SlotName(material->slot), ShellMaterialName(a_edit.material)));
				}
			}
			a_recipe.shell.material = a_edit.material;
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const SetShellBlend& a_edit)
		{
			a_recipe.shell.blend = a_edit.blend;
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const SetShellDepthBias& a_edit)
		{
			a_recipe.shell.depthBias = a_edit.on;
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const SetShellAlphaTest& a_edit)
		{
			if (!(a_edit.value >= 0.0f && a_edit.value <= 1.0f)) {
				return Refuse("shell", "alphaTest is 0..1");
			}
			a_recipe.shell.alphaTest = a_edit.value;
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const ResetShell&)
		{
			a_recipe.shell = ShellSettings{};
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const ClearOutputs&)
		{
			a_recipe.outputs.clear();
			return Edit(a_recipe, ResetShell{});
		}

		void Literal(Param& a_param, float a_value)
		{
			if (Is<Ref>(a_param)) {
				a_param = a_value;
			}
		}
		bool NamesSignal(const Vec3Param& a_param)
		{
			if (Is<Ref>(a_param)) {
				return true;
			}
			const auto* parts = Get<std::array<Param, 3>>(a_param);
			return parts && std::ranges::any_of(*parts, [](const Param& p) { return Is<Ref>(p); });
		}
		void Literal(Vec3Param& a_param, const std::array<float, 3>& a_value)
		{
			if (NamesSignal(a_param)) {
				a_param = std::array<Param, 3>{ a_value[0], a_value[1], a_value[2] };
			}
		}
		void Literal(std::optional<Vec3Param>& a_param)
		{
			if (a_param && NamesSignal(*a_param)) {
				a_param.reset();
			}
		}

		Refusal Edit(Recipe& a_recipe, const ClearResources&)
		{
			a_recipe.signals.clear();
			a_recipe.curves.clear();
			a_recipe.sources.clear();
			a_recipe.masks.clear();
			a_recipe.variants.clear();
			for (auto& output : a_recipe.outputs) {
				Match(
					output,
					[](SurfaceOutput& o) {
						std::erase_if(o.stack, [](const Layer& l) { return Is<Ref>(l.source); });
						for (auto& layer : o.stack) {
							layer.mask.reset();
							layer.curve.reset();
							Literal(layer.opacity, 1.0f);
							Literal(layer.color);
						}
						for (std::size_t i = 0; i < kScalarFieldCount; ++i) {
							const auto field = static_cast<ScalarField>(i);
							auto*      scalar = ScalarOf(o.scalars, field);
							if (!scalar || !*scalar || !Is<Ref>(**scalar)) {
								continue;
							}
							if (ScalarRequired(o.slot, field)) {
								*scalar = Param{ ScalarFallback(field) };
							} else {
								scalar->reset();
							}
						}
						if (o.scalars.color && NamesSignal(*o.scalars.color)) {
							const float fallback = ScalarFallback(ScalarField::kColor);
							if (ScalarRequired(o.slot, ScalarField::kColor)) {
								o.scalars.color = std::array<Param, 3>{ fallback, fallback, fallback };
							} else {
								o.scalars.color.reset();
							}
						}
					},
					[](LightOutput& o) {
						Literal(o.offset, { 0.0f, 0.0f, 0.0f });
						Literal(o.color, { 1.0f, 1.0f, 1.0f });
						Literal(o.intensity, 1.0f);
						Literal(o.size, 1.4142f);
						Literal(o.cutoff, 1.0f);
					});
			}
			auto& shell = a_recipe.shell;
			Literal(shell.alpha, 1.0f);
			Literal(shell.rimPower, 0.0f);
			Literal(shell.emissive, 0.0f);
			Literal(shell.pose.inflate, { 0.0f, 0.0f, 0.0f });
			Literal(shell.pose.offset, { 0.0f, 0.0f, 0.0f });
			Literal(shell.pose.scale, 1.0f);
			Literal(shell.pose.spin, 0.0f);
			return std::nullopt;
		}

		Refusal Edit(Recipe& a_recipe, const ClearRecipe&)
		{
			if (auto problem = Edit(a_recipe, ClearOutputs{})) return problem;
			return Edit(a_recipe, ClearResources{});
		}
	}

	std::optional<Diagnostic> Apply(Recipe& a_recipe, const RecipeEdit& a_edit)
	{
		return Match(a_edit, [&](const auto& edit) { return Edit(a_recipe, edit); });
	}

	std::optional<Diagnostic> Apply(Recipe& a_recipe, const EditBatch& a_batch)
	{
		Recipe copy = a_recipe;
		for (const auto& edit : a_batch.edits) {
			if (auto problem = Apply(copy, edit)) {
				return problem;
			}
		}
		a_recipe = std::move(copy);
		return std::nullopt;
	}

	std::string Describe(const EditBatch& a_batch)
	{
		std::string text;
		for (const auto& edit : a_batch.edits) {
			text += text.empty() ? Describe(edit) : "; " + Describe(edit);
		}
		return text;
	}

	bool ChangesKeys(const EditBatch& a_batch) noexcept
	{
		return std::ranges::any_of(a_batch.edits, [](const RecipeEdit& e) { return Is<AddKey>(e) || Is<RemoveKey>(e); });
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
			[](const AddKey& e) { return std::format("keys: add {}", e.key.ToString()); },
			[](const RemoveKey& e) { return std::format("keys: remove {}", e.key.ToString()); },
			[](const RemoveOutput& e) { return std::format("{}: remove", OutputWhere(e.output)); },
			[](const SetScalar& e) { return std::format("{}: {} {}", OutputWhere(e.output), ScalarFieldName(e.field), ParamText(e.value)); },
			[](const SetColorScalar& e) { return std::format("{}: color {}", OutputWhere(e.output), Vec3ParamText(e.color)); },
			[](const SetConstant& e) { return std::format("{}: constant {}", SignalWhere(e.signal), ValueText(e.value)); },
			[](const SetExpression& e) { return std::format("{}: expr {}", SignalWhere(e.signal), e.text); },
			[](const SetSignalCurve& e) { return std::format("{}: curve {}", SignalWhere(e.signal), CurveText(e.curve)); },
			[](const SetCurve& e) { return std::format("curve {}: {}", e.curve, e.text); },
			[](const SetMask& e) { return std::format("mask {}: {}", e.mask, e.text); },
			[](const AddSignal& e) { return std::format("signals: add {}", e.name); },
			[](const AddCurve& e) { return std::format("curves: add {}", e.name); },
			[](const RenameSignal& e) { return std::format("{}: rename to {}", SignalWhere(e.from), e.to); },
			[](const RenameCurve& e) { return std::format("{}: rename to {}", CurveWhere(e.from), e.to); },
			[](const RemoveSignal& e) { return std::format("{}: remove", SignalWhere(e.name)); },
			[](const AddMask& e) { return std::format("masks: add {}", e.name); },
			[](const AddSource& e) { return std::format("sources: add {} ({})", e.name, SourceKindName(e.kind)); },
			[](const SetSource& e) { return std::format("{}: {}", SourceWhere(e.name), DescribeSource(e.kind)); },
			[](const RenameSource& e) { return std::format("{}: rename to {}", SourceWhere(e.from), e.to); },
			[](const RemoveSource& e) { return std::format("{}: remove", SourceWhere(e.name)); },
			[](const RenameMask& e) { return std::format("{}: rename to {}", MaskWhere(e.from), e.to); },
			[](const RemoveMask& e) { return std::format("{}: remove", MaskWhere(e.name)); },
			[](const RemoveCurve& e) { return std::format("{}: remove", CurveWhere(e.name)); },
			[](const AddLight&) { return std::string{ "outputs: add light" }; },
			[](const SetLightParam& e) { return std::format("{}: {} {}", OutputWhere(e.output), LightParamName(e.field), ParamText(e.value)); },
			[](const SetLightVector& e) { return std::format("{}: {} {}", OutputWhere(e.output), LightVectorName(e.field), Vec3ParamText(e.value)); },
			[](const SetLightShadow& e) { return std::format("{}: shadow {}", OutputWhere(e.output), e.shadow ? "on" : "off"); },
			[](const SetLightBones& e) { return std::format("{}: bones {}", OutputWhere(e.output), Is<NamedBones>(e.bones) ? "named" : "skinned"); },
			[](const ResetLight& e) { return std::format("{}: reset light", OutputWhere(e.output)); },
			[](const SetShellParam& e) { return std::format("shell: {} {}", ShellParamName(e.field), ParamText(e.value)); },
			[](const SetShellVector& e) { return std::format("shell: {} {}", ShellVectorName(e.field), Vec3ParamText(e.value)); },
			[](const SetShellPoint& e) { return std::format("shell: {} {}, {}, {}", ShellPointName(e.field), e.value.x, e.value.y, e.value.z); },
			[](const SetShellMaterial& e) { return std::format("shell: material {}", ShellMaterialName(e.material)); },
			[](const SetShellBlend& e) { return std::format("shell: blend {}", ShellBlendName(e.blend)); },
			[](const SetShellDepthBias& e) { return std::format("shell: depthBias {}", e.on ? "on" : "off"); },
			[](const SetShellAlphaTest& e) { return std::format("shell: alphaTest {}", e.value); },
			[](const ResetShell&) { return std::string{ "shell: reset" }; },
			[](const ClearOutputs&) { return std::string{ "outputs: clear" }; },
			[](const ClearResources&) { return std::string{ "resources: clear" }; },
			[](const ClearRecipe&) { return std::string{ "recipe: clear" }; });
	}

	std::string_view LightParamName(LightParam a_field) noexcept
	{
		switch (a_field) {
		case LightParam::kIntensity:
			return "intensity";
		case LightParam::kSize:
			return "size";
		case LightParam::kCutoff:
			return "cutoff";
		}
		return "?";
	}

	std::string_view LightVectorName(LightVector a_field) noexcept
	{
		return a_field == LightVector::kColor ? "color" : "offset";
	}

	std::string_view ShellParamName(ShellParam a_field) noexcept
	{
		switch (a_field) {
		case ShellParam::kAlpha:
			return "alpha";
		case ShellParam::kRimPower:
			return "rimPower";
		case ShellParam::kEmissive:
			return "emissive";
		case ShellParam::kScale:
			return "scale";
		case ShellParam::kSpin:
			return "spin";
		}
		return "?";
	}

	std::string_view ShellVectorName(ShellVector a_field) noexcept
	{
		return a_field == ShellVector::kInflate ? "inflate" : "offset";
	}

	std::string_view ShellPointName(ShellPoint a_field) noexcept
	{
		return a_field == ShellPoint::kScalePoint ? "scalePoint" : "spinAxis";
	}

	ReferenceCounts CountReferences(const Recipe& a_recipe)
	{
		Recipe          copy = a_recipe;
		ReferenceCounts counts;
		ForEachSignalRef(copy, [&](Ref& a_ref) { ++counts.signals[a_ref.name]; });
		ForEachCurveRef(copy, [&](CurveRef& a_ref) {
			if (const auto name = a_ref.Named()) {
				++counts.curves[*name];
			}
		});
		ForEachText(copy, [&](std::string& a_text, bool a_mask) {
			const auto program = Program::Parse(a_text);
			if (!program) {
				return;
			}
			for (const auto& name : program->References()) {
				++counts.signals[name];
				if (a_mask) {
					++counts.images[name];
				}
			}
			for (const auto& name : program->Curves()) {
				++counts.curves[name];
			}
		});
		ForEachImageRef(copy, [&](Ref& a_ref) { ++counts.images[a_ref.name]; });
		for (const auto& variant : a_recipe.variants) {
			for (const auto& [name, value] : variant.overrides) {
				++counts.signals[name];
			}
		}
		return counts;
	}

	std::string RenameInExpression(std::string_view a_text, std::string_view a_from, std::string_view a_to, bool a_curve)
	{
		const auto nameChar = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
		std::string out;
		out.reserve(a_text.size());
		std::size_t at = 0;
		while (at < a_text.size()) {
			const bool here = a_text[at] == '@' && a_text.substr(at + 1).starts_with(a_from);
			if (here) {
				const std::size_t end = at + 1 + a_from.size();
				const bool        whole = end >= a_text.size() || !nameChar(a_text[end]);
				std::size_t next = end;
				while (next < a_text.size() && a_text[next] == ' ') {
					++next;
				}
				const bool call = next < a_text.size() && a_text[next] == '(';
				if (whole && call == a_curve) {
					out += '@';
					out += a_to;
					at = end;
					continue;
				}
			}
			out += a_text[at];
			++at;
		}
		return out;
	}

	Layer DefaultLayer()
	{
		Layer layer;
		layer.source = Vec3{ 1.0f, 1.0f, 1.0f };
		layer.blend = Blend::kReplace;
		layer.opacity = 1.0f;
		return layer;
	}

	SurfaceOutput DefaultOutput(Surface a_surface, Slot a_slot)
	{
		SurfaceOutput output;
		output.surface = a_surface;
		output.slot = a_slot;
		for (const auto field : ScalarsOf(a_slot)) {
			if (!ScalarRequired(a_slot, field)) {
				continue;
			}
			const float fallback = ScalarFallback(field);
			if (field == ScalarField::kColor) {
				output.scalars.color = std::array<Param, 3>{ fallback, fallback, fallback };
			} else if (auto* scalar = ScalarOf(output.scalars, field)) {
				*scalar = fallback;
			}
		}
		return output;
	}
}
