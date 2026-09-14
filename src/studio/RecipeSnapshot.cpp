#include "studio/RecipeSnapshot.h"

#include "studio/Relationships.h"
#include "studio/Rows.h"

#include <string>
#include <unordered_map>
#include <utility>

namespace BetterEnchantmentEffects::Studio {
namespace {
std::size_t RefCount(const std::map<std::string, std::size_t> &a_counts,
                     const std::string &a_name) {
  const auto it = a_counts.find(a_name);
  return it != a_counts.end() ? it->second : 0;
}

std::unordered_map<std::string, std::string>
InertReasons(const SignalGraph &a_graph) {
  std::unordered_map<std::string, std::string> reasons;
  for (const Diagnostic &d : a_graph.Diagnostics()) {
    if (d.where.starts_with("signal ")) {
      reasons.emplace(d.where.substr(7), d.message);
    }
  }
  return reasons;
}
}

RecipeRow BuildRecipeRow(const RecipeRowInput &a_input) {
  const Recipe &recipe = a_input.recipe;
  RecipeRow r;
  r.id = recipe.id;
  r.matchedKey = a_input.key;
  r.keys = recipe.keys;
  r.priority = a_input.priority;
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
  if (!a_input.full) {
    return r;
  }
  r.relationships = RelationshipsOf(recipe);

  for (std::size_t i = 0; i < recipe.outputs.size(); ++i) {
    r.outputs.push_back(OutputRowOf(recipe, i));
    if (Is<LightOutput>(recipe.outputs[i])) {
      r.lights.push_back(LightRowOf(recipe, i));
    }
  }
  for (const Mask &mask : recipe.masks) {
    r.masks.push_back(mask.name);
  }
  for (const Source &source : recipe.sources) {
    r.sourceRows.push_back(
        SourceRowOf(source, RefCount(a_input.references.images, source.name)));
  }
  for (const Mask &mask : recipe.masks) {
    r.maskRows.push_back(
        MaskRowOf(mask, RefCount(a_input.references.images, mask.name)));
  }
  r.problems.assign(a_input.problems.begin(), a_input.problems.end());
  r.heldBack = HasRecipeErrors(a_input.problems);
  {
    const std::optional<SignalGraph> compiled =
        a_input.graph ? std::nullopt
                      : std::optional<SignalGraph>{SignalGraph::Compile(
                            recipe.signals, recipe.curves)};
    const SignalGraph &graph = a_input.graph ? *a_input.graph : *compiled;
    const RowTypes rows{recipe, graph};
    const std::unordered_map<std::string, std::string> reasons =
        InertReasons(graph);
    for (const Signal &signal : recipe.signals) {
      SignalRow srow = SignalRowOf(
          signal, rows, RefCount(a_input.references.signals, signal.name));
      if (a_input.signals) {
        srow.value = a_input.signals->ValueOf(signal.name);
        srow.live = true;
      }
      if (srow.inert) {
        if (const auto reason = reasons.find(signal.name);
            reason != reasons.end()) {
          srow.problem = reason->second;
        }
      }
      r.signals.push_back(std::move(srow));
    }
  }
  for (const Curve &curve : recipe.curves) {
    r.curves.push_back(
        CurveRowOf(curve, RefCount(a_input.references.curves, curve.name)));
  }
  return r;
}
}
