#include "studio/PaintSession.h"

#include "Core.h"
#include "recipe/Expression.h"
#include "studio/Names.h"
#include "studio/TermTemplates.h"

#include <algorithm>
#include <ranges>

namespace BetterEnchantmentEffects::Studio {
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

namespace {
class SourceNamer {
public:
  explicit SourceNamer(const Existing &a_existing)
      : existing_(a_existing), taken_(a_existing.taken) {}

  [[nodiscard]] std::string NameFor(const std::string &a_wanted,
                                    const SourceKind &a_kind) {
    for (const auto &[name, kind] : existing_.sources) {
      if (name == a_wanted && kind == a_kind) {
        return name;
      }
    }
    for (const auto &[name, kind] : existing_.sources) {
      if (kind == a_kind) {
        return name;
      }
    }
    const std::string name = UniqueName(a_wanted, taken_);
    taken_.push_back(name);
    edits_.emplace_back(AddSource{name, a_kind});
    return name;
  }

  [[nodiscard]] std::vector<RecipeEdit> Edits() && { return std::move(edits_); }

private:
  const Existing &existing_;
  std::vector<std::string> taken_;
  std::vector<RecipeEdit> edits_;
};
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
  const Existing existing = ExistingOf(a_active);
  SourceNamer namer(existing);
  for (const std::string &read : program->References()) {
    const Source *source = a_paint.FindSource(read);
    if (!source) {
      continue;
    }
    const std::string to = namer.NameFor(read, source->kind);
    if (to != read) {
      text = RenameInExpression(text, read, to, false);
    }
  }
  edits = std::move(namer).Edits();
  if (!a_active.FindMask(a_name)) {
    edits.emplace_back(AddMask{std::string{a_name}});
  }
  edits.emplace_back(SetMask{std::string{a_name}, std::move(text)});
  return edits;
}
}
