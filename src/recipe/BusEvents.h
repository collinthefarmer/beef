// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "Core.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace BetterEnchantmentEffects {
enum class SpellSchool {
  kAlteration,
  kConjuration,
  kDestruction,
  kIllusion,
  kRestoration,
};
inline constexpr std::size_t kSpellSchoolCount = 5;

struct BusEvent {
  std::string_view id;
  ValueType payload = ValueType::kScalar;
  std::string_view meaning;
  bool carriesPosition = false;
};

inline constexpr std::string_view kEquipEvent = "equip";
inline constexpr std::string_view kHitReceivedEvent = "hit.received";
inline constexpr std::string_view kHitDealtEvent = "hit.dealt";
inline constexpr std::string_view kPositionSuffix = ".position";
inline constexpr std::string_view kAnimPrefix = "anim.";

inline constexpr Named<SpellSchool> kSpellCastEvents[]{
    {SpellSchool::kAlteration, "spell.cast.alteration"},
    {SpellSchool::kConjuration, "spell.cast.conjuration"},
    {SpellSchool::kDestruction, "spell.cast.destruction"},
    {SpellSchool::kIllusion, "spell.cast.illusion"},
    {SpellSchool::kRestoration, "spell.cast.restoration"}};
static_assert(Complete(kSpellCastEvents, kSpellSchoolCount));

inline constexpr std::string_view kSpellCostMeaning =
    "the spell's magicka cost for the caster";

inline constexpr BusEvent kBusEvents[]{
    {kEquipEvent, ValueType::kScalar,
     "1 when the actor equips a piece a recipe covers", true},
    {kHitReceivedEvent, ValueType::kScalar, "1 when the actor is hit", true},
    {kHitDealtEvent, ValueType::kScalar, "1 when the actor hits something",
     true},
    {"spell.cast.alteration", ValueType::kScalar, kSpellCostMeaning, false},
    {"spell.cast.conjuration", ValueType::kScalar, kSpellCostMeaning, false},
    {"spell.cast.destruction", ValueType::kScalar, kSpellCostMeaning, false},
    {"spell.cast.illusion", ValueType::kScalar, kSpellCostMeaning, false},
    {"spell.cast.restoration", ValueType::kScalar, kSpellCostMeaning, false}};

[[nodiscard]] std::string PositionEventId(std::string_view a_event);
[[nodiscard]] std::string AnimEventId(std::string_view a_tag);
[[nodiscard]] std::string_view SpellCastEventId(SpellSchool a_school) noexcept;
[[nodiscard]] const BusEvent *FindBusEvent(std::string_view a_id) noexcept;
[[nodiscard]] bool IsKnownEventGlob(std::string_view a_glob) noexcept;
}
