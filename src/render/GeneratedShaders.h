// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "PCH.h"
#include "planners/FieldProgram.h"
#include "planners/StackShader.h"

#include <REX/W32/COMPTR.h>

#include <cstdint>
#include <future>
#include <map>
#include <string>
#include <unordered_map>

namespace BetterEnchantmentEffects {
using CompiledShader = REX::W32::ComPtr<REX::W32::ID3D11PixelShader>;
struct GeneratedShader {
  std::shared_future<CompiledShader> compile;
  std::uint64_t lastUse = 0;
};
class GeneratedShaders {
public:
  inline static constexpr std::size_t kMaxPerCache = 256;
  [[nodiscard]] REX::W32::ID3D11PixelShader *
  ProgramFor(REX::W32::ID3D11Device *a_device, const FieldProgram &a_program);
  [[nodiscard]] REX::W32::ID3D11PixelShader *
  StackFor(REX::W32::ID3D11Device *a_device, const StackShape &a_shape);

private:
  std::unordered_map<std::string, GeneratedShader> programs_;
  std::map<StackShape, GeneratedShader> stacks_;
  std::uint64_t uses_ = 0;
  bool fullLogged_ = false;
};
}
