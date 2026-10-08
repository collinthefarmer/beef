// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "actors/Actor.h"
#include "actors/Lifecycle.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects::Actors {
inline constexpr std::uint32_t kSyncLogMs = 2000;
inline constexpr std::size_t kMaxSyncRecords = 64;

enum class SyncKind : std::uint8_t {
  kUnchanged,
  kInstalled,
  kReinstalled,
  kChanged,
  kClaimed,
  kEvicted,
  kForgotten
};

struct SyncRecord {
  std::uint32_t actor = 0;
  std::string name;
  SyncKind kind = SyncKind::kUnchanged;
  std::uint32_t atMs = 0;
  [[nodiscard]] bool operator==(const SyncRecord &) const = default;
};

[[nodiscard]] std::string_view SyncKindName(SyncKind a_kind) noexcept;
[[nodiscard]] SyncKind KindOf(const std::optional<ActorState> &a_before,
                              const Transition &a_after, Reinstall a_reinstall);
[[nodiscard]] std::vector<SyncRecord> Recorded(std::vector<SyncRecord> a_log,
                                               SyncRecord a_record);
[[nodiscard]] std::string SyncSummary(std::span<const SyncRecord> a_log,
                                      std::uint32_t a_nowMs,
                                      std::uint32_t a_windowMs);
}
