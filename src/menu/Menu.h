// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "studio/Intent.h"
#include "studio/MenuState.h"
#include "studio/Snapshot.h"
#include "studio/View.h"

namespace BetterEnchantmentEffects::Menu {
void Perform(const Studio::Intent &a_intent, Studio::MenuState &a_state,
             const Studio::View &a_view);
void Dispatch(Studio::Intents &a_intents, Studio::MenuState &a_state,
              const Studio::Snapshot &a_snapshot);

void RenderStatus(const Studio::Snapshot &a_snapshot);
void RenderHeader(const Studio::Snapshot &a_snapshot);
void RenderPendingStatus();

void __stdcall RenderRecipes();
void __stdcall RenderSetup();

void RegisterMenu();
}
