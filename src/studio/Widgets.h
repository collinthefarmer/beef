#pragma once

#include "Core.h"
#include "recipe/Recipe.h"
#include "studio/Snapshot.h"

#include <cstddef>
#include <span>
#include <string_view>

namespace BetterEnchantmentEffects::Studio {
enum class WidthMode {
  kFill,
  kFit,
  kPx,
};
inline constexpr std::size_t kWidthModeCount = 3;

struct Width {
  WidthMode mode = WidthMode::kFill;
  float ratio = 1.0f;
  float pixels = 0.0f;
  std::string_view text;

  [[nodiscard]] static constexpr Width Fill(float a_ratio = 1.0f) noexcept {
    return Width{WidthMode::kFill, a_ratio, 0.0f, {}};
  }
  [[nodiscard]] static constexpr Width
  Fit(std::string_view a_text = {}) noexcept {
    return Width{WidthMode::kFit, 1.0f, 0.0f, a_text};
  }
  [[nodiscard]] static constexpr Width Px(float a_pixels) noexcept {
    return Width{WidthMode::kPx, 1.0f, a_pixels, {}};
  }
  [[nodiscard]] bool operator==(const Width &) const = default;
};

struct Column {
  std::string_view label;
  Width width = Width::Fill();
};

enum class TableBorders {
  kAll,
  kInnerHorizontal,
  kNone,
};
inline constexpr std::size_t kTableBordersCount = 3;

struct TableStyle {
  TableBorders borders = TableBorders::kAll;
  bool stretch = false;
  bool headers = true;
  bool rowBackground = false;
};

inline constexpr TableStyle kFormTable{.borders =
                                           TableBorders::kInnerHorizontal,
                                       .stretch = true,
                                       .headers = false};
inline constexpr TableStyle kColumnsTable{
    .borders = TableBorders::kNone, .stretch = true, .headers = false};
inline constexpr TableStyle kGridTable{.borders = TableBorders::kAll,
                                       .stretch = true,
                                       .headers = true,
                                       .rowBackground = true};
inline constexpr TableStyle kFooterTable{
    .borders = TableBorders::kAll, .stretch = true, .headers = true};
inline constexpr TableStyle kRelationTable{.borders =
                                               TableBorders::kInnerHorizontal,
                                           .stretch = true,
                                           .headers = true};
inline constexpr TableStyle kLayerTable{.borders =
                                            TableBorders::kInnerHorizontal,
                                        .stretch = true,
                                        .headers = true,
                                        .rowBackground = true};

enum class RuleAction {
  kNew,
  kRename,
  kClear,
  kKeys,
  kUndo,
  kRedo,
  kAdd,
  kRemove,
  kApplyDefaults,
  kSwitchPane,
};
inline constexpr std::size_t kRuleActionCount = 10;

inline constexpr Named<RuleAction> kRuleActions[]{
    {RuleAction::kNew, "New"},
    {RuleAction::kRename, "Rename"},
    {RuleAction::kClear, "Clear"},
    {RuleAction::kKeys, "keys"},
    {RuleAction::kUndo, "Undo"},
    {RuleAction::kRedo, "Redo"},
    {RuleAction::kAdd, "Add"},
    {RuleAction::kRemove, "Remove"},
    {RuleAction::kApplyDefaults, "Apply Defaults"},
    {RuleAction::kSwitchPane, "switch"},
};
static_assert(std::size(kRuleActions) == kRuleActionCount);

[[nodiscard]] constexpr std::string_view
RuleActionLabel(RuleAction a_action) noexcept {
  return NameOf(kRuleActions, a_action);
}

struct RuleButton {
  RuleAction action = RuleAction::kNew;
  std::string_view label = {};
  Width width = Width::Fit();
  bool enabled = true;

  [[nodiscard]] constexpr std::string_view Label() const noexcept {
    return label.empty() ? RuleActionLabel(action) : label;
  }
  [[nodiscard]] bool operator==(const RuleButton &) const = default;
};

struct RuleSpec {
  std::string_view text;
  std::span<const RuleButton> buttons = {};
  bool collapsible = false;
  bool openByDefault = true;
  bool leadingSpace = true;
};

struct RuleClick {
  bool clicked = false;
  std::size_t index = 0;
};

struct RowMove {
  std::size_t from = 0;
  std::size_t to = 0;
};

struct ThumbnailSpec {
  TextureHandle texture{};
  ShaderChannel channel = ShaderChannel::kRgb;
  bool dynamic = false;
  float size = 0.0f;
};
}
