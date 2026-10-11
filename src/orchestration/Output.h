// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "orchestration/Tick.h"

#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects::Orchestration {
template <class T> void Append(std::vector<T> &a_into, std::vector<T> a_from) {
  a_into.insert(a_into.end(), std::make_move_iterator(a_from.begin()),
                std::make_move_iterator(a_from.end()));
}

inline void Issue(TickOutput &a_output, Command a_command) {
  a_output.commands.emplace_back(std::move(a_command));
}

inline void AddTrace(TickOutput &a_output, TraceRecord a_record) {
  a_output.trace.emplace_back(std::move(a_record));
}

inline void AddTrace(TickOutput &a_output, std::vector<Trace::Field> a_fields) {
  AddTrace(a_output, TraceRecord{Trace::Event::kMetrics, std::move(a_fields)});
}

inline void AddLog(TickOutput &a_output, LogLevel a_level, std::string a_text) {
  a_output.logs.emplace_back(a_level, std::move(a_text));
}

inline void ApplyBudget(TickOutput &a_output, const BudgetState &a_budget) {
  a_output.world.budget = a_budget;
}

inline void ApplyPressure(TickOutput &a_output, Pressure a_pressure) {
  a_output.world.budget.pressure = a_pressure;
}

inline void ApplyRadius(TickOutput &a_output,
                        const std::optional<BudgetRadius> &a_radius) {
  a_output.world.radius = a_radius;
}
}
