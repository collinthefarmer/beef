// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/FieldProgram.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects {
struct OpcodeStatement {
  ProgramOpcode opcode;
  std::string_view statement;
  bool keepsZ = false;
};
using InputTextureSlot = std::optional<std::uint32_t>;
inline constexpr std::string_view kGeneratedProgramEntry = "PSGeneratedProgram";
[[nodiscard]] std::span<const OpcodeStatement> OpcodeStatements() noexcept;
[[nodiscard]] std::string InterpreterSwitch();
[[nodiscard]] std::string InterpreterZRule();
[[nodiscard]] std::vector<InputTextureSlot>
InputTextureSlots(std::span<const ProgramInput> inputs);
[[nodiscard]] std::string
ProgramFunction(std::string_view name, std::span<const ProgramInstruction> code,
                std::size_t first, std::span<const InputTextureSlot> slots);
[[nodiscard]] std::string GenerateProgramShader(const FieldProgram &program);
}
