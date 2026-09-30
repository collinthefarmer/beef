// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/PaintSession.h"

#include "Core.h"
#include "recipe/Expression.h"
#include "studio/SourcePlan.h"

#include <algorithm>
#include <ranges>

namespace BetterEnchantmentEffects::Studio {
std::expected<EditBatch, Diagnostic>
PreparePaintCommit(const Recipe *a_paint, const Recipe *a_target,
                   const PaintCommitRequest &a_request,
                   std::uint64_t a_documentRevision) {
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
  if (!IsName(a_request.maskName) || a_request.maskName == kScratchMask ||
      a_request.maskName == kPeekMask) {
    return refuse("choose a valid mask name other than scratch or peek");
  }
  if (!a_request.replacingMask.empty() &&
      a_request.maskName == a_request.replacingMask &&
      !a_target->FindMask(a_request.replacingMask)) {
    return refuse(
        "the original mask was removed or renamed; choose a new name to Keep");
  }
  if (a_request.assignment &&
      a_request.assignment->documentRevision != a_documentRevision) {
    return refuse(
        "the destination changed; select the layer again before assigning");
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
  if (a_request.assignment) {
    edits.edits.emplace_back(SetLayerMask{a_request.assignment->output,
                                          a_request.assignment->layer,
                                          Ref{a_request.maskName}});
  }
  if (const auto checked = PrepareEdits(*a_target, edits); !checked) {
    return std::unexpected(checked.error());
  }
  return edits;
}

namespace {
bool ImageReferenced(const ReferenceCounts &a_counts,
                     const std::string &a_name) {
  const auto found = a_counts.images.find(a_name);
  return found != a_counts.images.end() && found->second > 0;
}

std::optional<Diagnostic>
AddPaintSources(EditBatch &a_batch, Recipe &a_available,
                const std::vector<RecipeEdit> &a_sources) {
  for (const RecipeEdit &edit : a_sources) {
    const AddSource *source = Get<AddSource>(edit);
    if (!source) {
      return MakeDiagnostic(Severity::kError, "paint",
                            "paint dependencies must be source additions");
    }
    if (const Source *existing = a_available.FindSource(source->name)) {
      if (existing->kind != source->kind) {
        return MakeDiagnostic(
            Severity::kError, "paint",
            "a paint source name has a conflicting definition");
      }
      continue;
    }
    if (a_available.sources.size() >= kMaxRecipeRows) {
      return MakeDiagnostic(Severity::kError, "paint",
                            "the recipe source limit was reached");
    }
    a_available.sources.push_back(Source{source->name, source->kind});
    a_batch.edits.push_back(edit);
  }
  return std::nullopt;
}

void PruneUnusedSources(EditBatch &a_batch, const Recipe &a_prepared,
                        const std::vector<RecipeEdit> &a_tracked) {
  std::set<std::string> keep;
  for (const RecipeEdit &edit : a_tracked) {
    if (const AddSource *source = Get<AddSource>(edit)) {
      keep.insert(source->name);
    }
  }
  const ReferenceCounts counts = CountReferences(a_prepared);
  for (const Source &source : a_prepared.sources) {
    if (!ImageReferenced(counts, source.name) && !keep.contains(source.name)) {
      a_batch.edits.emplace_back(RemoveSource{source.name});
    }
  }
}
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
  if (a_request.sources.size() + a_request.peekSources.size() >
      kMaxRecipeRows) {
    return refuse("the paint source limit was reached");
  }
  EditBatch batch;
  Recipe available = *a_paint;
  if (const auto problem =
          AddPaintSources(batch, available, a_request.sources)) {
    return std::unexpected(*problem);
  }
  if (const auto problem =
          AddPaintSources(batch, available, a_request.peekSources)) {
    return std::unexpected(*problem);
  }
  batch.edits.emplace_back(
      SetMask{std::string{kScratchMask}, a_request.expression});
  batch.edits.emplace_back(
      SetMask{std::string{kPeekMask},
              a_request.peek.empty() ? std::string{"0"} : a_request.peek});
  for (RecipeEdit &edit : PaintSurfaceEdits(a_request.surface)) {
    batch.edits.push_back(std::move(edit));
  }
  const auto prepared = PrepareEdits(*a_paint, batch);
  if (!prepared) {
    return std::unexpected(prepared.error());
  }
  PruneUnusedSources(batch, *prepared, a_request.sources);
  return batch;
}

namespace {
Layer ScratchLayer() {
  Layer layer = DefaultLayer();
  layer.mask = Ref{std::string{kScratchMask}};
  return layer;
}

Layer PeekLayer() {
  Layer layer = DefaultLayer();
  layer.source = Vec3{1.0f, 0.0f, 1.0f};
  layer.mask = Ref{std::string{kPeekMask}};
  return layer;
}
}

SurfaceOutput PaintOutput(Surface a_surface) {
  SurfaceOutput output = DefaultOutput(a_surface, Slot::kEmissive);
  output.stack = {ScratchLayer(), PeekLayer()};
  return output;
}

std::vector<RecipeEdit> PaintSurfaceEdits(Surface a_surface) {
  return {RemoveOutput{0}, AddOutput{a_surface, Slot::kEmissive, Selector{}},
          AddLayer{0, ScratchLayer(), std::nullopt},
          AddLayer{0, PeekLayer(), std::nullopt}};
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
  std::erase_if(recipe.masks, [](const Mask &a_mask) {
    return a_mask.name == kScratchMask || a_mask.name == kPeekMask;
  });
  recipe.masks.push_back(Mask{std::string{kScratchMask}, "0"});
  recipe.masks.push_back(Mask{std::string{kPeekMask}, "0"});
  return recipe;
}

namespace {
struct CarriedMask {
  std::vector<RecipeEdit> edits;
  std::string text;
};

std::optional<CarriedMask> CarriedMaskFor(const Recipe &a_paint,
                                          const Recipe &a_active) {
  const Mask *scratch = a_paint.FindMask(kScratchMask);
  if (!scratch) {
    return std::nullopt;
  }
  const auto program = Program::Parse(scratch->text);
  if (!program) {
    return std::nullopt;
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
  return CarriedMask{std::move(sources).TakeEdits(),
                     RenameInExpression(scratch->text, renames, false)};
}

std::vector<RecipeEdit>
ReleasedSourceEdits(const Recipe &a_active, const Mask &a_previous,
                    const std::vector<RecipeEdit> &a_edits) {
  std::vector<RecipeEdit> released;
  const auto prepared = PrepareEdits(a_active, EditBatch{a_edits});
  if (!prepared) {
    return released;
  }
  const auto oldProgram = Program::Parse(a_previous.text);
  if (!oldProgram) {
    return released;
  }
  const ReferenceCounts counts = CountReferences(*prepared);
  for (const std::string &name : oldProgram->References()) {
    if (a_active.FindSource(name) && !ImageReferenced(counts, name)) {
      released.emplace_back(RemoveSource{name});
    }
  }
  return released;
}
}

std::vector<RecipeEdit> KeepEdits(const Recipe &a_paint, const Recipe &a_active,
                                  std::string_view a_name) {
  std::optional<CarriedMask> carried = CarriedMaskFor(a_paint, a_active);
  if (!carried) {
    return {};
  }
  std::vector<RecipeEdit> edits = std::move(carried->edits);
  if (!a_active.FindMask(a_name)) {
    edits.emplace_back(AddMask{std::string{a_name}});
  }
  edits.emplace_back(SetMask{std::string{a_name}, std::move(carried->text)});
  if (const Mask *previous = a_active.FindMask(a_name)) {
    for (RecipeEdit &edit : ReleasedSourceEdits(a_active, *previous, edits)) {
      edits.push_back(std::move(edit));
    }
  }
  return edits;
}
}
