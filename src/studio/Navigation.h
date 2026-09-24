// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "studio/Edits.h"
#include "studio/Selection.h"

#include <cstddef>
#include <cstdint>
#include <optional>
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
  std::vector<InspectorVisit> forward;
  float scroll = 0.0f;
};

struct PreviewPin {
  Selection selection;
  std::uint64_t resetID = 0;
};

void ResolvePreviewPin(std::optional<PreviewPin> &a_pin,
                       const Selection &a_selection, const RecipeRow *a_recipe,
                       std::uint64_t a_resetID);
void InvalidatePreviewPin(std::optional<PreviewPin> &a_pin,
                          std::string_view a_recipeID);

[[nodiscard]] bool InspectorSubjectExists(const InspectorSubject &a_subject,
                                          const RecipeRow &a_recipe);
[[nodiscard]] bool ResolveInspectorSubject(Selection &a_selection,
                                           const RecipeRow *a_recipe);
[[nodiscard]] bool Navigate(Navigation &a_navigation, Selection &a_selection,
                            InspectorSubject a_subject,
                            const RecipeRow &a_recipe);
[[nodiscard]] bool
ResolvePendingSubject(Navigation &a_navigation, Selection &a_selection,
                      std::optional<InspectorSubject> &a_pending,
                      const RecipeRow *a_recipe);
[[nodiscard]] bool NavigateProperty(Navigation &a_navigation,
                                    Selection &a_selection,
                                    InspectorSubject a_subject,
                                    PropertyLocation a_property,
                                    const RecipeRow &a_recipe);
[[nodiscard]] bool GoBack(Navigation &a_navigation, Selection &a_selection,
                          const RecipeRow &a_recipe);
[[nodiscard]] bool GoForward(Navigation &a_navigation, Selection &a_selection,
                             const RecipeRow &a_recipe);
void InvalidateIndexedSubjects(Navigation &a_navigation, Selection &a_selection,
                               std::string_view a_recipeID);
[[nodiscard]] bool
ShouldInvalidateIndexedSubjects(std::span<const RecipeEdit> a_edits);
}
