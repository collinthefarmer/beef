// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/Renumbering.h"

#include <algorithm>
#include <set>
#include <span>
#include <utility>

namespace BetterEnchantmentEffects::Studio {
namespace {
using Alignment = std::vector<std::optional<std::size_t>>;

struct Anchor {
  std::size_t before = 0;
  std::size_t after = 0;
};

template <class T>
[[nodiscard]] std::vector<Anchor> LongestCommonRun(std::span<const T> a_before,
                                                   std::span<const T> a_after) {
  const std::size_t columns = a_after.size() + 1;
  std::vector<std::size_t> lengths((a_before.size() + 1) * columns, 0);
  const auto length = [&](std::size_t a_i, std::size_t a_j) -> std::size_t & {
    return lengths[a_i * columns + a_j];
  };
  for (std::size_t i = a_before.size(); i-- > 0;) {
    for (std::size_t j = a_after.size(); j-- > 0;) {
      length(i, j) = a_before[i] == a_after[j]
                         ? length(i + 1, j + 1) + 1
                         : (std::max)(length(i + 1, j), length(i, j + 1));
    }
  }
  std::vector<Anchor> anchors;
  std::size_t i = 0;
  std::size_t j = 0;
  while (i < a_before.size() && j < a_after.size()) {
    if (a_before[i] == a_after[j]) {
      anchors.push_back({i, j});
      ++i;
      ++j;
    } else if (length(i + 1, j) >= length(i, j + 1)) {
      ++i;
    } else {
      ++j;
    }
  }
  return anchors;
}

template <class T>
void MatchMoved(std::span<const T> a_before, std::span<const T> a_after,
                Alignment &a_alignment, std::vector<bool> &a_taken) {
  for (std::size_t i = 0; i < a_before.size(); ++i) {
    if (a_alignment[i]) {
      continue;
    }
    for (std::size_t j = 0; j < a_after.size(); ++j) {
      if (!a_taken[j] && a_before[i] == a_after[j]) {
        a_alignment[i] = j;
        a_taken[j] = true;
        break;
      }
    }
  }
}

template <class T, class Similar>
[[nodiscard]] std::optional<std::size_t>
OnlySimilar(const T &a_entry, std::span<const T> a_others,
            std::span<const std::size_t> a_candidates, Similar a_similar) {
  std::optional<std::size_t> found;
  for (const std::size_t k : a_candidates) {
    if (!a_similar(a_entry, a_others[k])) {
      continue;
    }
    if (found) {
      return std::nullopt;
    }
    found = k;
  }
  return found;
}

template <class T, class Similar>
void PairGap(std::span<const T> a_before, std::span<const T> a_after,
             Anchor a_from, Anchor a_to, Similar a_similar,
             Alignment &a_alignment, std::vector<bool> &a_taken) {
  std::vector<std::size_t> befores;
  for (std::size_t i = a_from.before; i < a_to.before; ++i) {
    if (!a_alignment[i]) {
      befores.push_back(i);
    }
  }
  std::vector<std::size_t> afters;
  for (std::size_t j = a_from.after; j < a_to.after; ++j) {
    if (!a_taken[j]) {
      afters.push_back(j);
    }
  }
  if (befores.size() == afters.size()) {
    for (std::size_t k = 0; k < befores.size(); ++k) {
      if (a_similar(a_before[befores[k]], a_after[afters[k]])) {
        a_alignment[befores[k]] = afters[k];
        a_taken[afters[k]] = true;
      }
    }
    return;
  }
  for (const std::size_t i : befores) {
    const std::optional<std::size_t> j =
        OnlySimilar(a_before[i], a_after, afters, a_similar);
    if (j && OnlySimilar(a_after[*j], a_before, befores, a_similar) == i) {
      a_alignment[i] = *j;
      a_taken[*j] = true;
    }
  }
}

template <class T, class Similar>
[[nodiscard]] Alignment Align(std::span<const T> a_before,
                              std::span<const T> a_after, Similar a_similar) {
  Alignment alignment(a_before.size());
  if (a_before.size() > kMaxRenumberedEntries ||
      a_after.size() > kMaxRenumberedEntries) {
    return alignment;
  }
  std::vector<bool> taken(a_after.size(), false);
  const std::vector<Anchor> anchors = LongestCommonRun(a_before, a_after);
  for (const Anchor &anchor : anchors) {
    alignment[anchor.before] = anchor.after;
    taken[anchor.after] = true;
  }
  MatchMoved(a_before, a_after, alignment, taken);
  Anchor from{};
  for (const Anchor &anchor : anchors) {
    PairGap(a_before, a_after, from, anchor, a_similar, alignment, taken);
    from = {anchor.before + 1, anchor.after + 1};
  }
  PairGap(a_before, a_after, from, {a_before.size(), a_after.size()}, a_similar,
          alignment, taken);
  return alignment;
}

[[nodiscard]] bool IsSameSlot(const Output &a_left, const Output &a_right) {
  const auto *left = Get<SurfaceOutput>(a_left);
  const auto *right = Get<SurfaceOutput>(a_right);
  if (!left || !right) {
    return !left && !right;
  }
  return left->surface == right->surface && left->slot == right->slot;
}

[[nodiscard]] bool IsAnyLayer(const Layer &, const Layer &) { return true; }

[[nodiscard]] std::span<const Layer> LayersOf(const Output &a_output) {
  const auto *surface = Get<SurfaceOutput>(a_output);
  return surface ? std::span<const Layer>{surface->stack}
                 : std::span<const Layer>{};
}

[[nodiscard]] bool IsMoved(const OutputRenumbering &a_entry,
                           std::size_t a_output) {
  if (a_entry.to != a_output) {
    return true;
  }
  for (std::size_t layer = 0; layer < a_entry.layers.size(); ++layer) {
    if (a_entry.layers[layer] != layer) {
      return true;
    }
  }
  return false;
}
}

std::optional<Renumbering> RenumberingOf(const Recipe &a_before,
                                         const Recipe &a_after) {
  const std::span<const Output> before{a_before.outputs};
  const std::span<const Output> after{a_after.outputs};
  const Alignment outputs = Align(before, after, IsSameSlot);
  Renumbering renumbering;
  renumbering.outputs.reserve(outputs.size());
  bool moved = false;
  for (std::size_t output = 0; output < outputs.size(); ++output) {
    OutputRenumbering entry{.to = outputs[output]};
    if (entry.to) {
      entry.layers = Align(LayersOf(before[output]), LayersOf(after[*entry.to]),
                           IsAnyLayer);
    }
    moved = moved || IsMoved(entry, output);
    renumbering.outputs.push_back(std::move(entry));
  }
  return moved ? std::optional{std::move(renumbering)} : std::nullopt;
}

std::optional<std::size_t> OutputAfter(const Renumbering &a_renumbering,
                                       std::size_t a_output) noexcept {
  if (a_output >= a_renumbering.outputs.size()) {
    return std::nullopt;
  }
  return a_renumbering.outputs[a_output].to;
}

std::optional<LayerPlace> LayerAfter(const Renumbering &a_renumbering,
                                     LayerPlace a_place) noexcept {
  if (a_place.output >= a_renumbering.outputs.size()) {
    return std::nullopt;
  }
  const OutputRenumbering &entry = a_renumbering.outputs[a_place.output];
  if (!entry.to || a_place.layer >= entry.layers.size() ||
      !entry.layers[a_place.layer]) {
    return std::nullopt;
  }
  return LayerPlace{*entry.to, *entry.layers[a_place.layer]};
}

Isolation RenumberedIsolation(Isolation a_isolation, std::string_view a_recipe,
                              const Renumbering &a_renumbering) {
  if (a_isolation.recipeID != a_recipe || !a_isolation.output) {
    return a_isolation;
  }
  const std::string recipe{a_recipe};
  if (a_isolation.layer) {
    const std::optional<LayerPlace> to =
        LayerAfter(a_renumbering, {*a_isolation.output, *a_isolation.layer});
    if (to) {
      a_isolation.output = to->output;
      a_isolation.layer = to->layer;
      return a_isolation;
    }
    a_isolation = a_isolation.SoloLayer(recipe, *a_isolation.output,
                                        *a_isolation.layer, false);
  }
  if (!a_isolation.output) {
    return a_isolation;
  }
  if (const std::optional<std::size_t> to =
          OutputAfter(a_renumbering, *a_isolation.output)) {
    a_isolation.output = *to;
    return a_isolation;
  }
  return a_isolation.SoloOutput(recipe, *a_isolation.output, false);
}

View RenumberedView(View a_view, std::string_view a_recipe,
                    const Renumbering &a_renumbering) {
  std::set<LayerKey> muted;
  for (const LayerKey &key : a_view.muted) {
    if (key.recipeID != a_recipe) {
      muted.insert(key);
    } else if (const std::optional<LayerPlace> to =
                   LayerAfter(a_renumbering, {key.output, key.layer})) {
      muted.insert(LayerKey{key.recipeID, to->output, to->layer});
    }
  }
  a_view.muted = std::move(muted);
  a_view.isolation =
      RenumberedIsolation(std::move(a_view.isolation), a_recipe, a_renumbering);
  return a_view;
}
}
