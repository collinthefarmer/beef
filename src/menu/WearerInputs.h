// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "menu/Frame.h"
#include "studio/Forms.h"

namespace BetterEnchantmentEffects::Menu {
void RequestWearerValue(const Studio::FormField &a_field);
void DrawWearerValue(const Frame &a_frame, const Studio::FormField &a_field);
void ConnectWhenHit(const Frame &a_frame, const Studio::FormField &a_field);
void DrawActorValueReadout(const Frame &a_frame,
                           const ActorValueSignal &a_signal);
}
