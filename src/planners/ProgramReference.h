// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/FieldProgram.h"

#include <array>
#include <span>

namespace BetterEnchantmentEffects {
using LookupTable = std::array<float, 256>;
struct ProgramTexel {
  std::span<const Vec3> inputs;
  std::span<const LookupTable> lookups;
};
[[nodiscard]] Vec3
EvaluateProgramOnCpu(std::span<const ProgramInstruction> code,
                     const ProgramTexel &texel);
[[nodiscard]] Vec3 EvaluateProgramOnCpu(const FieldProgram &program,
                                        const ProgramTexel &texel);
[[nodiscard]] Vec3 QuantizeUnorm8(Vec3 value);
}
