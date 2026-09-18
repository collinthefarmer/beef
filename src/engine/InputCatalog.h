#pragma once

#include "Core.h"
#include "studio/GameObjects.h"
#include "studio/InputCatalog.h"

#include <cstdint>
#include <vector>

namespace BetterEnchantmentEffects {
[[nodiscard]] std::vector<Studio::ActorValueSample>
BuildActorValueSamples(const Studio::GameObjectCatalog &a_catalog,
                       std::uint32_t a_actor);
}
