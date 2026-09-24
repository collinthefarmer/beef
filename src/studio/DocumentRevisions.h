// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace BetterEnchantmentEffects::Studio {
struct DocumentRevisions {
  std::uint64_t clock = 1;
  std::uint64_t epoch = 1;
  std::unordered_map<std::string, std::uint64_t> documents;
};

[[nodiscard]] std::uint64_t RevisionOf(const DocumentRevisions &a_revisions,
                                       const std::string &a_id);
void AdvanceRevision(DocumentRevisions &a_revisions, const std::string &a_id);
void ResetRevisions(DocumentRevisions &a_revisions);
}
