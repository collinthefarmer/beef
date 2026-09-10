#include "recipe/Recipe.h"

#include "recipe/Expression.h"

#include <algorithm>
#include <unordered_set>

namespace BetterEnchantmentEffects
{
	namespace
	{
		std::optional<std::string_view> RefOf(const Param& a_param) noexcept
		{
			const auto* ref = Get<Ref>(a_param);
			return ref ? std::optional<std::string_view>{ ref->name } : std::nullopt;
		}

		template <std::size_t N>
		void CollectRefs(const std::variant<std::array<Param, N>, Ref>& a_param, std::vector<std::string_view>& a_out)
		{
			Match(
				a_param,
				[&](const Ref& r) { a_out.push_back(r.name); },
				[&](const std::array<Param, N>& parts) {
					for (const auto& p : parts) {
						if (const auto r = RefOf(p)) {
							a_out.push_back(*r);
						}
					}
				});
		}
	}

	const Signal* Recipe::FindSignal(std::string_view a_name) const noexcept
	{
		const auto it = std::ranges::find(signals, a_name, &Signal::name);
		return it == signals.end() ? nullptr : &*it;
	}

	const Curve* Recipe::FindCurve(std::string_view a_name) const noexcept
	{
		const auto it = std::ranges::find(curves, a_name, &Curve::name);
		return it == curves.end() ? nullptr : &*it;
	}

	const Source* Recipe::FindSource(std::string_view a_name) const noexcept
	{
		const auto it = std::ranges::find(sources, a_name, &Source::name);
		return it == sources.end() ? nullptr : &*it;
	}

	const Mask* Recipe::FindMask(std::string_view a_name) const noexcept
	{
		const auto it = std::ranges::find(masks, a_name, &Mask::name);
		return it == masks.end() ? nullptr : &*it;
	}

	bool LoadResult::HasErrors() const noexcept
	{
		return !recipe || std::ranges::any_of(diagnostics, [](const Diagnostic& d) { return d.severity == Severity::kError; });
	}

	bool VariantApplies(const Variant& a_variant, const FormKey& a_armor) noexcept
	{
		const auto* armor = Get<FormRef>(a_variant.key);
		return armor && armor->key && *armor->key == a_armor;
	}

	bool VariantApplies(const Variant& a_variant, const GeometryIdentity& a_geometry)
	{
		const auto* selector = Get<Selector>(a_variant.key);
		return selector && Matches(*selector, a_geometry);
	}

	Recipe ApplyVariant(const Recipe& a_recipe, const Variant& a_variant)
	{
		Recipe out = a_recipe;
		for (auto& s : out.signals) {
			const auto it = a_variant.overrides.find(s.name);
			if (it == a_variant.overrides.end()) {
				continue;
			}
			s.kind = ConstantSignal{ it->second };
			s.curve.reset();
		}
		return out;
	}

	namespace
	{
		class AnimationQuery
		{
		public:
			explicit AnimationQuery(const Recipe& a_recipe) :
				recipe_(a_recipe) {}

			bool Signal(std::string_view a_name)
			{
				const auto* signal = recipe_.FindSignal(a_name);
				if (!signal) {
					return false;
				}
				return Guarded("s:" + std::string{ a_name }, [&] {
					return Match(
						signal->kind,
						[](const ConstantSignal&) { return false; },
						[&](const ExprSignal& e) {
							const auto program = Program::Parse(e.text);
							if (!program) {
								return false;
							}
							if (program->UsesTime()) {
								return true;
							}
							return std::ranges::any_of(program->References(), [&](const std::string& r) { return Signal(r); });
						},
						[&](const GradientSignal& g) {
							bool any = Param(g.t);
							for (const auto& stop : g.stops) {
								any = any || Vector(stop.color);
							}
							return any;
						},
						[&](const DeltaSignal& d) { return Signal(d.of.name); },
						[&](const SmoothSignal& s) { return Signal(s.of.name); },
						[](const auto&) { return true; });
				});
			}

			bool Param(const BetterEnchantmentEffects::Param& a_param)
			{
				const auto name = RefOf(a_param);
				return name && Signal(*name);
			}

			template <std::size_t N>
			bool Vector(const std::variant<std::array<BetterEnchantmentEffects::Param, N>, Ref>& a_param)
			{
				std::vector<std::string_view> refs;
				CollectRefs(a_param, refs);
				return std::ranges::any_of(refs, [&](std::string_view r) { return Signal(r); });
			}

			bool Source(std::string_view a_name)
			{
				const auto* source = recipe_.FindSource(a_name);
				if (!source) {
					return false;
				}
				return Guarded("r:" + std::string{ a_name }, [&] {
					return Match(
						source->kind,
						[&](const ImageSource& s) { return (s.scroll && Vector(*s.scroll)) || (s.tile && Vector(*s.tile)); },
						[](const RippleSource&) { return true; },
						[](const auto&) { return false; });
				});
			}

			bool Mask(std::string_view a_name)
			{
				const auto* mask = recipe_.FindMask(a_name);
				if (!mask) {
					return false;
				}
				return Guarded("m:" + std::string{ a_name }, [&] {
					const auto program = Program::Parse(mask->text);
					if (!program) {
						return false;
					}
					if (program->UsesTime()) {
						return true;
					}
					return std::ranges::any_of(program->References(), [&](const std::string& r) { return Image(r); });
				});
			}

			bool Image(std::string_view a_name)
			{
				if (recipe_.FindSource(a_name)) {
					return Source(a_name);
				}
				if (recipe_.FindMask(a_name)) {
					return Mask(a_name);
				}
				return Signal(a_name);
			}

		private:
			template <class F>
			bool Guarded(const std::string& a_key, F a_f)
			{
				if (!visiting_.insert(a_key).second) {
					return false;
				}
				const bool result = a_f();
				visiting_.erase(a_key);
				return result;
			}

			const Recipe&                   recipe_;
			std::unordered_set<std::string> visiting_;
		};
	}

	bool IsAnimated(const Recipe& a_recipe, std::string_view a_signal)
	{
		return AnimationQuery{ a_recipe }.Signal(a_signal);
	}

	bool IsAnimated(const Recipe& a_recipe, const Source& a_source)
	{
		return AnimationQuery{ a_recipe }.Source(a_source.name);
	}

	bool IsAnimated(const Recipe& a_recipe, const Mask& a_mask)
	{
		return AnimationQuery{ a_recipe }.Mask(a_mask.name);
	}

	bool IsAnimated(const Recipe& a_recipe, const Output& a_output)
	{
		AnimationQuery q{ a_recipe };
		return Match(
			a_output,
			[&](const LightOutput& l) {
				return q.Vector(l.color) || q.Param(l.intensity) || q.Param(l.size) || q.Param(l.cutoff) || q.Vector(l.offset);
			},
			[&](const SurfaceOutput& m) {
				for (const auto& l : m.stack) {
					if (const auto* ref = Get<Ref>(l.source); ref && q.Image(ref->name)) {
						return true;
					}
					if (q.Param(l.opacity) || (l.color && q.Vector(*l.color)) || (l.mask && q.Mask(l.mask->name))) {
						return true;
					}
				}
				return false;
			});
	}
}
