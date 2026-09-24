// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/Recipe.h"

#include "recipe/Precedence.h"
#include "recipe/Words.h"

#include <algorithm>
#include <cctype>

namespace BetterEnchantmentEffects {
bool GlobMatch(std::string_view a_glob, std::string_view a_text) noexcept {
  const auto norm = [](char c) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return c == '\\' ? '/' : c;
  };
  std::size_t globAt = 0;
  std::size_t textAt = 0;
  std::size_t starAt = std::string_view::npos;
  std::size_t starMatchedTo = 0;
  while (textAt < a_text.size()) {
    if (globAt < a_glob.size() && a_glob[globAt] == '*') {
      starAt = globAt++;
      starMatchedTo = textAt;
    } else if (globAt < a_glob.size() &&
               norm(a_glob[globAt]) == norm(a_text[textAt])) {
      ++globAt;
      ++textAt;
    } else if (starAt != std::string_view::npos) {
      globAt = starAt + 1;
      textAt = ++starMatchedTo;
    } else {
      return false;
    }
  }
  while (globAt < a_glob.size() && a_glob[globAt] == '*') {
    ++globAt;
  }
  return globAt == a_glob.size();
}

bool Matches(const Selector &a_selector, const GeometryIdentity &a_geometry) {
  if (a_selector.All()) {
    return true;
  }
  return std::ranges::any_of(a_selector.anyOf, [&](const SelectorClause &t) {
    switch (t.kind) {
    case SelectorKind::kAddon: {
      const auto *form = t.Form();
      return form && form->key && a_geometry.addon &&
             *a_geometry.addon == *form->key;
    }
    case SelectorKind::kGeometry:
      return GlobMatch(t.Glob(), a_geometry.name);
    case SelectorKind::kTexture:
      return GlobMatch(t.Glob(), a_geometry.diffusePath);
    }
    return false;
  });
}

namespace {
bool FormKeyMatches(const RecipeKey &a_key, const KeyKindSpec &a_row,
                    const WornPiece &a_piece) {
  const auto *form = a_key.Form();
  if (!form || !form->key) {
    return false;
  }
  if (a_row.singleForm) {
    const auto &have = a_piece.*a_row.singleForm;
    return have && *have == *form->key;
  }
  if (a_row.formList) {
    return std::ranges::any_of(a_piece.*a_row.formList, [&](const FormKey &k) {
      return k == *form->key;
    });
  }
  return false;
}

bool KeyMatches(const RecipeKey &a_key, const WornPiece &a_piece) {
  const auto *row = RowOf(kKeyKinds, a_key.kind);
  if (!row) {
    return false;
  }
  switch (row->operand) {
  case KeyOperand::kNone:
    return !row->enchantmentDerived || a_piece.enchantment.has_value();
  case KeyOperand::kGlob:
    return std::ranges::any_of(a_piece.diffusePaths, [&](const std::string &p) {
      return GlobMatch(a_key.Glob(), p);
    });
  case KeyOperand::kForm:
    return FormKeyMatches(a_key, *row, a_piece);
  }
  return false;
}
}

namespace {
void KeepOneSampled(std::vector<ResolvedRecipe> &a_matches,
                    std::uint32_t a_seed) {
  std::vector<std::size_t> pool;
  for (std::size_t i = 0; i < a_matches.size(); ++i) {
    if (a_matches[i].recipe &&
        a_matches[i].recipe->mergeMode == MergeMode::kSampled) {
      pool.push_back(i);
    }
  }
  if (pool.size() < 2) {
    return;
  }
  std::ranges::sort(pool, [&](std::size_t a_left, std::size_t a_right) {
    return a_matches[a_left].recipe->id < a_matches[a_right].recipe->id;
  });
  const std::size_t keep = pool[SamplingHash(a_seed) % pool.size()];
  std::vector<ResolvedRecipe> filtered;
  filtered.reserve(a_matches.size());
  for (std::size_t i = 0; i < a_matches.size(); ++i) {
    if (!std::ranges::contains(pool, i) || i == keep) {
      filtered.push_back(a_matches[i]);
    }
  }
  a_matches = std::move(filtered);
}
}

std::uint32_t SamplingHash(std::uint32_t a_actor) noexcept {
  std::uint32_t hash = 2166136261u;
  for (unsigned shift = 0; shift < 32; shift += 8) {
    hash ^= (a_actor >> shift) & 0xffu;
    hash *= 16777619u;
  }
  return hash;
}

namespace {
std::optional<RecipeKey> StrongestMatchingKey(const Recipe &a_recipe,
                                              const WornPiece &a_piece) {
  std::optional<RecipeKey> best;
  for (const RecipeKey &key : a_recipe.keys) {
    if (KeyMatches(key, a_piece) &&
        (!best || DefaultPriority(key.kind) > DefaultPriority(best->kind))) {
      best = key;
    }
  }
  return best;
}

std::vector<ResolvedRecipe> MatchingRecipes(const WornPiece &a_piece,
                                            std::span<const Recipe> a_loaded) {
  std::vector<ResolvedRecipe> matches;
  for (std::size_t i = 0; i < a_loaded.size(); ++i) {
    const Recipe &recipe = a_loaded[i];
    if (const std::optional<RecipeKey> key =
            StrongestMatchingKey(recipe, a_piece)) {
      matches.push_back({&recipe, *key,
                         recipe.priority.value_or(DefaultPriority(key->kind)),
                         i});
    }
  }
  return matches;
}

void RecordSelection(std::vector<RecipeSelection> *a_selections,
                     std::size_t a_index, SelectionOutcome a_outcome) {
  if (a_selections && a_index < a_selections->size()) {
    (*a_selections)[a_index].outcome = a_outcome;
  }
}

void FilterFallbacks(std::vector<ResolvedRecipe> &a_matches,
                     std::vector<RecipeSelection> *a_selections) {
  const bool enchanted =
      std::ranges::any_of(a_matches, [](const ResolvedRecipe &a_match) {
        return EnchantmentDerived(a_match.key.kind);
      });
  const bool specific =
      std::ranges::any_of(a_matches, [](const ResolvedRecipe &a_match) {
        return EnchantmentDerived(a_match.key.kind) &&
               KeyOperandOf(a_match.key.kind) == KeyOperand::kForm;
      });
  std::erase_if(a_matches, [&](const ResolvedRecipe &a_match) {
    const bool suppressed =
        (enchanted && a_match.key.kind == KeyKind::kDefault) ||
        (specific && a_match.key.kind == KeyKind::kEnchanted);
    RecordSelection(a_selections, a_match.loadOrder,
                    suppressed ? SelectionOutcome::kFallbackSuppressed
                               : SelectionOutcome::kSampledOut);
    return suppressed;
  });
}
}

std::vector<ResolvedRecipe>
Resolve(const WornPiece &a_piece, std::span<const Recipe> a_loaded,
        std::uint32_t a_seed, std::vector<RecipeSelection> *a_selections) {
  if (a_selections) {
    a_selections->clear();
    for (const Recipe &recipe : a_loaded) {
      a_selections->push_back({recipe.id, SelectionOutcome::kNonmatching});
    }
  }
  std::vector<ResolvedRecipe> matches = MatchingRecipes(a_piece, a_loaded);
  FilterFallbacks(matches, a_selections);
  KeepOneSampled(matches, a_seed);
  std::ranges::stable_sort(
      matches, [](const ResolvedRecipe &a_left, const ResolvedRecipe &a_right) {
        return Precedence{a_left.priority, a_left.loadOrder} <
               Precedence{a_right.priority, a_right.loadOrder};
      });
  for (const ResolvedRecipe &selected : matches) {
    RecordSelection(a_selections, selected.loadOrder,
                    SelectionOutcome::kSelected);
  }
  return matches;
}

std::string_view SelectionOutcomeName(SelectionOutcome a_outcome) noexcept {
  return NameOf(kSelectionOutcomes, a_outcome);
}

bool AnyUnenchantedKey(std::span<const Recipe> a_loaded) noexcept {
  return std::ranges::any_of(a_loaded, [](const Recipe &r) {
    return std::ranges::any_of(r.keys, [](const RecipeKey &k) {
      const auto *row = RowOf(kKeyKinds, k.kind);
      return row && row->operand != KeyOperand::kNone &&
             !row->enchantmentDerived;
    });
  });
}

std::vector<PieceKey> KeyChoicesOf(const WornPiece &a_piece) {
  std::vector<PieceKey> out;
  for (std::size_t i = std::size(kKeyKinds); i-- > 0;) {
    const auto &row = kKeyKinds[i];
    if (row.operand != KeyOperand::kForm) {
      continue;
    }
    if (row.singleForm) {
      if (const auto &form = a_piece.*row.singleForm) {
        out.push_back({row.value, *form});
      }
    } else if (row.formList) {
      for (const auto &form : a_piece.*row.formList) {
        out.push_back({row.value, form});
      }
    }
  }
  return out;
}

const PieceKey *DefaultKeyChoice(std::span<const PieceKey> a_choices) noexcept {
  if (const PieceKey *armor =
          FindBy(a_choices, KeyKind::kArmor, &PieceKey::kind)) {
    return armor;
  }
  return a_choices.empty() ? nullptr : &a_choices.front();
}

RecipeKey RecipeKeyOf(const PieceKey &a_key, std::string_view a_text) {
  RecipeKey key;
  key.kind = a_key.kind;
  key.operand = FormRef{std::string{a_text}, a_key.form};
  return key;
}
}
