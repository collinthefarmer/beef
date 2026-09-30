// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/InterpreterProgram.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects {
struct OpcodeStatement {
  InterpreterOpcode opcode;
  std::string_view statement;
};
using TextureSlot = std::optional<std::uint32_t>;
inline constexpr std::string_view kGeneratedProgramEntry = "PSGenerated";
[[nodiscard]] std::span<const OpcodeStatement> OpcodeStatements() noexcept;
[[nodiscard]] std::string InterpreterSwitch();
[[nodiscard]] std::vector<TextureSlot>
TextureSlots(std::span<const InterpreterInput> inputs);
[[nodiscard]] std::string
ProgramFunction(std::string_view name,
                std::span<const InterpreterInstruction> code, std::size_t first,
                std::span<const TextureSlot> slots);
[[nodiscard]] std::string
GenerateProgramShader(const InterpreterProgram &program);
}
