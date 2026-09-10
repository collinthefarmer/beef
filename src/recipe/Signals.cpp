#include "recipe/Signals.h"

#include "recipe/Efsh.h"
#include "recipe/Expression.h"
#include "recipe/Words.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <functional>
#include <map>
#include <numbers>
#include <unordered_set>

namespace BetterEnchantmentEffects
{
	namespace
	{
		constexpr float kEpsilon = 1e-6f;

		float Wave(Waveform a_waveform, float a_phase) noexcept
		{
			const float p = a_phase - std::floor(a_phase);
			switch (a_waveform) {
			case Waveform::kSine:
				return 0.5f - 0.5f * std::cos(2.0f * std::numbers::pi_v<float> * p);
			case Waveform::kTriangle:
				return p < 0.5f ? p * 2.0f : 2.0f - p * 2.0f;
			case Waveform::kSquare:
				return p < 0.5f ? 1.0f : 0.0f;
			case Waveform::kSaw:
				return p;
			}
			return 0.0f;
		}

		float Lattice(std::int64_t a_i, std::uint32_t a_seed) noexcept
		{
			std::uint32_t h = static_cast<std::uint32_t>(a_i) * 0x9E3779B1u ^ (a_seed + 0x7F4A7C15u);
			h ^= h >> 15;
			h *= 0x2C1B3C6Du;
			h ^= h >> 12;
			h *= 0x297A2D39u;
			h ^= h >> 15;
			return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0x7FFFFFu) - 1.0f;
		}

		float ValueNoise(float a_t, std::uint32_t a_seed) noexcept
		{
			const float        f = std::floor(a_t);
			const std::int64_t i = static_cast<std::int64_t>(f);
			const float        frac = a_t - f;
			const float        s = frac * frac * (3.0f - 2.0f * frac);
			return Lattice(i, a_seed) + (Lattice(i + 1, a_seed) - Lattice(i, a_seed)) * s;
		}

		Vec3 Lerp(const Vec3& a, const Vec3& b, float t) noexcept
		{
			return Vec3{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
		}

		Value ZeroOf(ValueType a_type) noexcept
		{
			switch (a_type) {
			case ValueType::kVec2:
				return Vec2{};
			case ValueType::kVec3:
				return Vec3{};
			default:
				return 0.0f;
			}
		}

		bool GlobMatches(std::string_view a_glob, std::string_view a_text) noexcept
		{
			const auto norm = [](char c) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				return c == '\\' ? '/' : c;
			};
			std::size_t g = 0, t = 0, starG = std::string_view::npos, starT = 0;
			while (t < a_text.size()) {
				if (g < a_glob.size() && a_glob[g] == '*') {
					starG = g++;
					starT = t;
				} else if (g < a_glob.size() && norm(a_glob[g]) == norm(a_text[t])) {
					++g;
					++t;
				} else if (starG != std::string_view::npos) {
					g = starG + 1;
					t = ++starT;
				} else {
					return false;
				}
			}
			while (g < a_glob.size() && a_glob[g] == '*') {
				++g;
			}
			return g == a_glob.size();
		}

		bool MatchesFilter(const EventFilter& a_filter, const TriggerPayload& a_payload) noexcept
		{
			if (!a_filter.node.empty() && !GlobMatches(a_filter.node, a_payload.node)) {
				return false;
			}
			if (!a_filter.arg.empty() && !GlobMatches(a_filter.arg, a_payload.arg)) {
				return false;
			}
			if (a_filter.value.min && a_payload.value < *a_filter.value.min) {
				return false;
			}
			if (a_filter.value.max && a_payload.value > *a_filter.value.max) {
				return false;
			}
			return true;
		}

		void AddRef(std::vector<std::string>& a_out, std::string_view a_name)
		{
			if (!a_name.empty() && std::ranges::find(a_out, a_name) == a_out.end()) {
				a_out.emplace_back(a_name);
			}
		}

		void AddRef(std::vector<std::string>& a_out, const Param& a_param)
		{
			if (const auto* ref = Get<Ref>(a_param)) {
				AddRef(a_out, ref->name);
			}
		}

		template <std::size_t N>
		void AddRef(std::vector<std::string>& a_out, const std::variant<std::array<Param, N>, Ref>& a_param)
		{
			Match(
				a_param,
				[&](const Ref& r) { AddRef(a_out, r.name); },
				[&](const std::array<Param, N>& parts) {
					for (const auto& p : parts) {
						AddRef(a_out, p);
					}
				});
		}

		std::vector<std::string> Dependencies(const Signal& a_signal, const Program* a_expression)
		{
			std::vector<std::string> out;
			Match(
				a_signal.kind,
				[&](const PulseSignal& k) {
					AddRef(out, k.base);
					AddRef(out, k.amplitude);
					AddRef(out, k.period);
					AddRef(out, k.phase);
				},
				[&](const RampSignal& k) {
					AddRef(out, k.from);
					AddRef(out, k.to);
					AddRef(out, k.seconds);
				},
				[&](const TriggerSignal& k) {
					AddRef(out, k.lifetime);
					if (const auto* when = Get<WhenOrigin>(k.origin)) {
						AddRef(out, when->when.name);
						if (when->value) {
							AddRef(out, when->value->name);
						}
					}
				},
				[&](const PayloadSignal& k) { AddRef(out, k.trigger.name); },
				[&](const CounterSignal& k) {
					AddRef(out, k.trigger.name);
					if (k.reset) AddRef(out, k.reset->name);
					if (k.cap) AddRef(out, *k.cap);
				},
				[&](const AccumulateSignal& k) {
					AddRef(out, k.trigger.name);
					AddRef(out, k.decay);
				},
				[&](const NoiseSignal& k) {
					AddRef(out, k.frequency);
					AddRef(out, k.amplitude);
				},
				[&](const GradientSignal& k) {
					AddRef(out, k.t);
					for (const auto& s : k.stops) {
						AddRef(out, s.color);
					}
				},
				[&](const DeltaSignal& k) { AddRef(out, k.of.name); },
				[&](const SmoothSignal& k) {
					AddRef(out, k.of.name);
					AddRef(out, k.seconds);
				},
				[&](const ExprSignal&) {
					if (a_expression) {
						for (const auto& r : a_expression->References()) {
							AddRef(out, r);
						}
					}
				},
				[](const auto&) {});
			return out;
		}

		ShaderChannel ChannelOf(ImageChannel a_channel) noexcept
		{
			const auto* row = RowOf(kImageChannels, a_channel);
			return row ? row->channel : ShaderChannel::kRgb;
		}

		ValueType ChannelType(MaterialChannel a_channel) noexcept
		{
			const auto* row = RowOf(kMaterialChannels, a_channel);
			return row ? row->type : ValueType::kScalar;
		}

		ValueType SourceValueType(const Source& a_source) noexcept
		{
			return Match(
				a_source.kind,
				[](const ImageSource& s) { return ChannelOf(s.channel) == ShaderChannel::kRgb ? ValueType::kVec3 : ValueType::kScalar; },
				[](const MaterialSource& s) { return ChannelType(s.channel); },
				[](const BakeSource& s) { return Is<PositionBake>(s.bake) || Is<LocalPositionBake>(s.bake) ? ValueType::kVec3 : ValueType::kScalar; },
				[](const auto&) { return ValueType::kScalar; });
		}

		bool IsIdentifier(std::string_view a_text) noexcept
		{
			if (a_text.empty() || (!std::isalpha(static_cast<unsigned char>(a_text[0])) && a_text[0] != '_')) {
				return false;
			}
			return std::ranges::all_of(a_text, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
		}

		std::optional<std::string_view> NamedCurve(const CurveRef& a_curve) noexcept
		{
			const std::string_view text = a_curve.text;
			if (text.size() > 1 && text.front() == '@' && IsIdentifier(text.substr(1))) {
				return text.substr(1);
			}
			return std::nullopt;
		}

		bool SlotWritable(Surface a_surface, ShellMaterial a_shell, Slot a_slot) noexcept
		{
			if (a_surface == Surface::kShell && a_shell == ShellMaterial::kVanilla) {
				return a_slot == Slot::kEmissive;
			}
			return true;
		}

		bool BlendOnSlot(Slot a_slot, Blend a_blend) noexcept
		{
			const auto* row = RowOf(kBlends, a_blend);
			return row && (!row->normalStackOnly || a_slot == Slot::kNormal);
		}

		std::span<const ScalarField> ScalarsFor(Slot a_slot) noexcept
		{
			const auto* row = RowOf(kSlots, a_slot);
			return row ? row->scalars : std::span<const ScalarField>{};
		}

		bool ScalarNeeded(Slot a_slot, ScalarField a_field) noexcept
		{
			const auto* row = RowOf(kSlots, a_slot);
			return row && row->scalarsRequired && std::ranges::find(row->scalars, a_field) != row->scalars.end();
		}

		const std::optional<Param>* ScalarMemberOf(const SlotScalars& a_scalars, ScalarField a_field) noexcept
		{
			const auto* row = RowOf(kScalarFields, a_field);
			if (!row) {
				return nullptr;
			}
			const auto* member = Get<std::optional<Param> SlotScalars::*>(row->member);
			return member ? &(a_scalars.**member) : nullptr;
		}

		struct Report
		{
			std::vector<Diagnostic>& out;
			std::string_view         where;

			void Error(std::string a_message) const { out.push_back({ Severity::kError, std::string{ where }, std::move(a_message) }); }
			void Warn(std::string a_message) const { out.push_back({ Severity::kWarning, std::string{ where }, std::move(a_message) }); }
		};

		void CheckScalar(const RowTypes& a_rows, const Report& a_report, const Param& a_param, std::string_view a_field)
		{
			const auto* ref = Get<Ref>(a_param);
			if (!ref) {
				return;
			}
			const auto type = a_rows.graph.TypeOf(ref->name);
			if (!type) {
				a_report.Error(std::format("'{}' reads unknown signal '@{}'", a_field, ref->name));
			} else if (*type != ValueType::kScalar) {
				a_report.Error(std::format("'{}' must be a scalar; '@{}' is a {}", a_field, ref->name, Name(*type)));
			}
		}

		template <std::size_t N>
		void CheckVector(const RowTypes& a_rows, const Report& a_report, const std::variant<std::array<Param, N>, Ref>& a_param, std::string_view a_field, bool a_color)
		{
			const ValueType want = N == 2 ? ValueType::kVec2 : ValueType::kVec3;
			Match(
				a_param,
				[&](const Ref& r) {
					const auto type = a_rows.graph.TypeOf(r.name);
					if (!type) {
						a_report.Error(std::format("'{}' reads unknown signal '@{}'", a_field, r.name));
					} else if (*type != want) {
						a_report.Error(std::format("'{}' must be a {}; '@{}' is a {}", a_field, Name(want), r.name, Name(*type)));
					}
				},
				[&](const std::array<Param, N>& parts) {
					for (const auto& p : parts) {
						CheckScalar(a_rows, a_report, p, a_field);
						if (const auto* number = Get<float>(p); a_color && number && (*number < 0.0f || *number > 1.0f)) {
							a_report.Error(std::format("'{}' components are 0..1", a_field));
						}
					}
				});
		}

		void CheckTrigger(const RowTypes& a_rows, const Report& a_report, const Ref& a_ref, std::string_view a_field)
		{
			const auto* signal = a_rows.recipe.FindSignal(a_ref.name);
			if (!signal) {
				a_report.Error(std::format("'{}' names unknown signal '@{}'", a_field, a_ref.name));
			} else if (!Is<TriggerSignal>(signal->kind)) {
				a_report.Error(std::format("'{}' must name a trigger; '@{}' is not one", a_field, a_ref.name));
			}
		}

		void CheckCurveRef(const RowTypes& a_rows, const Report& a_report, const std::optional<CurveRef>& a_curve)
		{
			if (!a_curve) {
				return;
			}
			if (const auto name = NamedCurve(*a_curve)) {
				if (!a_rows.recipe.FindCurve(*name)) {
					a_report.Error(std::format("'curve' names unknown curve '@{}'", *name));
				}
				return;
			}
			if (const auto program = ParseCurve(a_curve->text); !program) {
				a_report.Error(std::format("curve: {}", program.error()));
			}
		}

		void CheckUniqueNames(const Recipe& a_recipe, std::vector<Diagnostic>& a_out)
		{
			const auto error = [&](std::string a_where, std::string a_message) {
				a_out.push_back({ Severity::kError, std::move(a_where), std::move(a_message) });
			};
			const auto warn = [&](std::string a_where, std::string a_message) {
				a_out.push_back({ Severity::kWarning, std::move(a_where), std::move(a_message) });
			};
			const auto unique = [&]<class Row>(const std::vector<Row>& a_rows, const char* a_what) {
				std::unordered_set<std::string> seen;
				for (const auto& row : a_rows) {
					if (!IsName(row.name)) {
						error(std::format("{} '{}'", a_what, row.name), "names are letters, digits and underscores, not starting with a digit");
					} else if (!seen.insert(row.name).second) {
						error(std::format("{} {}", a_what, row.name), "duplicate name");
					}
				}
			};
			unique(a_recipe.signals, "signal");
			unique(a_recipe.curves, "curve");
			unique(a_recipe.sources, "source");
			unique(a_recipe.masks, "mask");
			std::unordered_set<std::string> images;
			for (const auto& s : a_recipe.sources) {
				images.insert(s.name);
			}
			for (const auto& m : a_recipe.masks) {
				if (!images.insert(m.name).second) {
					error(std::format("mask {}", m.name), "a source has the same name; per-texel expressions read both by name");
				}
			}
			for (const auto& s : a_recipe.signals) {
				if (images.contains(s.name)) {
					warn(std::format("signal {}", s.name), "a source or mask has the same name; inside masks the image wins");
				}
			}
			std::unordered_set<std::string> variants;
			for (const auto& v : a_recipe.variants) {
				if (v.name.empty()) {
					error("variant", "has no name");
				} else if (!variants.insert(v.name).second) {
					error(std::format("variant {}", v.name), "duplicate name");
				}
			}
		}
	}

	std::optional<std::size_t> SignalGraph::Index(std::string_view a_name) const noexcept
	{
		const auto it = byName_.find(std::string{ a_name });
		return it == byName_.end() ? std::nullopt : std::optional{ it->second };
	}

	std::optional<ValueType> SignalGraph::TypeOf(std::string_view a_name) const noexcept
	{
		const auto idx = Index(a_name);
		return idx ? std::optional{ nodes_[*idx].type } : std::nullopt;
	}

	const Program* SignalGraph::CurveProgram(std::string_view a_name) const noexcept
	{
		const auto it = curves_.find(std::string{ a_name });
		return it == curves_.end() ? nullptr : &it->second;
	}

	void SignalGraph::ReportSignal(SignalGraph& a_graph, std::string_view a_name, std::string a_message)
	{
		a_graph.diagnostics_.push_back({ Severity::kError, std::format("signal {}", a_name), std::move(a_message) });
	}

	void SignalGraph::ParseCurves(SignalGraph& a_graph, std::span<const Curve> a_curves)
	{
		for (const auto& c : a_curves) {
			if (auto program = ParseCurve(c.text)) {
				a_graph.curves_.emplace(c.name, std::move(*program));
			}
		}
	}

	void SignalGraph::RegisterNodes(SignalGraph& a_graph, std::span<const Signal> a_signals)
	{
		a_graph.nodes_.reserve(a_signals.size());
		for (const auto& s : a_signals) {
			Node n;
			n.signal = s;
			if (!a_graph.byName_.emplace(s.name, a_graph.nodes_.size()).second) {
				n.inert = true;
				ReportSignal(a_graph, s.name, "duplicate name; this row is inert");
			}
			a_graph.nodes_.push_back(std::move(n));
		}
	}

	void SignalGraph::ResolveDependencies(SignalGraph& a_graph, Node& a_node)
	{
		for (const auto& dep : Dependencies(a_node.signal, a_node.expression ? &*a_node.expression : nullptr)) {
			const auto idx = a_graph.Index(dep);
			if (!idx) {
				a_node.inert = true;
				ReportSignal(a_graph, a_node.signal.name, std::format("reads unknown signal '@{}'", dep));
				continue;
			}
			a_node.deps.push_back(*idx);
		}
	}

	void SignalGraph::ResolveNodeCurve(SignalGraph& a_graph, Node& a_node)
	{
		if (!a_node.signal.curve) {
			return;
		}
		const CurveRef& curveRef = *a_node.signal.curve;
		if (const auto name = NamedCurve(curveRef)) {
			if (const auto* program = a_graph.CurveProgram(*name)) {
				a_node.curve = *program;
			} else {
				a_node.inert = true;
				ReportSignal(a_graph, a_node.signal.name, std::format("curve names unknown curve '@{}'", *name));
			}
		} else if (auto program = ParseCurve(curveRef.text)) {
			a_node.curve = std::move(*program);
		} else {
			a_node.inert = true;
			ReportSignal(a_graph, a_node.signal.name, std::format("curve: {}", program.error()));
		}
	}

	void SignalGraph::ResolveRefs(SignalGraph& a_graph)
	{
		for (auto& n : a_graph.nodes_) {
			if (const auto* expr = Get<ExprSignal>(n.signal.kind)) {
				auto parsed = Program::Parse(expr->text);
				if (!parsed) {
					n.inert = true;
					ReportSignal(a_graph, n.signal.name, std::format("expr: {}", parsed.error()));
				} else {
					n.expression = std::move(*parsed);
				}
			}
			ResolveDependencies(a_graph, n);
			ResolveNodeCurve(a_graph, n);
		}
	}

	void SignalGraph::OrderNodes(SignalGraph& a_graph)
	{
		enum class Mark : std::uint8_t
		{
			kNone,
			kOpen,
			kDone
		};
		std::vector<Mark>        marks(a_graph.nodes_.size(), Mark::kNone);
		std::vector<std::size_t> path;
		const auto               visit = [&](this auto&& a_self, std::size_t a_i, std::size_t a_depth) -> void {
			if (marks[a_i] == Mark::kDone) {
				return;
			}
			if (marks[a_i] == Mark::kOpen) {
				std::string cycle;
				bool        on = false;
				for (const auto p : path) {
					on = on || p == a_i;
					if (on) {
						a_graph.nodes_[p].inert = true;
						cycle += a_graph.nodes_[p].signal.name + " -> ";
					}
				}
				ReportSignal(a_graph, a_graph.nodes_[a_i].signal.name, std::format("cycle: {}{}", cycle, a_graph.nodes_[a_i].signal.name));
				return;
			}
			if (a_depth >= kMaxRecipeDepth) {
				a_graph.nodes_[a_i].inert = true;
				ReportSignal(a_graph, a_graph.nodes_[a_i].signal.name, std::format("dependency chain is deeper than {} signals; this row is inert", kMaxRecipeDepth));
				marks[a_i] = Mark::kDone;
				a_graph.order_.push_back(a_i);
				return;
			}
			marks[a_i] = Mark::kOpen;
			path.push_back(a_i);
			for (const auto d : a_graph.nodes_[a_i].deps) {
				a_self(d, a_depth + 1);
			}
			path.pop_back();
			marks[a_i] = Mark::kDone;
			a_graph.order_.push_back(a_i);
		};
		for (std::size_t i = 0; i < a_graph.nodes_.size(); ++i) {
			visit(i, 0);
		}
	}

	void SignalGraph::LinkExpr(SignalGraph& a_graph, Node& a_node)
	{
		if (!a_node.expression) {
			return;
		}
		for (const auto& r : a_node.expression->References()) {
			a_node.exprRefs.push_back(static_cast<std::uint32_t>(a_graph.Index(r).value_or(0)));
		}
		for (const auto& c : a_node.expression->Curves()) {
			const auto* program = a_graph.CurveProgram(c);
			if (!program) {
				a_node.inert = true;
				ReportSignal(a_graph, a_node.signal.name, std::format("expr calls unknown curve '@{}'", c));
			}
			a_node.exprCurves.push_back(program);
		}
	}

	void SignalGraph::InferTypes(SignalGraph& a_graph)
	{
		for (const auto i : a_graph.order_) {
			auto&      n = a_graph.nodes_[i];
			const auto typeOf = [&](std::string_view name) -> std::optional<ValueType> { return a_graph.TypeOf(name); };
			n.type = Match(
				n.signal.kind,
				[](const ConstantSignal& k) { return BetterEnchantmentEffects::TypeOf(k.value); },
				[](const EfshSignal& k) {
					switch (k.field) {
					case EfshField::kFillColor:
					case EfshField::kEdgeColor:
						return ValueType::kVec3;
					case EfshField::kScroll:
						return ValueType::kVec2;
					default:
						return ValueType::kScalar;
					}
				},
				[](const PayloadSignal& k) { return k.field == PayloadField::kValue ? ValueType::kScalar : ValueType::kVec3; },
				[](const GradientSignal&) { return ValueType::kVec3; },
				[&](const DeltaSignal& k) { return typeOf(k.of.name).value_or(ValueType::kScalar); },
				[&](const SmoothSignal& k) { return typeOf(k.of.name).value_or(ValueType::kScalar); },
				[&](const ExprSignal&) {
					if (!n.expression) {
						return ValueType::kScalar;
					}
					auto checked = n.expression->Check(typeOf);
					if (!checked) {
						n.inert = true;
						ReportSignal(a_graph, n.signal.name, std::format("expr: {}", checked.error()));
						return ValueType::kScalar;
					}
					return *checked;
				},
				[](const auto&) { return ValueType::kScalar; });
			if (n.curve && n.type != ValueType::kScalar) {
				n.inert = true;
				ReportSignal(a_graph, n.signal.name, std::format("a curve applies only to a scalar signal; this one is a {}", Name(n.type)));
			}
			LinkExpr(a_graph, n);
		}
	}

	void SignalGraph::CheckReferenceTypes(SignalGraph& a_graph)
	{
		for (auto& n : a_graph.nodes_) {
			const auto scalar = [&](const Param& p, std::string_view what) {
				if (const auto* ref = Get<Ref>(p)) {
					if (const auto t = a_graph.TypeOf(ref->name); t && *t != ValueType::kScalar) {
						n.inert = true;
						ReportSignal(a_graph, n.signal.name, std::format("'{}' must be a scalar; '@{}' is a {}", what, ref->name, Name(*t)));
					}
				}
			};
			const auto trigger = [&](std::string_view a_ref, std::string a_message) {
				const auto idx = a_graph.Index(a_ref);
				if (idx && !Is<TriggerSignal>(a_graph.nodes_[*idx].signal.kind)) {
					n.inert = true;
					ReportSignal(a_graph, n.signal.name, std::move(a_message));
				}
			};
			Match(
				n.signal.kind,
				[&](const PulseSignal& k) {
					scalar(k.base, "base");
					scalar(k.amplitude, "amplitude");
					scalar(k.period, "period");
					scalar(k.phase, "phase");
				},
				[&](const RampSignal& k) {
					scalar(k.from, "from");
					scalar(k.to, "to");
					scalar(k.seconds, "seconds");
				},
				[&](const TriggerSignal& k) {
					scalar(k.lifetime, "lifetime");
					if (const auto* when = Get<WhenOrigin>(k.origin)) {
						if (const auto t = a_graph.TypeOf(when->when.name); t && *t != ValueType::kScalar) {
							n.inert = true;
							ReportSignal(a_graph, n.signal.name, std::format("'when' must be a scalar; '@{}' is a {}", when->when.name, Name(*t)));
						}
					}
				},
				[&](const PayloadSignal& k) {
					trigger(k.trigger.name, std::format("'trigger' must name a trigger; '@{}' is not one", k.trigger.name));
				},
				[&](const CounterSignal& k) {
					trigger(k.trigger.name, std::format("'@{}' must be a trigger", k.trigger.name));
					if (k.reset) trigger(k.reset->name, std::format("'@{}' must be a trigger", k.reset->name));
					if (k.cap) scalar(*k.cap, "cap");
				},
				[&](const AccumulateSignal& k) {
					trigger(k.trigger.name, std::format("'@{}' must be a trigger", k.trigger.name));
					scalar(k.decay, "decay");
				},
				[&](const NoiseSignal& k) {
					scalar(k.frequency, "frequency");
					scalar(k.amplitude, "amplitude");
				},
				[&](const GradientSignal& k) {
					scalar(k.t, "t");
					for (const auto& s : k.stops) {
						if (const auto* ref = Get<Ref>(s.color)) {
							if (const auto t = a_graph.TypeOf(ref->name); t && *t != ValueType::kVec3) {
								n.inert = true;
								ReportSignal(a_graph, n.signal.name, std::format("a stop colour must be a vec3; '@{}' is a {}", ref->name, Name(*t)));
							}
						}
					}
				},
				[&](const SmoothSignal& k) { scalar(k.seconds, "seconds"); },
				[](const auto&) {});
		}
	}

	void SignalGraph::PropagateInert(SignalGraph& a_graph)
	{
		for (const auto i : a_graph.order_) {
			for (const auto d : a_graph.nodes_[i].deps) {
				if (a_graph.nodes_[d].inert && !a_graph.nodes_[i].inert) {
					a_graph.nodes_[i].inert = true;
					a_graph.diagnostics_.push_back({ Severity::kWarning, std::format("signal {}", a_graph.nodes_[i].signal.name), std::format("inert because '@{}' is", a_graph.nodes_[d].signal.name) });
				}
			}
		}
	}

	SignalGraph SignalGraph::Compile(std::span<const Signal> a_signals, std::span<const Curve> a_curves)
	{
		SignalGraph g;
		ParseCurves(g, a_curves);
		RegisterNodes(g, a_signals);
		ResolveRefs(g);
		OrderNodes(g);
		InferTypes(g);
		CheckReferenceTypes(g);
		PropagateInert(g);
		return g;
	}

	std::optional<ValueType> SignalTypeOf(const RowTypes& a_rows, std::string_view a_name) noexcept
	{
		return a_rows.graph.TypeOf(a_name);
	}

	std::optional<ValueType> TexelTypeOf(const RowTypes& a_rows, std::string_view a_name, std::size_t a_depth)
	{
		if (a_depth >= kMaxRecipeDepth) {
			return std::nullopt;
		}
		if (const auto* source = a_rows.recipe.FindSource(a_name)) {
			return SourceValueType(*source);
		}
		if (const auto* mask = a_rows.recipe.FindMask(a_name)) {
			return MaskTypeOf(a_rows, *mask, a_depth + 1);
		}
		return a_rows.graph.TypeOf(a_name);
	}

	std::optional<ValueType> MaskTypeOf(const RowTypes& a_rows, const Mask& a_mask, std::size_t a_depth)
	{
		if (a_depth >= kMaxRecipeDepth) {
			return std::nullopt;
		}
		const auto program = Program::Parse(a_mask.text);
		if (!program) {
			return std::nullopt;
		}
		const auto type = program->Check([&](std::string_view name) { return TexelTypeOf(a_rows, name, a_depth + 1); });
		return type ? std::optional{ *type } : std::nullopt;
	}

	bool NamesTrigger(const RowTypes& a_rows, std::string_view a_name) noexcept
	{
		const auto* signal = a_rows.recipe.FindSignal(a_name);
		return signal && Is<TriggerSignal>(signal->kind);
	}

	std::vector<Diagnostic> CheckCurve(const RowTypes&, const Curve& a_curve)
	{
		std::vector<Diagnostic> out;
		const auto              where = std::format("curve {}", a_curve.name);
		if (const auto program = ParseCurve(a_curve.text); !program) {
			out.push_back({ Severity::kError, where, program.error() });
		}
		return out;
	}

	std::vector<Diagnostic> CheckSource(const RowTypes& a_rows, const Source& a_source)
	{
		std::vector<Diagnostic> out;
		const auto              where = std::format("source {}", a_source.name);
		const Report            report{ out, where };
		Match(
			a_source.kind,
			[&](const ImageSource& s) {
				if (s.path.empty()) {
					report.Error("'path' is empty");
				}
				if (s.scroll) {
					CheckVector<2>(a_rows, report, *s.scroll, "scroll", false);
				}
				if (s.tile) {
					CheckVector<2>(a_rows, report, *s.tile, "tile", false);
				}
			},
			[&](const BakeSource& s) {
				if (const auto* bw = Get<BoneWeightBake>(s.bake); bw && bw->bones.empty()) {
					report.Error("boneWeight needs at least one bone");
				}
			},
			[&](const DistanceSource& s) {
				if (const auto* node = Get<std::string>(s.from); node && node->empty()) {
					report.Error("'distance' needs a node name or a point");
				}
			},
			[&](const RippleSource& s) {
				CheckTrigger(a_rows, report, s.trigger, "trigger");
				CheckScalar(a_rows, report, s.speed, "speed");
				CheckScalar(a_rows, report, s.width, "width");
				CheckScalar(a_rows, report, s.decay, "decay");
			},
			[&](const MaterialClustersSource& s) {
				if (s.clusters < 1 || s.clusters > kMaxMaterialClusters) {
					report.Error(std::format("'clusters' is 1..{}", kMaxMaterialClusters));
				}
				if (s.iterations < 1 || s.iterations > kMaxClusterIterations) {
					report.Error(std::format("'iterations' is 1..{}", kMaxClusterIterations));
				}
				for (const auto& [weight, field] : { std::pair{ s.roughness, "roughness" }, std::pair{ s.metallic, "metallic" }, std::pair{ s.occlusion, "occlusion" }, std::pair{ s.reflectance, "reflectance" }, std::pair{ s.luma, "luma" } }) {
					if (weight < 0.0f || weight > kMaxChannelWeight) {
						report.Error(std::format("'weights.{}' is 0..{}", field, kMaxChannelWeight));
					}
				}
			},
			[](const auto&) {});
		return out;
	}

	std::vector<Diagnostic> CheckMask(const RowTypes& a_rows, const Mask& a_mask)
	{
		std::vector<Diagnostic> out;
		const auto              where = std::format("mask {}", a_mask.name);
		const auto              program = Program::Parse(a_mask.text);
		if (!program) {
			out.push_back({ Severity::kError, where, program.error() });
			return out;
		}
		if (program->UsesX()) {
			out.push_back({ Severity::kError, where, "'x' is only defined inside a curve" });
		}
		if (std::ranges::find(program->References(), a_mask.name) != program->References().end()) {
			out.push_back({ Severity::kError, where, "reads itself" });
			return out;
		}
		const auto type = program->Check([&](std::string_view name) { return TexelTypeOf(a_rows, name, 1); });
		if (!type) {
			out.push_back({ Severity::kError, where, type.error() });
		}
		for (const auto& curve : program->Curves()) {
			if (!a_rows.recipe.FindCurve(curve)) {
				out.push_back({ Severity::kError, where, std::format("calls unknown curve '@{}'", curve) });
			}
		}
		return out;
	}

	std::vector<Diagnostic> CheckLayer(const RowTypes& a_rows, const Layer& a_layer, Slot a_slot, std::string_view a_where)
	{
		std::vector<Diagnostic> out;
		const Report            report{ out, a_where };
		if (const auto* ref = Get<Ref>(a_layer.source)) {
			if (!a_rows.recipe.FindSource(ref->name) && !a_rows.recipe.FindMask(ref->name)) {
				report.Error(std::format("'source' names unknown source or mask '@{}'", ref->name));
			}
		}
		CheckCurveRef(a_rows, report, a_layer.curve);
		CheckScalar(a_rows, report, a_layer.opacity, "opacity");
		if (a_layer.color) {
			CheckVector<3>(a_rows, report, *a_layer.color, "color", true);
		}
		if (a_layer.mask && !a_rows.recipe.FindMask(a_layer.mask->name)) {
			report.Error(std::format("'mask' names unknown mask '@{}'", a_layer.mask->name));
		}
		if (!BlendOnSlot(a_slot, a_layer.blend)) {
			report.Error(std::format("blend '{}' is valid only on the normal stack", NameOf(kBlends, a_layer.blend)));
		}
		return out;
	}

	namespace
	{
		void CheckSurfaceScalars(const RowTypes& a_rows, const SurfaceOutput& a_m, const Report& a_report)
		{
			for (const auto field : ScalarsFor(a_m.slot)) {
				const auto name = NameOf(kScalarFields, field);
				if (field == ScalarField::kColor) {
					if (a_m.scalars.color) {
						CheckVector<3>(a_rows, a_report, *a_m.scalars.color, "color", true);
					} else if (ScalarNeeded(a_m.slot, field)) {
						a_report.Error(std::format("slot '{}' needs '{}'", NameOf(kSlots, a_m.slot), name));
					}
					continue;
				}
				const std::optional<Param>* param = ScalarMemberOf(a_m.scalars, field);
				if (!param) {
					continue;
				}
				if (*param) {
					CheckScalar(a_rows, a_report, **param, name);
				} else if (ScalarNeeded(a_m.slot, field)) {
					a_report.Error(std::format("slot '{}' needs '{}'", NameOf(kSlots, a_m.slot), name));
				}
			}
		}

		std::vector<Diagnostic> CheckSurfaceOutput(const RowTypes& a_rows, const SurfaceOutput& a_m, std::string_view a_where)
		{
			std::vector<Diagnostic> out;
			const Report            report{ out, a_where };
			CheckSurfaceScalars(a_rows, a_m, report);
			if (!SlotWritable(a_m.surface, a_rows.recipe.shell.material, a_m.slot)) {
				report.Error(std::format("a vanilla shell has only the emissive slot; '{}' is not one", NameOf(kSlots, a_m.slot)));
			}
			std::size_t li = 0;
			for (const auto& l : a_m.stack) {
				const auto layerWhere = std::format("{} layer {}", a_where, li++);
				for (auto& d : CheckLayer(a_rows, l, a_m.slot, layerWhere)) {
					out.push_back(std::move(d));
				}
			}
			return out;
		}

		std::vector<Diagnostic> CheckLightOutput(const RowTypes& a_rows, const LightOutput& a_l, std::string_view a_where)
		{
			std::vector<Diagnostic> out;
			const Report            report{ out, a_where };
			CheckVector<3>(a_rows, report, a_l.offset, "offset", false);
			CheckVector<3>(a_rows, report, a_l.color, "color", true);
			CheckScalar(a_rows, report, a_l.intensity, "intensity");
			CheckScalar(a_rows, report, a_l.size, "size");
			CheckScalar(a_rows, report, a_l.cutoff, "cutoff");
			if (const auto* named = Get<NamedBones>(a_l.bones); named && named->bones.empty()) {
				report.Error("'named' needs at least one bone");
			}
			if (const auto* skinned = Get<SkinnedBones>(a_l.bones); skinned && skinned->max == 0) {
				report.Error("'skinned.max' must be at least 1");
			}
			return out;
		}
	}

	std::vector<Diagnostic> CheckOutput(const RowTypes& a_rows, const Output& a_output, std::string_view a_where)
	{
		return Match(
			a_output,
			[&](const SurfaceOutput& m) { return CheckSurfaceOutput(a_rows, m, a_where); },
			[&](const LightOutput& l) { return CheckLightOutput(a_rows, l, a_where); });
	}

	namespace
	{
		void CheckSlotExclusions(std::string_view a_where, Slot a_slot, const std::vector<Slot>& a_held, std::vector<Diagnostic>& a_out)
		{
			for (const auto other : a_held) {
				if (SlotsExclude(other, a_slot)) {
					a_out.push_back({ Severity::kWarning, std::string{ a_where }, std::format("'{}' and '{}' on the same material exclude each other; this output is dropped", SlotName(other), SlotName(a_slot)) });
				}
			}
		}

		void CheckOutputs(const RowTypes& a_rows, std::vector<Diagnostic>& a_out)
		{
			std::map<Surface, std::vector<Slot>> bound;
			std::size_t                          index = 0;
			for (const auto& o : a_rows.recipe.outputs) {
				const auto where = std::format("output {}", index++);
				for (auto& d : CheckOutput(a_rows, o, where)) {
					a_out.push_back(std::move(d));
				}
				const auto* m = Get<SurfaceOutput>(o);
				if (!m) {
					continue;
				}
				CheckSlotExclusions(where, m->slot, bound[m->surface], a_out);
				bound[m->surface].push_back(m->slot);
			}
		}

		void CheckShell(const RowTypes& a_rows, std::vector<Diagnostic>& a_out)
		{
			const ShellSettings& s = a_rows.recipe.shell;
			const Report         shell{ a_out, "shell" };
			CheckScalar(a_rows, shell, s.alpha, "alpha");
			CheckScalar(a_rows, shell, s.rimPower, "rimPower");
			CheckScalar(a_rows, shell, s.emissive, "emissive");
			const Report pose{ a_out, "shell pose" };
			CheckVector<3>(a_rows, pose, s.pose.inflate, "inflate", false);
			CheckVector<3>(a_rows, pose, s.pose.offset, "offset", false);
			CheckScalar(a_rows, pose, s.pose.scale, "scale");
			CheckScalar(a_rows, pose, s.pose.spin, "spin");
		}

		void CheckVariants(const RowTypes& a_rows, std::vector<Diagnostic>& a_out)
		{
			for (const auto& v : a_rows.recipe.variants) {
				const auto where = std::format("variant {}", v.name);
				for (const auto& [name, value] : v.overrides) {
					const auto type = a_rows.graph.TypeOf(name);
					if (!type) {
						a_out.push_back({ Severity::kError, where, std::format("overrides unknown signal '{}'", name) });
					} else if (*type != TypeOf(value)) {
						a_out.push_back({ Severity::kError, where, std::format("override of '{}' is a {}; the signal is a {}", name, Name(TypeOf(value)), Name(*type)) });
					}
				}
			}
		}
	}

	std::vector<Diagnostic> Validate(const Recipe& a_recipe)
	{
		const SignalGraph graph = SignalGraph::Compile(a_recipe.signals, a_recipe.curves);
		const RowTypes    rows{ a_recipe, graph };

		std::vector<Diagnostic> out;
		const auto              append = [&](std::vector<Diagnostic> a_more) {
            for (auto& d : a_more) {
                out.push_back(std::move(d));
            }
		};

		for (const auto& d : graph.Diagnostics()) {
			out.push_back(d);
		}
		CheckUniqueNames(a_recipe, out);
		for (const auto& c : a_recipe.curves) {
			append(CheckCurve(rows, c));
		}
		for (const auto& s : a_recipe.sources) {
			append(CheckSource(rows, s));
		}
		for (const auto& m : a_recipe.masks) {
			append(CheckMask(rows, m));
		}
		CheckOutputs(rows, out);
		CheckShell(rows, out);
		CheckVariants(rows, out);
		return out;
	}

	SignalState::SignalState(const SignalGraph& a_graph) :
		graph_(a_graph), values_(a_graph.Size(), Value{ 0.0f }), states_(a_graph.Size())
	{
		for (std::size_t i = 0; i < graph_.Size(); ++i) {
			const auto& n = graph_.nodes_[i];
			values_[i] = ZeroOf(n.type);
			if (const auto* c = Get<ConstantSignal>(n.signal.kind)) {
				values_[i] = c->value;
			}
		}
	}

	Value SignalState::ValueOf(std::size_t a_index) const noexcept
	{
		return a_index < values_.size() ? values_[a_index] : Value{ 0.0f };
	}

	Value SignalState::ValueOf(std::string_view a_name) const noexcept
	{
		const auto idx = graph_.Index(a_name);
		return idx ? values_[*idx] : Value{ 0.0f };
	}

	float SignalState::Scalar(std::size_t a_index) const noexcept
	{
		return AsScalar(ValueOf(a_index));
	}

	Vec3 SignalState::Vector(std::size_t a_index) const noexcept
	{
		return AsVec3(ValueOf(a_index));
	}

	float SignalState::Scalar(std::string_view a_name) const noexcept
	{
		return AsScalar(ValueOf(a_name));
	}

	Vec3 SignalState::Vector(std::string_view a_name) const noexcept
	{
		return AsVec3(ValueOf(a_name));
	}

	float SignalState::Resolve(const Param& a_param) const noexcept
	{
		return Match(
			a_param,
			[](float f) { return f; },
			[&](const Ref& r) { return Scalar(r.name); });
	}

	Vec2 SignalState::Resolve(const Vec2Param& a_param) const noexcept
	{
		return Match(
			a_param,
			[&](const Ref& r) { return AsVec2(ValueOf(r.name)); },
			[&](const std::array<Param, 2>& parts) { return Vec2{ Resolve(parts[0]), Resolve(parts[1]) }; });
	}

	Vec3 SignalState::Resolve(const Vec3Param& a_param) const noexcept
	{
		return Match(
			a_param,
			[&](const Ref& r) { return Vector(r.name); },
			[&](const std::array<Param, 3>& parts) { return Vec3{ Resolve(parts[0]), Resolve(parts[1]), Resolve(parts[2]) }; });
	}

	std::span<const TriggerFiring> SignalState::Firings(std::string_view a_trigger) const noexcept
	{
		const auto idx = graph_.Index(a_trigger);
		if (!idx) {
			return {};
		}
		return states_[*idx].firings;
	}

	void SignalState::Accept(std::size_t a_index, const EventRecord& a_event, float a_time)
	{
		const auto& node = graph_.nodes_[a_index];
		const auto* trigger = Get<TriggerSignal>(node.signal.kind);
		if (!trigger || node.inert) {
			return;
		}
		TriggerFiring firing{ a_time, a_event.payload };
		const bool    accepted = Match(
            trigger->origin,
            [&](const EventOrigin& s) {
                if (!GlobMatches(s.event, a_event.id) || !MatchesFilter(s.filter, a_event.payload)) {
                    return false;
                }
                if (firing.payload.node.empty()) {
                    firing.payload.node = s.at;
                }
                return true;
            },
            [&](const PluginOrigin& s) { return GlobMatches(s.id, a_event.id); },
            [](const WhenOrigin&) { return false; });
		if (!accepted) {
			return;
		}
		auto& st = states_[a_index];
		st.firings.push_back(std::move(firing));
		++st.fired;
		const std::size_t keep = std::max<std::uint32_t>(1, trigger->max);
		while (st.firings.size() > keep) {
			st.firings.erase(st.firings.begin());
		}
	}

	void SignalState::Fire(const EventRecord& a_event, float a_time)
	{
		for (std::size_t i = 0; i < graph_.Size(); ++i) {
			Accept(i, a_event, a_time);
		}
	}

	Value SignalState::Evaluate(std::size_t a_index, const SignalEnvironment& a_environment, const TickInputs& a_inputs)
	{
		const auto& node = graph_.nodes_[a_index];
		auto&       st = states_[a_index];
		const float time = a_inputs.time;
		const float dt = std::max(0.0f, a_inputs.delta);
		const auto  triggerState = [&](const Ref& a_ref) -> const NodeState* {
            const auto idx = graph_.Index(a_ref.name);
            return idx ? &states_[*idx] : nullptr;
		};

		return Match(
			node.signal.kind,
			[&](const ConstantSignal& k) -> Value { return k.value; },
			[&](const PulseSignal& k) -> Value {
				const float period = Resolve(k.period);
				if (period > kEpsilon) {
					st.phase += dt / period;
					st.phase -= std::floor(st.phase);
				}
				return Resolve(k.base) + Resolve(k.amplitude) * Wave(k.waveform, st.phase + Resolve(k.phase));
			},
			[&](const RampSignal& k) -> Value {
				const float seconds = Resolve(k.seconds);
				const float t = seconds <= kEpsilon ? 1.0f : Clamp01(time / seconds);
				return Resolve(k.from) + (Resolve(k.to) - Resolve(k.from)) * t;
			},
			[&](const EfshSignal& k) -> Value {
				const auto params = a_environment.EffectShader(k.record);
				if (!params) {
					return ZeroOf(node.type);
				}
				const auto fill = Efsh::Evaluate(*params, time, 1.0f, 1.0f);
				switch (k.field) {
				case EfshField::kFillAlpha:
					return fill.alpha;
				case EfshField::kFillColor:
					return Vec3{ fill.color.x * fill.scale, fill.color.y * fill.scale, fill.color.z * fill.scale };
				case EfshField::kEdgeAlpha:
					return fill.edgeAlpha;
				case EfshField::kEdgeColor:
					return fill.edgeColor;
				case EfshField::kScroll:
					return Vec2{ fill.uOffset, fill.vOffset };
				}
				return 0.0f;
			},
			[&](const ActorValueSignal& k) -> Value { return a_environment.ActorValue(k.actorValue, k.measure); },
			[&](const ActorStateSignal& k) -> Value { return a_environment.ActorState(k.kind); },
			[&](const EnchantmentSignal& k) -> Value { return a_environment.Enchantment(k.field); },
			[&](const TriggerSignal& k) -> Value {
				if (const auto* when = Get<WhenOrigin>(k.origin)) {
					const float now = Scalar(when->when.name);
					const float before = st.previous ? AsScalar(*st.previous) : 0.0f;
					st.previous = now;
					if (before <= 0.0f && now > 0.0f) {
						TriggerFiring firing{ time, {} };
						if (when->value) {
							firing.payload.value = Scalar(when->value->name);
						}
						st.firings.push_back(std::move(firing));
						++st.fired;
						const std::size_t keep = std::max<std::uint32_t>(1, k.max);
						while (st.firings.size() > keep) {
							st.firings.erase(st.firings.begin());
						}
					}
				}
				const float lifetime = std::max(kEpsilon, Resolve(k.lifetime));
				std::erase_if(st.firings, [&](const TriggerFiring& f) { return time - f.startTime >= lifetime; });
				if (st.firings.empty()) {
					return 1.0f;
				}
				return Clamp01((time - st.firings.back().startTime) / lifetime);
			},
			[&](const PayloadSignal& k) -> Value {
				const auto* src = triggerState(k.trigger);
				if (src && !src->firings.empty()) {
					const auto& p = src->firings.back().payload;
					switch (k.field) {
					case PayloadField::kValue:
						st.held = p.value;
						break;
					case PayloadField::kPosition:
						st.held = p.position.value_or(AsVec3(st.held));
						break;
					case PayloadField::kNormal:
						st.held = p.normal.value_or(AsVec3(st.held));
						break;
					}
				}
				return st.held;
			},
			[&](const CounterSignal& k) -> Value {
				const auto* src = triggerState(k.trigger);
				const auto* reset = k.reset ? triggerState(*k.reset) : nullptr;
				if (reset && reset->fired > st.seenReset) {
					st.seenReset = reset->fired;
					st.accumulator = 0.0f;
				}
				if (src) {
					st.accumulator += static_cast<float>(src->fired - st.seen);
					st.seen = src->fired;
				}
				if (k.cap) {
					const float cap = Resolve(*k.cap);
					if (cap > 0.0f) {
						st.accumulator = std::min(st.accumulator, cap);
					}
				}
				return st.accumulator;
			},
			[&](const AccumulateSignal& k) -> Value {
				st.accumulator = std::max(0.0f, st.accumulator - Resolve(k.decay) * dt);
				if (const auto* src = triggerState(k.trigger)) {
					st.accumulator += static_cast<float>(src->fired - st.seen);
					st.seen = src->fired;
				}
				return st.accumulator;
			},
			[&](const NoiseSignal& k) -> Value { return Resolve(k.amplitude) * ValueNoise(time * Resolve(k.frequency), k.seed); },
			[&](const GradientSignal& k) -> Value {
				if (k.stops.empty()) {
					return Vec3{};
				}
				const float t = Resolve(k.t);
				const auto* lo = &k.stops.front();
				const auto* hi = &k.stops.back();
				for (const auto& s : k.stops) {
					if (s.at <= t && s.at >= lo->at) lo = &s;
					if (s.at >= t && s.at <= hi->at) hi = &s;
				}
				if (t <= k.stops.front().at) {
					return Resolve(k.stops.front().color);
				}
				if (t >= k.stops.back().at) {
					return Resolve(k.stops.back().color);
				}
				const float span = hi->at - lo->at;
				return Lerp(Resolve(lo->color), Resolve(hi->color), span <= kEpsilon ? 0.0f : (t - lo->at) / span);
			},
			[&](const DeltaSignal& k) -> Value {
				const Value now = ValueOf(k.of.name);
				const Value before = st.previous.value_or(now);
				st.previous = now;
				return Match(
					now,
					[&](float f) -> Value { return f - AsScalar(before); },
					[&](const Vec2& v) -> Value {
						const auto b = AsVec2(before);
						return Vec2{ v.x - b.x, v.y - b.y };
					},
					[&](const Vec3& v) -> Value {
						const auto b = AsVec3(before);
						return Vec3{ v.x - b.x, v.y - b.y, v.z - b.z };
					});
			},
			[&](const SmoothSignal& k) -> Value {
				const Value target = ValueOf(k.of.name);
				if (!st.previous) {
					st.previous = target;
					return target;
				}
				const float seconds = Resolve(k.seconds);
				const float a = seconds <= kEpsilon ? 1.0f : 1.0f - std::exp(-dt / seconds);
				const Value next = Match(
					target,
					[&](float f) -> Value { return AsScalar(*st.previous) + (f - AsScalar(*st.previous)) * a; },
					[&](const Vec2& v) -> Value {
						const auto p = AsVec2(*st.previous);
						return Vec2{ p.x + (v.x - p.x) * a, p.y + (v.y - p.y) * a };
					},
					[&](const Vec3& v) -> Value { return Lerp(AsVec3(*st.previous), v, a); });
				st.previous = next;
				return next;
			},
			[&](const ExprSignal&) -> Value {
				if (!node.expression) {
					return ZeroOf(node.type);
				}
				Value       refs[64];
				std::size_t count = 0;
				for (const auto r : node.exprRefs) {
					if (count < std::size(refs)) {
						refs[count++] = ValueOf(r);
					}
				}
				Program::Inputs in;
				in.refs = std::span{ refs, count };
				in.curves = node.exprCurves;
				in.time = time;
				return node.expression->Evaluate(in);
			});
	}

	void SignalState::Tick(const SignalEnvironment& a_environment, const TickInputs& a_inputs)
	{
		for (const auto i : graph_.Order()) {
			const auto& node = graph_.nodes_[i];
			if (node.inert) {
				continue;
			}
			Value value = Evaluate(i, a_environment, a_inputs);
			if (node.curve) {
				const auto& curve = *node.curve;
				value = Match(
					value,
					[&](float f) -> Value { return ApplyCurve(curve, f); },
					[&](const Vec2& v) -> Value { return Vec2{ ApplyCurve(curve, v.x), ApplyCurve(curve, v.y) }; },
					[&](const Vec3& v) -> Value { return Vec3{ ApplyCurve(curve, v.x), ApplyCurve(curve, v.y), ApplyCurve(curve, v.z) }; });
			}
			values_[i] = value;
		}
	}
}
