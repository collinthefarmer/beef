#include "studio/ResponseGraph.h"

#include "test_support.h"

#include <cmath>
#include <limits>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;
using test::Near;

int main() {
  RecipeRow recipe;
  SignalRow signal;
  signal.name = "response";
  signal.kind = SignalKindId::kRamp;
  signal.definition = RampSignal{2.0f, 6.0f, 4.0f};
  const auto ramp = BuildResponseGraph(signal, recipe);
  Check(ramp && Near(ramp->seconds, 4) && Near(ramp->values.front(), 2) &&
            Near(ramp->values.back(), 6),
        "ramp plot uses authored duration and evaluator endpoints");
  Check(ramp && !ramp->live,
        "unmatched document does not fabricate a live marker");
  signal.live = true;
  signal.value = 3.5f;
  signal.curve = "x * 2";
  const auto curve = BuildResponseGraph(signal, recipe);
  Check(curve && Near(curve->values.front(), 4) &&
            Near(curve->values.back(), 12) && curve->live &&
            Near(*curve->live, 3.5f),
        "simple authored response curve uses existing expression evaluation");
  signal.curve = "@missing(x)";
  Check(!BuildResponseGraph(signal, recipe),
        "dependent missing curve retains text-only editing");
  signal.curve.clear();
  signal.kind = SignalKindId::kPulse;
  signal.definition = PulseSignal{0.1f, 0.8f, 2.0f, 0.0f, Waveform::kSine};
  const auto pulse = BuildResponseGraph(signal, recipe);
  Check(pulse && Near(pulse->seconds, 2) && Near(pulse->values.front(), 0.1f) &&
            Near(pulse->values[32], 0.9f),
        "pulse plot samples the actual waveform over its authored period");
  signal.definition =
      PulseSignal{0.0f, 1.0f, Ref{"period"}, 0.0f, Waveform::kSine};
  Check(!BuildResponseGraph(signal, recipe),
        "missing duration input produces no guessed graph");
  SignalRow period;
  period.name = "period";
  period.constant = 3.0f;
  recipe.signals.push_back(period);
  const auto held = BuildResponseGraph(signal, recipe);
  Check(held && Near(held->seconds, 3),
        "constant referenced duration works without a live actor");
  recipe.signals.front().constant.reset();
  recipe.signals.front().value = 8.0f;
  Check(!BuildResponseGraph(signal, recipe),
        "unavailable reference does not use default displayed value");
  recipe.signals.front().live = true;
  recipe.signals.front().inert = true;
  Check(!BuildResponseGraph(signal, recipe),
        "inert reference cannot masquerade as a usable live input");
  recipe.signals.front().inert = false;
  recipe.signals.front().value = std::numeric_limits<float>::infinity();
  Check(!BuildResponseGraph(signal, recipe),
        "non-finite live input is rejected");
  signal.kind = SignalKindId::kTrigger;
  TriggerSignal trigger;
  trigger.lifetime = 2.0f;
  signal.definition = trigger;
  const auto event = BuildResponseGraph(signal, recipe);
  Check(event && Near(event->values.front(), 0) &&
            Near(event->values.back(), 1),
        "trigger plot shows progress from event start to expiry");
  signal.definition = ExprSignal{"time * 2"};
  Check(!BuildResponseGraph(signal, recipe),
        "arbitrary expressions keep their existing controls");
  return test::Finish("studio_responsegraph");
}
