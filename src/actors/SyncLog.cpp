// GPL-3.0-only with the additional permission in COPYING.md.
#include "actors/SyncLog.h"

#include <array>
#include <format>
#include <iterator>
#include <string_view>
#include <utility>

namespace BetterEnchantmentEffects::Actors {
namespace {
constexpr std::array<std::string_view, 8> kKindNames{
    "unchanged", "installed", "reinstalled", "changed",
    "claimed",   "evicted",   "forgotten",   "resized"};

bool Within(const SyncRecord &a_record, std::uint32_t a_nowMs,
            std::uint32_t a_windowMs) noexcept {
  const auto age = static_cast<std::int32_t>(a_nowMs - a_record.atMs);
  return age >= 0 && static_cast<std::uint32_t>(age) <= a_windowMs;
}

bool Empty(const ActorPlan &a_plan) { return a_plan == ActorPlan{}; }
}

std::string_view SyncKindName(SyncKind a_kind) noexcept {
  const auto index = static_cast<std::size_t>(a_kind);
  return index < kKindNames.size() ? kKindNames[index] : "unknown";
}

SyncKind KindOf(const std::optional<ActorState> &a_before,
                const Transition &a_after, Reinstall a_reinstall) {
  if (!a_after.state) {
    return a_before ? SyncKind::kForgotten : SyncKind::kUnchanged;
  }
  if (a_after.commands.empty()) {
    return SyncKind::kUnchanged;
  }
  const bool installedBefore = a_before && !Empty(a_before->installed);
  const bool installedAfter = !Empty(a_after.state->installed);
  if (!installedBefore && installedAfter) {
    return SyncKind::kInstalled;
  }
  if (installedBefore && !installedAfter) {
    return SyncKind::kEvicted;
  }
  if (a_reinstall == Reinstall::kAlways) {
    return SyncKind::kReinstalled;
  }
  const std::size_t claimsBefore = a_before ? a_before->claims.size() : 0;
  if (a_after.state->claims.size() > claimsBefore) {
    return SyncKind::kClaimed;
  }
  return SyncKind::kChanged;
}

std::vector<SyncRecord> Recorded(std::vector<SyncRecord> a_log,
                                 SyncRecord a_record) {
  const std::uint32_t now = a_record.atMs;
  std::erase_if(a_log, [&](const SyncRecord &a_old) {
    return !Within(a_old, now, kSyncLogMs);
  });
  if (a_log.size() >= kMaxSyncRecords) {
    a_log.erase(a_log.begin());
  }
  a_log.push_back(std::move(a_record));
  return a_log;
}

std::string SyncSummary(std::span<const SyncRecord> a_log,
                        std::uint32_t a_nowMs, std::uint32_t a_windowMs) {
  std::string summary;
  for (std::size_t kind = 1; kind < kKindNames.size(); ++kind) {
    std::string names;
    std::size_t count = 0;
    for (const SyncRecord &record : a_log) {
      if (static_cast<std::size_t>(record.kind) == kind &&
          Within(record, a_nowMs, a_windowMs)) {
        names += (count++ == 0 ? "" : ", ") + record.name;
      }
    }
    if (count > 0) {
      summary += std::format("{}{} {} ({})", summary.empty() ? "" : "; ",
                             kKindNames[kind], count, names);
    }
  }
  return summary;
}
}
