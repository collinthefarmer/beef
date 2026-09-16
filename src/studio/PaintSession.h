#pragma once

#include "recipe/Recipe.h"
#include "studio/Edits.h"
#include "studio/PaintCommit.h"
#include "studio/Selection.h"
#include "studio/Snapshot.h"

#include <array>
#include <expected>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
inline constexpr std::string_view kScratchMask = "scratch";
inline constexpr std::string_view kPaintRecipe = "paint";
inline constexpr int kPaintPriority = 1000;

struct PaintSession {
  std::string recipeID;
  Surface surface = Surface::kMaterial;
  std::set<std::string> readGeometries;
  std::optional<std::uint64_t> pendingCommit;
  std::optional<Diagnostic> problem;
  std::uint64_t sessionID = 0;
  bool ready = false;
  bool projected = false;
  std::uint64_t revision = 1;
  std::optional<std::uint64_t> pendingRevision;
  std::vector<RecipeEdit> sources{};
  std::array<char, 1024> keepName{};
  Selection origin;
  std::string previewGeometry;
  float originScroll = 0.0f;
  std::optional<PaintAssignment> assignment;
  bool assignmentInvalid = false;
  std::optional<std::string> createdMask;
};

[[nodiscard]] std::expected<EditBatch, Diagnostic>
PreparePaintUpdate(const Recipe *a_paint, const PaintUpdateRequest &a_request);

[[nodiscard]] SurfaceOutput PaintOutput(Surface a_surface);
[[nodiscard]] std::vector<RecipeEdit> PaintSurfaceEdits(Surface a_surface);
[[nodiscard]] Recipe PaintRecipe(const Recipe &a_active, RecipeKey a_key,
                                 Surface a_surface);
[[nodiscard]] std::vector<RecipeEdit> KeepEdits(const Recipe &a_paint,
                                                const Recipe &a_active,
                                                std::string_view a_name);
[[nodiscard]] std::expected<EditBatch, Diagnostic>
PreparePaintCommit(const Recipe *a_paint, const Recipe *a_target,
                   const PaintCommitRequest &a_request,
                   std::uint64_t a_documentRevision = 0);
}
