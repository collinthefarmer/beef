// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/Signals.h"
#include "test_support.h"

#include <algorithm>
#include <initializer_list>
#include <string_view>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
RecipeGraph CompileSignals(std::span<const Signal> signals) {
  Recipe recipe;
  recipe.signals.assign(signals.begin(), signals.end());
  return RecipeGraph::Compile(recipe);
}

std::vector<Signal> Inputs() {
  return {{"scalar", ConstantSignal{1.0f}, std::nullopt},
          {"vector", ConstantSignal{Vec3{1.0f, 0.5f, 0.0f}}, std::nullopt},
          {"hit", TriggerSignal{}, std::nullopt}};
}

void Rejects(SignalKind kind,
             std::initializer_list<std::string_view> messages) {
  auto signals = Inputs();
  signals.push_back({"bad", std::move(kind), std::nullopt});
  signals.push_back({"dependent", ExprSignal{"@bad + 1"}, std::nullopt});
  const auto graph = CompileSignals(signals);
  Check(graph.IsDisabled(3),
        "a signal with an incompatible reference becomes inert");
  Check(graph.IsDisabled(4), "inert reference state propagates to dependents");
  for (const auto message : messages) {
    Check(std::ranges::any_of(graph.Diagnostics(),
                              [&](const Diagnostic &diagnostic) {
                                return diagnostic.severity ==
                                           Severity::kError &&
                                       diagnostic.where == "signal bad" &&
                                       diagnostic.message == message;
                              }),
          std::string{message});
  }
}
}

int main() {
  WaveSignal wave;
  wave.period = Ref{"vector"};
  Rejects(wave, {"'period' must be a scalar; '@vector' is a vec3"});

  TriggerSignal conditional;
  conditional.origin = WhenOrigin{Ref{"vector"}, std::nullopt};
  Rejects(conditional, {"'when' must be a scalar; '@vector' is a vec3"});

  GradientSignal gradient;
  gradient.stops = {{0.0f, Ref{"scalar"}}};
  Rejects(gradient, {"a stop colour must be a vec3; '@scalar' is a scalar"});

  Rejects(CounterSignal{Ref{"hit"}, Ref{"scalar"}, Param{Ref{"vector"}}},
          {"'@scalar' must be a trigger",
           "'cap' must be a scalar; '@vector' is a vec3"});
  Rejects(PayloadSignal{Ref{"scalar"}},
          {"'trigger' must name a trigger; '@scalar' is not one"});
  Rejects(AccumulateSignal{Ref{"hit"}, Ref{"vector"}},
          {"'decay' must be a scalar; '@vector' is a vec3"});

  auto signals = Inputs();
  wave.period = Ref{"scalar"};
  conditional.origin = WhenOrigin{Ref{"scalar"}, std::nullopt};
  gradient.stops = {{0.0f, Ref{"vector"}}};
  signals.push_back({"wave", wave, std::nullopt});
  signals.push_back({"conditional", conditional, std::nullopt});
  signals.push_back({"gradient", gradient, std::nullopt});
  signals.push_back(
      {"counter", CounterSignal{Ref{"hit"}, Ref{"hit"}, Param{Ref{"scalar"}}},
       std::nullopt});
  signals.push_back({"payload", PayloadSignal{Ref{"hit"}}, std::nullopt});
  signals.push_back({"accumulate", AccumulateSignal{Ref{"hit"}, Ref{"scalar"}},
                     std::nullopt});
  const auto graph = CompileSignals(signals);
  Check(graph.Diagnostics().empty(),
        "compatible references produce no diagnostics");
  for (std::size_t i = 0; i < signals.size(); ++i) {
    Check(!graph.IsDisabled(i),
          "compatible signal remains active: " + signals[i].name);
  }
  return test::Finish("reference types");
}
