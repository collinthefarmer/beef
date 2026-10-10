// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "recipe/Recipe.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace BetterEnchantmentEffects {
struct SlotTarget {
  virtual ~SlotTarget() = default;
  [[nodiscard]] virtual std::optional<Diagnostic> Problem(Slot) const {
    return std::nullopt;
  }
};
struct MaterialBinding : SlotTarget {
  std::function<void()> onDestroy;
  std::string problem;
  ~MaterialBinding() override {
    if (onDestroy)
      onDestroy();
  }
  [[nodiscard]] std::optional<Diagnostic> Problem(Slot a_slot) const override {
    if (problem.empty()) {
      return std::nullopt;
    }
    return MakeDiagnostic(Severity::kError, std::string{SlotName(a_slot)},
                          problem);
  }
};
struct ShellBinding : SlotTarget {
  std::function<void()> onDestroy;
  ~ShellBinding() override {
    if (onDestroy)
      onDestroy();
  }
};
struct LightBinding {};
}
