#pragma once

#include "recipe/Recipe.h"
#include "studio/Edits.h"

#include <cstdint>
#include <optional>
#include <string>

namespace BetterEnchantmentEffects::Studio {
struct PaintCommitRequest {
  std::uint64_t id = 0;
  std::string recipeID;
  std::string maskName;
  std::string expression;
  std::uint64_t sessionID = 0;
  std::vector<RecipeEdit> sources{};
};

struct PaintUpdateRequest {
  std::uint64_t sessionID = 0;
  std::uint64_t revision = 0;
  std::string expression;
  std::vector<RecipeEdit> sources{};
  Surface surface = Surface::kMaterial;
};

struct PaintUpdateResult {
  std::uint64_t sessionID = 0;
  std::uint64_t revision = 0;
  std::optional<Diagnostic> problem;
  bool ended = false;
};

struct PaintCommitResult {
  std::uint64_t requestID = 0;
  std::optional<Diagnostic> problem;
};
}
