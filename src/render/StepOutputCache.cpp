// GPL-3.0-only with the additional permission in COPYING.md.
#include "render/StepOutputCache.h"

#include "planners/RetainedStore.h"

namespace BetterEnchantmentEffects {
namespace {
constexpr std::size_t kMaxEntries = 4096;

bool HeldOnlyByCache(const TextureView &a_view) {
  if (!a_view.target) {
    return true;
  }
  const long ownReferences = a_view.texture.Holds(a_view.target) ? 2 : 1;
  return a_view.target.use_count() <= ownReferences;
}
}

std::optional<TextureView> StepOutputCache::Find(const std::string &a_key,
                                                 std::uint64_t a_nowMS) {
  const auto found = entries_.find(a_key);
  if (found == entries_.end()) {
    return std::nullopt;
  }
  if (!found->second.view.target || !found->second.view.texture.Valid()) {
    entries_.erase(found);
    return std::nullopt;
  }
  found->second.lastUsedMS = a_nowMS;
  return found->second.view;
}

void StepOutputCache::Publish(const std::string &a_key,
                              const TextureView &a_view,
                              std::uint64_t a_nowMS) {
  if (!a_view.target || a_key.empty() ||
      (entries_.size() >= kMaxEntries && !entries_.contains(a_key))) {
    return;
  }
  entries_[a_key] = Entry{a_view, a_nowMS};
}

void StepOutputCache::Sweep(std::uint64_t a_nowMS) {
  std::erase_if(entries_, [a_nowMS](auto &a_entry) {
    Entry &entry = a_entry.second;
    switch (StepOutputRetention(!HeldOnlyByCache(entry.view),
                                a_nowMS - entry.lastUsedMS)) {
    case Retention::kRenew:
      entry.lastUsedMS = a_nowMS;
      return false;
    case Retention::kKeep:
      return false;
    case Retention::kEvict:
      return true;
    }
    return false;
  });
}

void StepOutputCache::Clear() noexcept { entries_.clear(); }

void StepOutputCache::CollectTargets(HolderSet &a_set) const {
  for (const auto &[key, entry] : entries_)
    CollectView(entry.view, a_set);
}
}
