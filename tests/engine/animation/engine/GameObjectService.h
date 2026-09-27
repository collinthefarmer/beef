// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "PCH.h"

#include <set>
#include <string_view>
#include <unordered_map>

namespace BetterEnchantmentEffects {
inline std::unordered_map<RE::FormID, std::set<std::string>> observedTags;
inline void NoteAnimEvent(RE::FormID a_actor, std::string_view a_tag) {
  observedTags[a_actor].emplace(a_tag);
}
inline void ForgetAnimEvents(RE::FormID a_actor) {
  observedTags.erase(a_actor);
}
}
