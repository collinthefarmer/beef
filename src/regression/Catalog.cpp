// GPL-3.0-only with the additional permission in COPYING.md.
#include "regression/Steps.h"

#include <algorithm>
#include <array>
#include <tuple>
#include <type_traits>

namespace BetterEnchantmentEffects::Regression {
namespace {
inline constexpr std::string_view kSoloRecipe = "arcane-circuit";
inline constexpr std::size_t kEquipCycles = 5;
inline constexpr std::uint32_t kControlHoldFrames = 120;
inline constexpr std::uint32_t kViewSettleFrames = 60;

inline constexpr Solo kSoloCircuit{kSoloRecipe};
inline constexpr Solo kEveryRecipe{""};
inline constexpr Equip kPlayerFixture{Role::kPlayer, Item::kFixture};
inline constexpr Equip kWearerFixture{Role::kWearer, Item::kFixture};
inline constexpr Equip kControlPlain{Role::kControl, Item::kPlainCuirass};

template <class... Kinds>
constexpr std::array<Step, sizeof...(Kinds)> Sequence(Kinds... a_steps) {
  return {Step{a_steps}...};
}

template <std::size_t... Sizes>
consteval std::array<Step, (Sizes + ...)>
Joined(const std::array<Step, Sizes> &...a_parts) {
  std::array<Step, (Sizes + ...)> joined{};
  std::size_t at = 0;
  ((std::ranges::copy(a_parts, joined.begin() + at), at += Sizes), ...);
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

template <class CycleSteps>
inline constexpr std::size_t kCycleSize =
    std::tuple_size_v<std::invoke_result_t<CycleSteps, std::uint32_t>>;

template <std::size_t Times, class CycleSteps>
consteval std::array<Step, kCycleSize<CycleSteps> * Times>
Cycles(CycleSteps a_cycle) {
  std::array<Step, kCycleSize<CycleSteps> * Times> cycles{};
  for (std::uint32_t cycle = 1; cycle <= Times; ++cycle) {
    std::ranges::copy(a_cycle(cycle),
                      cycles.begin() + (cycle - 1) * kCycleSize<CycleSteps>);
  }
  return cycles;
}

inline constexpr std::array kCleanup =
    Sequence(DespawnCrowd{}, ReturnToStart{}, SetCamera{Camera::kThirdPerson},
             Despawn{Role::kWearer}, Despawn{Role::kControl},
             Remove{Role::kPlayer, Item::kFixture}, RestoreSolo{});

inline constexpr std::array kLifecycle =
    Sequence(kSoloCircuit, kPlayerFixture, Apply{Role::kPlayer},
             Retire{Role::kPlayer}, Apply{Role::kPlayer});

inline constexpr std::array kRenderedOnPlayer =
    Sequence(kSoloCircuit, kPlayerFixture, AwaitRendered{Role::kPlayer});
inline constexpr std::array kReequip = Sequence(
    Unequip{Role::kPlayer, Item::kFixture}, AwaitBaseline{Role::kPlayer},
    kPlayerFixture, AwaitRendered{Role::kPlayer});
inline constexpr std::array kEquipCycle =
    Joined(kRenderedOnPlayer, Repeated<kEquipCycles>(kReequip));

inline constexpr std::array kCamera = Sequence(
    kSoloCircuit, kPlayerFixture, AwaitRendered{Role::kPlayer},
    SetCamera{Camera::kFirstPerson}, WaitFrames{kViewSettleFrames},
    SetCamera{Camera::kThirdPerson}, WaitFrames{kViewSettleFrames},
    ExpectEffect{Role::kPlayer}, Unequip{Role::kPlayer, Item::kFixture},
    AwaitBaseline{Role::kPlayer});

inline constexpr std::array kIsolation = Sequence(
    kSoloCircuit, Spawn{Role::kWearer}, Spawn{Role::kControl}, kPlayerFixture,
    kWearerFixture, kControlPlain, AwaitRendered{Role::kPlayer},
    AwaitRendered{Role::kWearer},
    HoldUntouched{Role::kControl, kControlHoldFrames}, Retire{Role::kWearer},
    AwaitBaseline{Role::kWearer}, ExpectEffect{Role::kPlayer},
    Apply{Role::kWearer}, ExpectEffect{Role::kWearer},
    Unequip{Role::kWearer, Item::kFixture}, AwaitBaseline{Role::kWearer},
    ExpectEffect{Role::kPlayer},
    HoldUntouched{Role::kControl, kControlHoldFrames});

inline constexpr std::array kUnload = Sequence(
    kSoloCircuit, kPlayerFixture, Spawn{Role::kWearer}, kWearerFixture,
    AwaitRendered{Role::kWearer}, Disable{Role::kWearer},
    AwaitRetired{Role::kWearer}, Enable{Role::kWearer}, kWearerFixture,
    AwaitRendered{Role::kWearer}, LeaveStart{}, AwaitRetired{Role::kWearer},
    ExpectEffect{Role::kPlayer}, ReturnToStart{}, kWearerFixture,
    AwaitRendered{Role::kWearer}, ExpectEffect{Role::kPlayer});

inline constexpr std::array kLoadIdle =
    Sequence(kSoloCircuit, LoadDuring{QueuedWork::kNothing, kSoloRecipe},
             AwaitBaseline{Role::kPlayer}, AwaitIdle{});

inline constexpr std::array kLoadApply =
    Sequence(kSoloCircuit, kPlayerFixture, AwaitRendered{Role::kPlayer},
             LoadDuring{QueuedWork::kApply, kSoloRecipe}, ExpectAborted{},
             AwaitBaseline{Role::kPlayer}, AwaitIdle{});

inline constexpr std::array kLoadEdit =
    Sequence(kSoloCircuit, kPlayerFixture, AwaitRendered{Role::kPlayer},
             LoadDuring{QueuedWork::kEdit, kSoloRecipe},
             ExpectSettled{TrackedWork::kEdit}, AwaitBaseline{Role::kPlayer},
             AwaitIdle{});

inline constexpr std::array kLoadGesture =
    Sequence(kSoloCircuit, kPlayerFixture, AwaitRendered{Role::kPlayer},
             BeginSession{Session::kGesture, kSoloRecipe},
             AwaitSessionActive{Session::kGesture},
             LoadDuring{QueuedWork::kNothing, kSoloRecipe},
             ExpectCancelled{TrackedWork::kGesture},
             AwaitBaseline{Role::kPlayer}, AwaitIdle{});

inline constexpr std::array kLoadPaint =
    Sequence(kSoloCircuit, kPlayerFixture, AwaitRendered{Role::kPlayer},
             BeginSession{Session::kPaint, kSoloRecipe},
             AwaitSessionActive{Session::kPaint},
             LoadDuring{QueuedWork::kNothing, kSoloRecipe},
             AwaitBaseline{Role::kPlayer}, AwaitIdle{});

inline constexpr float kScratchOpacity = 0.25f;

inline constexpr std::array kStudioSave = Sequence(
    DeleteScratch{}, DuplicateToScratch{kSoloRecipe},
    SetScratchOpacity{kScratchOpacity}, SaveScratch{},
    ExpectScratch{kScratchOpacity}, Solo{kScratchRecipe}, kPlayerFixture,
    AwaitRendered{Role::kPlayer}, Unequip{Role::kPlayer, Item::kFixture});

inline constexpr std::array kStudioReload =
    Sequence(ExpectScratch{kScratchOpacity}, Solo{kScratchRecipe},
             kPlayerFixture, AwaitRendered{Role::kPlayer},
             Unequip{Role::kPlayer, Item::kFixture}, DeleteScratch{});

inline constexpr std::array kStudioCleanup =
    Joined(kCleanup, Sequence(DeleteScratch{}));

inline constexpr std::uint32_t kCrowdSize = 12;
inline constexpr std::uint32_t kLargeCrowdSize = 32;
inline constexpr std::uint32_t kBaselineSeconds = 30;
inline constexpr std::uint32_t kSettleSeconds = 30;
inline constexpr std::uint32_t kRecoverySeconds = 30;
inline constexpr std::uint32_t kWaveHoldSeconds = 15;
inline constexpr std::uint32_t kWaveGapSeconds = 10;
inline constexpr std::uint32_t kChurnSteadySeconds = 60;

inline constexpr std::uint32_t kFightSeconds = 120;

inline constexpr SpawnCrowd kMannequins{kCrowdSize};
inline constexpr SpawnCrowd kMannequinsInVanilla{kCrowdSize, Body::kMannequin,
                                                 Dress::kVanillaCuirasses};
inline constexpr SpawnCrowd kIdleNpcs{kCrowdSize, Body::kIdleNpc};

consteval std::array<Step, 7> CrowdArrives(Solo a_view, SpawnCrowd a_crowd) {
  return Sequence(a_view, HoldWindow{"baseline", kBaselineSeconds},
                  BeginWindow{"burst"}, a_crowd, AwaitCrowdRendered{},
                  EndWindow{"burst"}, HoldWindow{"settle", kSettleSeconds});
}

inline constexpr std::array kCrowdLeaves =
    Sequence(DespawnCrowd{}, HoldWindow{"recovery", kRecoverySeconds});

consteval std::array<Step, 10> Soak(Solo a_view, SpawnCrowd a_crowd,
                                    std::uint32_t a_steadySeconds) {
  return Joined(CrowdArrives(a_view, a_crowd),
                Sequence(HoldWindow{"steady", a_steadySeconds}), kCrowdLeaves);
}
inline constexpr std::array kSoak = Soak(kSoloCircuit, kMannequins, 120);
inline constexpr std::array kSoakHour = Soak(kSoloCircuit, kMannequins, 3600);
inline constexpr std::array kSoakStack = Soak(kEveryRecipe, kMannequins, 120);
inline constexpr std::array kSoakLarge =
    Soak(kSoloCircuit, SpawnCrowd{kLargeCrowdSize}, 120);
inline constexpr std::array kSoakVanilla =
    Soak(kSoloCircuit, kMannequinsInVanilla, 120);
inline constexpr std::array kSoakIdle = Soak(kSoloCircuit, kIdleNpcs, 120);
inline constexpr std::array kSoakFight =
    Joined(CrowdArrives(kEveryRecipe, kIdleNpcs),
           Sequence(StartCrowdFight{}, AwaitCrowdFighting{},
                    HoldWindow{"fight", kFightSeconds}),
           kCrowdLeaves);

constexpr std::array<Step, 7> Wave(std::uint32_t a_cycle) {
  return Sequence(BeginWindow{"wave", a_cycle}, SpawnCrowd{kCrowdSize},
                  AwaitCrowdRendered{}, EndWindow{"wave", a_cycle},
                  HoldWindow{"held", kWaveHoldSeconds, a_cycle}, DespawnCrowd{},
                  HoldWindow{"gap", kWaveGapSeconds, a_cycle});
}
inline constexpr std::array kSoakWaves =
    Joined(Sequence(kSoloCircuit, HoldWindow{"baseline", kBaselineSeconds}),
           Cycles<3>(Wave));

constexpr std::array<Step, 6> Churn(std::uint32_t a_cycle) {
  return Sequence(BeginWindow{"churn", a_cycle}, UnequipCrowd{},
                  AwaitCrowdBare{}, EquipCrowd{}, AwaitCrowdRendered{},
                  EndWindow{"churn", a_cycle});
}
inline constexpr std::array kSoakChurn =
    Joined(CrowdArrives(kSoloCircuit, kMannequins), Cycles<4>(Churn),
           Sequence(HoldWindow{"steady", kChurnSteadySeconds}), kCrowdLeaves);

inline constexpr std::array kCatalog{
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
    Case{"soak-hour", kSoakHour, kCleanup},
    Case{"soak-stack", kSoakStack, kCleanup},
    Case{"soak-large", kSoakLarge, kCleanup},
    Case{"soak-waves", kSoakWaves, kCleanup},
    Case{"soak-churn", kSoakChurn, kCleanup},
    Case{"soak-vanilla", kSoakVanilla, kCleanup},
    Case{"soak-idle", kSoakIdle, kCleanup},
    Case{"soak-fight", kSoakFight, kCleanup}};
}

std::span<const Case> Catalog() { return kCatalog; }

const Case *FindCase(std::string_view a_name) {
  const auto found = std::ranges::find(kCatalog, a_name, &Case::name);
  return found == kCatalog.end() ? nullptr : &*found;
}
}
