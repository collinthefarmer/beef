// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/RecipeSnapshot.h"

#include "studio/Relationships.h"
#include "studio/Rows.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
namespace {
std::size_t RefCount(const std::map<std::string, std::size_t> &a_counts,
                     const std::string &a_name) {
  const auto it = a_counts.find(a_name);
  return it != a_counts.end() ? it->second : 0;
}

std::unordered_map<std::string, std::string>
InertReasons(const RecipeGraph &a_graph) {
  std::unordered_map<std::string, std::string> reasons;
  for (const Diagnostic &d : a_graph.Diagnostics()) {
    if (d.where.starts_with("signal ")) {
      reasons.emplace(d.where.substr(7), d.message);
    }
  }
  return reasons;
}

RecipeRow RecipeSummaryOf(const RecipeRowInput &a_input) {
  const Recipe &recipe = a_input.recipe;
  RecipeRow r;
  r.id = recipe.id;
  r.matchedKey = a_input.key;
  r.keys = recipe.keys;
  r.priority = a_input.priority;
  r.mergeMode = recipe.mergeMode;
  r.clockSpeed = recipe.clock.speed;
  r.time = a_input.time;
  r.dirty = a_input.dirty;
  r.pinned = a_input.pinned;
  r.shellMaterial = recipe.shell.material;
  r.lightOutput = a_input.lightOutput;
  r.lightRow = LightRowOf(recipe);
  r.shellRow = ShellRowOf(recipe);
  r.undoDepth = a_input.undoDepth;
  r.redoDepth = a_input.redoDepth;
  return r;
}

std::vector<OutputRow> OutputRowsOf(const Recipe &a_recipe) {
  std::vector<OutputRow> rows;
  rows.reserve(a_recipe.outputs.size());
  for (std::size_t i = 0; i < a_recipe.outputs.size(); ++i) {
    rows.push_back(OutputRowOf(a_recipe, i));
  }
  return rows;
}

std::vector<LightRow> LightRowsOf(const Recipe &a_recipe) {
  std::vector<LightRow> rows;
  rows.reserve(a_recipe.outputs.size());
  for (std::size_t i = 0; i < a_recipe.outputs.size(); ++i) {
    if (Is<LightOutput>(a_recipe.outputs[i])) {
      rows.push_back(LightRowOf(a_recipe, i));
    }
  }
  return rows;
}

std::vector<std::string> MaskNamesOf(const Recipe &a_recipe) {
  std::vector<std::string> names;
  names.reserve(a_recipe.masks.size());
  for (const Mask &mask : a_recipe.masks) {
    names.push_back(mask.name);
  }
  return names;
}

std::vector<SourceRow> SourceRowsOf(const Recipe &a_recipe,
                                    const ReferenceCounts &a_references) {
  std::vector<SourceRow> rows;
  rows.reserve(a_recipe.sources.size());
  for (const Source &source : a_recipe.sources) {
    rows.push_back(
        SourceRowOf(source, RefCount(a_references.images, source.name)));
  }
  return rows;
}

std::vector<TextRow> MaskRowsOf(const Recipe &a_recipe,
                                const ReferenceCounts &a_references) {
  std::vector<TextRow> rows;
  rows.reserve(a_recipe.masks.size());
  for (const Mask &mask : a_recipe.masks) {
    rows.push_back(MaskRowOf(mask, RefCount(a_references.images, mask.name)));
  }
  return rows;
}

std::vector<TextRow> CurveRowsOf(const Recipe &a_recipe,
                                 const ReferenceCounts &a_references) {
  std::vector<TextRow> rows;
  rows.reserve(a_recipe.curves.size());
  for (const Curve &curve : a_recipe.curves) {
    rows.push_back(
        CurveRowOf(curve, RefCount(a_references.curves, curve.name)));
  }
  return rows;
}

std::vector<SignalRow> SignalRowsWith(const RecipeRowInput &a_input,
                                      const RecipeGraph &a_graph) {
  const Recipe &recipe = a_input.recipe;
  const RowTypes rows{recipe, a_graph};
  const std::unordered_map<std::string, std::string> reasons =
      InertReasons(a_graph);
  std::vector<SignalRow> signalRows;
  for (const Signal &signal : recipe.signals) {
    SignalRow signalRow = SignalRowOf(
        signal, rows, RefCount(a_input.references.signals, signal.name));
    if (a_input.signals) {
      signalRow.value = a_input.signals->ValueOf(signal.name);
      signalRow.live = true;
    }
    if (signalRow.inert) {
      if (const auto reason = reasons.find(signal.name);
          reason != reasons.end()) {
        signalRow.problem = reason->second;
      }
    }
    signalRows.push_back(std::move(signalRow));
  }
  return signalRows;
}

std::vector<SignalRow> SignalRowsOf(const RecipeRowInput &a_input) {
  if (a_input.graph) {
    return SignalRowsWith(a_input, *a_input.graph);
  }
  const RecipeGraph compiled = RecipeGraph::Compile(a_input.recipe);
  return SignalRowsWith(a_input, compiled);
}
}

RecipeRow BuildRecipeRow(const RecipeRowInput &a_input) {
  RecipeRow r = RecipeSummaryOf(a_input);
  if (!a_input.full) {
    return r;
  }
  const Recipe &recipe = a_input.recipe;
  r.relationships = RelationshipsOf(recipe);
  r.outputs = OutputRowsOf(recipe);
  r.lights = LightRowsOf(recipe);
  r.masks = MaskNamesOf(recipe);
  r.sourceRows = SourceRowsOf(recipe, a_input.references);
  r.maskRows = MaskRowsOf(recipe, a_input.references);
  r.problems.assign(a_input.problems.begin(), a_input.problems.end());
  r.heldBack = HasRecipeErrors(a_input.problems);
  r.signals = SignalRowsOf(a_input);
  r.curves = CurveRowsOf(recipe, a_input.references);
  return r;
}
}
