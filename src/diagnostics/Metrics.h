// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <chrono>
#include <cstdint>

namespace BetterEnchantmentEffects::Metrics {
struct Snapshot {
  std::uint64_t refreshes = 0;
  std::uint64_t refreshMicros = 0;
  std::uint64_t refreshMaxMicros = 0;
  std::uint64_t sinkAdds = 0;
  std::uint64_t sinkRemoves = 0;
  std::uint64_t readbacks = 0;
  std::uint64_t readbackMicros = 0;
  std::uint64_t readbackMaxMicros = 0;
  std::uint64_t targets = 0;
  std::uint64_t targetsPeak = 0;
  std::uint64_t targetBytes = 0;
  std::uint64_t targetBytesPeak = 0;
  std::uint64_t frames = 0;
  std::uint64_t renderEvaluations = 0;
  std::uint64_t stepExecutions = 0;
  [[nodiscard]] bool Quiet() const noexcept {
    return refreshes == 0 && sinkAdds == 0 && sinkRemoves == 0 &&
           readbacks == 0 && renderEvaluations == 0;
  }
};

void CountRefresh(std::uint64_t a_micros) noexcept;
void CountSinkAdd() noexcept;
void CountSinkRemove() noexcept;
void CountReadback(std::uint64_t a_micros) noexcept;
void CountTargetCreated(std::uint64_t a_bytes) noexcept;
void CountTargetDestroyed(std::uint64_t a_bytes) noexcept;
void CountFrame() noexcept;
void CountRenderEvaluation() noexcept;
void CountStepExecution() noexcept;
[[nodiscard]] Snapshot Drain() noexcept;

[[nodiscard]] constexpr std::uint64_t
MippedRgbaBytes(std::uint32_t a_side) noexcept {
  return static_cast<std::uint64_t>(a_side) * a_side * 4 * 4 / 3;
}

class Stopwatch {
public:
  [[nodiscard]] std::uint64_t Micros() const noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start_)
            .count());
  }

private:
  std::chrono::steady_clock::time_point start_ =
      std::chrono::steady_clock::now();
};
}
