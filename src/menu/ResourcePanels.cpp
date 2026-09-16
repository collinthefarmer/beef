#include "menu/ResourcePanels.h"

#include "recipe/Recipe.h"
#include "studio/Intent.h"
#include "studio/Names.h"

#include <string>

namespace BetterEnchantmentEffects::Menu {
void PostResourceAdd(const Frame &a_frame, Studio::ResourceTab a_tab) {
  if (!a_frame.recipe || !a_frame.names || !a_frame.state || !a_frame.intents) {
    return;
  }
  const auto name = [&](Studio::RowKind a_kind, const char *a_stem) {
    return Studio::UniqueName(a_stem,
                              Studio::TakenNames(a_kind, *a_frame.names));
  };
  const std::string &id = a_frame.recipe->id;
  switch (a_tab) {
  case Studio::ResourceTab::kSignals:
    Studio::Post(*a_frame.intents, id,
                 Studio::AddSignal{name(Studio::RowKind::kSignal, "signal")});
    break;
  case Studio::ResourceTab::kCurves:
    Studio::Post(*a_frame.intents, id,
                 Studio::AddCurve{name(Studio::RowKind::kCurve, "curve")});
    break;
  case Studio::ResourceTab::kSources:
    Studio::Post(*a_frame.intents, id,
                 Studio::AddSource{name(Studio::RowKind::kSource, "source"),
                                   MaterialSource{}});
    break;
  case Studio::ResourceTab::kMasks:
    Studio::Post(*a_frame.intents, id,
                 Studio::AddMask{name(Studio::RowKind::kMask, "mask")});
    break;
  }
}
}
