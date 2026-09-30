// GPL-3.0-only with the additional permission in COPYING.md.
#include "diagnostics/GpuTiming.h"

#include <algorithm>
#include <limits>

namespace BetterEnchantmentEffects::GpuTiming {
namespace {
std::uint64_t Nanoseconds(Stamps stamps, std::uint64_t frequency) {
  const auto ticks = stamps.end - stamps.begin;
  const auto seconds = ticks / frequency;
  const auto remainder = ticks % frequency;
  if (seconds > std::numeric_limits<std::uint64_t>::max() / 1'000'000'000)
    return std::numeric_limits<std::uint64_t>::max();
  return seconds * 1'000'000'000 +
         static_cast<std::uint64_t>(static_cast<double>(remainder) * 1e9 /
                                    static_cast<double>(frequency));
}
}
std::optional<std::size_t> BeginTick(Ring &ring, Totals &totals) {
  if (ring.recording)
    return std::nullopt;
  for (std::size_t slot = 0; slot < ring.slots.size(); ++slot) {
    if (ring.slots[slot].state != SlotState::kFree)
      continue;
    ring.slots[slot] = {SlotState::kRecording, {}};
    ring.order[slot] = ring.nextTick++;
    ring.recording = slot;
    return slot;
  }
  ++totals.droppedTicks;
  return std::nullopt;
}
std::optional<std::size_t> OpenSpan(Ring &ring, Totals &totals,
                                    std::string key) {
  if (!ring.recording)
    return std::nullopt;
  auto &spans = ring.slots[*ring.recording].spans;
  if (spans.size() >= kMaxSpans) {
    ++totals.untimedSpans;
    return std::nullopt;
  }
  spans.push_back(std::move(key));
  return spans.size() - 1;
}
void EndTick(Ring &ring) {
  if (!ring.recording)
    return;
  ring.slots[*ring.recording].state = SlotState::kPending;
  ring.recording.reset();
}
std::vector<std::size_t> PendingOldestFirst(const Ring &ring) {
  std::vector<std::size_t> slots;
  for (std::size_t slot = 0; slot < ring.slots.size(); ++slot)
    if (ring.slots[slot].state == SlotState::kPending)
      slots.push_back(slot);
  std::ranges::sort(slots, [&](std::size_t a, std::size_t b) {
    return ring.order[a] < ring.order[b];
  });
  return slots;
}
void Resolve(Ring &ring, Totals &totals, std::size_t slot,
             const ResolvedTick &tick) {
  if (slot >= ring.slots.size() ||
      ring.slots[slot].state != SlotState::kPending)
    return;
  auto &frame = ring.slots[slot];
  const bool usable = !tick.disjoint && tick.frequency != 0 &&
                      tick.spans.size() == frame.spans.size();
  if (!usable) {
    ++totals.discardedTicks;
  } else {
    ++totals.timedTicks;
    for (std::size_t i = 0; i < frame.spans.size(); ++i) {
      const auto stamps = tick.spans[i];
      if (stamps.end < stamps.begin)
        continue;
      const auto elapsed = Nanoseconds(stamps, tick.frequency);
      auto &total = totals.spans[frame.spans[i]];
      ++total.count;
      total.nanoseconds += elapsed;
      total.maxNanoseconds = std::max(total.maxNanoseconds, elapsed);
    }
  }
  frame = {};
}
Totals Drain(Totals &totals) {
  Totals drained = std::move(totals);
  totals = {};
  return drained;
}
}
