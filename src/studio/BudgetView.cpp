// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/BudgetView.h"

#include "planners/RenderMemory.h"

#include <format>
#include <string>

namespace BetterEnchantmentEffects::Studio {
namespace {
std::string MemoryText(const std::optional<MemoryReading> &a_reading) {
  return a_reading ? std::format("budget {}, usage {}",
                                 Mebibytes(a_reading->memory.budget),
                                 Mebibytes(a_reading->memory.usage))
                   : std::string{"unknown"};
}

std::string FreeText(const std::optional<MemoryReading> &a_reading) {
  return a_reading ? Mebibytes(FreeVideoMemory(a_reading->memory))
                   : std::string{"unknown"};
}

std::string AllowanceText(const std::optional<MemoryReading> &a_reading) {
  if (!a_reading || !a_reading->allowance) {
    return "none: video memory is unknown";
  }
  const LimitedAllowance &allowance = *a_reading->allowance;
  if (allowance.limit == AllowanceLimit::kCap) {
    return std::format("{} (the cap)", Mebibytes(allowance.bytes));
  }
  return std::format("{} (free video memory less the {} margin)",
                     Mebibytes(allowance.bytes),
                     Mebibytes(MarginOf(a_reading->memory.budget)));
}

std::string HeadroomText(const BudgetView &a_view) {
  return a_view.reading && a_view.reading->allowance
             ? Mebibytes(Less(a_view.reading->allowance->bytes, a_view.planned))
             : std::string{"unknown"};
}

std::string RadiusText(const std::optional<float> &a_radius) {
  return a_radius ? std::format("{:.0f} units", *a_radius) : std::string{"off"};
}

std::string LevelsText(const PerLevel<std::size_t> &a_actors) {
  std::string text;
  for (const ResolutionLevel level : kResolutionLevels) {
    text += std::format("{}{} {}", text.empty() ? "" : ", ", LevelName(level),
                        a_actors.At(level));
  }
  return text;
}
}

BudgetView BudgetViewOf(const BudgetSample &a_sample,
                        const BudgetState &a_state,
                        const std::optional<BudgetRadius> &a_radius,
                        const PerLevel<std::size_t> &a_actors) {
  const std::optional<MemoryReading> reading =
      a_sample.memory ? std::optional<MemoryReading>{MemoryReading{
                            *a_sample.memory, LimitedAllowanceOf(a_sample)}}
                      : std::nullopt;
  return {reading,
          a_state.planned,
          a_sample.pluginBytes,
          a_state.pressure,
          a_radius ? std::optional<float>{a_radius->distance} : std::nullopt,
          a_actors};
}

std::string_view PressureText(Pressure a_pressure) noexcept {
  switch (a_pressure) {
  case Pressure::kNone:
    return "none";
  case Pressure::kOver:
    return "over";
  case Pressure::kOverWarned:
    return "over and exhausted";
  }
  return "none";
}

std::vector<ReadoutLine> BudgetReadout(const BudgetView &a_view) {
  return {{"video memory", MemoryText(a_view.reading)},
          {"free video memory", FreeText(a_view.reading)},
          {"allowance", AllowanceText(a_view.reading)},
          {"planned", Mebibytes(a_view.planned)},
          {"headroom", HeadroomText(a_view)},
          {"measured targets", Mebibytes(a_view.measured)},
          {"pressure", std::string{PressureText(a_view.pressure)}},
          {"budget radius", RadiusText(a_view.radius)},
          {"actors by resolution", LevelsText(a_view.actors)}};
}
}
