#pragma once

#include "menu/Frame.h"
#include "studio/Forms.h"

namespace BetterEnchantmentEffects::Menu {
void DrawTuning(const Studio::FormField &a_field, const Frame &a_frame);
void BeginTuningFrame(Studio::MenuState &a_state,
                      const Studio::Snapshot &a_snapshot);
void EndTuningFrame(Studio::MenuState &a_state);
void FinishTuning(Studio::MenuState &a_state, bool a_commit);
}
