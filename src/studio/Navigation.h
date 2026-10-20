// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "studio/Edits.h"
#include "studio/Renumbering.h"
#include "studio/Selection.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
struct InspectorVisit {
  Selection selection;
  float scroll = 0.0f;
};

inline constexpr std::size_t kMaxInspectorHistory = 64;

struct Navigation {
  std::vector<InspectorVisit> back;
  std::vector<InspectorVisit> forward;
  float scroll = 0.0f;
};

struct PendingSubject {
  std::string recipeID;
  InspectorSubject subject;
};

struct PropertyTarget {
  InspectorSubject subject;
  PropertyLocation property;
};

enum class HistoryDirection : std::uint8_t { kBack, kForward };

[[nodiscard]] bool InspectorSubjectExists(const InspectorSubject &a_subject,
                                          const RecipeRow &a_recipe);
[[nodiscard]] bool ResolveInspectorSubject(Selection &a_selection,
                                           const RecipeRow *a_recipe);
[[nodiscard]] bool Navigate(Navigation &a_navigation, Selection &a_selection,
                            InspectorSubject a_subject,
                            const RecipeRow &a_recipe);
[[nodiscard]] bool
ResolvePendingSubject(Navigation &a_navigation, Selection &a_selection,
                      std::optional<PendingSubject> &a_pending,
                      const RecipeRow *a_recipe);
[[nodiscard]] bool NavigateProperty(Navigation &a_navigation,
                                    Selection &a_selection,
                                    PropertyTarget a_target,
                                    const RecipeRow &a_recipe);
[[nodiscard]] std::optional<InspectorVisit>
ReachableVisit(InspectorVisit a_visit, const Selection &a_current,
               const RecipeRow &a_recipe);
[[nodiscard]] bool StepHistory(Navigation &a_navigation,
                               HistoryDirection a_direction,
                               Selection &a_selection,
                               const RecipeRow &a_recipe);
[[nodiscard]] bool GoBack(Navigation &a_navigation, Selection &a_selection,
                          const RecipeRow &a_recipe);
[[nodiscard]] bool GoForward(Navigation &a_navigation, Selection &a_selection,
                             const RecipeRow &a_recipe);
void RenumberIndexedSubjects(Navigation &a_navigation, Selection &a_selection,
                             std::string_view a_recipeID,
                             const Renumbering &a_renumbering);
[[nodiscard]] bool CanRenumber(std::span<const RecipeEdit> a_edits);
}
