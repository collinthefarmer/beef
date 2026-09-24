// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "studio/Intent.h"
#include "studio/MenuState.h"
#include "studio/Names.h"
#include "studio/Selection.h"
#include "studio/Snapshot.h"
#include "studio/View.h"

namespace BetterEnchantmentEffects::Menu {
struct Frame {
  const Studio::Snapshot *snapshot = nullptr;
  const Studio::PieceRow *piece = nullptr;
  const Studio::RecipeRow *recipe = nullptr;
  const Studio::GeometryRow *geometry = nullptr;
  const Studio::Names *names = nullptr;
  Studio::MenuState *state = nullptr;
  Studio::Intents *intents = nullptr;
  float scale = 1.0f;
};

[[nodiscard]] Studio::FormID ActorOf(const Frame &a_frame) noexcept;
[[nodiscard]] const Studio::Selection &
SelectionOf(const Frame &a_frame) noexcept;
[[nodiscard]] const Studio::View &ViewOf(const Frame &a_frame) noexcept;
[[nodiscard]] const Studio::Layout &LayoutOf(const Frame &a_frame) noexcept;
}
