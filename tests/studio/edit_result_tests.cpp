#include "studio/EditResult.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects::Studio;
using test::Check;

int main() {
  {
    std::optional<PendingIndexedEdit> pending;
    const RecipeEditResult old{1, "glow", std::nullopt};
    Check(!IndexedEditPendingFor(pending, "glow") &&
              !AcknowledgeIndexedEdit(pending, &old),
          "an idle editor ignores already published results");
    pending = PendingIndexedEdit{2, "glow"};
    Check(IndexedEditPendingFor(pending, "glow") &&
              !IndexedEditPendingFor(pending, "other"),
          "the pending gate belongs to the submitted document");
    Check(!AcknowledgeIndexedEdit(pending, nullptr) &&
              !AcknowledgeIndexedEdit(pending, &old) &&
              IndexedEditPendingFor(pending, "glow"),
          "missing and older results leave stale positional actions blocked");
    const RecipeEditResult other{2, "other", std::nullopt};
    Check(!AcknowledgeIndexedEdit(pending, &other) && pending.has_value(),
          "a matching request number from another document cannot release the "
          "gate");
    const RecipeEditResult future{3, "glow", std::nullopt};
    Check(!AcknowledgeIndexedEdit(pending, &future) && pending.has_value(),
          "an unrelated later result cannot acknowledge the pending edit");
    const RecipeEditResult success{2, "glow", std::nullopt};
    Check(AcknowledgeIndexedEdit(pending, &success) && !pending &&
              !AcknowledgeIndexedEdit(pending, &success),
          "the matching success releases the gate exactly once");
  }
  {
    std::optional<PendingIndexedEdit> pending = PendingIndexedEdit{4, "glow"};
    const RecipeEditResult refusal{
        4, "glow",
        MakeDiagnostic(BetterEnchantmentEffects::Severity::kError, "glow",
                       "The layer no longer exists")};
    Check(AcknowledgeIndexedEdit(pending, &refusal) && !pending &&
              ProblemText(refusal.error) == "The layer no longer exists",
          "a refused edit releases the gate while preserving its displayed "
          "error");
    pending = PendingIndexedEdit{5, "glow"};
    Check(!AcknowledgeIndexedEdit(pending, &refusal) &&
              IndexedEditPendingFor(pending, "glow"),
          "a repeated refusal cannot clear the next pending operation");
  }
  return test::Finish("studio_edit_result");
}
