#include "studio/ResponseGraph.h"

#include "recipe/Expression.h"
#include "recipe/Signals.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
namespace {
[[nodiscard]] std::optional<float> Number(const Param &a_param,
                                          const RecipeRow &a_recipe) {
  if (const float *value = Get<float>(a_param)) {
    return std::isfinite(*value) ? std::optional{*value} : std::nullopt;
  }
  const Ref *ref = Get<Ref>(a_param);
  if (!ref) {
    return std::nullopt;
  }
  const auto found =
      std::ranges::find(a_recipe.signals, ref->name, &SignalRow::name);
  if (found == a_recipe.signals.end() || found->inert ||
      (!found->live && !found->constant)) {
    return std::nullopt;
  }
  const Value &value = found->live ? found->value : *found->constant;
  const float *number = Get<float>(value);
  return number && std::isfinite(*number) ? std::optional{*number}
                                          : std::nullopt;
}

[[nodiscard]] bool Hold(Param &a_param, const RecipeRow &a_recipe) {
  const auto number = Number(a_param, a_recipe);
  if (!number) {
    return false;
  }
  a_param = *number;
  return true;
}

[[nodiscard]] std::optional<float> HoldDefinition(SignalKind &a_definition,
                                                  const RecipeRow &a_recipe) {
  if (PulseSignal *pulse = Get<PulseSignal>(a_definition)) {
    if (Hold(pulse->base, a_recipe) && Hold(pulse->amplitude, a_recipe) &&
        Hold(pulse->phase, a_recipe) && Hold(pulse->period, a_recipe)) {
      return Number(pulse->period, a_recipe);
    }
  } else if (RampSignal *ramp = Get<RampSignal>(a_definition)) {
    if (Hold(ramp->from, a_recipe) && Hold(ramp->to, a_recipe) &&
        Hold(ramp->seconds, a_recipe)) {
      return Number(ramp->seconds, a_recipe);
    }
  } else if (TriggerSignal *trigger = Get<TriggerSignal>(a_definition)) {
    trigger->origin = EventOrigin{"studio.response", {}, {}};
    trigger->max = 1;
    if (Hold(trigger->lifetime, a_recipe)) {
      return Number(trigger->lifetime, a_recipe);
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::expected<std::string, std::string>
SimpleCurve(const std::string &a_text, const RecipeRow &a_recipe) {
  if (a_text.empty()) {
    return std::string{"x"};
  }
  std::string expression = a_text;
  if (const auto name = CurveRef{a_text}.Named()) {
    const auto found =
        std::ranges::find(a_recipe.curves, *name, &TextRow::name);
    if (found == a_recipe.curves.end()) {
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
  if (!duration || *duration <= 0.0f) {
    return std::unexpected(
        "A response graph is available for pulse, ramp, and trigger signals "
        "with a positive duration and available inputs.");
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
