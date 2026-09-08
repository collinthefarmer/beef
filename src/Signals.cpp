#include "Signals.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <numbers>

namespace WornEnchantmentPBR
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

		bool MatchesFilter(const EventFilter& a_filter, const TriggerPayload& a_payload) noexcept
		{
			if (!a_filter.node.empty() && !GlobMatch(a_filter.node, a_payload.node)) {
				return false;
			}
			if (!a_filter.arg.empty() && !GlobMatch(a_filter.arg, a_payload.arg)) {
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

	SignalGraph SignalGraph::Compile(std::span<const Signal> a_signals, std::span<const Curve> a_curves)
	{
		SignalGraph g;
		const auto  report = [&](const std::string& a_name, std::string a_message) {
            g.diagnostics_.push_back({ Severity::kError, std::format("signal {}", a_name), std::move(a_message) });
		};

		for (const auto& c : a_curves) {
			if (auto program = ParseCurve(c.text)) {
				g.curves_.emplace(c.name, std::move(*program));
			}
		}

		g.nodes_.reserve(a_signals.size());
		for (const auto& s : a_signals) {
			Node n;
			n.signal = s;
			if (!g.byName_.emplace(s.name, g.nodes_.size()).second) {
				n.inert = true;
				report(s.name, "duplicate name; this row is inert");
			}
			g.nodes_.push_back(std::move(n));
		}

		for (auto& n : g.nodes_) {
			if (const auto* expr = Get<ExprSignal>(n.signal.kind)) {
				auto parsed = Program::Parse(expr->text);
				if (!parsed) {
					n.inert = true;
					report(n.signal.name, std::format("expr: {}", parsed.error()));
				} else {
					n.expression = std::move(*parsed);
				}
			}
			for (const auto& dep : Dependencies(n.signal, n.expression ? &*n.expression : nullptr)) {
				const auto idx = g.Index(dep);
				if (!idx) {
					n.inert = true;
					report(n.signal.name, std::format("reads unknown signal '@{}'", dep));
					continue;
				}
				n.deps.push_back(*idx);
			}
			if (n.signal.curve) {
				if (const auto name = n.signal.curve->Named()) {
					if (const auto* program = g.CurveProgram(*name)) {
						n.curve = *program;
					} else {
						n.inert = true;
						report(n.signal.name, std::format("curve names unknown curve '@{}'", *name));
					}
				} else if (auto program = ParseCurve(n.signal.curve->text)) {
					n.curve = std::move(*program);
				} else {
					n.inert = true;
					report(n.signal.name, std::format("curve: {}", program.error()));
				}
			}
		}

		enum class Mark : std::uint8_t { kNone, kOpen, kDone };
		std::vector<Mark>                marks(g.nodes_.size(), Mark::kNone);
		std::vector<std::size_t>         path;
		std::function<void(std::size_t)> visit = [&](std::size_t i) {
			if (marks[i] == Mark::kDone) {
				return;
			}
			if (marks[i] == Mark::kOpen) {
				std::string cycle;
				bool        on = false;
				for (const auto p : path) {
					on = on || p == i;
					if (on) {
						g.nodes_[p].inert = true;
						cycle += g.nodes_[p].signal.name + " -> ";
					}
				}
				report(g.nodes_[i].signal.name, std::format("cycle: {}{}", cycle, g.nodes_[i].signal.name));
				return;
			}
			marks[i] = Mark::kOpen;
			path.push_back(i);
			for (const auto d : g.nodes_[i].deps) {
				visit(d);
			}
			path.pop_back();
			marks[i] = Mark::kDone;
			g.order_.push_back(i);
		};
		for (std::size_t i = 0; i < g.nodes_.size(); ++i) {
			visit(i);
		}

		for (const auto i : g.order_) {
			auto&      n = g.nodes_[i];
			const auto typeOf = [&](std::string_view name) -> std::optional<ValueType> { return g.TypeOf(name); };
			n.type = Match(
				n.signal.kind,
				[](const ConstantSignal& k) { return WornEnchantmentPBR::TypeOf(k.value); },
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
						report(n.signal.name, std::format("expr: {}", checked.error()));
						return ValueType::kScalar;
					}
					return *checked;
				},
				[](const auto&) { return ValueType::kScalar; });
			if (n.curve && n.type != ValueType::kScalar) {
			}
			if (n.expression) {
				for (const auto& r : n.expression->References()) {
					n.exprRefs.push_back(static_cast<std::uint32_t>(g.Index(r).value_or(0)));
				}
				for (const auto& c : n.expression->Curves()) {
					const auto* program = g.CurveProgram(c);
					if (!program) {
						n.inert = true;
						report(n.signal.name, std::format("expr calls unknown curve '@{}'", c));
					}
					n.exprCurves.push_back(program);
				}
			}
		}

		for (auto& n : g.nodes_) {
			const auto scalar = [&](const Param& p, const char* what) {
				if (const auto* ref = Get<Ref>(p)) {
					if (const auto t = g.TypeOf(ref->name); t && *t != ValueType::kScalar) {
						n.inert = true;
						report(n.signal.name, std::format("'{}' must be a scalar; '@{}' is a {}", what, ref->name, Name(*t)));
					}
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
						if (const auto t = g.TypeOf(when->when.name); t && *t != ValueType::kScalar) {
							n.inert = true;
							report(n.signal.name, std::format("'when' must be a scalar; '@{}' is a {}", when->when.name, Name(*t)));
						}
					}
				},
				[&](const PayloadSignal& k) {
					const auto idx = g.Index(k.trigger.name);
					if (idx && !Is<TriggerSignal>(g.nodes_[*idx].signal.kind)) {
						n.inert = true;
						report(n.signal.name, std::format("'trigger' must name a trigger; '@{}' is not one", k.trigger.name));
					}
				},
				[&](const CounterSignal& k) {
					for (const auto* ref : { &k.trigger, k.reset ? &*k.reset : nullptr }) {
						if (!ref) continue;
						const auto idx = g.Index(ref->name);
						if (idx && !Is<TriggerSignal>(g.nodes_[*idx].signal.kind)) {
							n.inert = true;
							report(n.signal.name, std::format("'@{}' must be a trigger", ref->name));
						}
					}
					if (k.cap) scalar(*k.cap, "cap");
				},
				[&](const AccumulateSignal& k) {
					const auto idx = g.Index(k.trigger.name);
					if (idx && !Is<TriggerSignal>(g.nodes_[*idx].signal.kind)) {
						n.inert = true;
						report(n.signal.name, std::format("'@{}' must be a trigger", k.trigger.name));
					}
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
							if (const auto t = g.TypeOf(ref->name); t && *t != ValueType::kVec3) {
								n.inert = true;
								report(n.signal.name, std::format("a stop colour must be a vec3; '@{}' is a {}", ref->name, Name(*t)));
							}
						}
					}
				},
				[&](const SmoothSignal& k) { scalar(k.seconds, "seconds"); },
				[](const auto&) {});
		}

		for (const auto i : g.order_) {
			for (const auto d : g.nodes_[i].deps) {
				if (g.nodes_[d].inert && !g.nodes_[i].inert) {
					g.nodes_[i].inert = true;
					g.diagnostics_.push_back({ Severity::kWarning, std::format("signal {}", g.nodes_[i].signal.name), std::format("inert because '@{}' is", g.nodes_[d].signal.name) });
				}
			}
		}
		return g;
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
                if (!GlobMatch(s.event, a_event.id) || !MatchesFilter(s.filter, a_event.payload)) {
                    return false;
                }
                if (firing.payload.node.empty()) {
                    firing.payload.node = s.at;
                }
                return true;
            },
            [&](const PluginOrigin& s) { return GlobMatch(s.id, a_event.id); },
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
				const auto fill = Timing::Evaluate(*params, time, 1.0f, 1.0f);
				switch (k.field) {
				case EfshField::kFillAlpha:
					return fill.alpha;
				case EfshField::kFillColor:
					return Vec3{ fill.color.r * fill.scale, fill.color.g * fill.scale, fill.color.b * fill.scale };
				case EfshField::kEdgeAlpha:
					return fill.edgeAlpha;
				case EfshField::kEdgeColor:
					return Vec3{ fill.edgeColor.r, fill.edgeColor.g, fill.edgeColor.b };
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
				Value refs[64];
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
