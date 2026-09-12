#include "studio/PaintSession.h"

#include "Core.h"
#include "recipe/Expression.h"
#include "studio/SourcePlan.h"

#include <algorithm>
#include <ranges>

namespace BetterEnchantmentEffects::Studio {
std::expected<EditBatch, Diagnostic>
PreparePaintCommit(const Recipe *a_paint, const Recipe *a_target,
                   const PaintCommitRequest &a_request) {
  const auto refuse =
      [](std::string a_message) -> std::expected<EditBatch, Diagnostic> {
    return std::unexpected(
        MakeDiagnostic(Severity::kError, "paint", std::move(a_message)));
  };
  if (!a_paint || !a_target) {
    return refuse("the paint recipe or target recipe is not loaded");
  }
  if (a_target->id != a_request.recipeID || a_target->id == kPaintRecipe) {
    return refuse("the target recipe does not match the paint request");
  }
  if (!IsName(a_request.maskName) || a_request.maskName == kScratchMask) {
    return refuse("choose a mask name other than scratch");
  }
  Recipe paint = *a_paint;
  const auto prepared = PreparePaintUpdate(
      a_paint, PaintUpdateRequest{a_request.sessionID, 0, a_request.expression,
                                  a_request.sources});
  if (!prepared) {
    return std::unexpected(prepared.error());
  }
  if (const auto problem = Apply(paint, *prepared)) {
    return std::unexpected(*problem);
  }
  EditBatch edits{KeepEdits(paint, *a_target, a_request.maskName)};
  if (edits.edits.empty()) {
    return refuse("the paint mask could not be prepared");
  }
  return edits;
}

std::expected<EditBatch, Diagnostic>
PreparePaintUpdate(const Recipe *a_paint, const PaintUpdateRequest &a_request) {
  const auto refuse =
      [](std::string a_message) -> std::expected<EditBatch, Diagnostic> {
    return std::unexpected(
        MakeDiagnostic(Severity::kError, "paint", std::move(a_message)));
  };
  if (!a_paint || a_paint->id != kPaintRecipe) {
    return refuse("the paint recipe is not loaded");
  }
  if (a_request.sources.size() > kMaxRecipeRows) {
    return refuse("the paint source limit was reached");
  }
  EditBatch batch;
  Recipe available = *a_paint;
  for (const RecipeEdit &edit : a_request.sources) {
    const AddSource *source = Get<AddSource>(edit);
    if (!source) {
      return refuse("paint dependencies must be source additions");
    }
    if (const Source *existing = available.FindSource(source->name)) {
      if (existing->kind != source->kind) {
        return refuse("a paint source name has a conflicting definition");
      }
      continue;
    }
    if (available.sources.size() >= kMaxRecipeRows) {
      return refuse("the recipe source limit was reached");
    }
    available.sources.push_back(Source{source->name, source->kind});
    batch.edits.push_back(edit);
  }
  batch.edits.emplace_back(
      SetMask{std::string{kScratchMask}, a_request.expression});
  for (RecipeEdit &edit : PaintSurfaceEdits(a_request.surface)) {
    batch.edits.push_back(std::move(edit));
  }
  const auto prepared = PrepareEdits(*a_paint, batch);
  if (!prepared) {
    return std::unexpected(prepared.error());
  }
  return batch;
}

std::optional<std::string> ScratchOf(const RecipeRow &a_recipe) {
  const auto it =
      std::ranges::find(a_recipe.maskRows, kScratchMask, &TextRow::name);
  return it != a_recipe.maskRows.end() ? std::optional{it->text} : std::nullopt;
}

SurfaceOutput PaintOutput(Surface a_surface) {
  SurfaceOutput output = DefaultOutput(a_surface, Slot::kEmissive);
  Layer layer = DefaultLayer();
  layer.mask = Ref{std::string{kScratchMask}};
  output.stack = {std::move(layer)};
  return output;
}

std::vector<RecipeEdit> PaintSurfaceEdits(Surface a_surface) {
  Layer layer = DefaultLayer();
  layer.mask = Ref{std::string{kScratchMask}};
  return {RemoveOutput{0}, AddOutput{a_surface, Slot::kEmissive, Selector{}},
          AddLayer{0, std::move(layer), std::nullopt}};
}

Recipe PaintRecipe(const Recipe &a_active, RecipeKey a_key, Surface a_surface) {
  Recipe recipe = a_active;
  recipe.id = std::string{kPaintRecipe};
  recipe.metadata = Metadata{};
  recipe.metadata.name = recipe.id;
  recipe.keys = {std::move(a_key)};
  recipe.priority = kPaintPriority;
  recipe.variants.clear();
  recipe.outputs = {PaintOutput(a_surface)};
  std::erase_if(recipe.masks,
                [](const Mask &a_mask) { return a_mask.name == kScratchMask; });
  recipe.masks.push_back(Mask{std::string{kScratchMask}, "0"});
  return recipe;
}

std::vector<RecipeEdit> KeepEdits(const Recipe &a_paint, const Recipe &a_active,
                                  std::string_view a_name) {
  std::vector<RecipeEdit> edits;
  const Mask *scratch = a_paint.FindMask(kScratchMask);
  if (!scratch) {
    return edits;
  }
  std::string text = scratch->text;
  const auto program = Program::Parse(text);
  if (!program) {
    return edits;
  }
  const SourceCatalog existing = SourceCatalogOf(a_active);
  SourcePlanBuilder sources(existing);
  std::vector<ExpressionRename> renames;
  for (const std::string &read : program->References()) {
    const Source *source = a_paint.FindSource(read);
    if (!source) {
      continue;
    }
    const std::string to = sources.ReuseOrAdd(read, source->kind);
    if (to != read) {
      renames.push_back({read, to});
    }
  }
  text = RenameInExpression(text, renames, false);
  edits = std::move(sources).TakeEdits();
  if (!a_active.FindMask(a_name)) {
    edits.emplace_back(AddMask{std::string{a_name}});
  }
  edits.emplace_back(SetMask{std::string{a_name}, std::move(text)});
  return edits;
}
}
