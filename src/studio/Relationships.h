#pragma once

#include "recipe/Recipe.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
enum class ResourceKind { kSignal, kSource, kMask, kCurve, kCount };
inline constexpr std::array<std::string_view, 4> kResourceKindNames{
    "signal", "source", "mask", "curve"};
static_assert(kResourceKindNames.size() ==
              static_cast<std::size_t>(ResourceKind::kCount));

struct ResourceRef {
  ResourceKind kind = ResourceKind::kSignal;
  std::string name;
  [[nodiscard]] bool operator==(const ResourceRef &) const = default;
};
struct OutputOwner {
  std::size_t output = 0;
  [[nodiscard]] bool operator==(const OutputOwner &) const = default;
};
struct LayerOwner {
  std::size_t output = 0;
  std::size_t layer = 0;
  [[nodiscard]] bool operator==(const LayerOwner &) const = default;
};
struct ShellOwner {
  [[nodiscard]] bool operator==(const ShellOwner &) const = default;
};
struct VariantOwner {
  std::size_t index = 0;
  [[nodiscard]] bool operator==(const VariantOwner &) const = default;
};
using RelationshipOwner = std::variant<ResourceRef, OutputOwner, LayerOwner,
                                       ShellOwner, VariantOwner>;
struct PropertyLocation {
  RelationshipOwner owner = ShellOwner{};
  std::string property;
  std::optional<std::size_t> component;
  [[nodiscard]] bool operator==(const PropertyLocation &) const = default;
};
struct Relationship {
  PropertyLocation consumer;
  ResourceRef driver;
  [[nodiscard]] bool operator==(const Relationship &) const = default;
};
[[nodiscard]] std::vector<Relationship> RelationshipsOf(const Recipe &a_recipe);
}
