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
inline constexpr std::array<std::string_view, 3> kDemoRecipes{
    "arcane-circuit", "resonant-ward", "winterglass"};
inline constexpr ShowRecipes kDemoSet{kDemoRecipes};
inline constexpr ShowAllRecipes kShowAll{};
inline constexpr Solo kSoloWard{"resonant-ward"};
inline constexpr Solo kSoloFrost{"winterglass"};
inline constexpr Equip kPlayerFixture{Role::kPlayer, Item::kFixture};
inline constexpr Equip kWearerFixture{Role::kWearer, Item::kFixture};
inline constexpr Equip kControlPlain{Role::kControl, Item::kPlainCuirass};
inline constexpr Equip kPlayerGauntlets{Role::kPlayer, Item::kPairedGauntlets};

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
             Remove{Role::kPlayer, Item::kFixture},
             Remove{Role::kPlayer, Item::kPairedGauntlets}, RestoreSolo{});

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

inline constexpr std::array kUnequipOne =
    Sequence(kSoloCircuit, kPlayerFixture, AwaitRendered{Role::kPlayer},
             kPlayerGauntlets, AwaitLiveItems{Role::kPlayer, 2},
             Unequip{Role::kPlayer, Item::kPairedGauntlets},
             AwaitLiveItems{Role::kPlayer, 1}, ExpectEffect{Role::kPlayer},
             kPlayerGauntlets, AwaitLiveItems{Role::kPlayer, 2},
             Unequip{Role::kPlayer, Item::kFixture},
             AwaitLiveItems{Role::kPlayer, 1}, ExpectEffect{Role::kPlayer});

inline constexpr std::array kCamera = Sequence(
    kSoloCircuit, kPlayerFixture, AwaitRendered{Role::kPlayer},
    WaitFrames{kViewSettleFrames}, SetCamera{Camera::kFirstPerson},
    HoldCamera{Camera::kFirstPerson, kViewSettleFrames},
    ExpectFirstPersonEffect{}, SetCamera{Camera::kThirdPerson},
    HoldCamera{Camera::kThirdPerson, kViewSettleFrames},
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
inline constexpr std::uint32_t kIdleCameraSeconds = 3;
inline constexpr std::uint32_t kOrbitSeconds = 120;

inline constexpr SpawnCrowd kMannequins{kCrowdSize};
inline constexpr SpawnCrowd kMannequinsInVanilla{kCrowdSize, Body::kMannequin,
                                                 Dress::kVanillaCuirasses};
inline constexpr SpawnCrowd kMannequinsEnchanted{kCrowdSize, Body::kMannequin,
                                                 Dress::kEnchantedCuirasses};
inline constexpr SpawnCrowd kMannequinsInOneEnchanted{
    kCrowdSize, Body::kMannequin, Dress::kOneEnchantedCuirass};
inline constexpr SpawnCrowd kMannequinsEnchantedAhead{
    kCrowdSize, Body::kMannequin, Dress::kEnchantedCuirasses, Spot::kAhead};
inline constexpr SpawnCrowd kIdleNpcs{kCrowdSize, Body::kIdleNpc};
inline constexpr SpawnCrowd kFighters{kCrowdSize, Body::kFighter};

consteval std::array<Step, 7> CrowdArrives(Step a_view, SpawnCrowd a_crowd) {
  return Sequence(a_view,
                  HoldWindow{"baseline", kBaselineSeconds, 0, HoldRole::kEdge},
                  BeginWindow{"burst"}, a_crowd, AwaitCrowdRendered{},
                  EndWindow{"burst"}, HoldWindow{"settle", kSettleSeconds});
}

inline constexpr std::array kCrowdLeaves =
    Sequence(DespawnCrowd{},
             HoldWindow{"recovery", kRecoverySeconds, 0, HoldRole::kEdge});

consteval std::array<Step, 10> Soak(Step a_view, SpawnCrowd a_crowd,
                                    std::uint32_t a_steadySeconds) {
  return Joined(
      CrowdArrives(a_view, a_crowd),
      Sequence(HoldWindow{"steady", a_steadySeconds, 0, HoldRole::kMeasured}),
      kCrowdLeaves);
}
inline constexpr std::array kSoak = Soak(kSoloCircuit, kMannequins, 120);
inline constexpr std::array kSoakHour = Soak(kSoloCircuit, kMannequins, 3600);
inline constexpr std::array kSoakStack = Soak(kDemoSet, kMannequins, 120);
inline constexpr std::array kSoakWard = Soak(kSoloWard, kMannequins, 120);
inline constexpr std::array kSoakFrost = Soak(kSoloFrost, kMannequins, 120);
inline constexpr std::array kSoakLarge =
    Soak(kSoloCircuit, SpawnCrowd{kLargeCrowdSize}, 120);
inline constexpr std::array kSoakVanilla =
    Soak(kSoloCircuit, kMannequinsInVanilla, 120);
inline constexpr std::array kSoakEnchanted =
    Soak(kShowAll, kMannequinsEnchanted, 120);
inline constexpr std::array kSoakEnchantedOne =
    Soak(kShowAll, kMannequinsInOneEnchanted, 120);
inline constexpr std::array kSoakDistance =
    Soak(kShowAll, kMannequinsEnchantedAhead, 120);
inline constexpr std::array kSoakIdle = Soak(kSoloCircuit, kIdleNpcs, 120);
inline constexpr std::array kSoakFight =
    Joined(CrowdArrives(kDemoSet, kFighters),
           Sequence(StartCrowdFight{}, AwaitCrowdFighting{}, AwaitCrowdHurt{},
                    HoldWindow{"fight", kFightSeconds, 0, HoldRole::kMeasured}),
           kCrowdLeaves);

inline constexpr std::uint32_t kCullCrowdSize = 6;

inline constexpr std::array kCull =
    Sequence(kSoloCircuit,
             SpawnCrowd{kCullCrowdSize, Body::kMannequin, Dress::kDemoCuirass,
                        Spot::kBehind},
             AwaitCrowdOutOfView{}, TurnAround{}, AwaitCrowdRendered{},
             TurnAround{}, AwaitCrowdSkipped{});

inline constexpr std::array kSoakOrbit =
    Joined(CrowdArrives(kSoloCircuit, kMannequins),
           Sequence(AllowIdleCamera{kIdleCameraSeconds}, AwaitIdleOrbit{},
                    HoldWindow{"orbit", kOrbitSeconds, 0, HoldRole::kMeasured},
                    HoldIdleCamera{}),
           kCrowdLeaves);

inline constexpr std::array kSoakFightOrbit =
    Joined(CrowdArrives(kDemoSet, kFighters),
           Sequence(StartCrowdFight{}, AwaitCrowdFighting{}, AwaitCrowdHurt{},
                    AllowIdleCamera{kIdleCameraSeconds}, AwaitIdleOrbit{},
                    HoldWindow{"orbit", kOrbitSeconds, 0, HoldRole::kMeasured},
                    HoldIdleCamera{}),
           kCrowdLeaves);

constexpr std::array<Step, 7> Wave(std::uint32_t a_cycle) {
  return Sequence(BeginWindow{"wave", a_cycle}, SpawnCrowd{kCrowdSize},
                  AwaitCrowdRendered{}, EndWindow{"wave", a_cycle},
                  HoldWindow{"held", kWaveHoldSeconds, a_cycle}, DespawnCrowd{},
                  HoldWindow{"gap", kWaveGapSeconds, a_cycle});
}
inline constexpr std::array kSoakWaves =
    Joined(Sequence(kSoloCircuit, HoldWindow{"baseline", kBaselineSeconds, 0,
                                             HoldRole::kEdge}),
           Cycles<3>(Wave));

constexpr std::array<Step, 6> Churn(std::uint32_t a_cycle) {
  return Sequence(BeginWindow{"churn", a_cycle}, UnequipCrowd{},
                  AwaitCrowdBare{}, EquipCrowd{}, AwaitCrowdRendered{},
                  EndWindow{"churn", a_cycle});
}
inline constexpr std::array kSoakChurn = Joined(
    CrowdArrives(kSoloCircuit, kMannequins), Cycles<4>(Churn),
    Sequence(HoldWindow{"steady", kChurnSteadySeconds, 0, HoldRole::kMeasured}),
    kCrowdLeaves);

inline constexpr std::array kCatalog{
    Case{"lifecycle", kLifecycle, kCleanup},
    Case{"equip-cycle", kEquipCycle, kCleanup},
    Case{"unequip-one", kUnequipOne, kCleanup},
    Case{"camera", kCamera, kCleanup},
    Case{"cull", kCull, kCleanup},
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
    Case{"soak-ward", kSoakWard, kCleanup},
    Case{"soak-frost", kSoakFrost, kCleanup},
    Case{"soak-large", kSoakLarge, kCleanup},
    Case{"soak-waves", kSoakWaves, kCleanup},
    Case{"soak-churn", kSoakChurn, kCleanup},
    Case{"soak-vanilla", kSoakVanilla, kCleanup},
    Case{"soak-enchanted", kSoakEnchanted, kCleanup},
    Case{"soak-enchanted-one", kSoakEnchantedOne, kCleanup},
    Case{"soak-distance", kSoakDistance, kCleanup},
    Case{"soak-idle", kSoakIdle, kCleanup},
    Case{"soak-fight", kSoakFight, kCleanup},
    Case{"soak-orbit", kSoakOrbit, kCleanup},
    Case{"soak-fight-orbit", kSoakFightOrbit, kCleanup}};
}

std::span<const Case> Catalog() { return kCatalog; }

const Case *FindCase(std::string_view a_name) {
  const auto found = std::ranges::find(kCatalog, a_name, &Case::name);
  return found == kCatalog.end() ? nullptr : &*found;
}
}
