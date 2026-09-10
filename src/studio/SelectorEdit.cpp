#include "studio/SelectorEdit.h"

#include "Core.h"
#include "recipe/Recipe.h"

#include <cstddef>
#include <string>
#include <utility>
#include <variant>

namespace BetterEnchantmentEffects::Studio {
namespace {
[[nodiscard]] std::variant<FormRef, std::string>
DefaultOperand(SelectorKind a_kind) {
  if (a_kind == SelectorKind::kAddon) {
    return FormRef{};
  }
  return std::string{};
}

[[nodiscard]] std::string ClauseValue(const SelectorClause &a_clause) {
  if (const FormRef *form = a_clause.Form()) {
    return form->text;
  }
  return std::string{a_clause.Glob()};
}
}

SelectorView SelectorViewOf(const Selector &a_selector) {
  SelectorView view;
  view.matchAll = a_selector.All();
  view.clauses.reserve(a_selector.anyOf.size());
  for (const SelectorClause &clause : a_selector.anyOf) {
    SelectorClauseRow row;
    row.kind = clause.kind;
    row.isForm = clause.kind == SelectorKind::kAddon;
    row.value = ClauseValue(clause);
    view.clauses.push_back(std::move(row));
  }
  return view;
}

Selector SelectorWithClause(const Selector &a_selector, SelectorKind a_kind) {
  Selector out = a_selector;
  SelectorClause clause;
  clause.kind = a_kind;
  clause.operand = DefaultOperand(a_kind);
  out.anyOf.push_back(std::move(clause));
  return out;
}

Selector SelectorWithoutClause(const Selector &a_selector,
                               std::size_t a_index) {
  if (a_index >= a_selector.anyOf.size()) {
    return a_selector;
  }
  Selector out = a_selector;
  out.anyOf.erase(out.anyOf.begin() + static_cast<std::ptrdiff_t>(a_index));
  return out;
}

Selector SelectorWithKind(const Selector &a_selector, std::size_t a_index,
                          SelectorKind a_kind) {
  if (a_index >= a_selector.anyOf.size()) {
    return a_selector;
  }
  Selector out = a_selector;
  SelectorClause &clause = out.anyOf[a_index];
  const bool wasForm = clause.kind == SelectorKind::kAddon;
  const bool nowForm = a_kind == SelectorKind::kAddon;
  clause.kind = a_kind;
  if (wasForm != nowForm) {
    clause.operand = DefaultOperand(a_kind);
  }
  return out;
}

Selector SelectorWithOperand(const Selector &a_selector, std::size_t a_index,
                             std::string a_text) {
  if (a_index >= a_selector.anyOf.size()) {
    return a_selector;
  }
  Selector out = a_selector;
  SelectorClause &clause = out.anyOf[a_index];
  if (clause.kind == SelectorKind::kAddon) {
    FormRef ref = FormRef::From(a_text);
    if (ref.text.empty()) {
      return a_selector;
    }
    clause.operand = std::move(ref);
  } else {
    clause.operand = std::move(a_text);
  }
  return out;
}
}
