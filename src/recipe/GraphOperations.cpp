// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/GraphOperations.h"

namespace BetterEnchantmentEffects {
std::vector<OutputRef> InputsOf(const NodeKind &kind) {
  std::vector<OutputRef> inputs;
  const auto add = [&](OutputRef input) { inputs.push_back(input); };
  const auto optional = [&](const std::optional<OutputRef> &input) {
    if (input)
      add(*input);
  };
  Match(
      kind, [](const ConstantOperation &) {}, [](const ExternalInput &) {},
      [](const ParameterOperation &) {},
      [&](const VectorOperation &k) { inputs = k.components; },
      [&](const ExpressionOperation &k) {
        inputs = k.expression.valueBindings;
        for (const auto &function : k.expression.functionBindings)
          for (const auto &argument : function.arguments)
            add(argument.value);
      },
      [&](const MapFunctionOperation &k) { inputs = k.arguments; },
      [&](const CallOperation &k) { inputs = k.arguments; },
      [&](const WaveOperation &k) {
        inputs = {k.base, k.amplitude, k.period, k.phase, k.deltaTime};
      },
      [&](const RampOperation &k) {
        inputs = {k.from, k.to, k.seconds, k.time};
      },
      [&](const EffectShaderOperation &k) { inputs = {k.record, k.time}; },
      [&](const NoiseOperation &k) {
        inputs = {k.frequency, k.amplitude, k.time};
      },
      [&](const GradientOperation &k) {
        add(k.position);
        for (const auto &s : k.stops)
          add(s.color);
      },
      [&](const ToRootOperation &k) { inputs = {k.value, k.transform}; },
      [&](const TriggerOperation &k) {
        inputs = {k.time, k.lifetime};
        Match(
            k.origin, [&](const EventTriggerInput &s) { add(s.events); },
            [&](const ConditionTriggerInput &s) {
              add(s.condition);
              optional(s.payload);
            });
      },
      [&](const HoldOperation &k) { add(k.firings); },
      [&](const CounterOperation &k) {
        add(k.count);
        optional(k.reset);
        optional(k.cap);
      },
      [&](const AccumulateOperation &k) {
        inputs = {k.count, k.decay, k.deltaTime};
      },
      [&](const RateOperation &k) { inputs = {k.value, k.deltaTime}; },
      [&](const SmoothOperation &k) {
        inputs = {k.value, k.seconds, k.deltaTime};
      },
      [&](const TextureCoordinatesOperation &k) {
        add(k.uv);
        optional(k.scroll);
        optional(k.tile);
      },
      [&](const ImageOperation &k) { inputs = {k.texture, k.coordinates}; },
      [&](const MaterialOperation &k) { inputs = {k.material, k.coordinates}; },
      [&](const BakeOperation &k) { inputs = {k.geometry, k.coordinates}; },
      [&](const DistanceOperation &k) {
        inputs = {k.geometry, k.coordinates, k.origin};
      },
      [&](const RippleOperation &k) {
        inputs = {k.firings, k.geometry, k.coordinates, k.transform, k.time,
                  k.speed,   k.width,    k.decay,       k.direction};
      },
      [&](const MaterialClustersOperation &k) {
        inputs = {k.material, k.geometry, k.coordinates};
      },
      [&](const ReductionOperation &k) { add(k.value); });
  return inputs;
}
bool Stateful(const NodeKind &kind) {
  return Is<WaveOperation>(kind) || Is<TriggerOperation>(kind) ||
         Is<HoldOperation>(kind) || Is<CounterOperation>(kind) ||
         Is<AccumulateOperation>(kind) || Is<RateOperation>(kind) ||
         Is<SmoothOperation>(kind);
}
}
