// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects::GpuTiming {
inline constexpr std::size_t kFrameSlots = 4;
inline constexpr std::size_t kMaxSpans = 256;

enum class SlotState { kFree, kRecording, kPending };
struct FrameSlot {
  SlotState state = SlotState::kFree;
  std::vector<std::string> spans;
};
struct Ring {
  std::array<FrameSlot, kFrameSlots> slots{};
  std::array<std::uint64_t, kFrameSlots> order{};
  std::optional<std::size_t> recording;
  std::uint64_t nextTick = 1;
};
struct SpanTotal {
  std::uint64_t count = 0;
  std::uint64_t nanoseconds = 0;
  std::uint64_t maxNanoseconds = 0;
  [[nodiscard]] bool operator==(const SpanTotal &) const = default;
};
struct Totals {
  std::map<std::string, SpanTotal> spans;
  std::uint64_t timedTicks = 0;
  std::uint64_t droppedTicks = 0;
  std::uint64_t discardedTicks = 0;
  std::uint64_t untimedSpans = 0;
};
struct Stamps {
  std::uint64_t begin = 0;
  std::uint64_t end = 0;
};
struct ResolvedTick {
  bool disjoint = false;
  std::uint64_t frequency = 0;
  std::vector<Stamps> spans;
};

[[nodiscard]] std::optional<std::size_t> BeginTick(Ring &ring, Totals &totals);
[[nodiscard]] std::optional<std::size_t> OpenSpan(Ring &ring, Totals &totals,
                                                  std::string key);
void EndTick(Ring &ring);
[[nodiscard]] std::vector<std::size_t> PendingOldestFirst(const Ring &ring);
void Resolve(Ring &ring, Totals &totals, std::size_t slot,
             const ResolvedTick &tick);
[[nodiscard]] Totals Drain(Totals &totals);
}
