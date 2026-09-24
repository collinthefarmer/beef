// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/InputCatalog.h"

#include "engine/Environment.h"

#include <cmath>
#include <utility>

namespace BetterEnchantmentEffects {
std::vector<Studio::ActorValueSample>
BuildActorValueSamples(const Studio::GameObjectCatalog &a_catalog,
                       std::uint32_t a_actor) {
  const RE::NiPointer<RE::Actor> actor{
      RE::TESForm::LookupByID<RE::Actor>(a_actor)};
  std::vector<Studio::ActorValueSample> samples;
  if (!actor || !actor->AsActorValueOwner()) {
    return samples;
  }
  const ActorEnvironment environment{actor.get(), nullptr};
  samples.reserve(a_catalog.candidates.size());
  for (const Studio::GameObjectCandidate &candidate : a_catalog.candidates) {
    Studio::ActorValueSample sample;
    sample.name = candidate.value;
    for (std::size_t measure = 0; measure < Studio::kInputMeasures.size();
         ++measure) {
      const float value = environment.ActorValue(
          sample.name, Studio::kInputMeasures[measure].measure);
      if (std::isfinite(value)) {
        sample.samples[measure] = value;
      }
    }
    samples.push_back(std::move(sample));
  }
  return samples;
}
}
