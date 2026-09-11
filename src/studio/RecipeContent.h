#pragma once

#include "recipe/Recipe.h"
#include "recipe/Signals.h"
#include "studio/Edits.h"
#include "studio/Snapshot.h"

#include <cstddef>
#include <optional>
#include <span>

namespace BetterEnchantmentEffects::Studio {
struct RecipeContentInput {
  const Recipe &recipe;
  RecipeKey key;
  int priority = 0;
  float time = 0.0f;
  std::optional<std::size_t> lightOutput;
  bool dirty = false;
  bool pinned = false;
  bool full = false;
  std::size_t undoDepth = 0;
  std::size_t redoDepth = 0;
  const ReferenceCounts &references;
  const SignalGraph *graph = nullptr;
  const SignalState *signals = nullptr;
  std::span<const Diagnostic> problems;
};

[[nodiscard]] RecipeRow RecipeContent(const RecipeContentInput &a_input);
}
