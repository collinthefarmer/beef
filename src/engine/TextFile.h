#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace BetterEnchantmentEffects {
inline constexpr std::size_t kMaxTextFileBytes = 4u * 1024u * 1024u;

[[nodiscard]] std::expected<std::string, std::string>
ReadText(const std::filesystem::path &a_path);
[[nodiscard]] bool WriteText(const std::filesystem::path &a_path,
                             std::string_view a_text);
}
