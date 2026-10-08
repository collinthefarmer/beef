// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/LogOnce.h"
#include "PCH.h"

namespace BetterEnchantmentEffects {
void LogOnce(WarningHistory &a_history, Severity a_severity,
             std::string_view a_line) {
  const WarningDecision decision = a_history.Observe(a_line);
  if (decision == WarningDecision::kLimit) {
    logger::warn("the log's repeat history reached its budget; faults that "
                 "exceed it are omitted from the log until the next load; "
                 "diagnostics remain available in the editor");
  }
  if (decision != WarningDecision::kFirst) {
    return;
  }
  if (a_severity == Severity::kError) {
    logger::error("{} (repeats suppressed)", a_line);
  } else {
    logger::warn("{} (repeats suppressed)", a_line);
  }
}
}
