// GPL-3.0-only with the additional permission in COPYING.md.
#include "regression/Steps.h"

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

template <std::size_t First, std::size_t Second>
consteval std::array<Step, First + Second>
Joined(const std::array<Step, First> &a_first,
       const std::array<Step, Second> &a_second) {
  std::array<Step, First + Second> joined{};
  std::ranges::copy(a_first, joined.begin());
  std::ranges::copy(a_second, joined.begin() + First);
  return joined;
}

template <std::size_t Times, std::size_t Size>
consteval std::array<Step, Size * Times>
Repeated(const std::array<Step, Size> &a_steps) {
  std::array<Step, Size * Times> repeated{};
  for (std::size_t time = 0; time < Times; ++time) {
    std::ranges::copy(a_steps, repeated.begin() + time * Size);
  }
  return repeated;
}

inline constexpr std::array<Step, 7> kCleanup{
    DespawnCrowd{},
    ReturnToStart{},
    SetCamera{Camera::kThirdPerson},
    Despawn{Role::kWearer},
    Despawn{Role::kControl},
    Remove{Role::kPlayer, Item::kFixture},
    RestoreSolo{}};

inline constexpr std::array<Step, 5> kLifecycle{
    Solo{kSoloRecipe}, kPlayerFixture, Apply{Role::kPlayer},
    Retire{Role::kPlayer}, Apply{Role::kPlayer}};

inline constexpr std::array<Step, 3> kRenderedOnPlayer{
    Solo{kSoloRecipe}, kPlayerFixture, AwaitRendered{Role::kPlayer}};
inline constexpr std::array<Step, 4> kReequip{
    Unequip{Role::kPlayer, Item::kFixture}, AwaitBaseline{Role::kPlayer},
    kPlayerFixture, AwaitRendered{Role::kPlayer}};
inline constexpr std::array<Step, 3 + 4 * kEquipCycles> kEquipCycle =
    Joined(kRenderedOnPlayer, Repeated<kEquipCycles>(kReequip));

inline constexpr std::array<Step, 10> kCamera{
    Solo{kSoloRecipe},
    kPlayerFixture,
    AwaitRendered{Role::kPlayer},
    SetCamera{Camera::kFirstPerson},
    WaitFrames{kViewSettleFrames},
    SetCamera{Camera::kThirdPerson},
    WaitFrames{kViewSettleFrames},
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
                                              LeaveStart{},
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

inline constexpr std::array<Step, 8> kStudioCleanup =
    Joined(kCleanup, std::array<Step, 1>{DeleteScratch{}});

inline constexpr std::uint32_t kCrowdSize = 12;
inline constexpr std::uint32_t kBaselineSeconds = 30;
inline constexpr std::uint32_t kRecoverySeconds = 30;

inline constexpr std::uint32_t kSettleSeconds = 30;

consteval std::array<Step, 10> Soak(std::uint32_t a_steadySeconds) {
  return {Solo{kSoloRecipe},
          HoldWindow{"baseline", kBaselineSeconds},
          BeginWindow{"burst"},
          SpawnCrowd{kCrowdSize},
          AwaitCrowdRendered{},
          EndWindow{"burst"},
          HoldWindow{"settle", kSettleSeconds},
          HoldWindow{"steady", a_steadySeconds},
          DespawnCrowd{},
          HoldWindow{"recovery", kRecoverySeconds}};
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
