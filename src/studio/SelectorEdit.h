// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "recipe/Recipe.h"

#include <cstddef>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
struct SelectorClauseRow {
  SelectorKind kind = SelectorKind::kGeometry;
  std::string value;
  bool isForm = false;
};

struct SelectorView {
  bool matchAll = true;
  std::vector<SelectorClauseRow> clauses;
};

[[nodiscard]] SelectorView SelectorViewOf(const Selector &a_selector);
[[nodiscard]] Selector SelectorWithClause(const Selector &a_selector,
                                          SelectorKind a_kind);
[[nodiscard]] Selector SelectorWithoutClause(const Selector &a_selector,
                                             std::size_t a_index);
[[nodiscard]] Selector SelectorWithKind(const Selector &a_selector,
                                        std::size_t a_index,
                                        SelectorKind a_kind);
[[nodiscard]] Selector SelectorWithOperand(const Selector &a_selector,
                                           std::size_t a_index,
                                           std::string a_text);
}
