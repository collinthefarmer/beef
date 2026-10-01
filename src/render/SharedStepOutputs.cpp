// GPL-3.0-only with the additional permission in COPYING.md.
#include "render/SharedStepOutputs.h"

namespace BetterEnchantmentEffects {
namespace {
constexpr std::uint64_t kUnheldEntryMS = 500;
constexpr std::size_t kMaxEntries = 4096;

bool HeldOnlyByCache(const TextureView &a_view) {
  if (!a_view.target) {
    return true;
  }
  const long ownReferences = a_view.texture.Holds(a_view.target) ? 2 : 1;
  return a_view.target.use_count() <= ownReferences;
}
}

SharedStepOutputs *SharedStepOutputs::GetSingleton() {
  static SharedStepOutputs outputs;
  return &outputs;
}

std::optional<TextureView> SharedStepOutputs::Find(const std::string &a_key,
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

void SharedStepOutputs::Publish(const std::string &a_key,
                                const TextureView &a_view,
                                std::uint64_t a_nowMS) {
  if (!a_view.target || a_key.empty() ||
      (entries_.size() >= kMaxEntries && !entries_.contains(a_key))) {
    return;
  }
  entries_[a_key] = Entry{a_view, a_nowMS};
}

void SharedStepOutputs::Sweep(std::uint64_t a_nowMS) {
  std::erase_if(entries_, [a_nowMS](const auto &a_entry) {
    const Entry &entry = a_entry.second;
    return HeldOnlyByCache(entry.view) &&
           a_nowMS - entry.lastUsedMS > kUnheldEntryMS;
  });
}

void SharedStepOutputs::Clear() noexcept { entries_.clear(); }
}
