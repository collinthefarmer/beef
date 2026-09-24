// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/DocumentRevisions.h"

namespace BetterEnchantmentEffects::Studio {
std::uint64_t RevisionOf(const DocumentRevisions &a_revisions,
                         const std::string &a_id) {
  const auto found = a_revisions.documents.find(a_id);
  return found != a_revisions.documents.end() ? found->second
                                              : a_revisions.epoch;
}

void AdvanceRevision(DocumentRevisions &a_revisions, const std::string &a_id) {
  a_revisions.documents[a_id] = ++a_revisions.clock;
}

void ResetRevisions(DocumentRevisions &a_revisions) {
  a_revisions.documents.clear();
  a_revisions.epoch = ++a_revisions.clock;
}
}
