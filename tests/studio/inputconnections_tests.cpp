#include "recipe/Signals.h"
#include "studio/Fields.h"
#include "studio/InputCatalog.h"
#include "studio/InputConnections.h"

#include "test_support.h"

#include <filesystem>
#include <string>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;
using test::Near;

namespace {
class Resources final : public SignalEnvironment {
public:
  float current = 50;
  float maximum = 100;
  float ActorValue(std::string_view, Measure a_measure) const override {
    return a_measure == Measure::kMax ? maximum : current;
  }
  float ActorState(ActorStateKind) const override { return 0; }
  float Enchantment(EnchantmentField) const override { return 0; }
  std::optional<Efsh::EffectParams>
  EffectShader(const FormRef &) const override {
    return std::nullopt;
  }
};

[[nodiscard]] Recipe SampleRecipe() {
  const auto path = test::Fixtures().parent_path().parent_path() / "schema" /
                    "example-magicka.json";
  const auto parsed = ParseRecipe(test::ReadFile(path), "input");
  Check(parsed.recipe.has_value(), "input fixture parses");
  return parsed.recipe.value_or(Recipe{});
}

[[nodiscard]] FormField Opacity() {
  return ValueField({.name = "opacity",
                     .kind = FieldKind::kScalar,
                     .text = "1",
                     .names = {},
                     .bind = BindLayerOpacity(0, 0)});
}

void CheckConnections(const Recipe &a_base) {
  for (const InputConnectionKind kind :
       {InputConnectionKind::kFraction, InputConnectionKind::kExhausted,
        InputConnectionKind::kHitResponse}) {
    const auto edits =
        ConnectInput(Opacity(), {}, {kind, "Stamina", Measure::kCurrent});
    Check(edits.has_value(), "connection produces one atomic edit batch");
    if (!edits) {
      continue;
    }
    const auto prepared = PrepareEdits(a_base, *edits);
    Check(prepared.has_value(),
          "helper batch validates against the existing recipe format");
    if (!prepared || edits->edits.empty()) {
      continue;
    }
    const auto *binding = Get<SetLayerOpacity>(edits->edits.back());
    const auto *ref = binding ? Get<Ref>(binding->opacity) : nullptr;
    Check(ref != nullptr, "helper binds the destination to its output signal");
    if (!ref) {
      continue;
    }
    const SignalGraph graph =
        SignalGraph::Compile(prepared->signals, prepared->curves);
    SignalState state{graph};
    Resources resources;
    state.Tick(resources, {0, 0});
    if (kind == InputConnectionKind::kFraction) {
      Check(Near(state.Scalar(ref->name), 0.5f),
            "half stamina produces one-half fraction");
      resources.maximum = 0;
      state.Tick(resources, {0.1f, 0.1f});
      Check(Near(state.Scalar(ref->name), 0),
            "zero maximum produces finite zero");
      resources.maximum = -10;
      state.Tick(resources, {0.2f, 0.1f});
      Check(Near(state.Scalar(ref->name), 0),
            "negative maximum produces finite zero");
      resources.maximum = 25;
      state.Tick(resources, {0.3f, 0.1f});
      Check(Near(state.Scalar(ref->name), 2),
            "fraction does not invent a hard upper limit");
    } else if (kind == InputConnectionKind::kExhausted) {
      Check(Near(state.Scalar(ref->name), 0),
            "remaining stamina keeps exhaustion off");
      resources.current = 0;
      state.Tick(resources, {0.1f, 0.1f});
      Check(Near(state.Scalar(ref->name), 1),
            "empty stamina turns exhaustion on");
      state.Tick(resources, {5, 4.9f});
      Check(Near(state.Scalar(ref->name), 1),
            "exhaustion remains a sustained condition");
      resources.current = 0.01f;
      state.Tick(resources, {5.1f, 0.1f});
      Check(Near(state.Scalar(ref->name), 0),
            "recovered stamina releases exhaustion");
    } else {
      Check(Near(state.Scalar(ref->name), 0), "hit glow is off before any hit");
      state.Fire({"hit.received", {}}, 0);
      state.Tick(resources, {0, 0});
      Check(Near(state.Scalar(ref->name), 1),
            "received hit begins maximum glow");
      state.Tick(resources, {0.5f, 0.5f});
      Check(Near(state.Scalar(ref->name), 0.5f),
            "hit response fades over its lifetime");
      state.Tick(resources, {1, 0.5f});
      Check(Near(state.Scalar(ref->name), 0),
            "expired hit response returns to zero");
    }
  }
}
}

int main() {
  const Recipe base = SampleRecipe();
  CheckConnections(base);
  Names names;
  names.signals.emplace_back("av_stamina_current", ValueType::kScalar);
  names.sources.emplace_back("av_stamina_current2", ValueType::kScalar);
  names.masks.emplace_back("av_stamina_current3");
  names.curves.emplace_back("av_stamina_current4");
  const auto connection = ConnectInput(
      Opacity(), names,
      {InputConnectionKind::kMeasure, "Stamina", Measure::kCurrent});
  Check(connection && !connection->edits.empty() &&
            Get<AddSignal>(connection->edits.front()) &&
            Get<AddSignal>(connection->edits.front())->name ==
                "av_stamina_current5",
        "input creation avoids signal, source, mask and curve names");
  const auto created = CreateInput(
      names, {InputConnectionKind::kFraction, "Stamina", Measure::kCurrent});
  const auto bound = ConnectInput(
      Opacity(), names,
      {InputConnectionKind::kFraction, "Stamina", Measure::kCurrent});
  Check(created && bound && !created->edits.empty() &&
            bound->edits.size() == created->edits.size() + 1,
        "create-only input builds the same signals, minus the field binding");
  if (created) {
    bool binds = false;
    for (const RecipeEdit &edit : created->edits) {
      if (Get<SetLayerOpacity>(edit)) {
        binds = true;
      }
    }
    Check(!binds, "create-only input never binds a destination field");
  }
  Check(
      !CreateInput({}, {InputConnectionKind::kFraction, "", Measure::kCurrent}),
      "create-only input refuses a missing actor value");
  FormField count = Opacity();
  count.bind = [](const std::string &) -> std::optional<RecipeEdit> {
    return std::nullopt;
  };
  Check(!ConnectInput(
            count, {},
            {InputConnectionKind::kFraction, "Stamina", Measure::kCurrent}),
        "literal-only destination refuses the whole helper operation");
  Check(!ConnectInput(Opacity(), {},
                      {InputConnectionKind::kMeasure, "Stamina",
                       static_cast<Measure>(99)}),
        "unsupported measure is refused before creating any signals");
  Check(!ConnectInput(Opacity(), {},
                      {InputConnectionKind::kFraction, "", Measure::kCurrent}),
        "missing actor input is refused before creating any signals");
  const ActorValueHelp stamina = ActorValueHelpOf("Stamina");
  Check(stamina.units == "points", "known actor-value units are surfaced");
  Check(stamina.description.find("sprinting") != std::string::npos,
        "actor-value help carries meaning, not just identity");
  Check(SampleAt(ActorValueSample{}, Measure::kCurrent) == std::nullopt,
        "missing actor samples remain unavailable rather than fabricated zero");
  Check(ActorValueHelpOf("CustomUnknown").units.empty(),
        "unknown actor-value units are not invented");
  return test::Finish("studio_inputconnections");
}
