#pragma once

#include "recipe/Recipe.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
using FormID = std::uint32_t;

struct PieceRef {
  FormID actorID = 0;
  FormID armorID = 0;
  bool firstPerson = false;
  [[nodiscard]] bool operator==(const PieceRef &) const = default;
};

struct Pin {
  PieceRef piece;
  std::string recipeID;
  [[nodiscard]] bool operator==(const Pin &) const = default;
};

struct LayerKey {
  std::string recipeID;
  std::size_t output = 0;
  std::size_t layer = 0;
  [[nodiscard]] auto operator<=>(const LayerKey &) const = default;
};

struct View {
  bool freeze = false;
  float scrubSeconds = 0.0f;
  float speed = 1.0f;
  std::string isolateRecipe;
  int isolateOutput = -1;
  int isolateLayer = -1;
  bool isolatedBySolo = false;
  std::set<LayerKey> muted;
  std::optional<Pin> pin;

  [[nodiscard]] bool Isolating() const noexcept {
    return !isolateRecipe.empty();
  }

  [[nodiscard]] std::vector<std::string> RecipeIDs() const;
  void RenameRecipe(std::string_view a_from, std::string_view a_to);
  void ForgetRecipe(std::string_view a_id);

  [[nodiscard]] bool RecipeShown(const std::string &a_recipe) const noexcept {
    return !Isolating() || isolateRecipe == a_recipe;
  }

  [[nodiscard]] bool OutputShown(const std::string &a_recipe,
                                 std::size_t a_output) const noexcept {
    return RecipeShown(a_recipe) &&
           (isolateOutput < 0 ||
            static_cast<std::size_t>(isolateOutput) == a_output);
  }

  [[nodiscard]] bool LayerShown(const std::string &a_recipe,
                                std::size_t a_output,
                                std::size_t a_layer) const {
    if (!OutputShown(a_recipe, a_output)) {
      return false;
    }
    if (isolateLayer >= 0 && isolateRecipe == a_recipe && isolateOutput >= 0 &&
        static_cast<std::size_t>(isolateOutput) == a_output) {
      return static_cast<std::size_t>(isolateLayer) == a_layer;
    }
    return !muted.contains(LayerKey{a_recipe, a_output, a_layer});
  }

  [[nodiscard]] bool LayerMuted(const std::string &a_recipe,
                                std::size_t a_output,
                                std::size_t a_layer) const {
    return muted.contains(LayerKey{a_recipe, a_output, a_layer});
  }

  [[nodiscard]] bool FiltersLayers(const std::string &a_recipe,
                                   std::size_t a_output) const {
    if (isolateLayer >= 0 && isolateRecipe == a_recipe && isolateOutput >= 0 &&
        static_cast<std::size_t>(isolateOutput) == a_output) {
      return true;
    }
    for (const auto &key : muted) {
      if (key.recipeID == a_recipe && key.output == a_output) {
        return true;
      }
    }
    return false;
  }
};

enum class Mode {
  kCompose,
  kPaint,
};
inline constexpr std::size_t kModeCount = 2;
inline constexpr std::array<Mode, kModeCount> kModes{Mode::kCompose,
                                                     Mode::kPaint};
[[nodiscard]] std::string_view ModeName(Mode a_mode) noexcept;

struct Layout {
  Mode mode = Mode::kCompose;
  bool contextRows = true;
  bool stack = true;
  bool inspector = true;
  bool signals = true;
  bool maskEditor = false;
  float cellSize = 40.0f;
  float inspectorThumbnail = 96.0f;
  float stackSplit = 0.5f;
  float resourcesShare = 0.35f;
  bool developerSignals = true;
};
inline constexpr std::array<Layout, kModeCount> kLayouts{
    Layout{.mode = Mode::kCompose},
    Layout{.mode = Mode::kPaint,
           .contextRows = false,
           .signals = false,
           .maskEditor = true},
};
[[nodiscard]] Layout LayoutFor(Mode a_mode) noexcept;
}
