#pragma once

#include "studio/Edits.h"
#include "studio/Selection.h"

#include <cstddef>
#include <span>
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
  float scroll = 0.0f;
};

[[nodiscard]] bool InspectorSubjectExists(const InspectorSubject &a_subject,
                                          const RecipeRow &a_recipe);
[[nodiscard]] bool ResolveInspectorSubject(Selection &a_selection,
                                           const RecipeRow *a_recipe);
[[nodiscard]] bool Navigate(Navigation &a_navigation, Selection &a_selection,
                            InspectorSubject a_subject,
                            const RecipeRow &a_recipe);
[[nodiscard]] bool GoBack(Navigation &a_navigation, Selection &a_selection,
                          const RecipeRow &a_recipe);
void InvalidateIndexedSubjects(Navigation &a_navigation, Selection &a_selection,
                               std::string_view a_recipeID);
[[nodiscard]] bool
ShouldInvalidateIndexedSubjects(std::span<const RecipeEdit> a_edits);
}
