#include "studio/FieldCheck.h"
#include "studio/Fields.h"
#include "studio/Forms.h"

#include "test_support.h"

#include <algorithm>
#include <span>
#include <string_view>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
[[nodiscard]] const FormField *Find(std::span<const FormField> a_fields,
                                    std::string_view a_name) {
  const auto found = std::ranges::find(a_fields, a_name, &FormField::name);
  return found == a_fields.end() ? nullptr : &*found;
}
}

int main() {
  FormField field = ValueField({.name = "opacity",
                                .kind = FieldKind::kScalar,
                                .text = "0.5",
                                .names = {"strength"},
                                .bind = BindLayerOpacity(0, 0),
                                .workingRange = std::pair{0.0f, 1.0f},
                                .units = "fraction"});
  Check(!CheckField(field, "2", {}).has_value() && field.bind("2").has_value(),
        "working range does not constrain exact authoring input");
  Check(!CheckField(field, "@strength", {}).has_value(),
        "numeric metadata preserves compatible driven values");
  field.range = std::pair{0.0f, 1.0f};
  Check(CheckField(field, "2", {}).has_value(),
        "hard range still rejects out-of-range literals");

  SignalRow wave;
  wave.name = "wave";
  wave.kind = SignalKindId::kWave;
  wave.definition = WaveSignal{};
  const auto waveFields = SignalForm(wave, {});
  const FormField *phase = Find(waveFields, "phase");
  Check(phase && phase->workingRange && !phase->range &&
            phase->units == "cycles" &&
            !CheckField(*phase, "2.5", {}).has_value(),
        "phase offers a cycle without rejecting additional cycles");
  const FormField *period = Find(waveFields, "period");
  Check(period && period->units == "seconds" && !period->workingRange,
        "duration exposes units without inventing a maximum");
  const FormField *amplitude = Find(waveFields, "amplitude");
  Check(amplitude && !amplitude->workingRange && !amplitude->range,
        "arbitrary signal amplitude retains an unknown range");

  SignalRow trigger;
  trigger.name = "hit";
  trigger.kind = SignalKindId::kTrigger;
  trigger.definition = TriggerSignal{};
  const auto triggerFields = SignalForm(trigger, {});
  const FormField *maximum = Find(triggerFields, "max");
  Check(maximum && maximum->integral &&
            maximum->workingRange == maximum->range &&
            CheckField(*maximum, "1.5", {}).has_value() &&
            CheckField(*maximum, "65", {}).has_value() &&
            !CheckField(*maximum, "64", {}).has_value(),
        "event count retains hard bounds and rejects fractional input");
  return test::failures == 0 ? 0 : 1;
}
