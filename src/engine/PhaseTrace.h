// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "diagnostics/Metrics.h"
#include "diagnostics/Trace.h"

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects {
class PhaseTrace {
public:
  PhaseTrace(Trace::Event a_event, std::string a_action, std::uint32_t a_actor)
      : event_(a_event) {
    fields_.push_back({"action", std::move(a_action)});
    fields_.push_back({"actor", std::to_string(a_actor)});
  }

  template <class Work>
  std::invoke_result_t<Work> Measure(std::string a_name, Work &&a_work) {
    const Metrics::Stopwatch watch;
    if constexpr (std::is_void_v<std::invoke_result_t<Work>>) {
      std::forward<Work>(a_work)();
      Add(std::move(a_name), std::to_string(watch.Micros()));
    } else {
      std::invoke_result_t<Work> result = std::forward<Work>(a_work)();
      Add(std::move(a_name), std::to_string(watch.Micros()));
      return result;
    }
  }

  void Add(std::string a_name, std::string a_value) {
    fields_.push_back({std::move(a_name), std::move(a_value)});
  }

  void Emit() const noexcept { Trace::EmitSafely(event_, fields_); }

private:
  Trace::Event event_;
  std::vector<Trace::Field> fields_;
};
}
