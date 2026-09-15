#pragma once

#include "menu/Frame.h"
#include "studio/Forms.h"

namespace BetterEnchantmentEffects::Menu {
void DrawInputBrowser(const Frame &a_frame, const Studio::FormField &a_field);
void DrawInputWizard(const Frame &a_frame, const Studio::FormField &a_field);
void DrawSignalWizardButton(const Frame &a_frame);
}
