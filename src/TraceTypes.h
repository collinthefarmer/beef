// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <string>

namespace BetterEnchantmentEffects::Trace {
enum class Event {
  kStartup,
  kSettings,
  kRecipe,
  kOperation,
  kPage,
  kQueue,
  kLoad,
  kApplication,
  kRetire,
  kBinding,
  kRestore,
  kTexture,
  kShell,
  kPreview,
  kMetrics,
  kCaptureFailure,
};

struct Field {
  std::string name;
  std::string value;
};
}
