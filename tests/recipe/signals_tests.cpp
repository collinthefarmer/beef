#include "recipe/Signals.h"
#include "recipe/Words.h"
#include "test_support.h"

#include <algorithm>
#include <limits>
#include <string>
#include <unordered_map>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Near;

namespace {
class FakeEnvironment final : public SignalEnvironment {
public:
  std::unordered_map<std::string, float> actorValues;
  float state = 0.0f;
  float enchantment = 0.0f;
  std::optional<Efsh::EffectParams> effect;
  Vec3 vector{};
  Vec3 rootOrigin{};

  float ActorValue(std::string_view a_name, Measure) const override {
    const auto it = actorValues.find(std::string{a_name});
    return it == actorValues.end() ? 0.0f : it->second;
  }
  float ActorState(ActorStateKind) const override { return state; }
  Vec3 ActorVector(ActorStateKind) const override { return vector; }
  Vec3 WorldToRoot(const Vec3 &a_world) const override {
    return Vec3{a_world.x - rootOrigin.x, a_world.y - rootOrigin.y,
                a_world.z - rootOrigin.z};
  }
  float Enchantment(EnchantmentField) const override { return enchantment; }
  std::optional<Efsh::EffectParams>
  EffectShader(const FormRef &) const override {
    return effect;
  }
};

Signal Const(std::string a_name, Value a_value) {
  return Signal{std::move(a_name), ConstantSignal{a_value}, std::nullopt};
}

Signal Expr(std::string a_name, std::string a_text) {
  return Signal{std::move(a_name), ExprSignal{std::move(a_text)}, std::nullopt};
}

bool HasMessage(std::span<const Diagnostic> a_diagnostics,
                std::string_view a_needle) {
  return std::ranges::any_of(a_diagnostics, [&](const Diagnostic &d) {
    return d.message.find(a_needle) != std::string::npos;
  });
}

bool HasMessage(const std::vector<Diagnostic> &a_diagnostics,
                std::string_view a_needle) {
  return HasMessage(std::span<const Diagnostic>{a_diagnostics}, a_needle);
}

std::size_t OrderPosition(const SignalGraph &a_graph, std::string_view a_name) {
  const auto idx = a_graph.Index(a_name);
  const auto order = a_graph.Order();
  for (std::size_t p = 0; p < order.size(); ++p) {
    if (idx && order[p] == *idx) {
      return p;
    }
  }
  return order.size();
}
}

int main() {
  {
    std::vector<Signal> signals{Const("a", 5.0f), Expr("b", "@a + 1"),
                                Expr("c", "@b + 1")};
    const auto graph = SignalGraph::Compile(signals, {});
    Check(graph.Diagnostics().empty(),
          "a clean chain compiles without diagnostics");
    Check(OrderPosition(graph, "a") < OrderPosition(graph, "b"),
          "a is ordered before b");
    Check(OrderPosition(graph, "b") < OrderPosition(graph, "c"),
          "b is ordered before c");

    SignalState state{graph};
    NullEnvironment environment;
    state.Tick(environment, TickInputs{0.0f, 0.0f});
    Check(Near(state.Scalar("c"), 7.0f),
          "the chain evaluates in order: 5 -> 6 -> 7");
  }

  {
    std::vector<Signal> signals{Expr("a", "@b"), Expr("b", "@a")};
    const auto graph = SignalGraph::Compile(signals, {});
    Check(HasMessage(graph.Diagnostics(), "cycle"),
          "a two-signal cycle is reported");
    Check(graph.Inert(0) && graph.Inert(1),
          "both signals in the cycle are inert");
  }

  {
    std::vector<Signal> signals;
    signals.reserve(40);
    for (std::size_t i = 0; i < 39; ++i) {
      signals.push_back(
          Expr("s" + std::to_string(i), "@s" + std::to_string(i + 1)));
    }
    signals.push_back(Const("s39", 1.0f));
    const auto graph = SignalGraph::Compile(signals, {});
    Check(HasMessage(graph.Diagnostics(), "deeper than"),
          "a chain past the depth bound is rejected, not overflowed");
  }

  {
    std::vector<Signal> signals{
        Signal{"rate", ActorValueSignal{"Rate", Measure::kCurrent},
               std::nullopt},
        Signal{"p", WaveSignal{0.0f, 1.0f, Ref{"rate"}, 0.0f, Waveform::kSaw},
               std::nullopt}};
    const auto graph = SignalGraph::Compile(signals, {});
    Check(graph.Diagnostics().empty(),
          "a wave driven by a scalar signal compiles");
    SignalState state{graph};
    FakeEnvironment environment;
    environment.actorValues["Rate"] = 1.0f;
    state.Tick(environment, TickInputs{0.0f, 0.4f});
    Check(Near(state.Scalar("p"), 0.4f),
          "the saw wave integrates phase to 0.4 after dt=0.4 at period 1");
    environment.actorValues["Rate"] = 10.0f;
    state.Tick(environment, TickInputs{0.4f, 0.4f});
    Check(
        Near(state.Scalar("p"), 0.44f),
        "changing the period advances phase continuously (0.44), never jumps");
  }

  {
    std::vector<Signal> signals{
        Signal{"r", RampSignal{0.0f, 10.0f, 2.0f}, std::nullopt}};
    const auto graph = SignalGraph::Compile(signals, {});
    SignalState state{graph};
    NullEnvironment environment;
    state.Tick(environment, TickInputs{1.0f, 0.0f});
    Check(Near(state.Scalar("r"), 5.0f),
          "a ramp is halfway at half its duration");
    state.Tick(environment, TickInputs{3.0f, 0.0f});
    Check(Near(state.Scalar("r"), 10.0f),
          "a ramp holds at its end past its duration");
  }

  {
    std::vector<Signal> signals{
        Signal{"hit", TriggerSignal{EventOrigin{"hit", {}}, 1.0f, 4},
               std::nullopt},
        Signal{"val", PayloadSignal{Ref{"hit"}}, std::nullopt},
        Signal{"count", CounterSignal{Ref{"hit"}, std::nullopt, std::nullopt},
               std::nullopt},
        Signal{"acc", AccumulateSignal{Ref{"hit"}, 1.0f}, std::nullopt}};
    const auto graph = SignalGraph::Compile(signals, {});
    Check(graph.Diagnostics().empty(), "triggers and their readers compile");
    SignalState state{graph};
    NullEnvironment environment;

    EventRecord event;
    event.id = "hit";
    event.payload.value = 3.0f;
    state.Fire(event, 0.0f);
    state.Fire(event, 0.0f);
    state.Tick(environment, TickInputs{0.0f, 0.0f});
    Check(Near(state.Scalar("val"), 3.0f),
          "a payload signal holds the last firing's value");
    Check(Near(state.Scalar("count"), 2.0f), "a counter counts both firings");
    Check(Near(state.Scalar("acc"), 2.0f), "an accumulator sums both firings");
    state.Tick(environment, TickInputs{0.5f, 0.5f});
    Check(Near(state.Scalar("acc"), 1.5f),
          "the accumulator decays by decay*dt between ticks");
    Check(state.Firings("hit").size() == 2, "the trigger reports its firings");
  }

  {
    std::vector<Signal> signals{
        Signal{"charge", TriggerSignal{PluginOrigin{"myMod.*"}, 1.0f, 4},
               std::nullopt},
        Signal{"hit", TriggerSignal{EventOrigin{"myMod.*", {}}, 1.0f, 4},
               std::nullopt}};
    const auto graph = SignalGraph::Compile(signals, {});
    Check(graph.Diagnostics().empty(), "a plugin trigger compiles");
    SignalState state{graph};
    EventRecord fromPlugin;
    fromPlugin.id = "myMod.charge";
    fromPlugin.plugin = true;
    fromPlugin.payload.value = 2.0f;
    state.Fire(fromPlugin, 0.0f);
    EventRecord fromEngine;
    fromEngine.id = "myMod.charge";
    state.Fire(fromEngine, 0.0f);
    Check(state.Firings("charge").size() == 1,
          "a plugin origin fires on the plugin channel only");
    Check(state.Firings("hit").size() == 1,
          "an event origin fires on the engine channel only");
  }

  {
    std::vector<Signal> signals{
        Signal{"combat", ActorStateSignal{ActorStateKind::kInCombat},
               std::nullopt},
        Signal{"power", EnchantmentSignal{EnchantmentField::kMagnitude},
               std::nullopt}};
    const auto graph = SignalGraph::Compile(signals, {});
    SignalState state{graph};
    FakeEnvironment environment;
    environment.state = 1.0f;
    environment.enchantment = 25.0f;
    state.Tick(environment, TickInputs{0.0f, 0.0f});
    Check(Near(state.Scalar("combat"), 1.0f),
          "an actorState signal reads the environment");
    Check(Near(state.Scalar("power"), 25.0f),
          "an enchantment signal reads the environment");
  }

  {
    std::vector<Signal> signals{
        Signal{"impact",
               TriggerSignal{EventOrigin{"hit.pos", {}}, 5.0f, 4,
                             ValueType::kVec3},
               std::nullopt},
        Signal{"where", PayloadSignal{Ref{"impact"}}, std::nullopt}};
    const auto graph = SignalGraph::Compile(signals, {});
    Check(graph.Diagnostics().empty(), "a vec3-typed trigger compiles");
    Check(graph.TypeOf("where") == std::optional{ValueType::kVec3},
          "a payload signal takes the trigger's declared type");
    SignalState state{graph};
    NullEnvironment environment;
    EventRecord scalarEvent;
    scalarEvent.id = "hit.pos";
    scalarEvent.payload.value = 1.0f;
    state.Fire(scalarEvent, 0.0f);
    Check(state.Firings("impact").empty() && state.Mismatched("impact") == 1,
          "a scalar firing at a vec3 trigger is dropped and counted");
    EventRecord vecEvent;
    vecEvent.id = "hit.pos";
    vecEvent.payload.value = Vec3{1.0f, 2.0f, 3.0f};
    state.Fire(vecEvent, 0.0f);
    state.Tick(environment, TickInputs{0.0f, 0.0f});
    const Vec3 where = state.Vector("where");
    Check(state.Firings("impact").size() == 1 && Near(where.y, 2.0f),
          "a vec3 firing lands and the payload reads it as a vec3");
  }

  {
    std::vector<Signal> signals{
        Signal{"hue", ConstantSignal{Vec3{0.2f, 0.4f, 0.6f}}, std::nullopt},
        Signal{"gate", ActorValueSignal{"Gate", Measure::kCurrent},
               std::nullopt},
        Signal{"snap",
               TriggerSignal{WhenOrigin{Ref{"gate"}, Ref{"hue"}}, 5.0f, 4,
                             ValueType::kVec3},
               std::nullopt},
        Signal{"held", PayloadSignal{Ref{"snap"}}, std::nullopt}};
    const auto graph = SignalGraph::Compile(signals, {});
    Check(graph.Diagnostics().empty(),
          "a when trigger samples a vec3 into a vec3 payload");
    SignalState state{graph};
    FakeEnvironment environment;
    state.Tick(environment, TickInputs{0.0f, 0.0f});
    environment.actorValues["Gate"] = 1.0f;
    state.Tick(environment, TickInputs{0.1f, 0.1f});
    Check(Near(state.Vector("held").z, 0.6f),
          "the sampled vec3 reaches the payload reader");
  }

  {
    std::vector<Signal> signals{
        Signal{"gate", ActorValueSignal{"Gate", Measure::kCurrent},
               std::nullopt},
        Signal{"snap",
               TriggerSignal{WhenOrigin{Ref{"gate"}, Ref{"gate"}}, 5.0f, 4,
                             ValueType::kVec3},
               std::nullopt}};
    const auto graph = SignalGraph::Compile(signals, {});
    Check(HasMessage(graph.Diagnostics(), "payload is a vec3"),
          "a when value of the wrong type is rejected at compile");
  }

  {
    TriggerSignal world;
    world.payload = ValueType::kVec3;
    world.anchor = WorldAnchor{};
    TriggerPayload carried;
    carried.value = Vec3{1.0f, 2.0f, 3.0f};
    const auto worldAnchor = AnchorOf(world, carried);
    const auto *point = Get<CarriedPoint>(worldAnchor);
    Check(point != nullptr && Near(point->position.y, 2.0f),
          "a world anchor reads the payload as the point");

    TriggerSignal declared;
    declared.anchor = NodeAnchor{"NPC L Foot [Lft ]"};
    TriggerPayload bare;
    bare.value = 1.0f;
    const auto declaredAnchor = AnchorOf(declared, bare);
    const auto *node = Get<AnchorNode>(declaredAnchor);
    Check(node != nullptr && node->node == "NPC L Foot [Lft ]",
          "a node anchor locates at the declared node");
    TriggerPayload withNode = bare;
    withNode.node = "WEAPON";
    const auto carriedAnchor = AnchorOf(declared, withNode);
    const auto *carriedNode = Get<AnchorNode>(carriedAnchor);
    Check(carriedNode != nullptr && carriedNode->node == "WEAPON",
          "a firing's own node overrides the declared one");

    TriggerSignal unanchored;
    Check(Get<std::monostate>(AnchorOf(unanchored, carried)) != nullptr,
          "an unanchored trigger's payload is just a value");
  }

  for (const bool conditional : {false, true}) {
    const TriggerOrigin origin =
        conditional ? TriggerOrigin{WhenOrigin{Ref{"gate"}, Ref{"gate"}}}
                    : TriggerOrigin{EventOrigin{"hit", {}}};
    const std::vector<Signal> signals{
        Signal{"gate", ActorValueSignal{"Gate", Measure::kCurrent},
               std::nullopt},
        Signal{"hit", TriggerSignal{origin, 10.0f, 2}, std::nullopt},
        Signal{"count", CounterSignal{Ref{"hit"}, std::nullopt, std::nullopt},
               std::nullopt}};
    const auto graph = SignalGraph::Compile(signals, {});
    Check(graph.Diagnostics().empty(),
          "bounded event and conditional triggers compile");
    SignalState state{graph};
    FakeEnvironment environment;
    for (int firing = 1; firing <= 4; ++firing) {
      environment.actorValues["Gate"] = 0.0f;
      state.Tick(environment, {0.0f, 0.0f});
      environment.actorValues["Gate"] = static_cast<float>(firing);
      if (!conditional) {
        EventRecord event;
        event.id = "hit";
        event.payload.value = static_cast<float>(firing);
        state.Fire(event, 0.0f);
      }
      state.Tick(environment, {0.0f, 0.0f});
    }
    const auto retained = state.Firings("hit");
    Check(retained.size() == 2,
          "both trigger origins retain only their configured maximum");
    Check(retained.size() == 2 &&
              Near(AsScalar(retained.front().payload.value), 3.0f) &&
              Near(AsScalar(retained.back().payload.value), 4.0f),
          "retention discards oldest firings and preserves payload order");
    Check(Near(state.Scalar("count"), 4.0f),
          "discarding old payloads does not lose counter events");
    state.Tick(environment, {10.0f, 0.0f});
    Check(state.Firings("hit").empty(),
          "retained firings expire at their lifetime boundary");
  }

  {
    std::vector<Signal> signals{
        Signal{"n", NoiseSignal{1.0f, 2.0f, 7}, std::nullopt}};
    const auto graph = SignalGraph::Compile(signals, {});
    SignalState a{graph};
    SignalState b{graph};
    NullEnvironment environment;
    a.Tick(environment, TickInputs{1.25f, 0.0f});
    b.Tick(environment, TickInputs{1.25f, 0.0f});
    Check(Near(a.Scalar("n"), b.Scalar("n")),
          "noise is deterministic for the same time and seed");
  }

  {
    GradientSignal gradient;
    gradient.t = 0.5f;
    gradient.stops = {
        GradientStop{0.0f, std::array<Param, 3>{0.0f, 0.0f, 0.0f}},
        GradientStop{1.0f, std::array<Param, 3>{1.0f, 0.0f, 0.0f}}};
    std::vector<Signal> signals{Signal{"g", gradient, std::nullopt}};
    const auto graph = SignalGraph::Compile(signals, {});
    Check(graph.TypeOf(0) == ValueType::kVec3, "a gradient is a vec3");
    SignalState state{graph};
    NullEnvironment environment;
    state.Tick(environment, TickInputs{0.0f, 0.0f});
    const Vec3 g = state.Vector("g");
    Check(Near(g.x, 0.5f) && Near(g.y, 0.0f),
          "a gradient interpolates between stops");
  }

  {
    std::vector<Signal> signals{
        Signal{"v", ActorValueSignal{"V", Measure::kCurrent}, std::nullopt},
        Signal{"d", RateSignal{Ref{"v"}}, std::nullopt},
        Signal{"s", SmoothSignal{Ref{"v"}, 1.0f}, std::nullopt}};
    const auto graph = SignalGraph::Compile(signals, {});
    SignalState state{graph};
    FakeEnvironment environment;
    environment.actorValues["V"] = 0.0f;
    state.Tick(environment, TickInputs{0.0f, 0.0f});
    Check(Near(state.Scalar("d"), 0.0f), "a rate is zero on the first tick");
    environment.actorValues["V"] = 8.0f;
    state.Tick(environment, TickInputs{1.0f, 1.0f});
    Check(Near(state.Scalar("d"), 8.0f),
          "a rate reports the change per second");
    Check(state.Scalar("s") > 4.0f && state.Scalar("s") < 8.0f,
          "a smooth signal approaches the target without reaching it");
    environment.actorValues["V"] = 12.0f;
    state.Tick(environment, TickInputs{1.5f, 0.5f});
    Check(Near(state.Scalar("d"), 8.0f),
          "a rate divides the change by the tick's seconds");
  }

  {
    Efsh::EffectParams params;
    params.colorScale = 1.0f;
    params.colorKeys[0] = Vec3{1.0f, 0.0f, 0.0f};
    params.fill.fullAlphaRatio = 1.0f;
    params.fill.persistentAlphaRatio = 0.5f;
    params.edgeColor = Vec3{0.0f, 0.0f, 1.0f};
    params.edge.persistentAlphaRatio = 0.3f;
    params.animationSpeedU = 0.5f;

    std::vector<Signal> signals{
        Signal{"alpha", EfshSignal{EfshField::kFillAlpha, FormRef{}},
               std::nullopt},
        Signal{"scroll", EfshSignal{EfshField::kScroll, FormRef{}},
               std::nullopt}};
    const auto graph = SignalGraph::Compile(signals, {});
    SignalState state{graph};
    FakeEnvironment environment;
    environment.effect = params;
    state.Tick(environment, TickInputs{1.0f, 0.0f});
    Check(Near(state.Scalar("alpha"), 0.5f),
          "the efsh fill alpha reads the persistent ratio");
    const Vec2 scroll = AsVec2(state.ValueOf("scroll"));
    Check(Near(scroll.x, 0.5f),
          "the efsh scroll advances with the animation speed");

    SignalState empty{graph};
    NullEnvironment none;
    empty.Tick(none, TickInputs{1.0f, 0.0f});
    Check(Near(empty.Scalar("alpha"), 0.0f),
          "with no effect shader the efsh signal is zero");
  }

  {
    Signal signal = Const("g", Vec3{1.0f, 1.0f, 1.0f});
    signal.curve = CurveRef{"x"};
    std::vector<Signal> signals{signal};
    const auto graph = SignalGraph::Compile(signals, {});
    Check(HasMessage(graph.Diagnostics(), "curve applies only to a scalar"),
          "a curve on a non-scalar signal is an error");
    Check(graph.Inert(0), "the curved non-scalar signal is inert");
  }

  {
    Recipe recipe;
    recipe.sources = {
        Source{"albedo", MaterialSource{MaterialChannel::kDiffuseRgb}},
        Source{"rough", MaterialSource{MaterialChannel::kRoughness}}};
    recipe.masks = {Mask{"tint", "@albedo"}};
    recipe.signals = {Const("glow", 1.0f),
                      Signal{"hit",
                             TriggerSignal{EventOrigin{"hit", {}}, 1.0f, 4},
                             std::nullopt}};
    const auto graph = SignalGraph::Compile(recipe.signals, recipe.curves);
    const RowTypes rows{recipe, graph};

    Check(SignalTypeOf(rows, "glow") == ValueType::kScalar,
          "SignalTypeOf reads a signal's type");
    Check(TexelTypeOf(rows, "albedo") == ValueType::kVec3,
          "TexelTypeOf reads a source's type");
    Check(TexelTypeOf(rows, "rough") == ValueType::kScalar,
          "TexelTypeOf reads a scalar source");
    Check(MaskTypeOf(rows, recipe.masks[0]) == ValueType::kVec3,
          "MaskTypeOf resolves through a source");
    Check(NamesTrigger(rows, "hit"), "NamesTrigger accepts a trigger signal");
    Check(!NamesTrigger(rows, "glow"), "NamesTrigger rejects a non-trigger");
  }

  {
    Recipe recipe;
    const auto graph = SignalGraph::Compile(recipe.signals, recipe.curves);
    const RowTypes rows{recipe, graph};

    Source bad{"k", MaterialClustersSource{}};
    Get<MaterialClustersSource>(bad.kind)->settings.clusters = 0;
    Check(HasMessage(CheckSource(rows, bad), "clusters"),
          "CheckSource rejects a zero cluster count");

    Source clean{"k2", MaterialClustersSource{}};
    Check(CheckSource(rows, clean).empty(),
          "CheckSource accepts a valid clusters source");
  }

  {
    Recipe recipe;
    recipe.sources = {
        Source{"albedo", MaterialSource{MaterialChannel::kDiffuseRgb}}};
    const auto graph = SignalGraph::Compile(recipe.signals, recipe.curves);
    const RowTypes rows{recipe, graph};

    Layer unknown;
    unknown.source = Ref{"missing"};
    Check(HasMessage(
              CheckLayer(rows, unknown, Slot::kDiffuse, "output 0 layer 0"),
              "unknown source or mask"),
          "CheckLayer rejects an unknown source");

    Layer normalBlend;
    normalBlend.source = Ref{"albedo"};
    normalBlend.blend = Blend::kReorient;
    Check(HasMessage(CheckLayer(rows, normalBlend, Slot::kDiffuse, "w"),
                     "normal stack"),
          "CheckLayer rejects the normal blend off the normal slot");
    Check(CheckLayer(rows, normalBlend, Slot::kNormal, "w").empty(),
          "CheckLayer accepts the normal blend on the normal slot");
  }

  {
    Recipe recipe;
    const auto graph = SignalGraph::Compile(recipe.signals, recipe.curves);
    const RowTypes rows{recipe, graph};

    SurfaceOutput emissive;
    emissive.surface = Surface::kMaterial;
    emissive.slot = Slot::kEmissive;
    Check(
        HasMessage(CheckOutput(rows, emissive, "output 0"), "needs 'strength'"),
        "CheckOutput demands a required scalar");

    emissive.scalars.strength = 1.0f;
    Check(CheckOutput(rows, emissive, "output 0").empty(),
          "CheckOutput passes once the scalar is present");
  }

  {
    Recipe recipe;
    recipe.masks = {Mask{"self", "@self"}};
    const auto graph = SignalGraph::Compile(recipe.signals, recipe.curves);
    const RowTypes rows{recipe, graph};
    Check(HasMessage(CheckMask(rows, recipe.masks[0]), "reads itself"),
          "CheckMask catches a self-referential mask");
    Check(!MaskTypeOf(rows, recipe.masks[0]).has_value(),
          "a self-referential mask resolves to no type, bounded");
  }

  {
    Recipe recipe;
    for (std::size_t i = 0; i < 39; ++i) {
      recipe.masks.push_back(
          Mask{"m" + std::to_string(i), "@m" + std::to_string(i + 1)});
    }
    recipe.masks.push_back(Mask{"m39", "1.0"});
    const auto graph = SignalGraph::Compile(recipe.signals, recipe.curves);
    const RowTypes rows{recipe, graph};
    Check(!MaskTypeOf(rows, recipe.masks[0]).has_value(),
          "a mask chain past the depth bound resolves to no type, never "
          "overflows");
  }

  {
    Recipe recipe;
    recipe.signals = {Const("only", 1.0f)};
    const auto graph = SignalGraph::Compile(recipe.signals, recipe.curves);
    Check(graph.At(0) != nullptr && graph.At(0)->name == "only",
          "At returns the signal at a valid index");
    Check(graph.At(1) == nullptr, "At past the end is null, not a read");
    Check(graph.At(std::numeric_limits<std::size_t>::max()) == nullptr,
          "At at the largest index is null");
    Check(graph.TypeOf(0) == ValueType::kScalar,
          "TypeOf by index reports the inferred type");
    Check(!graph.TypeOf(1).has_value(), "TypeOf past the end has no value");
    Check(!graph.Inert(0), "a healthy signal is live");
    Check(graph.Inert(1), "an index past the end reads as inert");
    SignalState state{graph};
    state.Fire(EventRecord{"hit.received", {}}, 0.0f);
    Check(state.Firings("only").empty(),
          "firing an event over a graph with no trigger records nothing");
  }

  {
    const std::optional<SignalKind> av = DefaultSignalKind("av");
    const ActorValueSignal *avSignal =
        av ? Get<ActorValueSignal>(*av) : nullptr;
    Check(avSignal != nullptr && avSignal->actorValue == "Health",
          "a new actor-value signal defaults to a usable actor value");
    const std::optional<SignalKind> expr = DefaultSignalKind("expr");
    const ExprSignal *exprSignal = expr ? Get<ExprSignal>(*expr) : nullptr;
    Check(exprSignal != nullptr && !exprSignal->text.empty(),
          "a new expression signal defaults to a usable expression");
  }

  {
    std::vector<Signal> signals{Signal{"a", ActorValueSignal{}, std::nullopt},
                                Signal{"d", RateSignal{}, std::nullopt}};
    const auto graph = SignalGraph::Compile(signals, {});
    Check(HasMessage(graph.Diagnostics(), "names an actor value"),
          "an incomplete actor-value signal is flagged, not silently accepted");
    Check(HasMessage(graph.Diagnostics(), "reads a signal"),
          "an incomplete rate signal is flagged");
    Check(graph.Inert(0) && graph.Inert(1),
          "incomplete signals are inert rather than a crash");
  }

  {
    std::vector<Signal> signals{
        Signal{"w", TriggerSignal{WhenOrigin{}}, std::nullopt}};
    const auto graph = SignalGraph::Compile(signals, {});
    Check(HasMessage(graph.Diagnostics(), "when trigger names a signal"),
          "a when trigger with no signal is flagged");
    Check(graph.Inert(0), "an incomplete when trigger is inert");
  }

  {
    std::vector<Signal> signals{
        Signal{"pos", ActorStateSignal{ActorStateKind::kPosition},
               std::nullopt},
        Signal{"root", ToRootSignal{Ref{"pos"}}, std::nullopt}};
    const auto graph = SignalGraph::Compile(signals, {});
    Check(graph.Diagnostics().empty(), "position and toRoot compile clean");
    Check(graph.TypeOf("pos") == ValueType::kVec3,
          "actorState position is a vec3");
    Check(graph.TypeOf("root") == ValueType::kVec3, "toRoot is a vec3");

    SignalState state{graph};
    FakeEnvironment environment;
    environment.vector = Vec3{100.0f, 200.0f, 300.0f};
    environment.rootOrigin = Vec3{10.0f, 20.0f, 30.0f};
    state.Tick(environment, TickInputs{0.0f, 0.0f});
    const Vec3 pos = state.Vector("pos");
    Check(Near(pos.x, 100.0f) && Near(pos.y, 200.0f) && Near(pos.z, 300.0f),
          "position reads the wearer's world position");
    const Vec3 root = state.Vector("root");
    Check(Near(root.x, 90.0f) && Near(root.y, 180.0f) && Near(root.z, 270.0f),
          "toRoot converts the world position into root space");
  }

  {
    std::vector<Signal> signals{Signal{
        "swim", ActorStateSignal{ActorStateKind::kSwimming}, std::nullopt}};
    const auto graph = SignalGraph::Compile(signals, {});
    Check(graph.TypeOf("swim") == ValueType::kScalar,
          "a boolean actorState stays scalar");
    SignalState state{graph};
    FakeEnvironment environment;
    environment.state = 1.0f;
    state.Tick(environment, TickInputs{0.0f, 0.0f});
    Check(Near(state.Scalar("swim"), 1.0f), "swimming reads the state flag");
  }

  {
    std::vector<Signal> signals{
        Signal{"target", ActorStateSignal{ActorStateKind::kTarget},
               std::nullopt},
        Signal{"has", ActorStateSignal{ActorStateKind::kHasTarget},
               std::nullopt},
        Expr("range", "@has * distance(@target, [0, 0, 0])")};
    const auto graph = SignalGraph::Compile(signals, {});
    Check(graph.Diagnostics().empty(),
          "target, hasTarget and distance compile clean");
    Check(graph.TypeOf("target") == ValueType::kVec3,
          "actorState target is a vec3");
    Check(graph.TypeOf("has") == ValueType::kScalar,
          "actorState hasTarget is a scalar");
    SignalState state{graph};
    FakeEnvironment environment;
    environment.vector = Vec3{30.0f, 40.0f, 0.0f};
    environment.state = 1.0f;
    state.Tick(environment, TickInputs{0.0f, 0.0f});
    Check(Near(state.Scalar("range"), 50.0f),
          "distance reduces the target position to a scalar range");
  }

  return test::Finish("signals");
}
