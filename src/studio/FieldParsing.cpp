// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/FieldParsing.h"

#include "recipe/Words.h"

#include <algorithm>
#include <charconv>

namespace BetterEnchantmentEffects::Studio {
std::optional<Param> ParseNoiseStyle(std::string_view a_text) {
  if (const auto named = FromName(kNoiseStyles, a_text)) {
    return Param{static_cast<float>(std::to_underlying(*named))};
  }
  return ParseParam(a_text);
}

bool IsWholeReference(std::string_view a_text) noexcept {
  return a_text.size() > 1 && a_text.starts_with('@') &&
         a_text.find(',') == std::string_view::npos;
}

[[nodiscard]] std::optional<std::uint32_t> WholeNumber(std::string_view a_text,
                                                       std::uint32_t a_max) {
  while (!a_text.empty() && a_text.front() == ' ') {
    a_text.remove_prefix(1);
  }
  while (!a_text.empty() && a_text.back() == ' ') {
    a_text.remove_suffix(1);
  }
  std::uint32_t value = 0;
  const auto result =
      std::from_chars(a_text.data(), a_text.data() + a_text.size(), value);
  if (result.ec != std::errc{} || result.ptr != a_text.data() + a_text.size() ||
      value > a_max) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] std::vector<std::string> SplitNames(std::string_view a_text) {
  std::vector<std::string> names;
  std::size_t at = 0;
  while (at <= a_text.size()) {
    const auto comma = a_text.find(',', at);
    auto part = a_text.substr(at, comma == std::string_view::npos
                                      ? std::string_view::npos
                                      : comma - at);
    while (!part.empty() && part.front() == ' ') {
      part.remove_prefix(1);
    }
    while (!part.empty() && part.back() == ' ') {
      part.remove_suffix(1);
    }
    if (!part.empty()) {
      names.emplace_back(part);
    }
    if (comma == std::string_view::npos) {
      break;
    }
    at = comma + 1;
  }
  return names;
}

std::string JoinNames(const std::vector<std::string> &a_names) {
  std::string text;
  for (const std::string &name : a_names) {
    text += (text.empty() ? "" : ", ") + name;
  }
  return text;
}

std::string AppendName(std::string_view a_list, std::string_view a_name) {
  std::vector<std::string> names = SplitNames(a_list);
  const std::vector<std::string> added = SplitNames(a_name);
  for (const std::string &name : added) {
    if (!std::ranges::contains(names, name)) {
      names.push_back(name);
    }
  }
  return JoinNames(names);
}

std::optional<Vec3> LiteralColor(std::string_view a_text) {
  const auto param = ParseVec3Param(a_text);
  const auto *parts = param ? Get<std::array<Param, 3>>(*param) : nullptr;
  if (parts == nullptr) {
    return std::nullopt;
  }
  const float *x = Get<float>((*parts)[0]);
  const float *y = Get<float>((*parts)[1]);
  const float *z = Get<float>((*parts)[2]);
  if (x == nullptr || y == nullptr || z == nullptr) {
    return std::nullopt;
  }
  return Vec3{*x, *y, *z};
}

std::string LiteralColorText(const Vec3 &a_color) {
  return Vec3ParamText(std::array<Param, 3>{a_color.x, a_color.y, a_color.z});
}
}
