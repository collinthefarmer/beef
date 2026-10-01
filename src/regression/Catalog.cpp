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

inline constexpr std::array<Step, 7> kCleanup{
    DespawnCrowd{},
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

inline constexpr std::array<Step, 4> kLoadIdle{
    Solo{kSoloRecipe}, LoadDuring{QueuedWork::kNothing, kSoloRecipe},
    AwaitBaseline{Role::kPlayer}, AwaitIdle{}};

inline constexpr std::array<Step, 7> kLoadApply{
    Solo{kSoloRecipe},
    kPlayerFixture,
    AwaitRendered{Role::kPlayer},
    LoadDuring{QueuedWork::kApply, kSoloRecipe},
    ExpectAborted{},
    AwaitBaseline{Role::kPlayer},
    AwaitIdle{}};

inline constexpr std::array<Step, 7> kLoadEdit{
    Solo{kSoloRecipe},
    kPlayerFixture,
    AwaitRendered{Role::kPlayer},
    LoadDuring{QueuedWork::kEdit, kSoloRecipe},
    ExpectSettled{TrackedWork::kEdit},
    AwaitBaseline{Role::kPlayer},
    AwaitIdle{}};

inline constexpr std::array<Step, 9> kLoadGesture{
    Solo{kSoloRecipe},
    kPlayerFixture,
    AwaitRendered{Role::kPlayer},
    BeginSession{Session::kGesture, kSoloRecipe},
    AwaitSessionActive{Session::kGesture},
    LoadDuring{QueuedWork::kNothing, kSoloRecipe},
    ExpectCancelled{TrackedWork::kGesture},
    AwaitBaseline{Role::kPlayer},
    AwaitIdle{}};

inline constexpr std::array<Step, 8> kLoadPaint{
    Solo{kSoloRecipe},
    kPlayerFixture,
    AwaitRendered{Role::kPlayer},
    BeginSession{Session::kPaint, kSoloRecipe},
    AwaitSessionActive{Session::kPaint},
    LoadDuring{QueuedWork::kNothing, kSoloRecipe},
    AwaitBaseline{Role::kPlayer},
    AwaitIdle{}};

inline constexpr float kScratchOpacity = 0.25f;

inline constexpr std::array<Step, 9> kStudioSave{
    DeleteScratch{},
    DuplicateToScratch{kSoloRecipe},
    SetScratchOpacity{kScratchOpacity},
    SaveScratch{},
    ExpectScratch{kScratchOpacity},
    Solo{kScratchRecipe},
    kPlayerFixture,
    AwaitRendered{Role::kPlayer},
    Unequip{Role::kPlayer, Item::kFixture}};

inline constexpr std::array<Step, 6> kStudioReload{
    ExpectScratch{kScratchOpacity},
    Solo{kScratchRecipe},
    kPlayerFixture,
    AwaitRendered{Role::kPlayer},
    Unequip{Role::kPlayer, Item::kFixture},
    DeleteScratch{}};

inline constexpr std::array<Step, 8> kStudioCleanup{
    DespawnCrowd{},
    ReturnToStart{},
    SetView{View::kThirdPerson},
    Despawn{Role::kWearer},
    Despawn{Role::kControl},
    Remove{Role::kPlayer, Item::kFixture},
    RestoreView{},
    DeleteScratch{}};

inline constexpr std::uint32_t kCrowdSize = 12;
inline constexpr std::uint32_t kBaselineSeconds = 30;
inline constexpr std::uint32_t kRecoverySeconds = 30;

inline constexpr std::uint32_t kSettleSeconds = 30;

consteval std::array<Step, 10> Soak(std::uint32_t a_steadySeconds) {
  return {Solo{kSoloRecipe},
          HoldFor{"baseline", kBaselineSeconds},
          BeginWindow{"burst"},
          SpawnCrowd{kCrowdSize},
          AwaitCrowdRendered{},
          EndWindow{"burst"},
          HoldFor{"settle", kSettleSeconds},
          HoldFor{"steady", a_steadySeconds},
          DespawnCrowd{},
          HoldFor{"recovery", kRecoverySeconds}};
}
inline constexpr std::array<Step, 10> kSoak = Soak(120);
inline constexpr std::array<Step, 10> kSoakHour = Soak(3600);

inline constexpr std::array<Case, 14> kCatalog{
    Case{"lifecycle", kLifecycle, kCleanup},
    Case{"equip-cycle", kEquipCycle, kCleanup},
    Case{"camera", kCamera, kCleanup},
    Case{"isolation", kIsolation, kCleanup},
    Case{"unload", kUnload, kCleanup},
    Case{"load-idle", kLoadIdle, kCleanup},
    Case{"load-apply", kLoadApply, kCleanup},
    Case{"load-edit", kLoadEdit, kCleanup},
    Case{"load-gesture", kLoadGesture, kCleanup},
    Case{"load-paint", kLoadPaint, kCleanup},
    Case{"studio-save", kStudioSave, kCleanup},
    Case{"studio-reload", kStudioReload, kStudioCleanup},
    Case{"soak", kSoak, kCleanup},
    Case{"soak-hour", kSoakHour, kCleanup}};
}

std::span<const Case> Catalog() { return kCatalog; }

const Case *FindCase(std::string_view a_name) {
  const auto found = std::ranges::find(kCatalog, a_name, &Case::name);
  return found == kCatalog.end() ? nullptr : &*found;
}
}
