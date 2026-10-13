// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "menu/Frame.h"
#include "studio/MenuState.h"
#include "studio/PreviewWindows.h"
#include "studio/Snapshot.h"

#include <optional>

namespace BetterEnchantmentEffects::Menu {
void PinPreviewButton(const Studio::PreviewSubject &a_subject,
                      const std::optional<Studio::PreviewTarget> &a_target,
                      Studio::MenuState &a_state);
void PinSubjectButton(const Frame &a_frame, Studio::PreviewSubject a_subject);
void DrawPreviewLimit(const Studio::MenuState &a_state);
void DrawPreviewWindows(const Studio::Snapshot &a_snapshot,
                        Studio::MenuState &a_state, float a_scale);
}
