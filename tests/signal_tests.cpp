#include "Signals.h"
#include "test_support.h"

#include <format>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Near;

namespace
{
	class FakeEnvironment final : public SignalEnvironment
	{
	public:
		float                               health = 100.0f;
		float                               healthMax = 100.0f;
		float                               period = 1.0f;
		float                               combat = 0.0f;
		float                               magnitude = 25.0f;
		std::optional<Timing::EffectParams> efsh;

		float ActorValue(std::string_view a_name, Measure a_measure) const override
		{
			if (a_name == "Health") {
				switch (a_measure) {
				case Measure::kCurrent:
					return health;
				case Measure::kMax:
					return healthMax;
				case Measure::kDamage:
					return healthMax - health;
				default:
					return healthMax;
				}
			}
			if (a_name == "Period") {
				return period;
			}
			return 0.0f;
		}
		float ActorState(ActorStateKind a_kind) const override { return a_kind == ActorStateKind::kInCombat ? combat : 0.0f; }
		float Enchantment(EnchantmentField a_field) const override { return a_field == EnchantmentField::kMagnitude ? magnitude : 0.0f; }
		std::optional<Timing::EffectParams> EffectShader(const FormRef&) const override { return efsh; }
	};

	Ref At(const char* a_name)
	{
		return Ref{ a_name };
	}

	Signal Const(const char* a_name, float a_value)
	{
		return Signal{ a_name, ConstantSignal{ a_value }, std::nullopt };
	}

	Signal Expr(const char* a_name, const char* a_text)
	{
		return Signal{ a_name, ExprSignal{ a_text }, std::nullopt };
	}

	Signal Trigger(const char* a_name, TriggerOrigin a_source, float a_lifetime, std::uint32_t a_max, std::optional<CurveRef> a_curve = std::nullopt)
	{
		return Signal{ a_name, TriggerSignal{ std::move(a_source), a_lifetime, a_max }, std::move(a_curve) };
	}

	EventRecord Event(const char* a_id, TriggerPayload a_payload = {})
	{
		return EventRecord{ a_id, std::move(a_payload) };
	}

	void Graph()
	{
		std::vector<Signal> signals{
			Expr("c", "@b * 2"),
			Expr("b", "@a + 1"),
			Const("a", 3.0f),
			Expr("loop1", "@loop2 + 1"),
			Expr("loop2", "@loop1 + 1"),
			Expr("reader", "@loop1"),
			Expr("orphan", "@nothing * 2"),
			Expr("broken", "1 +"),
			Signal{ "shaped", ConstantSignal{ 0.5f }, CurveRef{ "@flash" } },
			Signal{ "inline", ConstantSignal{ 0.5f }, CurveRef{ "x * 4" } },
			Expr("calls", "@flash(@a / 3) + @inline"),
			Expr("colour", "[1, 0, 0] * @a"),
			Expr("bad", "@colour + [1, 1]"),
		};
		const std::vector<Curve> curves{ { "flash", "pow(1 - x, 2)" } };
		const auto               graph = SignalGraph::Compile(signals, curves);
		Check(graph.Size() == 13, "all nodes present");
		const auto pos = [&](const char* a_name) {
			const auto idx = *graph.Index(a_name);
			return static_cast<std::size_t>(std::ranges::find(graph.Order(), idx) - graph.Order().begin());
		};
		Check(pos("a") < pos("b") && pos("b") < pos("c"), "dependency order regardless of declaration order");
		Check(graph.Inert(*graph.Index("loop1")) && graph.Inert(*graph.Index("loop2")) && graph.Inert(*graph.Index("reader")), "cycle members and their readers are inert");
		Check(graph.Inert(*graph.Index("orphan")) && graph.Inert(*graph.Index("broken")) && graph.Inert(*graph.Index("bad")), "unknown reference, parse error and type error are inert");
		Check(!graph.Inert(*graph.Index("c")) && !graph.Inert(*graph.Index("calls")), "healthy nodes are live");
		Check(graph.TypeOf("colour") == ValueType::kVec3 && graph.TypeOf("c") == ValueType::kScalar, "types inferred");
		bool cycle = false, unknown = false, parse = false, type = false;
		for (const auto& d : graph.Diagnostics()) {
			cycle = cycle || d.message.find("cycle: loop1 -> loop2 -> loop1") != std::string::npos;
			unknown = unknown || (d.where == "signal orphan" && d.message.find("unknown signal '@nothing'") != std::string::npos);
			parse = parse || (d.where == "signal broken" && d.message.find("expr:") != std::string::npos);
			type = type || (d.where == "signal bad" && d.message.find("mixes vec3 with vec2") != std::string::npos);
		}
		Check(cycle && unknown && parse && type, "each problem reported on its row");

		SignalState     state(graph);
		NullEnvironment env;
		state.Tick(env, { 0.0f, 0.0f });
		Check(Near(state.Scalar("c"), 8.0f), "evaluation through the chain");
		Check(Near(state.Scalar("loop1"), 0.0f), "inert nodes hold 0");
		Check(Near(state.Scalar("shaped"), 0.25f), "declared curve applied to a row");
		Check(Near(state.Scalar("inline"), 2.0f), "inline curve applied to a row");
		Check(Near(state.Scalar("calls"), 2.0f), "curve called inside an expression");
		Check(state.Vector("colour") == Vec3{ 3, 0, 0 }, "vector expression");

		const std::vector<Signal> dup{ Const("x", 1.0f), Const("x", 2.0f) };
		const auto                dupGraph = SignalGraph::Compile(dup, {});
		Check(dupGraph.Diagnostics().size() == 1 && dupGraph.Diagnostics()[0].message.find("duplicate") != std::string::npos, "duplicate names reported");
	}

	void Pulses()
	{
		std::vector<Signal> signals{
			Signal{ "period", ActorValueSignal{ "Period", Measure::kCurrent }, std::nullopt },
			Signal{ "p", PulseSignal{ 0.0f, 1.0f, At("period"), 0.0f, Waveform::kSine }, std::nullopt },
			Signal{ "sq", PulseSignal{ 0.0f, 1.0f, 1.0f, 0.0f, Waveform::kSquare }, std::nullopt },
			Signal{ "saw", PulseSignal{ 0.0f, 1.0f, 1.0f, 0.5f, Waveform::kSaw }, std::nullopt },
		};
		const auto      graph = SignalGraph::Compile(signals, {});
		SignalState     state(graph);
		FakeEnvironment env;
		float           time = 0.0f;
		state.Tick(env, { time, 0.0f });
		Check(Near(state.Scalar("p"), 0.0f), "sine pulse starts at 0");
		for (int i = 0; i < 5; ++i) {
			time += 0.05f;
			state.Tick(env, { time, 0.05f });
		}
		Check(Near(state.Scalar("p"), 0.5f, 1e-3f), "a quarter period in: 0.5");
		env.period = 2.0f;
		const float before = state.Scalar("p");
		time += 0.05f;
		state.Tick(env, { time, 0.05f });
		const float after = state.Scalar("p");
		Check(after > before && after < 0.6f, std::format("phase continuous across a period change ({} -> {})", before, after));
		Check(state.Scalar("sq") == 1.0f && Near(state.Scalar("saw"), 0.8f, 1e-3f), "square and saw with phase offset");
	}

	void EventTriggers()
	{
		std::vector<Signal> signals{
			Trigger("hit", EventOrigin{ "hit.received", {}, "" }, 1.0f, 2),
			Trigger("flash", EventOrigin{ "hit.received", {}, "" }, 1.0f, 2, CurveRef{ "1 - x" }),
			Trigger("spine", EventOrigin{ "hit.*", EventFilter{ "NPC Spine*", "", {} }, "" }, 1.0f, 2),
			Trigger("hard", EventOrigin{ "hit.received", EventFilter{ "", "", ValueRange{ 10.0f, std::nullopt } }, "" }, 1.0f, 2),
			Trigger("step", EventOrigin{ "anim.Foot*", {}, "NPC L Foot [Lft ]" }, 0.6f, 2),
			Trigger("surge", PluginOrigin{ "MyMod.Surge" }, 2.0f, 1),
			Trigger("reset", EventOrigin{ "equip", {}, "" }, 0.1f, 1),
			Signal{ "count", CounterSignal{ At("hit"), At("reset"), Param{ 3.0f } }, std::nullopt },
			Signal{ "heat", AccumulateSignal{ At("hit"), 2.0f }, std::nullopt },
			Signal{ "force", PayloadSignal{ At("hit"), PayloadField::kValue }, std::nullopt },
			Signal{ "where", PayloadSignal{ At("hit"), PayloadField::kPosition }, std::nullopt },
		};
		const auto      graph = SignalGraph::Compile(signals, {});
		Check(graph.Diagnostics().empty(), "trigger graph compiles");
		SignalState     state(graph);
		NullEnvironment env;
		state.Tick(env, { 0.0f, 0.0f });
		Check(Near(state.Scalar("hit"), 1.0f) && Near(state.Scalar("flash"), 0.0f), "no firing: fully aged, no flash");

		TriggerPayload payload;
		payload.node = "NPC Spine2 [Spn2]";
		payload.position = Vec3{ 1, 2, 3 };
		payload.value = 25.0f;
		state.Fire(Event("hit.received", payload), 0.0f);
		state.Tick(env, { 0.0f, 0.0f });
		Check(Near(state.Scalar("hit"), 0.0f) && Near(state.Scalar("flash"), 1.0f), "fresh firing: age 0, full flash");
		Check(Near(state.Scalar("spine"), 0.0f), "node filter accepts a matching node");
		Check(Near(state.Scalar("hard"), 0.0f), "value filter accepts 25 >= 10");
		Check(Near(state.Scalar("force"), 25.0f) && state.Vector("where") == Vec3{ 1, 2, 3 }, "payload signals read the firing");
		Check(state.Firings("hit").size() == 1 && state.Firings("hit")[0].payload.node == "NPC Spine2 [Spn2]", "firing kept with its payload");

		TriggerPayload weak;
		weak.node = "NPC L Hand [LHnd]";
		weak.value = 3.0f;
		state.Fire(Event("hit.received", weak), 0.2f);
		state.Tick(env, { 0.2f, 0.2f });
		Check(state.Firings("spine").size() == 1 && state.Firings("hard").size() == 1 && state.Firings("hit").size() == 2, "filters reject the second firing, the plain trigger keeps both");
		Check(Near(state.Scalar("force"), 3.0f), "payload follows the newest firing");

		state.Fire(Event("hit.dealt"), 0.3f);
		state.Tick(env, { 0.3f, 0.1f });
		Check(state.Firings("spine").size() == 1 && state.Firings("hit").size() == 2, "hit.dealt matches hit.* only, and its empty node fails the node filter");

		state.Fire(Event("anim.FootLeft"), 0.4f);
		state.Tick(env, { 0.4f, 0.1f });
		Check(state.Firings("step").size() == 1 && state.Firings("step")[0].payload.node == "NPC L Foot [Lft ]", "at supplies the node when the firing has none");
		state.Fire(Event("MyMod.Surge"), 0.5f);
		state.Fire(Event("MyMod.Other"), 0.5f);
		state.Tick(env, { 0.5f, 0.1f });
		Check(state.Firings("surge").size() == 1, "plugin source matches its id only");

		state.Tick(env, { 0.6f, 0.1f });
		Check(Near(state.Scalar("count"), 2.0f), "counter counts two hits");
		Check(state.Scalar("heat") > 0.0f && state.Scalar("heat") < 2.0f, "accumulate summed and decayed");
		state.Fire(Event("hit.received"), 0.7f);
		state.Fire(Event("hit.received"), 0.75f);
		state.Tick(env, { 0.8f, 0.2f });
		Check(state.Firings("hit").size() == 2 && Near(state.Firings("hit")[0].startTime, 0.7f), "live cap keeps the newest firings");
		Check(Near(state.Scalar("count"), 3.0f), "counter capped at 3 after four");
		state.Fire(Event("equip"), 0.9f);
		state.Tick(env, { 0.9f, 0.1f });
		Check(Near(state.Scalar("count"), 0.0f), "reset trigger clears the counter");
		state.Tick(env, { 2.0f, 1.1f });
		Check(Near(state.Scalar("hit"), 1.0f) && state.Firings("hit").empty(), "firings die after their lifetime");
		Check(Near(state.Scalar("force"), 0.0f), "payload holds the last firing's value (0, the bare firings carried none) when no firing is alive");
	}

	void WhenTriggers()
	{
		std::vector<Signal> signals{
			Signal{ "damage", ActorValueSignal{ "Health", Measure::kDamage }, std::nullopt },
			Signal{ "wound", DeltaSignal{ At("damage") }, std::nullopt },
			Trigger("hurt", WhenOrigin{ At("wound"), At("wound") }, 0.8f, 3, CurveRef{ "pow(1 - x, 2)" }),
			Signal{ "drop", PayloadSignal{ At("hurt"), PayloadField::kValue }, std::nullopt },
			Signal{ "eased", SmoothSignal{ At("damage"), 1.0f }, std::nullopt },
			Signal{ "healthFrac", ExprSignal{ "@health / @healthMax" }, std::nullopt },
			Signal{ "health", ActorValueSignal{ "Health", Measure::kCurrent }, std::nullopt },
			Signal{ "healthMax", ActorValueSignal{ "Health", Measure::kMax }, std::nullopt },
			Expr("low", "@healthFrac < 0.25"),
			Trigger("warn", WhenOrigin{ At("low"), std::nullopt }, 2.0f, 1),
		};
		const auto      graph = SignalGraph::Compile(signals, {});
		Check(graph.Diagnostics().empty(), "when graph compiles");
		SignalState     state(graph);
		FakeEnvironment env;
		env.health = 100.0f;
		state.Tick(env, { 0.0f, 0.0f });
		state.Tick(env, { 0.1f, 0.1f });
		Check(Near(state.Scalar("wound"), 0.0f) && Near(state.Scalar("hurt"), 0.0f), "no change: no wound, no flash (1 - 1)");
		env.health = 70.0f;
		state.Tick(env, { 0.2f, 0.1f });
		Check(Near(state.Scalar("wound"), 30.0f), "delta reports the drop");
		Check(Near(state.Scalar("hurt"), 1.0f) && Near(state.Scalar("drop"), 30.0f), "when fires on the rising edge with the sampled value");
		state.Tick(env, { 0.3f, 0.1f });
		Check(Near(state.Scalar("wound"), 0.0f) && state.Scalar("hurt") < 1.0f && state.Scalar("hurt") > 0.5f, "next tick: delta back to 0, the firing ages");
		env.health = 90.0f;
		state.Tick(env, { 0.4f, 0.1f });
		Check(Near(state.Scalar("wound"), -20.0f) && state.Firings("hurt").size() == 1, "healing is a negative delta and does not fire");
		Check(state.Scalar("eased") < 30.0f && state.Scalar("eased") > 0.0f, "smooth lags toward the target");
		env.health = 20.0f;
		state.Tick(env, { 0.5f, 0.1f });
		Check(state.Firings("hurt").size() == 2 && Near(state.Scalar("drop"), 70.0f), "a second wound fires again with its own size");
		Check(state.Firings("warn").size() == 1, "crossing below a quarter fires once");
		state.Tick(env, { 0.6f, 0.1f });
		Check(state.Firings("warn").size() == 1, "staying below does not fire again");
		env.health = 30.0f;
		state.Tick(env, { 0.7f, 0.1f });
		env.health = 20.0f;
		state.Tick(env, { 0.8f, 0.1f });
		Check(state.Firings("warn").size() == 1 && Near(state.Firings("warn")[0].startTime, 0.8f), "crossing back and down again fires again (max 1 keeps only the newest)");
	}

	void Environment()
	{
		Timing::EffectParams efsh{};
		efsh.colorKeys = { Timing::Rgb{ 0.4f, 0.4f, 0.4f }, Timing::Rgb{ 0.4f, 0.4f, 0.4f }, Timing::Rgb{ 0.4f, 0.4f, 0.4f } };
		efsh.colorScale = 1.5f;
		efsh.fill.fullAlphaRatio = 0.05f;
		efsh.fill.persistentAlphaRatio = 0.05f;
		efsh.fill.pulseFrequency = 1.0f;
		efsh.animationSpeedV = 0.1f;
		efsh.edgeColor = Timing::Rgb{ 0.1f, 0.2f, 0.5f };
		efsh.edge.fullAlphaRatio = 0.9f;
		efsh.edge.persistentAlphaRatio = 0.9f;

		const auto          record = FormRef::From("EnchArmorMagickaFXS");
		std::vector<Signal> signals{
			Signal{ "health", ActorValueSignal{ "Health", Measure::kCurrent }, std::nullopt },
			Signal{ "healthMax", ActorValueSignal{ "Health", Measure::kMax }, std::nullopt },
			Signal{ "combat", ActorStateSignal{ ActorStateKind::kInCombat }, std::nullopt },
			Signal{ "magnitude", EnchantmentSignal{ EnchantmentField::kMagnitude }, std::nullopt },
			Signal{ "fillAlpha", EfshSignal{ EfshField::kFillAlpha, record }, std::nullopt },
			Signal{ "fillLevel", EfshSignal{ EfshField::kFillAlpha, record }, CurveRef{ "x / 0.05" } },
			Signal{ "fillColor", EfshSignal{ EfshField::kFillColor, record }, std::nullopt },
			Signal{ "edgeColor", EfshSignal{ EfshField::kEdgeColor, record }, std::nullopt },
			Signal{ "scroll", EfshSignal{ EfshField::kScroll, record }, std::nullopt },
			Signal{ "ramp", RampSignal{ 1.0f, 3.0f, 2.0f }, std::nullopt },
			Signal{ "noise", NoiseSignal{ 2.0f, 0.5f, 3 }, std::nullopt },
			Signal{ "frac", ExprSignal{ "@health / @healthMax" }, std::nullopt },
			Signal{ "palette", GradientSignal{ At("frac"), { { 0.0f, std::array<Param, 3>{ 0.0f, 0.0f, 1.0f } }, { 0.5f, std::array<Param, 3>{ 0.0f, 1.0f, 0.0f } }, { 1.0f, Vec3Param{ At("edgeColor") } } } }, std::nullopt },
			Signal{ "mix", ExprSignal{ "lerp([0, 0, 0], @palette, 0.5)" }, std::nullopt },
			Signal{ "shifted", ExprSignal{ "@scroll + [0.5, 0.5]" }, std::nullopt },
		};
		const auto graph = SignalGraph::Compile(signals, {});
		Check(graph.Diagnostics().empty(), "environment graph compiles");
		Check(graph.TypeOf("scroll") == ValueType::kVec2 && graph.TypeOf("shifted") == ValueType::kVec2 && graph.TypeOf("mix") == ValueType::kVec3, "efsh scroll is a vec2 and flows through expressions");
		SignalState     state(graph);
		FakeEnvironment env;
		env.health = 75.0f;
		env.combat = 1.0f;
		env.efsh = efsh;
		state.Tick(env, { 0.0f, 0.0f });
		Check(Near(state.Scalar("health"), 75.0f) && Near(state.Scalar("frac"), 0.75f), "actor value and its fraction by expression");
		Check(Near(state.Scalar("combat"), 1.0f) && Near(state.Scalar("magnitude"), 25.0f), "actor state and enchantment magnitude");
		const auto reference = Timing::Evaluate(efsh, 0.0f, 1.0f, 1.0f);
		Check(Near(state.Scalar("fillAlpha"), reference.alpha) && Near(state.Scalar("fillLevel"), 1.0f), "efsh fill alpha matches the timing port; level 1 at rest");
		Check(Near(state.Vector("fillColor").x, 0.6f) && Near(state.Vector("edgeColor").z, 0.5f), "efsh colours");
		Check(Near(state.Scalar("ramp"), 1.0f), "ramp at t=0");
		state.Tick(env, { 2.5f, 2.5f });
		const auto scroll = AsVec2(state.ValueOf("scroll"));
		Check(Near(scroll.y, 0.25f) && Near(scroll.x, 0.0f), "efsh scroll offset wraps the timing port's value");
		Check(Near(AsVec2(state.ValueOf("shifted")).y, 0.75f), "vec2 arithmetic on scroll");
		Check(Near(state.Scalar("ramp"), 3.0f), "ramp clamps at its end");
		const float n1 = state.Scalar("noise");
		Check(n1 >= -0.5f && n1 <= 0.5f, "noise within amplitude");
		SignalState other(graph);
		other.Tick(env, { 0.0f, 0.0f });
		other.Tick(env, { 2.5f, 2.5f });
		Check(Near(other.Scalar("noise"), n1), "noise is deterministic for a seed and time");
		Check(Near(state.Vector("palette").y, 0.6f) && Near(state.Vector("palette").z, 0.25f), "gradient halfway between the green stop and the referenced edge colour");
		env.health = 50.0f;
		state.Tick(env, { 2.6f, 0.1f });
		Check(state.Vector("palette") == Vec3{ 0, 1, 0 }, "gradient at a stop");
		Check(Near(state.Vector("mix").y, 0.5f), "lerp function over colours");
		env.efsh.reset();
		state.Tick(env, { 2.7f, 0.1f });
		Check(Near(state.Scalar("fillAlpha"), 0.0f) && state.Vector("edgeColor") == Vec3{}, "a missing record evaluates to 0 and black");
	}
}

int main()
{
	Graph();
	Pulses();
	EventTriggers();
	WhenTriggers();
	Environment();
	return test::Finish("signal");
}
