// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "studio/Intent.h"
#include "studio/Snapshot.h"
#include "studio/View.h"

#include <string>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
struct ActivePreview {
  std::string label;
  Intent clear;
};

[[nodiscard]] std::vector<ActivePreview>
ActivePreviewsOf(const View &a_view, const Snapshot &a_snapshot);
}
