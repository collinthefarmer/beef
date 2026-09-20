#include "menu/ResourcePanels.h"

#include "recipe/Recipe.h"
#include "studio/Create.h"
#include "studio/Intent.h"
#include "studio/Selection.h"

#include <optional>
#include <utility>

namespace BetterEnchantmentEffects::Menu {
namespace {
[[nodiscard]] std::optional<Studio::Creation>
CreationOf(Studio::ResourceTab a_tab) {
  switch (a_tab) {
  case Studio::ResourceTab::kSignals:
    return Studio::NewSignal{};
  case Studio::ResourceTab::kCurves:
    return Studio::NewCurve{};
  case Studio::ResourceTab::kSources:
    return Studio::NewSource{};
  case Studio::ResourceTab::kMasks:
    return Studio::NewMask{};
  }
  return std::nullopt;
}
}

void PostResourceAdd(const Frame &a_frame, Studio::ResourceTab a_tab) {
  if (!a_frame.recipe || !a_frame.state || !a_frame.intents) {
    return;
  }
  const std::optional<Studio::Creation> request = CreationOf(a_tab);
  if (!request) {
    return;
  }
  Studio::Created made = Studio::Create(*request, *a_frame.recipe);
  Studio::Post(*a_frame.intents,
               Studio::EditRecipe{a_frame.recipe->id, std::move(made.edits)});
  a_frame.state->pendingSelection = made.subject;
}
}
