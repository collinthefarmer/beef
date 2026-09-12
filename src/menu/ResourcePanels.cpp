#include "menu/ResourcePanels.h"

#include "menu/FormDraw.h"
#include "menu/MenuWidgets.h"
#include "menu/PaintPanel.h"
#include "studio/Rows.h"

#include "recipe/Recipe.h"
#include "studio/Edits.h"
#include "studio/Fields.h"
#include "studio/Forms.h"
#include "studio/Intent.h"
#include "studio/Names.h"
#include "studio/PaintSession.h"
#include "studio/Panels.h"
#include "studio/Widgets.h"

#include <SKSEMenuFramework.h>

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ImGui = ImGuiMCP;
using ImGuiMCP::ImVec2;

namespace BetterEnchantmentEffects::Menu {
namespace {
constexpr float kFilterWidth = 160.0f;
constexpr Studio::TableStyle kGridStyle{.borders = Studio::TableBorders::kAll,
                                        .stretch = true,
                                        .headers = true,
                                        .rowBackground = true};

struct SignalRowContext {
  const Studio::SignalNames *names = nullptr;
  std::span<const std::string> curves;
  float curveWidth = 0.0f;
  bool tunable = false;
};

void DrawSignalEditor(const Studio::SignalRow &a_signal,
                      const Studio::SignalNames &a_names,
                      const Frame &a_frame) {
  const std::vector<Studio::FormField> form =
      Studio::SignalForm(a_signal, a_names);
  const auto value =
      a_signal.kind == SignalKindId::kConstant ||
              a_signal.kind == SignalKindId::kExpr
          ? std::ranges::find(form, "value", &Studio::FormField::name)
          : form.end();
  const std::string title =
      std::format("signal {}###signal-settings", a_signal.name);
  if (DetailButton()) {
    ImGui::OpenPopup(title.c_str());
  }
  DetailModal(title.c_str(), [&]() {
    [[maybe_unused]] const std::optional<std::size_t> detail =
        DrawForm("form", form, a_frame);
  });
  ImGui::SameLine();
  if (value != form.end()) {
    DrawRowField("value", *value, a_frame);
    return;
  }
  if (!a_signal.event.empty()) {
    FirePopup(a_signal, a_frame);
    ImGui::SameLine();
  }
  ImGui::AlignTextToFramePadding();
  Dim(SignalKindName(a_signal.kind));
}

void DrawSignalCurve(const Studio::SignalRow &a_signal,
                     std::span<const std::string> a_curves, float a_width,
                     const Frame &a_frame) {
  Studio::FormField field;
  field.text = a_signal.curve;
  field.names.assign(a_curves.begin(), a_curves.end());
  field.allowEmpty = true;
  if (const std::optional<std::string> chosen = ReferenceCombo(
          "curve", field, {Studio::Width::Px(a_width), a_frame.scale})) {
    Studio::Post(*a_frame.intents, a_frame.recipe->id,
                 Studio::SetSignalCurve{
                     a_signal.name, chosen->empty()
                                        ? std::nullopt
                                        : std::optional{CurveRef{*chosen}}});
  }
  Tooltip("a declared curve applied to the signal's value; none passes it "
          "through");
}

void DrawSignalRow(Table &a_table, const Studio::SignalRow &a_signal,
                   const SignalRowContext &a_context, const Frame &a_frame) {
  ImGui::PushID(a_signal.name.c_str());
  a_table.Cell();
  if (RemoveButton(a_signal.references)) {
    Studio::Post(*a_frame.intents, a_frame.recipe->id,
                 Studio::RemoveSignal{a_signal.name});
  }
  a_table.Cell();
  DrawRowField("name",
               Studio::RowNameField(Studio::RowKind::kSignal, a_signal.name,
                                    Studio::TakenNames(Studio::RowKind::kSignal,
                                                       *a_frame.names)),
               a_frame);
  Tooltip(SignalKindName(a_signal.kind));
  a_table.Cell();
  DrawSignalEditor(a_signal, *a_context.names, a_frame);
  a_table.Cell();
  if (a_context.tunable) {
    DrawSignalCurve(a_signal, a_context.curves, a_context.curveWidth, a_frame);
  } else {
    Dim(a_signal.curve);
  }
  a_table.Cell();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("=");
  a_table.Cell();
  ImGui::AlignTextToFramePadding();
  if (a_signal.inert) {
    Problem("inert");
    Tooltip(a_signal.problem.empty() ? std::string_view{"inert"}
                                     : std::string_view{a_signal.problem});
  } else {
    ValueSwatch(a_signal.value);
  }
  ImGui::PopID();
}

void DrawSignals(const Frame &a_frame, std::string_view a_filter) {
  const Studio::RecipeRow &recipe = *a_frame.recipe;
  const Studio::SignalList list =
      Studio::BuildSignalList(recipe, LayoutOf(a_frame));
  const float scale = a_frame.scale;
  const Studio::SignalNames signalNames = Studio::SignalNamesOf(recipe);

  std::vector<std::string> curveNames;
  std::vector<std::string> curveTexts{"none"};
  curveNames.reserve(recipe.curves.size());
  curveTexts.reserve(recipe.curves.size() + 1);
  for (const Studio::TextRow &curve : recipe.curves) {
    curveNames.push_back(curve.name);
    curveTexts.push_back(Studio::ReferenceText(curve.name));
  }
  const float curveWidth = WidestOf(curveTexts) * scale;

  std::vector<std::string> names;
  names.reserve(recipe.signals.size());
  for (const Studio::SignalRow &signal : recipe.signals) {
    names.push_back(signal.name);
  }
  const float nameWidth = WidestOf(names) * scale;

  Table signals = Table::Begin("signals",
                               {{"", Studio::Width::Fit()},
                                {"signal", Studio::Width::Px(nameWidth)},
                                {"edit", Studio::Width::Fill()},
                                {"curve", Studio::Width::Px(curveWidth)},
                                {"=", Studio::Width::Fit()},
                                {"value", Studio::Width::Fit()}},
                               kGridStyle);
  if (!signals.Open()) {
    return;
  }
  const SignalRowContext tunable{&signalNames, curveNames, curveWidth, true};
  const SignalRowContext developer{&signalNames, curveNames, curveWidth, false};
  for (const Studio::SignalRow &signal : list.tunable) {
    if (Studio::NameMatches(signal.name, a_filter)) {
      DrawSignalRow(signals, signal, tunable, a_frame);
    }
  }
  for (const Studio::SignalRow &signal : list.developer) {
    if (Studio::NameMatches(signal.name, a_filter)) {
      DrawSignalRow(signals, signal, developer, a_frame);
    }
  }
  signals.End();
}

void DrawCurves(const Frame &a_frame, std::string_view a_filter) {
  const Studio::RecipeRow &recipe = *a_frame.recipe;
  const float scale = a_frame.scale;
  std::vector<std::string> names;
  names.reserve(recipe.curves.size());
  for (const Studio::TextRow &curve : recipe.curves) {
    names.push_back(curve.name);
  }
  const float nameWidth = WidestOf(names) * scale;
  Table curves = Table::Begin("curves",
                              {{"", Studio::Width::Fit()},
                               {"curve", Studio::Width::Px(nameWidth)},
                               {"expression", Studio::Width::Fill()}},
                              kGridStyle);
  if (!curves.Open()) {
    return;
  }
  for (const Studio::TextRow &curve : recipe.curves) {
    if (!Studio::NameMatches(curve.name, a_filter)) {
      continue;
    }
    ImGui::PushID(curve.name.c_str());
    curves.Cell();
    if (RemoveButton(curve.references)) {
      Studio::Post(*a_frame.intents, recipe.id,
                   Studio::RemoveCurve{curve.name});
    }
    curves.Cell();
    DrawRowField(
        "name",
        Studio::RowNameField(
            Studio::RowKind::kCurve, curve.name,
            Studio::TakenNames(Studio::RowKind::kCurve, *a_frame.names)),
        a_frame);
    curves.Cell();
    DrawRowField("text", Studio::CurveTextField(curve.name, curve.text),
                 a_frame);
    ImGui::PopID();
  }
  curves.End();
}

void DrawSources(const Frame &a_frame, std::string_view a_filter) {
  const Studio::RecipeRow &recipe = *a_frame.recipe;
  const float scale = a_frame.scale;
  const Studio::SignalNames signalNames = Studio::SignalNamesOf(recipe);
  std::vector<std::string> names;
  names.reserve(recipe.sourceRows.size());
  for (const Studio::SourceRow &source : recipe.sourceRows) {
    names.push_back(source.name);
  }
  const float nameWidth = WidestOf(names) * scale;
  Table sources =
      Table::Begin("sources",
                   {{"", Studio::Width::Fit()},
                    {"source", Studio::Width::Px(nameWidth)},
                    {"", Studio::Width::Px(ImGui::GetFrameHeight())},
                    {"definition", Studio::Width::Fill()}},
                   kGridStyle);
  if (!sources.Open()) {
    return;
  }
  for (const Studio::SourceRow &source : recipe.sourceRows) {
    if (!Studio::NameMatches(source.name, a_filter)) {
      continue;
    }
    ImGui::PushID(source.name.c_str());
    sources.Cell();
    if (RemoveButton(source.references)) {
      Studio::Post(*a_frame.intents, recipe.id,
                   Studio::RemoveSource{source.name});
    }
    sources.Cell();
    DrawRowField(
        "name",
        Studio::RowNameField(
            Studio::RowKind::kSource, source.name,
            Studio::TakenNames(Studio::RowKind::kSource, *a_frame.names)),
        a_frame);
    sources.Cell();
    const std::string title =
        std::format("source {}###source-detail", source.name);
    if (DetailButton()) {
      ImGui::OpenPopup(title.c_str());
    }
    DetailModal(title.c_str(), [&]() {
      [[maybe_unused]] const std::optional<std::size_t> detail =
          DrawForm("form", Studio::SourceForm(source, signalNames), a_frame);
    });
    sources.Cell();
    ImGui::AlignTextToFramePadding();
    Dim(DescribeSource(
        Studio::SourceKindOf(source).value_or(SourceKind{MaterialSource{}})));
    ImGui::PopID();
  }
  sources.End();
}

void DrawMasks(const Frame &a_frame, std::string_view a_filter) {
  const Studio::RecipeRow &recipe = *a_frame.recipe;
  const float scale = a_frame.scale;
  std::vector<std::string> names;
  names.reserve(recipe.maskRows.size());
  for (const Studio::TextRow &mask : recipe.maskRows) {
    names.push_back(mask.name);
  }
  const float nameWidth = WidestOf(names) * scale;
  Table masks = Table::Begin("masks",
                             {{"", Studio::Width::Fit()},
                              {"mask", Studio::Width::Px(nameWidth)},
                              {"expression", Studio::Width::Fill()}},
                             kGridStyle);
  if (!masks.Open()) {
    return;
  }
  for (const Studio::TextRow &mask : recipe.maskRows) {
    if (!Studio::NameMatches(mask.name, a_filter)) {
      continue;
    }
    ImGui::PushID(mask.name.c_str());
    masks.Cell();
    if (RemoveButton(mask.references)) {
      Studio::Post(*a_frame.intents, recipe.id, Studio::RemoveMask{mask.name});
    }
    if (mask.name != Studio::kScratchMask) {
      ImGui::SameLine();
      if (ImGui::SmallButton("edit")) {
        EditMaskAsTerms(mask, a_frame);
      }
    }
    masks.Cell();
    DrawRowField("name",
                 Studio::RowNameField(Studio::RowKind::kMask, mask.name,
                                      Studio::TakenNames(Studio::RowKind::kMask,
                                                         *a_frame.names)),
                 a_frame);
    masks.Cell();
    DrawRowField("text", Studio::MaskTextField(mask.name, mask.text), a_frame);
    ImGui::PopID();
  }
  masks.End();
}

void PostResourceAdd(const Frame &a_frame) {
  if (!a_frame.recipe || !a_frame.names || !a_frame.state || !a_frame.intents) {
    return;
  }
  const auto name = [&](Studio::RowKind a_kind, const char *a_stem) {
    return Studio::UniqueName(a_stem,
                              Studio::TakenNames(a_kind, *a_frame.names));
  };
  const std::string &id = a_frame.recipe->id;
  switch (a_frame.state->resource) {
  case Studio::ResourceTab::kSignals:
    Studio::Post(*a_frame.intents, id,
                 Studio::AddSignal{name(Studio::RowKind::kSignal, "signal")});
    break;
  case Studio::ResourceTab::kCurves:
    Studio::Post(*a_frame.intents, id,
                 Studio::AddCurve{name(Studio::RowKind::kCurve, "curve")});
    break;
  case Studio::ResourceTab::kSources:
    Studio::Post(*a_frame.intents, id,
                 Studio::AddSource{name(Studio::RowKind::kSource, "source"),
                                   MaterialSource{}});
    break;
  case Studio::ResourceTab::kMasks:
    Studio::Post(*a_frame.intents, id,
                 Studio::AddMask{name(Studio::RowKind::kMask, "mask")});
    break;
  }
}
}

std::string_view DrawResourcesRule(const Frame &a_frame) {
  static constexpr std::array<Studio::RuleButton, 2> buttons{
      Studio::RuleButton{.action = Studio::RuleAction::kClear},
      Studio::RuleButton{.action = Studio::RuleAction::kAdd},
  };
  const Studio::RuleSpec spec{.text = "Resources", .buttons = buttons};
  const RuleFilter result =
      RuleWithFilter(spec, {"filter", "filter by name",
                            kFilterWidth * a_frame.scale, a_frame.scale});
  if (result.click.clicked && result.click.index < buttons.size()) {
    switch (buttons[result.click.index].action) {
    case Studio::RuleAction::kClear:
      Studio::Post(*a_frame.intents, a_frame.recipe->id,
                   Studio::ClearResources{});
      break;
    case Studio::RuleAction::kAdd:
      PostResourceAdd(a_frame);
      break;
    default:
      break;
    }
  }
  return result.filter;
}

void DrawResources(const Frame &a_frame, std::string_view a_filter) {
  if (!ImGui::BeginTabBar("resources")) {
    return;
  }
  for (const Studio::ResourceTab tab : Studio::kResourceTabs) {
    const std::string name{Studio::ResourceTabName(tab)};
    if (!ImGui::BeginTabItem(name.c_str())) {
      continue;
    }
    if (tab != a_frame.state->resource) {
      Studio::Post(*a_frame.intents, Studio::ShowResource{tab});
    }
    if (ImGui::BeginChild(name.c_str(), ImVec2{0.0f, 0.0f}, 0, 0)) {
      switch (tab) {
      case Studio::ResourceTab::kSignals:
        DrawSignals(a_frame, a_filter);
        break;
      case Studio::ResourceTab::kCurves:
        DrawCurves(a_frame, a_filter);
        break;
      case Studio::ResourceTab::kSources:
        DrawSources(a_frame, a_filter);
        break;
      case Studio::ResourceTab::kMasks:
        DrawMasks(a_frame, a_filter);
        break;
      }
    }
    ImGui::EndChild();
    ImGui::EndTabItem();
  }
  ImGui::EndTabBar();
}
}
