// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "studio/MenuState.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace BetterEnchantmentEffects::Studio {
enum class NameEvent { kNone, kEnter, kDeactivated, kClickedAway };

struct NameInput {
  std::string text;
  bool active = false;
  NameEvent event = NameEvent::kNone;
};

struct NameRules {
  std::string_view current;
  std::span<const std::string> taken;
};

struct KeepNaming {
  NameDraft draft;
  std::optional<std::string> problem;
};
struct CommitName {
  std::string to;
};
struct CancelNaming {};
using NameStep = std::variant<KeepNaming, CommitName, CancelNaming>;

[[nodiscard]] NameDraft BeginNameDraft(FieldKey a_label,
                                       std::string_view a_name);
[[nodiscard]] bool IsNaming(const std::optional<NameDraft> &a_draft,
                            FieldKey a_label) noexcept;
[[nodiscard]] std::optional<std::string> NameProblem(std::string_view a_text,
                                                     const NameRules &a_rules);
[[nodiscard]] std::optional<std::string>
RecipeNameProblem(std::string_view a_text,
                  std::span<const std::string> a_taken);
[[nodiscard]] NameStep StepNameDraft(const NameDraft &a_draft,
                                     const NameInput &a_input,
                                     const NameRules &a_rules);
}
