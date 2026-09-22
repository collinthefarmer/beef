#include "studio/ResponseGraph.h"

#include "recipe/Expression.h"
#include "recipe/Signals.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
namespace {
[[nodiscard]] bool ReadsTheGame(SignalKindId a_kind) noexcept {
  return a_kind == SignalKindId::kEfsh || a_kind == SignalKindId::kActorValue ||
         a_kind == SignalKindId::kActorState ||
         a_kind == SignalKindId::kEnchantment;
}

[[nodiscard]] std::expected<float, std::string>
ResolveNumber(const Param &a_param, const RecipeRow &a_recipe) {
  if (const float *value = Get<float>(a_param)) {
    if (std::isfinite(*value)) {
      return *value;
    }
    return std::unexpected("an input is not a finite number");
  }
  const Ref *ref = Get<Ref>(a_param);
  if (!ref) {
    return std::unexpected("an input could not be read");
  }
  const SignalRow *found = FindByName(a_recipe.signals, ref->name);
  if (!found) {
    return std::unexpected(
        std::format("'@{}' is not a signal of this recipe", ref->name));
  }
  if (found->inert) {
    return std::unexpected(std::format("'@{}' is inert", ref->name));
  }
  if (!found->live && !found->constant) {
    if (ReadsTheGame(found->kind)) {
      return std::unexpected(std::format(
          "'@{}' ({}) reads the game, so it has no value here; it reads a "
          "live value in game on a tracked wearer",
          ref->name, SignalKindName(found->kind)));
    }
    return std::unexpected(
        std::format("'@{}' has no value to hold", ref->name));
  }
  const Value &value = found->live ? found->value : *found->constant;
  const float *number = Get<float>(value);
  if (number && std::isfinite(*number)) {
    return *number;
  }
  return std::unexpected(
      std::format("'@{}' does not hold a finite scalar", ref->name));
}

[[nodiscard]] std::optional<std::string> Hold(Param &a_param,
                                              const RecipeRow &a_recipe) {
  const auto number = ResolveNumber(a_param, a_recipe);
  if (!number) {
    return number.error();
  }
  a_param = *number;
  return std::nullopt;
}

[[nodiscard]] std::expected<float, std::string>
HoldDefinition(SignalKind &a_definition, const RecipeRow &a_recipe) {
  if (WaveSignal *wave = Get<WaveSignal>(a_definition)) {
    for (Param *param :
         {&wave->base, &wave->amplitude, &wave->phase, &wave->period}) {
      if (const auto problem = Hold(*param, a_recipe)) {
        return std::unexpected(*problem);
      }
    }
    return ResolveNumber(wave->period, a_recipe);
  }
  if (RampSignal *ramp = Get<RampSignal>(a_definition)) {
    for (Param *param : {&ramp->from, &ramp->to, &ramp->seconds}) {
      if (const auto problem = Hold(*param, a_recipe)) {
        return std::unexpected(*problem);
      }
    }
    return ResolveNumber(ramp->seconds, a_recipe);
  }
  if (TriggerSignal *trigger = Get<TriggerSignal>(a_definition)) {
    trigger->origin = EventOrigin{"studio.response", {}};
    trigger->max = 1;
    if (const auto problem = Hold(trigger->lifetime, a_recipe)) {
      return std::unexpected(*problem);
    }
    return ResolveNumber(trigger->lifetime, a_recipe);
  }
  return std::unexpected(
      "A response graph is available for wave, ramp, and trigger signals.");
}

[[nodiscard]] std::expected<std::string, std::string>
SimpleCurve(const std::string &a_text, const RecipeRow &a_recipe) {
  if (a_text.empty()) {
    return std::string{"x"};
  }
  std::string expression = a_text;
  if (const auto name = CurveRef{a_text}.Named()) {
    const TextRow *found = FindByName(a_recipe.curves, *name);
    if (!found) {
      return std::unexpected("The selected response curve is missing.");
    }
    expression = found->text;
  }
  const auto program = Program::Parse(expression);
  if (!program || !program->References().empty() ||
      !program->Curves().empty() || program->UsesTime() ||
      program->UsesMean()) {
    return std::unexpected("This response curve needs additional live inputs; "
                           "use its expression controls.");
  }
  return expression;
}
}

std::expected<ResponseGraph, std::string>
BuildResponseGraph(const SignalRow &a_signal, const RecipeRow &a_recipe) {
  Signal signal{a_signal.name, a_signal.definition, std::nullopt};
  const auto duration = HoldDefinition(signal.kind, a_recipe);
  if (!duration) {
    return std::unexpected(duration.error());
  }
  if (*duration <= 0.0f) {
    return std::unexpected("The response needs a positive duration.");
  }
  const auto curve = SimpleCurve(a_signal.curve, a_recipe);
  if (!curve) {
    return std::unexpected(curve.error());
  }
  signal.curve = CurveRef{*curve};
  const SignalGraph graph = SignalGraph::Compile(std::span{&signal, 1}, {});
  const auto index = graph.Index(signal.name);
  if (!index || graph.Inert(*index) ||
      graph.TypeOf(*index) != ValueType::kScalar) {
    return std::unexpected(
        "This signal cannot provide a scalar response graph.");
  }
  SignalState state{graph};
  const NullEnvironment environment;
  state.Fire({"studio.response", {}}, 0);
  ResponseGraph response;
  response.seconds = *duration;
  response.caption =
      "Elapsed seconds; referenced inputs held at their displayed values.";
  if (a_signal.live && !a_signal.inert) {
    if (const float *live = Get<float>(a_signal.value);
        live && std::isfinite(*live)) {
      response.live = *live;
    }
  }
  const float step =
      response.seconds / static_cast<float>(response.values.size() - 1);
  for (std::size_t i = 0; i < response.values.size(); ++i) {
    state.Tick(environment,
               {static_cast<float>(i) * step, i == 0 ? 0.0f : step});
    const float value = state.Scalar(signal.name);
    if (!std::isfinite(value)) {
      return std::unexpected("The response contains a non-finite value.");
    }
    response.values[i] = value;
  }
  return response;
}
}
