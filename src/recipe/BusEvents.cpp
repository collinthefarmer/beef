// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/BusEvents.h"

#include "recipe/Recipe.h"

#include <algorithm>

namespace BetterEnchantmentEffects {
namespace {
constexpr std::string_view kAnyAnimEvent = "anim.event";
}

std::string PositionEventId(std::string_view a_event) {
  return std::string{a_event} + std::string{kPositionSuffix};
}

std::string AnimEventId(std::string_view a_tag) {
  return std::string{kAnimPrefix} + std::string{a_tag};
}

std::string_view SpellCastEventId(SpellSchool a_school) noexcept {
  return NameOf(kSpellCastEvents, a_school);
}

const BusEvent *FindBusEvent(std::string_view a_id) noexcept {
  const auto *found = std::ranges::find(kBusEvents, a_id, &BusEvent::id);
  return found != std::ranges::end(kBusEvents) ? found : nullptr;
}

bool IsKnownEventGlob(std::string_view a_glob) noexcept {
  if (a_glob.starts_with(kAnimPrefix) || GlobMatch(a_glob, kAnyAnimEvent)) {
    return true;
  }
  return std::ranges::any_of(kBusEvents, [&](const BusEvent &a_event) {
    return GlobMatch(a_glob, a_event.id) ||
           (a_event.carriesPosition &&
            GlobMatch(a_glob, PositionEventId(a_event.id)));
  });
}
}
