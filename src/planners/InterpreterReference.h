// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/InterpreterProgram.h"

#include <array>
#include <span>

namespace BetterEnchantmentEffects {
using LookupTable = std::array<float, 256>;
struct InterpreterTexel {
  std::span<const Vec3> inputs;
  std::span<const LookupTable> lookups;
};
[[nodiscard]] Vec3
EvaluateInterpreter(std::span<const InterpreterInstruction> code,
                    const InterpreterTexel &texel);
[[nodiscard]] Vec3 EvaluateInterpreter(const InterpreterProgram &program,
                                       const InterpreterTexel &texel);
[[nodiscard]] Vec3 QuantizeUnorm8(Vec3 value);
}
