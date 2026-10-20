// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/Widgets.h"
#include "test_support.h"

#include <array>
#include <cstddef>
#include <optional>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

int main() {
  const Width fill = Width::Fill(0.35f);
  Check(fill.mode == WidthMode::kFill && test::Near(fill.ratio, 0.35f),
        "Width::Fill carries a ratio and no pixels");
  const Width px = Width::Px(240.0f);
  Check(px.mode == WidthMode::kPx && test::Near(px.pixels, 240.0f),
        "Width::Px carries pixels the renderer uses directly");
  const Width fit = Width::Fit("stack");
  Check(fit.mode == WidthMode::kFit && fit.text == "stack",
        "Width::Fit carries the label the renderer measures");

  const TableStyle style;
  Check(style.borders == TableBorders::kAll && style.headers && !style.stretch,
        "TableStyle defaults match the frozen context table");

  static_assert(std::size(kRuleActions) == kRuleActionCount);
  Check(RuleActionLabel(RuleAction::kApplyDefaults) == "Apply Defaults",
        "RuleActionLabel resolves through the spec table");
  Check(RuleActionLabel(RuleAction::kNew) == "New",
        "the New action labels New");

  const RuleButton keys{.action = RuleAction::kKeys};
  Check(keys.Label() == "keys" && keys.enabled,
        "a RuleButton with no override takes its label from the action");
  const RuleButton pane{
      .action = RuleAction::kSwitchPane, .label = "stack", .enabled = false};
  Check(pane.Label() == "stack" && !pane.enabled,
        "an explicit label overrides the action default");

  const std::array<RuleButton, 3> buttons{
      RuleButton{.action = RuleAction::kNew},
      RuleButton{.action = RuleAction::kRename},
      RuleButton{.action = RuleAction::kClear}};
  const RuleSpec spec{.text = "Recipe", .buttons = buttons};
  Check(spec.text == "Recipe" && spec.buttons.size() == 3 &&
            spec.buttons[1].action == RuleAction::kRename,
        "RuleSpec is a left label plus a span of button data, no closure");

  const RuleClick click{.clicked = true, .index = 2};
  Check(click.clicked && click.index == 2,
        "the renderer reports the clicked button as an index");

  const ThumbnailSpec thumb{.channel = ShaderChannel::kRgb, .size = 96.0f};
  Check(thumb.texture == TextureHandle{} && test::Near(thumb.size, 96.0f),
        "a thumbnail is pure data the renderer draws");
  constexpr std::array<Preset, 3> presets{
      Preset{"Automatic", 0}, Preset{"512 MiB", 512}, Preset{"", 1024}};
  const PresetSliderSpec slider{
      .presets = presets, .min = 128, .max = 8192, .step = 32, .unit = "MiB"};
  Check(PresetIndexOf(slider, 512) == std::optional<std::size_t>{1},
        "a value equal to a preset finds that preset");
  Check(!PresetIndexOf(slider, 700), "a value between presets is custom");
  Check(PresetValueText(slider, 0) == "Automatic",
        "a preset value shows the preset's label");
  Check(PresetValueText(slider, 65536) == "65536 MiB",
        "a value outside the slider range still shows with its unit");
  Check(PresetValueText(slider, 1024) == "1024 MiB",
        "a preset without a label shows the value");
  Check(SliderValueOf(slider, 1000) == 992,
        "a dragged value snaps to the nearest step");
  Check(SliderValueOf(slider, 0) == 128 && SliderValueOf(slider, -5) == 128,
        "a dragged value below the range takes the minimum");
  Check(SliderValueOf(slider, 100000) == 8192,
        "a dragged value above the range takes the maximum");
  const PresetSliderSpec noStep{.min = 1, .max = 10, .step = 0};
  Check(SliderValueOf(noStep, 7) == 7, "a zero step counts as one");
  const std::vector<std::size_t> lopsided =
      ShareRows(std::vector<std::size_t>{1, 9}, 6);
  Check(lopsided == std::vector<std::size_t>{1, 5},
        "a nearly empty block gives its unused rows to the other");
  Check(ShareRows(std::vector<std::size_t>{9, 9}, 6) ==
            std::vector<std::size_t>{3, 3},
        "two full blocks share the rows evenly");
  Check(ShareRows(std::vector<std::size_t>{2, 0}, 6) ==
            std::vector<std::size_t>{2, 0},
        "a block shows no more rows than it has");

  return test::Finish("studio_widgets");
}
