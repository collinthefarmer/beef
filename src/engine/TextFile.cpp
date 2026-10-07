// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/TextFile.h"

#include <format>
#include <fstream>
#include <system_error>

namespace BetterEnchantmentEffects {
std::expected<std::string, std::string>
ReadText(const std::filesystem::path &a_path) {
  std::error_code ec;
  if (!std::filesystem::exists(a_path, ec)) {
    return std::unexpected("does not exist");
  }
  const std::uintmax_t size = std::filesystem::file_size(a_path, ec);
  if (ec) {
    return std::unexpected(std::format("cannot be read ({})", ec.message()));
  }
  if (size > kMaxTextFileBytes) {
    return std::unexpected(std::format("is {} bytes, larger than the {} byte "
                                       "cap for a text file",
                                       size, kMaxTextFileBytes));
  }
  std::ifstream in(a_path, std::ios::binary);
  if (!in) {
    return std::unexpected("cannot be opened");
  }
  std::string text(static_cast<std::size_t>(size), '\0');
  in.read(text.data(), static_cast<std::streamsize>(text.size()));
  if (in.bad()) {
    return std::unexpected("cannot be read");
  }
  text.resize(static_cast<std::size_t>(in.gcount()));
  return text;
}

bool WriteText(const std::filesystem::path &a_path, std::string_view a_text) {
  std::error_code ec;
  std::filesystem::create_directories(a_path.parent_path(), ec);
  std::filesystem::path staged = a_path;
  staged += ".writing";
  std::ofstream out(staged, std::ios::binary | std::ios::trunc);
  out << a_text;
  out.flush();
  out.close();
  if (!out) {
    std::filesystem::remove(staged, ec);
    return false;
  }
  std::filesystem::rename(staged, a_path, ec);
  if (ec) {
    std::filesystem::remove(staged, ec);
    return false;
  }
  return true;
}

namespace {
bool Occupied(const std::filesystem::path &a_path) {
  std::error_code ec;
  const std::filesystem::file_status status =
      std::filesystem::symlink_status(a_path, ec);
  return ec ? ec != std::errc::no_such_file_or_directory
            : std::filesystem::exists(status);
}
}

Created CreateText(const std::filesystem::path &a_path,
                   std::string_view a_text) {
  if (Occupied(a_path)) {
    return Created::kExists;
  }
  return WriteText(a_path, a_text) ? Created::kWritten : Created::kFailed;
}

std::optional<std::filesystem::path>
SetAside(const std::filesystem::path &a_path) {
  std::filesystem::path aside = a_path;
  aside += kSetAsideSuffix;
  if (!Occupied(a_path) || Occupied(aside)) {
    return std::nullopt;
  }
  std::error_code ec;
  std::filesystem::rename(a_path, aside, ec);
  if (ec) {
    return std::nullopt;
  }
  return aside;
}
}
