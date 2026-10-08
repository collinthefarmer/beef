// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "PCH.h"
#include "planners/RenderBudget.h"

#include <REX/W32/COMPTR.h>
#include <REX/W32/DXGI_4.h>

#include <expected>
#include <string>
#include <variant>

namespace BetterEnchantmentEffects {
class VideoMemoryProbe {
public:
  [[nodiscard]] std::expected<VideoMemory, std::string> Sample();

private:
  using Adapter = REX::W32::ComPtr<REX::W32::IDXGIAdapter3>;
  struct NotProbed {};
  struct Unavailable {
    std::string problem;
  };
  std::variant<NotProbed, Adapter, Unavailable> adapter_;
};
}
