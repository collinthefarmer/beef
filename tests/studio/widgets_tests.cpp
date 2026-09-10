#include "studio/Widgets.h"
#include "test_support.h"

#include <array>

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
  Check(thumb.texture == nullptr && test::Near(thumb.size, 96.0f),
        "a thumbnail is pure data the renderer draws");
  return test::Finish("studio_widgets");
}
