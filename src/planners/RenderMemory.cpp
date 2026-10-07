// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/RenderMemory.h"

#include <algorithm>
#include <format>
#include <map>
#include <set>
#include <utility>

namespace BetterEnchantmentEffects {
namespace {
std::uint32_t LevelOf(std::uint64_t a_bytes) noexcept {
  return static_cast<std::uint32_t>(a_bytes / kHoldingStepBytes);
}

std::string Joined(std::span<const std::string> a_names) {
  std::string joined;
  for (const std::string &name : a_names) {
    joined += (joined.empty() ? "" : ", ") + name;
  }
  return joined.empty() ? std::string{"none"} : joined;
}

std::string HoldingText(const ActorHolding &a_holding) {
  const std::string actor =
      a_holding.name.empty()
          ? std::format("{:08X}", a_holding.actor)
          : std::format("{} ({:08X})", a_holding.name, a_holding.actor);
  return std::format("{} {} ({} shared), {:.2f} of the screen height, "
                     "recipes {}",
                     actor, Mebibytes(a_holding.bytes),
                     Mebibytes(a_holding.sharedBytes), a_holding.screenFraction,
                     Joined(a_holding.recipes));
}

std::map<std::string, std::size_t>
HoldersByKey(std::span<const ActorTargets> a_actors) {
  std::map<std::string, std::size_t> holders;
  for (const ActorTargets &actor : a_actors) {
    std::set<std::string> keys;
    for (const HeldTarget &target : actor.held) {
      if (target.contentKey && keys.insert(*target.contentKey).second) {
        ++holders[*target.contentKey];
      }
    }
  }
  return holders;
}
}

std::string Mebibytes(std::uint64_t a_bytes) {
  return std::format("{:.0f} MiB",
                     static_cast<double>(a_bytes) / (1024.0 * 1024.0));
}

HoldingLevel NextHoldingLevel(std::uint32_t a_level,
                              std::uint64_t a_bytes) noexcept {
  const std::uint32_t level = LevelOf(a_bytes);
  if (level > a_level) {
    return {level, Crossing::kRose};
  }
  const std::uint64_t fallBelow =
      a_level == 0
          ? 0
          : std::uint64_t{a_level} * kHoldingStepBytes - kHoldingStepBytes / 2;
  if (a_level > 0 && a_bytes < fallBelow) {
    return {level, Crossing::kFell};
  }
  return {a_level, Crossing::kNone};
}

bool IsBurst(const TargetBurst &a_burst) noexcept {
  return a_burst.createdBytes >= kBurstBytes;
}

ScreenSpread SpreadOf(std::vector<float> a_fractions) {
  if (a_fractions.empty()) {
    return {};
  }
  std::ranges::sort(a_fractions);
  return {a_fractions.front(), a_fractions[a_fractions.size() / 2],
          a_fractions.back()};
}

std::vector<ActorHolding> HoldingsOf(std::span<const ActorTargets> a_actors) {
  const std::map<std::string, std::size_t> holders = HoldersByKey(a_actors);
  std::vector<ActorHolding> holdings;
  for (const ActorTargets &actor : a_actors) {
    std::vector<HeldTarget> shared;
    for (const HeldTarget &target : actor.held) {
      const auto found =
          target.contentKey ? holders.find(*target.contentKey) : holders.end();
      if (found != holders.end() && found->second > 1) {
        shared.push_back(target);
      }
    }
    holdings.push_back({actor.actor, actor.name, CostOf(actor.held).bytes,
                        CostOf(shared).bytes, actor.screenFraction,
                        actor.recipes});
  }
  return holdings;
}

std::vector<ActorHolding> LargestHoldings(std::vector<ActorHolding> a_holdings,
                                          std::size_t a_count) {
  std::ranges::stable_sort(a_holdings, std::ranges::greater{},
                           &ActorHolding::bytes);
  if (a_holdings.size() > a_count) {
    a_holdings.resize(a_count);
  }
  return a_holdings;
}

std::string HoldingLine(const TargetTotals &a_totals,
                        std::span<const ActorHolding> a_largest) {
  std::string line = std::format("render targets: {} in {} targets",
                                 Mebibytes(a_totals.bytes), a_totals.targets);
  for (const ActorHolding &holding : a_largest) {
    line += "; " + HoldingText(holding);
  }
  return line;
}

std::string BurstLine(const TargetBurst &a_burst, std::string_view a_syncs) {
  return std::format("render targets: created {} in {} targets and destroyed "
                     "{} within {} ms; syncs: {}",
                     Mebibytes(a_burst.createdBytes), a_burst.created,
                     Mebibytes(a_burst.destroyedBytes), a_burst.intervalMs,
                     a_syncs.empty() ? std::string_view{"none"} : a_syncs);
}
}
