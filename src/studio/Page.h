#pragma once

#include "mesh/MeshFacts.h"
#include "studio/Intent.h"
#include "studio/Names.h"
#include "studio/Snapshot.h"
#include "studio/View.h"

#include <span>

namespace BetterEnchantmentEffects::Studio {
struct Page {
  const PieceRow *piece = nullptr;
  const RecipeRow *recipe = nullptr;
  const GeometryRow *geometry = nullptr;
  const Names *names = nullptr;
  Intents *intents = nullptr;
  Layout layout;
  float scale = 1.0f;
};

[[nodiscard]] FormID ActorOf(const Page &a_page) noexcept;
[[nodiscard]] std::span<const BoneCoverage>
BonesOf(const Page &a_page) noexcept;
[[nodiscard]] float ScaleOf(const Page &a_page) noexcept;
}
