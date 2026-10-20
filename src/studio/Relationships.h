// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "recipe/Recipe.h"
#include "recipe/Visit.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
enum class LinkKind : std::uint8_t {
  kSignal,
  kSource,
  kMask,
  kCurve,
  kOutput,
  kLayer,
  kShell,
  kVariant,
};
inline constexpr std::size_t kLinkKindCount = 8;

struct LinkKindSpec {
  LinkKind value;
  std::string_view glyph;
  std::string_view help;
  std::array<float, 3> colour;
};
inline constexpr LinkKindSpec kLinkKinds[]{
    {LinkKind::kSignal,
     "~",
     "signal: a value the recipe computes each tick",
     {0.55f, 0.80f, 1.00f}},
    {LinkKind::kSource,
     "S",
     "source: an image the recipe reads or bakes",
     {1.00f, 0.70f, 0.45f}},
    {LinkKind::kMask,
     "m",
     "mask: a per-texel selection",
     {0.55f, 0.90f, 0.55f}},
    {LinkKind::kCurve,
     "x",
     "curve: a mapping from one value to another",
     {0.85f, 0.60f, 1.00f}},
    {LinkKind::kOutput,
     "o",
     "output: a slot the recipe writes",
     {0.90f, 0.90f, 0.55f}},
    {LinkKind::kLayer,
     "L",
     "layer: one step of an output's stack",
     {0.90f, 0.90f, 0.55f}},
    {LinkKind::kShell,
     "h",
     "shell: the recipe's shell settings",
     {0.70f, 0.70f, 0.70f}},
    {LinkKind::kVariant,
     "V",
     "variant: overrides for a matching piece",
     {0.70f, 0.70f, 0.70f}},
};
static_assert(std::size(kLinkKinds) == kLinkKindCount);

[[nodiscard]] LinkKind LinkKindOf(ResourceKind a_kind) noexcept;
[[nodiscard]] LinkKind LinkKindOf(const RelationshipOwner &a_owner) noexcept;

struct Relationship {
  PropertyLocation consumer;
  ResourceRef driver;
  [[nodiscard]] bool operator==(const Relationship &) const = default;
};
[[nodiscard]] std::vector<Relationship> RelationshipsOf(const Recipe &a_recipe);

struct CascadePlan {
  std::vector<ResourceRef> resources;
  std::vector<LayerOwner> layers;
  std::vector<Relationship> blocked;
};
[[nodiscard]] CascadePlan PlanCascade(const Recipe &a_recipe,
                                      const ResourceRef &a_seed);
[[nodiscard]] CascadePlan
PlanCascade(std::span<const Relationship> a_relationships,
            const ResourceRef &a_seed);
}
