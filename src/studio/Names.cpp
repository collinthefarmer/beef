#include "studio/Names.h"

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <utility>

namespace BetterEnchantmentEffects::Studio {
namespace {
constexpr std::array<std::string_view, kRowKindCount> kRowKindNames{
    "signal", "curve", "source", "mask"};

[[nodiscard]] char Lower(char a_ch) noexcept {
  return (a_ch >= 'A' && a_ch <= 'Z') ? static_cast<char>(a_ch - 'A' + 'a')
                                      : a_ch;
}

[[nodiscard]] bool HexRun(std::string_view a_text, std::size_t a_at) noexcept {
  if (a_at + 8 > a_text.size()) {
    return false;
  }
  for (std::size_t i = 0; i < 8; ++i) {
    const char ch = a_text[a_at + i];
    const bool digit = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') ||
                       (ch >= 'a' && ch <= 'f');
    if (!digit) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::optional<std::pair<std::string_view, std::size_t>>
NumberField(std::string_view a_text, std::size_t a_at, char a_close) noexcept {
  std::size_t end = a_at;
  while (end < a_text.size() && a_text[end] >= '0' && a_text[end] <= '9') {
    ++end;
  }
  if (end == a_at || end >= a_text.size() || a_text[end] != a_close) {
    return std::nullopt;
  }
  return std::pair<std::string_view, std::size_t>{
      a_text.substr(a_at, end - a_at), end + 1};
}
}

std::string_view RowKindName(RowKind a_kind) noexcept {
  const std::size_t index = static_cast<std::size_t>(a_kind);
  if (index >= kRowKindNames.size()) {
    return "?";
  }
  return kRowKindNames[index];
}

Names NamesOf(const RecipeRow &a_recipe, const GeometryRow &a_geometry) {
  Names names;
  for (const SignalRow &signal : a_recipe.signals) {
    names.signals.emplace_back(signal.name, signal.type);
  }
  for (const TextRow &curve : a_recipe.curves) {
    names.curves.push_back(curve.name);
  }
  for (const PictureRow &source : a_geometry.sources) {
    names.sources.emplace_back(source.name, source.type);
  }
  for (const PictureRow &mask : a_geometry.masks) {
    names.masks.push_back(mask.name);
  }
  return names;
}

std::vector<std::string> TakenNames(RowKind a_kind, const Names &a_names) {
  std::vector<std::string> taken;
  switch (a_kind) {
  case RowKind::kSignal:
    for (const std::pair<std::string, ValueType> &signal : a_names.signals) {
      taken.push_back(signal.first);
    }
    break;
  case RowKind::kCurve:
    taken = a_names.curves;
    break;
  case RowKind::kSource:
  case RowKind::kMask:
    for (const std::pair<std::string, ValueType> &source : a_names.sources) {
      taken.push_back(source.first);
    }
    taken.insert(taken.end(), a_names.masks.begin(), a_names.masks.end());
    break;
  }
  return taken;
}

std::string UniqueName(std::string_view a_stem,
                       std::span<const std::string> a_taken) {
  const auto taken = [&](const std::string &a_name) {
    return std::ranges::find(a_taken, a_name) != a_taken.end();
  };
  std::string name{a_stem};
  for (std::size_t n = 2; taken(name); ++n) {
    name = std::string{a_stem} + std::to_string(n);
  }
  return name;
}

std::string ReferenceText(std::string_view a_name) {
  return "@" + std::string{a_name};
}

std::string ReferenceName(std::string_view a_text) {
  return std::string{a_text.starts_with('@') ? a_text.substr(1) : a_text};
}

bool NameMatches(std::string_view a_name, std::string_view a_filter) noexcept {
  if (a_filter.empty()) {
    return true;
  }
  if (a_filter.size() > a_name.size()) {
    return false;
  }
  for (std::size_t at = 0; at + a_filter.size() <= a_name.size(); ++at) {
    bool same = true;
    for (std::size_t i = 0; i < a_filter.size() && same; ++i) {
      same = Lower(a_name[at + i]) == Lower(a_filter[i]);
    }
    if (same) {
      return true;
    }
  }
  return false;
}

std::string GeometryLabel(std::string_view a_name,
                          std::string_view a_armorName) {
  if (!a_name.starts_with(" (") || !HexRun(a_name, 2) ||
      a_name.substr(10, 2) != ")[") {
    return std::string{a_name};
  }
  const std::string_view addon = a_name.substr(2, 8);
  const std::optional<std::pair<std::string_view, std::size_t>> index =
      NumberField(a_name, 12, ']');
  if (!index || a_name.substr(index->second, 3) != "/ (" ||
      !HexRun(a_name, index->second + 3)) {
    return std::string{a_name};
  }
  const std::string armor =
      a_armorName.empty() ? std::string{"armor "} +
                                std::string{a_name.substr(index->second + 3, 8)}
                          : std::string{a_armorName};
  return std::format("{} geometry {} (addon {})", armor, index->first, addon);
}
}
