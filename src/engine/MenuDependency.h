#pragma once

#include <functional>
#include <optional>
#include <span>
#include <string>

namespace BetterEnchantmentEffects {
[[nodiscard]] std::span<const char *const> MenuFrameworkExports();
[[nodiscard]] std::optional<std::string>
CheckMenuFramework(bool a_loaded,
                   const std::function<bool(const char *)> &a_hasExport);
}
