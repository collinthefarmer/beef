// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace BetterEnchantmentEffects {
inline constexpr std::size_t kMaxTextFileBytes = 4u * 1024u * 1024u;
inline constexpr std::string_view kSetAsideSuffix = ".unloaded";

enum class Created : std::uint8_t { kWritten, kExists, kFailed };

[[nodiscard]] std::expected<std::string, std::string>
ReadText(const std::filesystem::path &a_path);
[[nodiscard]] bool WriteText(const std::filesystem::path &a_path,
                             std::string_view a_text);
[[nodiscard]] Created CreateText(const std::filesystem::path &a_path,
                                 std::string_view a_text);
[[nodiscard]] std::optional<std::filesystem::path>
SetAside(const std::filesystem::path &a_path);
}
