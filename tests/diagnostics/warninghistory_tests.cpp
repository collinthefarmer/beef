// GPL-3.0-only with the additional permission in COPYING.md.
#include "diagnostics/WarningHistory.h"
#include "test_support.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Equal;

namespace {
void RepeatsAndReset() {
  WarningHistory history;
  Check(history.Observe("recipe|output|geometry|problem") ==
            WarningDecision::kFirst,
        "first warning is logged");
  const auto first = history.Inspect();
  Check(history.Observe("recipe|output|geometry|problem") ==
            WarningDecision::kRepeat,
        "same warning is suppressed");
  Check(history.Inspect().keys == first.keys &&
            history.Inspect().bytes == first.bytes,
        "repeats retain no additional memory");
  Check(history.Observe("recipe|output|other geometry|problem") ==
            WarningDecision::kFirst,
        "a different context still logs");
  history.Clear();
  Check(history.Inspect().keys == 0 && history.Inspect().bytes == 0,
        "load clearing releases retained keys and bytes");
  Check(history.Observe("recipe|output|geometry|problem") ==
            WarningDecision::kFirst,
        "the warning is visible again in the new session");
}

void KeyLimit() {
  WarningHistory history;
  for (std::size_t i = 0; i < kMaxWarningKeys; ++i) {
    Check(history.Observe(std::to_string(i)) == WarningDecision::kFirst,
          "distinct warnings fit up to the key limit");
  }
  Check(history.Observe("0") == WarningDecision::kRepeat,
        "a repeat at capacity does not report overflow");
  Check(history.Observe("overflow") == WarningDecision::kLimit,
        "first excess warning requests a limit notice");
  const auto full = history.Inspect();
  for (std::size_t i = 0; i < 2 * kMaxWarningKeys; ++i) {
    Check(history.Observe("extra-" + std::to_string(i)) ==
              WarningDecision::kOmitted,
          "excess warnings cannot flood the log with limit notices");
  }
  Check(history.Inspect().keys == kMaxWarningKeys &&
            history.Inspect().bytes == full.bytes,
        "unique-warning flood does not grow storage");
  history.Clear();
  Check(history.Observe("overflow") == WarningDecision::kFirst,
        "clearing restores capacity for previously omitted warnings");
}

void ByteLimit() {
  WarningHistory history;
  const std::string oversized(kMaxWarningKeyBytes + 1, 'x');
  Check(history.Observe(oversized) == WarningDecision::kLimit,
        "one oversized key is refused without retaining it");
  Check(history.Inspect().keys == 0 && history.Inspect().bytes == 0,
        "oversized text consumes no history budget");
  Check(history.Observe("small") == WarningDecision::kFirst,
        "oversized input does not prevent smaller warnings from logging");
  Check(history.Observe(oversized) == WarningDecision::kOmitted,
        "an oversized repeat does not repeat the limit notice");
  history.Clear();
  const std::string first(kMaxWarningKeyBytes / 2, 'a');
  const std::string second(kMaxWarningKeyBytes / 2, 'b');
  Check(history.Observe(first) == WarningDecision::kFirst &&
            history.Observe(second) == WarningDecision::kFirst,
        "combined key text may reach the byte limit exactly");
  Equal(history.Inspect().bytes, kMaxWarningKeyBytes,
        "retained bytes are accounted exactly");
  Check(history.Observe(first) == WarningDecision::kRepeat,
        "repeat remains a repeat at the byte limit");
  Check(history.Observe("one more") == WarningDecision::kLimit,
        "load clearing also resets the limit notice");
  Check(history.Inspect().keys == 2 &&
            history.Inspect().bytes == kMaxWarningKeyBytes,
        "byte limit prevents growth independently of the key limit");
}

void ConcurrentRepeats() {
  WarningHistory history;
  std::atomic<unsigned> first{0};
  std::vector<std::thread> workers;
  for (int i = 0; i < 8; ++i) {
    workers.emplace_back([&] {
      if (history.Observe("shared warning") == WarningDecision::kFirst) {
        ++first;
      }
    });
  }
  for (auto &worker : workers) {
    worker.join();
  }
  Equal(first.load(), 1u, "concurrent observations emit a warning only once");
  Equal(history.Inspect().keys, std::size_t{1},
        "concurrent repeats retain one key");
}
}

int main() {
  RepeatsAndReset();
  KeyLimit();
  ByteLimit();
  ConcurrentRepeats();
  return test::Finish("diagnostics warning history");
}
