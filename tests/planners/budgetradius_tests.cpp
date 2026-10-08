// GPL-3.0-only with the additional permission in COPYING.md.
#include "Core.h"
#include "planners/BudgetRadius.h"
#include "planners/Eviction.h"
#include "test_support.h"

#include <limits>
#include <optional>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Equal;

namespace {
BudgetActor At(std::optional<float> a_distance, std::uint64_t a_planned = 0) {
  BudgetActor actor;
  actor.distance = a_distance;
  actor.plannedBytes = a_planned;
  return actor;
}

std::vector<BudgetActor> Numbered(std::vector<BudgetActor> a_actors) {
  for (std::size_t i = 0; i < a_actors.size(); ++i) {
    a_actors[i].actor = static_cast<std::uint32_t>(i + 1);
  }
  return a_actors;
}

std::size_t Evicted(std::span<const BudgetActor> a_actors, float a_radius) {
  std::size_t count = 0;
  for (const BudgetActor &actor : a_actors) {
    if (actor.distance && EvictionFor(*actor.distance, a_radius, true) ==
                              EvictionAction::kEvict) {
      ++count;
    }
  }
  return count;
}

void ShrinkEvictsTheFarthest() {
  const std::vector<BudgetActor> actors =
      Numbered({At(1000.0f), At(3000.0f, 700), At(2990.0f), At(500.0f)});
  const std::optional<RadiusShrink> shrink =
      ShrinkRadius(actors, std::nullopt, 42);
  Check(shrink.has_value(), "pressure with installed NPCs shrinks a radius");
  Equal(shrink->actor, std::uint32_t{2}, "the farthest NPC is evicted");
  Equal(shrink->freed, std::uint64_t{700},
        "the shrink frees its planned bytes");
  Equal(shrink->radius.sinceMs, std::uint32_t{42}, "the shrink starts a dwell");
  Check(EvictionFor(3000.0f, shrink->radius.distance, true) ==
            EvictionAction::kEvict,
        "the radius evicts the farthest NPC");
  Equal(Evicted(actors, shrink->radius.distance), std::size_t{1},
        "the radius evicts exactly one NPC");

  const std::vector<BudgetActor> spread{At(1000.0f), At(4000.0f)};
  const std::optional<RadiusShrink> near =
      ShrinkRadius(spread, std::nullopt, 0);
  Check(near && near->radius.distance == 4000.0f * kRadiusShrinkFactor,
        "the radius sits just inside the evicted NPC");

  const BudgetRadius current{2995.0f, 0};
  const std::optional<RadiusShrink> next = ShrinkRadius(actors, current, 7);
  Check(next && next->actor == 3,
        "an NPC beyond the current radius is already leaving");
  Check(next && next->radius.distance < current.distance,
        "a second shrink only lowers the radius");
  Equal(Evicted(actors, next->radius.distance), std::size_t{2},
        "the second shrink evicts one NPC more");
}

void PlayerAndResizesAreNeverCandidates() {
  BudgetActor player = At(std::nullopt, 900);
  player.role = ActorRole::kPlayer;
  const std::vector<BudgetActor> alone{player};
  Check(!ShrinkRadius(alone, std::nullopt, 0),
        "the player alone leaves nothing to shrink");

  BudgetActor farPlayer = At(9000.0f);
  farPlayer.role = ActorRole::kPlayer;
  const std::vector<BudgetActor> withPlayer =
      Numbered({farPlayer, At(1000.0f)});
  const std::optional<RadiusShrink> shrink =
      ShrinkRadius(withPlayer, std::nullopt, 0);
  Check(shrink && shrink->actor == 2,
        "a player with a distance is still never a candidate");

  BudgetActor resizing = At(5000.0f);
  resizing.resizing = true;
  const std::vector<BudgetActor> busy{resizing};
  Check(!ShrinkRadius(busy, std::nullopt, 0),
        "an actor mid-resize is not a candidate");
  const std::vector<BudgetActor> beyond{At(1000.0f), resizing};
  Check(!ShrinkRadius(beyond, std::nullopt, 0),
        "no radius may evict an actor mid-resize beyond the candidate");
  resizing.distance = 500.0f;
  const std::vector<BudgetActor> inside = Numbered({At(1000.0f), resizing});
  const std::optional<RadiusShrink> kept =
      ShrinkRadius(inside, std::nullopt, 0);
  Check(kept && kept->actor == 1 && Evicted(inside, kept->radius.distance) == 1,
        "an actor mid-resize inside the radius keeps its effect");
}

void GarbageDistancesAreSkipped() {
  const std::vector<BudgetActor> garbage{
      At(std::numeric_limits<float>::quiet_NaN()), At(-50.0f), At(0.0f),
      At(std::numeric_limits<float>::infinity()), At(std::nullopt)};
  Check(!ShrinkRadius(garbage, std::nullopt, 0),
        "NaN, negative, zero, infinite and missing distances give no "
        "candidate");
  std::vector<BudgetActor> mixed = garbage;
  mixed.push_back(At(800.0f));
  mixed = Numbered(std::move(mixed));
  const std::optional<RadiusShrink> shrink =
      ShrinkRadius(mixed, std::nullopt, 0);
  Check(shrink && shrink->actor == mixed.size() &&
            shrink->radius.distance > 0.0f,
        "a usable distance among garbage gives a positive radius");

  const std::vector<BudgetActor> single = Numbered({At(2500.0f)});
  const std::optional<RadiusShrink> one = ShrinkRadius(single, std::nullopt, 0);
  Check(one && one->actor == 1 && one->radius.distance > 0.0f &&
            one->radius.distance < 2500.0f,
        "a single NPC gets a positive radius just inside it");
  Check(!ShrinkRadius({}, std::nullopt, 0), "no actors give no shrink");
}

void GrowthWaitsForTheDwell() {
  const BudgetRadius radius{1000.0f, 5000};
  const RadiusGrowth early = GrowRadius(
      radius, Pressure::kNone, 5000 + kRadiusDwellMs - 1, 0.0f, 9000.0f);
  Check(early == RadiusGrowth{RadiusUnchanged{radius}},
        "the radius is unchanged until the dwell has passed");
  const RadiusGrowth due =
      GrowRadius(radius, Pressure::kNone, 5000 + kRadiusDwellMs, 0.0f, 9000.0f);
  const RadiusGrown *grown = Get<RadiusGrown>(due);
  Check(grown && grown->radius.distance == 1000.0f / kRestoreFraction &&
            grown->radius.sinceMs == 5000 + kRadiusDwellMs,
        "after the dwell the radius grows by 1 / 0.8 and restarts the dwell");
  const RadiusGrowth wrapped =
      GrowRadius({1000.0f, 0xFFFFFF00u}, Pressure::kNone, 100, 0.0f, 9000.0f);
  Check(Is<RadiusUnchanged>(wrapped), "the dwell survives a clock wrap");
}

void GrowthStopsUnderPressure() {
  const BudgetRadius radius{1000.0f, 0};
  for (const Pressure pressure : {Pressure::kOver, Pressure::kOverWarned}) {
    const RadiusGrowth growth =
        GrowRadius(radius, pressure, 60000, 4000.0f, std::nullopt);
    const RadiusUnchanged *unchanged = Get<RadiusUnchanged>(growth);
    Check(unchanged && unchanged->radius.distance == 1000.0f &&
              unchanged->radius.sinceMs == 60000,
          "pressure keeps the radius and restarts the dwell");
  }
}

void RadiusClearsWhenGrowthReachesTheLimit() {
  const RadiusGrowth reaches = GrowRadius({3200.0f, 0}, Pressure::kNone,
                                          kRadiusDwellMs, 4000.0f, 100.0f);
  Check(Is<RadiusCleared>(reaches),
        "a radius that reaches EvictDistance is cleared");
  const RadiusGrowth below = GrowRadius({3000.0f, 0}, Pressure::kNone,
                                        kRadiusDwellMs, 4000.0f, 100.0f);
  Check(Is<RadiusGrown>(below),
        "EvictDistance, not the farthest NPC, limits a set distance");
  const RadiusGrowth farthest =
      GrowRadius({2000.0f, 0}, Pressure::kNone, kRadiusDwellMs, 0.0f, 2400.0f);
  Check(Is<RadiusCleared>(farthest),
        "without EvictDistance the farthest loaded NPC limits the radius");
  const RadiusGrowth unlimited =
      GrowRadius({1000.0f, 0}, Pressure::kNone, kRadiusDwellMs, -1.0f, 2400.0f);
  Check(Is<RadiusGrown>(unlimited), "a negative EvictDistance sets no limit");
  const RadiusGrowth none = GrowRadius({1000.0f, 0}, Pressure::kNone,
                                       kRadiusDwellMs, 0.0f, std::nullopt);
  Check(Is<RadiusCleared>(none),
        "no limit and no loaded NPC clears the radius");
  const RadiusGrowth nan =
      GrowRadius({1000.0f, 0}, Pressure::kNone, kRadiusDwellMs,
                 std::numeric_limits<float>::quiet_NaN(),
                 std::numeric_limits<float>::quiet_NaN());
  Check(Is<RadiusCleared>(nan),
        "a NaN EvictDistance and a NaN farthest NPC clear the radius");
}

void RadiusCombinesWithEvictDistance() {
  const std::optional<BudgetRadius> none;
  Equal(EffectiveEvictDistance(4000.0f, none), 4000.0f,
        "no radius keeps EvictDistance");
  Equal(EffectiveEvictDistance(0.0f, none), 0.0f,
        "no radius keeps eviction off");
  const std::optional<BudgetRadius> radius = BudgetRadius{2500.0f, 0};
  Equal(EffectiveEvictDistance(4000.0f, radius), 2500.0f,
        "a radius inside EvictDistance applies");
  Equal(EffectiveEvictDistance(2000.0f, radius), 2000.0f,
        "EvictDistance inside the radius applies");
  Equal(EffectiveEvictDistance(0.0f, radius), 2500.0f,
        "a radius applies when eviction is off");
  Equal(EffectiveEvictDistance(-5.0f, radius), 2500.0f,
        "a radius applies over a negative EvictDistance");
  Equal(EffectiveEvictDistance(std::numeric_limits<float>::quiet_NaN(), radius),
        2500.0f, "a radius applies over a NaN EvictDistance");
}

void LinesCarryTheNumbers() {
  Equal(RadiusShrinkLine(2970.4f, 0x1A2B, "Vigilant of Stendarr",
                         600 * kMebibyte),
        std::string{"render target budget: effects beyond 2970 units are off: "
                    "00001A2B (Vigilant of Stendarr) is evicted, frees ~600 "
                    "MiB"},
        "the shrink line names the radius, the actor and the bytes");
  Equal(RadiusGrowLine(3713.0f),
        std::string{"render target budget: effects return up to 3713 units"},
        "the grow line names the radius");
  Equal(RadiusClearLine(), std::string{"render target budget: radius cleared"},
        "the clear line is fixed");
}
}

int main() {
  ShrinkEvictsTheFarthest();
  PlayerAndResizesAreNeverCandidates();
  GarbageDistancesAreSkipped();
  GrowthWaitsForTheDwell();
  GrowthStopsUnderPressure();
  RadiusClearsWhenGrowthReachesTheLimit();
  RadiusCombinesWithEvictDistance();
  LinesCarryTheNumbers();
  return test::Finish("planners_budgetradius");
}
