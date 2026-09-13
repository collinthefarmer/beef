#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace BetterEnchantmentEffects::Studio {
struct RecipeEditResult {
  std::uint64_t requestID = 0;
  std::string recipeID;
  std::optional<std::string> error;
};

struct PendingIndexedEdit {
  std::uint64_t requestID = 0;
  std::string recipeID;
};

[[nodiscard]] bool
IndexedEditPendingFor(const std::optional<PendingIndexedEdit> &a_pending,
                      std::string_view a_recipeID) noexcept;
[[nodiscard]] bool
AcknowledgeIndexedEdit(std::optional<PendingIndexedEdit> &a_pending,
                       const RecipeEditResult *a_result) noexcept;
}
