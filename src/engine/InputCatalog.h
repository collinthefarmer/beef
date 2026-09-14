#pragma once

#include "Core.h"
#include "studio/InputCatalog.h"

#include <cstdint>
#include <vector>

namespace BetterEnchantmentEffects {
[[nodiscard]] std::vector<Studio::ActorInputInfo>
BuildActorInputCatalog(std::uint32_t a_actor);
}
