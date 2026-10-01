// GPL-3.0-only with the additional permission in COPYING.md.
#include "regression/Run.h"

#include <algorithm>
#include <array>

namespace BetterEnchantmentEffects::Regression {
namespace {
inline constexpr std::string_view kSoloRecipe = "arcane-circuit";
inline constexpr std::size_t kEquipCycles = 5;
inline constexpr std::uint32_t kControlHoldFrames = 120;
inline constexpr std::uint32_t kViewSettleFrames = 60;

inline constexpr Equip kPlayerFixture{Role::kPlayer, Item::kFixture};
inline constexpr Equip kWearerFixture{Role::kWearer, Item::kFixture};
inline constexpr Equip kControlPlain{Role::kControl, Item::kPlainCuirass};

inline constexpr std::array<Step, 6> kCleanup{
    ReturnToStart{},
    SetView{View::kThirdPerson},
    Despawn{Role::kWearer},
    Despawn{Role::kControl},
    Remove{Role::kPlayer, Item::kFixture},
    RestoreView{}};

inline constexpr std::array<Step, 5> kLifecycle{
    Solo{kSoloRecipe}, kPlayerFixture, Apply{Role::kPlayer},
    Retire{Role::kPlayer}, Apply{Role::kPlayer}};

consteval std::array<Step, 3 + 4 * kEquipCycles> EquipCycle() {
  std::array<Step, 3 + 4 * kEquipCycles> steps{};
  steps[0] = Solo{kSoloRecipe};
  steps[1] = kPlayerFixture;
  steps[2] = AwaitRendered{Role::kPlayer};
  for (std::size_t cycle = 0; cycle < kEquipCycles; ++cycle) {
    const std::size_t first = 3 + 4 * cycle;
    steps[first] = Unequip{Role::kPlayer, Item::kFixture};
    steps[first + 1] = AwaitBaseline{Role::kPlayer};
    steps[first + 2] = kPlayerFixture;
    steps[first + 3] = AwaitRendered{Role::kPlayer};
  }
  return steps;
}
inline constexpr std::array<Step, 3 + 4 * kEquipCycles> kEquipCycle =
    EquipCycle();

inline constexpr std::array<Step, 10> kCamera{
    Solo{kSoloRecipe},
    kPlayerFixture,
    AwaitRendered{Role::kPlayer},
    SetView{View::kFirstPerson},
    Settle{kViewSettleFrames},
    SetView{View::kThirdPerson},
    Settle{kViewSettleFrames},
    ExpectEffect{Role::kPlayer},
    Unequip{Role::kPlayer, Item::kFixture},
    AwaitBaseline{Role::kPlayer}};

inline constexpr std::array<Step, 18> kIsolation{
    Solo{kSoloRecipe},
    Spawn{Role::kWearer},
    Spawn{Role::kControl},
    kPlayerFixture,
    kWearerFixture,
    kControlPlain,
    AwaitRendered{Role::kPlayer},
    AwaitRendered{Role::kWearer},
    HoldUntouched{Role::kControl, kControlHoldFrames},
    Retire{Role::kWearer},
    AwaitBaseline{Role::kWearer},
    ExpectEffect{Role::kPlayer},
    Apply{Role::kWearer},
    ExpectEffect{Role::kWearer},
    Unequip{Role::kWearer, Item::kFixture},
    AwaitBaseline{Role::kWearer},
    ExpectEffect{Role::kPlayer},
    HoldUntouched{Role::kControl, kControlHoldFrames}};

inline constexpr std::array<Step, 17> kUnload{Solo{kSoloRecipe},
                                              kPlayerFixture,
                                              Spawn{Role::kWearer},
                                              kWearerFixture,
                                              AwaitRendered{Role::kWearer},
                                              Disable{Role::kWearer},
                                              AwaitRetired{Role::kWearer},
                                              Enable{Role::kWearer},
                                              kWearerFixture,
                                              AwaitRendered{Role::kWearer},
                                              LeaveCell{},
                                              AwaitRetired{Role::kWearer},
                                              ExpectEffect{Role::kPlayer},
                                              ReturnToStart{},
                                              kWearerFixture,
                                              AwaitRendered{Role::kWearer},
                                              ExpectEffect{Role::kPlayer}};

inline constexpr std::array<Case, 5> kCatalog{
    Case{"lifecycle", kLifecycle, kCleanup},
    Case{"equip-cycle", kEquipCycle, kCleanup},
    Case{"camera", kCamera, kCleanup}, Case{"isolation", kIsolation, kCleanup},
    Case{"unload", kUnload, kCleanup}};
}

std::span<const Case> Catalog() { return kCatalog; }

const Case *FindCase(std::string_view a_name) {
  const auto found = std::ranges::find(kCatalog, a_name, &Case::name);
  return found == kCatalog.end() ? nullptr : &*found;
}
}
