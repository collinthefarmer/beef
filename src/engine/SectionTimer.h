// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "diagnostics/Metrics.h"

namespace BetterEnchantmentEffects {
class SectionTimer {
public:
  explicit SectionTimer(Metrics::Section a_section) noexcept
      : section_(a_section) {}
  ~SectionTimer() { Metrics::CountSection(section_, watch_.Micros()); }
  SectionTimer(const SectionTimer &) = delete;
  SectionTimer &operator=(const SectionTimer &) = delete;
  SectionTimer(SectionTimer &&) = delete;
  SectionTimer &operator=(SectionTimer &&) = delete;

private:
  Metrics::Section section_;
  Metrics::Stopwatch watch_;
};
}
