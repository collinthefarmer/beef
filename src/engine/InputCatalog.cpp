#include "engine/InputCatalog.h"

#include "engine/Environment.h"

#include <cmath>
#include <utility>

namespace BetterEnchantmentEffects {
namespace {
void ReadInputSamples(Studio::ActorInputInfo &a_input,
                      const ActorEnvironment &a_environment) {
  for (std::size_t measure = 0; measure < Studio::kInputMeasures.size();
       ++measure) {
    const float sample = a_environment.ActorValue(
        a_input.name, Studio::kInputMeasures[measure].measure);
    if (std::isfinite(sample)) {
      a_input.samples[measure] = sample;
    }
  }
}
}

std::vector<Studio::ActorInputInfo>
BuildActorInputCatalog(std::uint32_t a_actor) {
  const RE::ActorValueList *list = RE::ActorValueList::GetSingleton();
  if (!list) {
    return {};
  }
  const RE::NiPointer<RE::Actor> actor{
      RE::TESForm::LookupByID<RE::Actor>(a_actor)};
  const ActorEnvironment environment{actor.get(), nullptr};
  std::vector<Studio::ActorInputInfo> inputs;
  inputs.reserve(static_cast<std::size_t>(RE::ActorValue::kTotal));
  for (std::uint32_t i = 0;
       i < static_cast<std::uint32_t>(RE::ActorValue::kTotal); ++i) {
    const auto value = static_cast<RE::ActorValue>(i);
    const RE::ActorValueInfo *info = list->GetActorValue(value);
    if (!info || !info->enumName || info->enumName[0] == '\0' ||
        list->LookupActorValueByName(info->enumName) != value) {
      continue;
    }
    const char *label = info->GetFullName();
    Studio::ActorInputInfo input =
        Studio::DescribeActorInput(info->enumName, label ? label : "");
    if (actor && actor->AsActorValueOwner()) {
      ReadInputSamples(input, environment);
    }
    inputs.push_back(std::move(input));
  }
  return inputs;
}
}
