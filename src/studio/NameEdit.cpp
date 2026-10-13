// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/NameEdit.h"

#include "recipe/Recipe.h"

#include <algorithm>
#include <utility>

namespace BetterEnchantmentEffects::Studio {
NameDraft BeginNameDraft(FieldKey a_label, std::string_view a_name) {
  return NameDraft{
      .label = a_label, .text = std::string{a_name}, .awaitingFocus = true};
}

bool IsNaming(const std::optional<NameDraft> &a_draft,
              FieldKey a_label) noexcept {
  return a_draft.has_value() && a_draft->label == a_label;
}

std::optional<std::string> NameProblem(std::string_view a_text,
                                       const NameRules &a_rules) {
  if (a_text == a_rules.current) {
    return std::nullopt;
  }
  if (!IsName(a_text)) {
    return "Use letters, digits and underscores; do not start with a digit.";
  }
  if (std::ranges::contains(a_rules.taken, a_text)) {
    return "That name is already in use.";
  }
  return std::nullopt;
}

NameStep StepNameDraft(const NameDraft &a_draft, const NameInput &a_input,
                       const NameRules &a_rules) {
  NameDraft next{.label = a_draft.label,
                 .text = a_input.text,
                 .awaitingFocus = a_draft.awaitingFocus && !a_input.active};
  std::optional<std::string> problem = NameProblem(next.text, a_rules);
  switch (a_input.event) {
  case NameEvent::kClickedAway:
    return CancelNaming{};
  case NameEvent::kDeactivated:
    if (!next.awaitingFocus) {
      return CancelNaming{};
    }
    break;
  case NameEvent::kEnter:
    if (problem) {
      next.awaitingFocus = true;
      break;
    }
    if (next.text == a_rules.current) {
      return CancelNaming{};
    }
    return CommitName{std::move(next.text)};
  case NameEvent::kNone:
    break;
  }
  return KeepNaming{std::move(next), std::move(problem)};
}
}
