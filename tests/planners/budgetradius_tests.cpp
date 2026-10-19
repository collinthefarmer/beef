// GPL-3.0-only with the additional permission in COPYING.md.
#include "Core.h"
#include "planners/BudgetRadius.h"
#include "planners/Eviction.h"
#include "test_support.h"

#include <limits>
#include <optional>
#include <span>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Equal;

namespace {
BudgetActor At(std::optional<float> a_distance, std::uint64_t a_planned = 0) {
  BudgetActor actor;
  actor.distance = a_distance;
  actor.costs.planned = a_planned;
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
  player.visibility.role = ActorRole::kPlayer;
  const std::vector<BudgetActor> alone{player};
  Check(!ShrinkRadius(alone, std::nullopt, 0),
        "the player alone leaves nothing to shrink");

  BudgetActor farPlayer = At(9000.0f);
  farPlayer.visibility.role = ActorRole::kPlayer;
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

RadiusGrowthRequest GrowthOf(BudgetRadius a_radius, std::uint32_t a_nowMs,
                             float a_evictDistance,
                             std::optional<float> a_farthest,
                             BudgetState a_budget = BudgetState{},
                             std::span<const EvictedActor> a_evicted = {},
                             std::uint64_t a_unpricedBytes = 0) {
  return {a_radius,   a_budget,  a_nowMs,        a_evictDistance,
          a_farthest, a_evicted, a_unpricedBytes};
}

BudgetState Calm(std::uint64_t a_plannedMiB) {
  return {1000 * kMebibyte, a_plannedMiB * kMebibyte, Pressure::kNone};
}

EvictedActor Away(std::optional<float> a_distance,
                  std::optional<std::uint64_t> a_returnMiB) {
  return {a_distance,
          a_returnMiB ? std::optional<std::uint64_t>{*a_returnMiB * kMebibyte}
                      : std::nullopt};
}

void GrowthWaitsForTheDwell() {
  const BudgetRadius radius{1000.0f, 5000};
  const RadiusGrowth early =
      GrowRadius(GrowthOf(radius, 5000 + kRadiusDwellMs - 1, 0.0f, 9000.0f));
  Check(early == RadiusGrowth{RadiusUnchanged{radius}},
        "the radius is unchanged until the dwell has passed");
  const RadiusGrowth due =
      GrowRadius(GrowthOf(radius, 5000 + kRadiusDwellMs, 0.0f, 9000.0f));
  const RadiusGrown *grown = Get<RadiusGrown>(due);
  Check(grown && grown->radius.distance == 1000.0f / kRestoreFraction &&
            grown->radius.sinceMs == 5000 + kRadiusDwellMs,
        "after the dwell the radius grows by 1 / 0.8 and restarts the dwell");
  const RadiusGrowth wrapped =
      GrowRadius(GrowthOf({1000.0f, 0xFFFFFF00u}, 100, 0.0f, 9000.0f));
  Check(Is<RadiusUnchanged>(wrapped), "the dwell survives a clock wrap");
}

void RepeatedShrinksWaitLonger() {
  const std::vector<BudgetActor> actors = Numbered({At(1000.0f), At(2000.0f)});
  const std::optional<RadiusShrink> first =
      ShrinkRadius(actors, std::nullopt, 0);
  const std::optional<RadiusShrink> second =
      first ? ShrinkRadius(actors, first->radius, 10) : std::nullopt;
  Check(first && first->radius.shrinks == 1 && second &&
            second->radius.shrinks == 2,
        "each shrink in one episode counts");
  Equal(DwellAfter(1), kRadiusDwellMs, "a first shrink waits the base dwell");
  Equal(DwellAfter(2), 2 * kRadiusDwellMs,
        "a second shrink waits twice as long");
  Equal(DwellAfter(1000), kMaxRadiusDwellMs, "the dwell has a ceiling");
  const BudgetRadius twice{1000.0f, 5000, false, 2};
  Check(Is<RadiusUnchanged>(
            GrowRadius(GrowthOf(twice, 5000 + kRadiusDwellMs, 0.0f, 9000.0f))),
        "a twice-shrunk radius does not grow after the base dwell");
  const RadiusGrowth due =
      GrowRadius(GrowthOf(twice, 5000 + 2 * kRadiusDwellMs, 0.0f, 9000.0f));
  const RadiusGrown *grown = Get<RadiusGrown>(due);
  Check(grown && grown->radius.shrinks == 2,
        "a twice-shrunk radius grows after the doubled dwell and keeps its "
        "count");
}

void GrowthStopsUnderPressure() {
  const BudgetRadius radius{1000.0f, 0};
  for (const Pressure pressure : {Pressure::kOver, Pressure::kOverWarned}) {
    const RadiusGrowth growth = GrowRadius(GrowthOf(
        radius, 60000, 4000.0f, std::nullopt, BudgetState{{}, 0, pressure}));
    const RadiusUnchanged *unchanged = Get<RadiusUnchanged>(growth);
    Check(unchanged && unchanged->radius.distance == 1000.0f &&
              unchanged->radius.sinceMs == 60000,
          "pressure keeps the radius and restarts the dwell");
  }
}

void RadiusClearsWhenGrowthReachesTheLimit() {
  const RadiusGrowth reaches =
      GrowRadius(GrowthOf({3200.0f, 0}, kRadiusDwellMs, 4000.0f, 100.0f));
  Check(Is<RadiusCleared>(reaches),
        "a radius that reaches EvictDistance is cleared");
  const RadiusGrowth below =
      GrowRadius(GrowthOf({3000.0f, 0}, kRadiusDwellMs, 4000.0f, 100.0f));
  Check(Is<RadiusGrown>(below),
        "EvictDistance, not the farthest NPC, limits a set distance");
  const RadiusGrowth farthest =
      GrowRadius(GrowthOf({2000.0f, 0}, kRadiusDwellMs, 0.0f, 2400.0f));
  Check(Is<RadiusCleared>(farthest),
        "without EvictDistance the farthest loaded NPC limits the radius");
  const RadiusGrowth unlimited =
      GrowRadius(GrowthOf({1000.0f, 0}, kRadiusDwellMs, -1.0f, 2400.0f));
  Check(Is<RadiusGrown>(unlimited), "a negative EvictDistance sets no limit");
  const RadiusGrowth none =
      GrowRadius(GrowthOf({1000.0f, 0}, kRadiusDwellMs, 0.0f, std::nullopt));
  Check(Is<RadiusCleared>(none),
        "no limit and no loaded NPC clears the radius");
  const RadiusGrowth nan = GrowRadius(GrowthOf(
      {1000.0f, 0}, kRadiusDwellMs, std::numeric_limits<float>::quiet_NaN(),
      std::numeric_limits<float>::quiet_NaN()));
  Check(Is<RadiusCleared>(nan),
        "a NaN EvictDistance and a NaN farthest NPC clear the radius");
}

void RingIsWhatGrowthLetsReturn() {
  const std::vector<EvictedActor> evicted{
      Away(799.0f, 1),  Away(800.0f, 2),  Away(900.0f, 3),      Away(999.9f, 4),
      Away(1000.0f, 5), Away(2000.0f, 6), Away(std::nullopt, 7)};
  const std::vector<RingActor> ring = RingOf(evicted, 1000.0f, 1250.0f);
  Equal(ring.size(), std::size_t{3},
        "the ring holds the NPCs from 0.8 x the radius to 0.8 x the grown "
        "radius");
  Check(ring.size() == 3 && ring[0].distance == 800.0f &&
            ring[2].distance == 999.9f,
        "an NPC exactly at 0.8 x the radius is in the ring and one exactly "
        "at 0.8 x the grown radius is not");
  for (const RingActor &actor : ring) {
    Check(!Returns(actor.distance, 1000.0f) && Returns(actor.distance, 1250.0f),
          "every ring NPC is one EvictionFor returns only after the growth");
  }
  const std::vector<EvictedActor> unsorted{Away(950.0f, 1), Away(850.0f, 2)};
  const std::vector<RingActor> sorted = RingOf(unsorted, 1000.0f, 1250.0f);
  Check(sorted.size() == 2 && sorted[0].distance == 850.0f,
        "the ring is ordered nearest first");
  Equal(RingOf(evicted, 1000.0f, 0.0f).size(), std::size_t{5},
        "without a limit after the growth every NPC beyond the radius is in "
        "the ring");
}

void RingRoomKeepsHalfTheBand() {
  const std::optional<RingRoom> room = RingRoomOf(Calm(400));
  Check(room && room->headroom == 600 * kMebibyte &&
            room->margin == BandOf(1000 * kMebibyte) / 2,
        "the room is the headroom less half the pressure band");
  Check(!RingRoomOf(BudgetState{}), "an unknown allowance gives no room");
}

void RingThatFitsGrowsFully() {
  const std::vector<EvictedActor> evicted{Away(850.0f, 100), Away(900.0f, 200)};
  const RadiusGrowth growth = GrowRadius(GrowthOf(
      {1000.0f, 0}, kRadiusDwellMs, 0.0f, 9000.0f, Calm(400), evicted));
  const RadiusGrown *grown = Get<RadiusGrown>(growth);
  Check(grown && grown->radius.distance == 1000.0f / kRestoreFraction &&
            grown->returningBytes == 300 * kMebibyte,
        "a ring that fits the room grows the radius by 1 / 0.8");
}

void EmptyRingGrowsFully() {
  const std::vector<EvictedActor> evicted{Away(500.0f, 900),
                                          Away(3000.0f, 900)};
  const RadiusGrowth growth = GrowRadius(GrowthOf(
      {1000.0f, 0}, kRadiusDwellMs, 0.0f, 9000.0f, Calm(990), evicted));
  const RadiusGrown *grown = Get<RadiusGrown>(growth);
  Check(grown && grown->radius.distance == 1000.0f / kRestoreFraction &&
            grown->returningBytes == 0,
        "an empty ring grows the radius by 1 / 0.8 even without headroom");
}

void RingThatPartlyFitsStopsAtTheFirstThatDoesNot() {
  const std::vector<EvictedActor> evicted{Away(850.0f, 100), Away(900.0f, 200),
                                          Away(950.0f, 300)};
  const RadiusGrowth growth = GrowRadius(GrowthOf(
      {1000.0f, 0}, kRadiusDwellMs, 0.0f, 9000.0f, Calm(400), evicted));
  const RadiusGrown *grown = Get<RadiusGrown>(growth);
  Check(grown != nullptr, "a ring that partly fits still grows");
  if (!grown) {
    return;
  }
  Equal(grown->returningBytes, std::uint64_t{300 * kMebibyte},
        "the growth prices the NPCs that fit");
  Check(Returns(900.0f, grown->radius.distance) &&
            !Returns(950.0f, grown->radius.distance),
        "the radius grows just past the last NPC that fits");
  Check(grown->radius.distance > 1000.0f &&
            grown->radius.distance < 1000.0f / kRestoreFraction,
        "a partial growth lies between the radius and 1 / 0.8 of it");

  const std::vector<EvictedActor> tied{Away(850.0f, 100), Away(900.0f, 200),
                                       Away(900.0f, 200)};
  const RadiusGrowth tiedGrowth = GrowRadius(
      GrowthOf({1000.0f, 0}, kRadiusDwellMs, 0.0f, 9000.0f, Calm(400), tied));
  const RadiusGrown *split = Get<RadiusGrown>(tiedGrowth);
  Check(split && split->returningBytes == 100 * kMebibyte &&
            !Returns(900.0f, split->radius.distance),
        "NPCs at one distance return together or not at all");
}

void RingWhereNothingFitsBlocksTheRadius() {
  const std::vector<EvictedActor> evicted{Away(850.0f, 500), Away(900.0f, 100)};
  const RadiusGrowth growth = GrowRadius(GrowthOf(
      {1000.0f, 0}, kRadiusDwellMs, 0.0f, 9000.0f, Calm(400), evicted));
  const RadiusBlocked *blocked = Get<RadiusBlocked>(growth);
  Check(blocked && blocked->radius.distance == 1000.0f &&
            blocked->radius.sinceMs == kRadiusDwellMs,
        "when the nearest ring NPC does not fit the radius stays and the "
        "dwell restarts");
  Check(blocked && blocked->ringBytes == 600 * kMebibyte &&
            blocked->room.headroom == 600 * kMebibyte &&
            blocked->firstInEpisode,
        "the first block names the ring bytes and the headroom");
  if (!blocked) {
    return;
  }
  const RadiusGrowth again = GrowRadius(GrowthOf(
      blocked->radius, 2 * kRadiusDwellMs, 0.0f, 9000.0f, Calm(400), evicted));
  const RadiusBlocked *repeated = Get<RadiusBlocked>(again);
  Check(repeated && !repeated->firstInEpisode,
        "a later block in the same episode is not reported again");
  const std::vector<EvictedActor> lighter{Away(850.0f, 100)};
  const RadiusGrowth lighterGrowth = GrowRadius(GrowthOf(
      blocked->radius, 2 * kRadiusDwellMs, 0.0f, 9000.0f, Calm(400), lighter));
  const RadiusGrown *grown = Get<RadiusGrown>(lighterGrowth);
  Check(grown && !grown->radius.blockReported,
        "a growth ends the blocked episode");
}

void UnpricedNpcsReturnOnePerGrowth() {
  const std::vector<EvictedActor> evicted{Away(850.0f, std::nullopt),
                                          Away(900.0f, std::nullopt)};
  const RadiusGrowth growth =
      GrowRadius(GrowthOf({1000.0f, 0}, kRadiusDwellMs, 0.0f, 9000.0f,
                          Calm(400), evicted, 50 * kMebibyte));
  const RadiusGrown *grown = Get<RadiusGrown>(growth);
  Check(grown && Returns(850.0f, grown->radius.distance) &&
            !Returns(900.0f, grown->radius.distance) &&
            grown->returningBytes == 50 * kMebibyte,
        "one unpriced NPC per growth returns, priced at the largest recorded "
        "bytes");
  const RadiusGrowth heavy =
      GrowRadius(GrowthOf({1000.0f, 0}, kRadiusDwellMs, 0.0f, 9000.0f,
                          Calm(400), evicted, 500 * kMebibyte));
  Check(Is<RadiusBlocked>(heavy),
        "an unpriced NPC whose largest recorded price does not fit stays out");
  const RadiusGrowth unknownGrowth = GrowRadius(GrowthOf(
      {1000.0f, 0}, kRadiusDwellMs, 0.0f, 9000.0f, Calm(400), evicted));
  const RadiusGrown *unknown = Get<RadiusGrown>(unknownGrowth);
  Check(unknown && !Returns(900.0f, unknown->radius.distance),
        "with no recorded price at all one unpriced NPC still returns");
}

void ClearingPricesTheRing() {
  const std::vector<EvictedActor> evicted{Away(850.0f, 100),
                                          Away(1050.0f, 500)};
  const RadiusGrowth growth = GrowRadius(GrowthOf(
      {1000.0f, 0}, kRadiusDwellMs, 0.0f, 1100.0f, Calm(400), evicted));
  const RadiusGrown *grown = Get<RadiusGrown>(growth);
  Check(grown && Returns(850.0f, grown->radius.distance) &&
            !Returns(1050.0f, grown->radius.distance),
        "a clear that would return more than fits grows to the fitting "
        "radius");
  const RadiusGrowth fits = GrowRadius(
      GrowthOf({1000.0f, 0}, kRadiusDwellMs, 0.0f, 1100.0f, Calm(0), evicted));
  const RadiusCleared *cleared = Get<RadiusCleared>(fits);
  Check(cleared && cleared->returningBytes == 600 * kMebibyte,
        "a clear whose ring fits clears");
  const RadiusGrowth limited = GrowRadius(GrowthOf(
      {1000.0f, 0}, kRadiusDwellMs, 1200.0f, std::nullopt, Calm(400), evicted));
  const RadiusCleared *bounded = Get<RadiusCleared>(limited);
  Check(bounded && bounded->returningBytes == 100 * kMebibyte,
        "a clear at EvictDistance prices only the NPCs inside 0.8 x "
        "EvictDistance");
}

void GarbageDistancesStayOutOfTheRing() {
  const std::vector<EvictedActor> evicted{
      Away(std::numeric_limits<float>::quiet_NaN(), 900), Away(-5.0f, 900),
      Away(std::numeric_limits<float>::infinity(), 900),
      Away(std::nullopt, 900)};
  Check(RingOf(evicted, 1000.0f, 0.0f).empty(),
        "NaN, negative, infinite and missing distances are never in the "
        "ring");
  const RadiusGrowth growth = GrowRadius(GrowthOf(
      {1000.0f, 0}, kRadiusDwellMs, 0.0f, 9000.0f, Calm(990), evicted));
  const RadiusGrown *grown = Get<RadiusGrown>(growth);
  Check(grown && grown->radius.distance == 1000.0f / kRestoreFraction,
        "garbage distances do not block a growth");
  Check(RadiusStoppingAt(950.0f) * kRestoreFraction <= 950.0f,
        "the stopping radius never returns the NPC it stops at");
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
  Equal(RadiusBlockedLine({{1000.0f, 0, true},
                           600 * kMebibyte,
                           {300 * kMebibyte, 128 * kMebibyte},
                           true}),
        std::string{"render target budget: effects stay off beyond 1000 "
                    "units: the returning NPCs plan ~600 MiB, the headroom "
                    "is ~300 MiB less a margin of ~128 MiB"},
        "the blocked line names the radius, the ring and the room");
  Equal(RadiusClearLine(), std::string{"render target budget: radius cleared"},
        "the clear line is fixed");
}
}

int main() {
  ShrinkEvictsTheFarthest();
  PlayerAndResizesAreNeverCandidates();
  GarbageDistancesAreSkipped();
  GrowthWaitsForTheDwell();
  RepeatedShrinksWaitLonger();
  GrowthStopsUnderPressure();
  RadiusClearsWhenGrowthReachesTheLimit();
  RingIsWhatGrowthLetsReturn();
  RingRoomKeepsHalfTheBand();
  RingThatFitsGrowsFully();
  EmptyRingGrowsFully();
  RingThatPartlyFitsStopsAtTheFirstThatDoesNot();
  RingWhereNothingFitsBlocksTheRadius();
  UnpricedNpcsReturnOnePerGrowth();
  ClearingPricesTheRing();
  GarbageDistancesStayOutOfTheRing();
  RadiusCombinesWithEvictDistance();
  LinesCarryTheNumbers();
  return test::Finish("planners_budgetradius");
}
