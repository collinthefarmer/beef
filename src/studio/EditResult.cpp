// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/EditResult.h"

namespace BetterEnchantmentEffects::Studio {
bool IndexedEditPendingFor(const std::optional<PendingIndexedEdit> &a_pending,
                           std::string_view a_recipeID) noexcept {
  return a_pending && a_pending->recipeID == a_recipeID;
}

bool AcknowledgeIndexedEdit(std::optional<PendingIndexedEdit> &a_pending,
                            const RecipeEditResult *a_result) noexcept {
  if (!a_pending || !a_result || a_pending->requestID != a_result->requestID ||
      a_pending->recipeID != a_result->recipeID) {
    return false;
  }
  a_pending.reset();
  return true;
}
}
