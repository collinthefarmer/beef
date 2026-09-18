#include "menu/ResourcePanels.h"

#include "recipe/Recipe.h"
#include "studio/Intent.h"
#include "studio/Names.h"
#include "studio/Selection.h"

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
  case Studio::ResourceTab::kSignals: {
    const std::string added = name(Studio::RowKind::kSignal, "signal");
    Studio::Post(*a_frame.intents, id, Studio::AddSignal{added});
    a_frame.state->pendingSelection = Studio::SignalSubject{added};
    break;
  }
  case Studio::ResourceTab::kCurves: {
    const std::string added = name(Studio::RowKind::kCurve, "curve");
    Studio::Post(*a_frame.intents, id, Studio::AddCurve{added});
    a_frame.state->pendingSelection = Studio::CurveSubject{added};
    break;
  }
  case Studio::ResourceTab::kSources: {
    const std::string added = name(Studio::RowKind::kSource, "source");
    Studio::Post(*a_frame.intents, id,
                 Studio::AddSource{added, MaterialSource{}});
    a_frame.state->pendingSelection = Studio::SourceSubject{added};
    break;
  }
  case Studio::ResourceTab::kMasks: {
    const std::string added = name(Studio::RowKind::kMask, "mask");
    Studio::Post(*a_frame.intents, id, Studio::AddMask{added});
    a_frame.state->pendingSelection = Studio::MaskSubject{added};
    break;
  }
  }
}
}
