// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/BusEvents.h"
#include "recipe/Recipe.h"
#include "test_support.h"

#include <algorithm>
#include <set>
#include <string>

using namespace BetterEnchantmentEffects;
using test::Check;

int main() {
  std::set<std::string_view> ids;
  for (const BusEvent &event : kBusEvents) {
    ids.insert(event.id);
  }
  Check(ids.size() == std::size(kBusEvents),
        "every built-in event id is unique");
  Check(std::ranges::all_of(kSpellCastEvents,
                            [](const Named<SpellSchool> &a_school) {
                              const BusEvent *event =
                                  FindBusEvent(a_school.name);
                              return event &&
                                     event->payload == ValueType::kScalar &&
                                     !event->carriesPosition;
                            }),
        "every school's cast event is on the bus with a scalar cost");
  Check(SpellCastEventId(SpellSchool::kDestruction) == "spell.cast.destruction",
        "a school names its cast event");
  Check(PositionEventId(kHitDealtEvent) == "hit.dealt.position" &&
            AnimEventId("weaponSwing") == "anim.weaponSwing",
        "position and animation ids are built in one place");

  for (const std::string_view known :
       {"equip", "hit.*", "hit.dealt.position", "spell.cast.*",
        "spell.cast.restoration", "anim.weaponSwing", "anim.*", "*"}) {
    Check(IsKnownEventGlob(known),
          std::format("'{}' matches a built-in event", known));
  }
  for (const std::string_view unknown :
       {"hti.received", "spell.cast.necromancy",
        "spell.cast.destruction.position", "equipped"}) {
    Check(!IsKnownEventGlob(unknown),
          std::format("'{}' matches no built-in event", unknown));
  }
  return test::Finish("busevents");
}
