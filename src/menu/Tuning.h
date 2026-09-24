// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "menu/Frame.h"
#include "studio/Forms.h"

#include <functional>
#include <optional>

namespace BetterEnchantmentEffects::Menu {
struct TuningSink {
  std::function<void(float a_value)> commit;
};

void DrawTuning(const Studio::FormField &a_field, const Frame &a_frame,
                const std::optional<TuningSink> &a_sink = std::nullopt);
[[nodiscard]] bool FieldTunable(const Studio::FormField &a_field,
                                const Frame &a_frame);
void BeginTuningFrame(Studio::MenuState &a_state,
                      const Studio::Snapshot &a_snapshot);
void EndTuningFrame(Studio::MenuState &a_state);
void FinishTuning(Studio::MenuState &a_state, bool a_commit);
}
