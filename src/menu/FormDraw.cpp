#include "menu/FormDraw.h"

#include "menu/ExpressionShelf.h"
#include "menu/InputBrowser.h"
#include "menu/MenuWidgets.h"
#include "menu/PaintPanel.h"
#include "menu/Tuning.h"
#include "recipe/Expression.h"
#include "recipe/Recipe.h"
#include "recipe/Words.h"
#include "studio/Edits.h"
#include "studio/FieldCheck.h"
#include "studio/Intent.h"
#include "studio/MenuState.h"
#include "studio/Names.h"
#include "studio/Navigation.h"
#include "studio/Panels.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include <SKSEMenuFramework.h>
#pragma clang diagnostic pop

namespace ImGui = ImGuiMCP;

namespace BetterEnchantmentEffects::Menu {
using namespace Studio;

namespace {

void RevealProperty(const FormField &a_field, const Frame &a_frame) {
  if (!a_frame.state || !a_frame.state->selection.property) {
    return;
  }
  const auto &property = *a_frame.state->selection.property;
  const bool expression =
      property.property == "expression" && a_field.name == "value";
  if (property.property != a_field.name && !expression) {
    return;
  }
  if (a_frame.state->revealedProperty != property) {
    ImGui::SetScrollHereY(0.3f);
    a_frame.state->revealedProperty = property;
  }
  Warn(property.component ? std::format("Selected property / component {}",
                                        *property.component + 1)
                          : "Selected property");
}

void Refuse(const std::string &a_field, const std::string &a_text) {
  logger::warn("{} not applied: '{}' does not parse", a_field, a_text);
}

void RefuseCreate(const std::string &a_field, const std::string &a_text) {
  logger::warn("{} not applied: '{}' could not create edits", a_field, a_text);
}

[[nodiscard]] Selector SelectorOf(const SelectorView &a_view) {
  Selector selector;
  selector.anyOf.reserve(a_view.clauses.size());
  for (const SelectorClauseRow &clause : a_view.clauses) {
    SelectorClause built;
    built.kind = clause.kind;
    if (clause.kind == SelectorKind::kAddon) {
      built.operand = FormRef::From(clause.value);
    } else {
      built.operand = clause.value;
    }
    selector.anyOf.push_back(std::move(built));
  }
  return selector;
}

void PostSelector(const Frame &a_frame, std::size_t a_output, bool a_light,
                  Selector a_selector) {
  if (a_light) {
    Post(*a_frame.intents, a_frame.recipe->id,
         SetLightSelector{a_output, std::move(a_selector)});
  } else {
    Post(*a_frame.intents, a_frame.recipe->id,
         SetOutputSelector{a_output, std::move(a_selector)});
  }
}

[[nodiscard]] std::optional<SelectorKind> ClauseKindCombo(SelectorKind a_kind,
                                                          float a_scale) {
  std::vector<std::string> names;
  for (const Named<SelectorKind> &row : kSelectorKinds) {
    names.emplace_back(row.name);
  }
  const std::string current{NameOf(kSelectorKinds, a_kind)};
  if (const auto chosen = ChoiceCombo("kind", current, names,
                                      {Width::Px(WidestOf(names)), a_scale})) {
    return FromName(kSelectorKinds, *chosen);
  }
  return std::nullopt;
}

struct SelectorTarget {
  const Selector &selector;
  std::size_t output;
  bool light;
};

void DrawSelectorClauses(const SelectorView &a_view,
                         const SelectorTarget &a_target, const Frame &a_frame) {
  auto table = Table::Begin("selector-clauses",
                            {{"match", Width::Fit()},
                             {"value", Width::Fill()},
                             {"", Width::Px(RowButtonWidth())}},
                            Studio::kFormTable);
  if (table.Open()) {
    for (std::size_t i = 0; i < a_view.clauses.size(); ++i) {
      const SelectorClauseRow &clause = a_view.clauses[i];
      ImGui::PushID(static_cast<int>(i));
      const FieldScope clauseScope(std::to_string(i));
      table.Cell();
      if (const auto kind = ClauseKindCombo(clause.kind, a_frame.scale)) {
        PostSelector(a_frame, a_target.output, a_target.light,
                     SelectorWithKind(a_target.selector, i, *kind));
      }
      table.Cell();
      if (const auto text = TextField("value", clause.value,
                                      {Width::Fill(), a_frame.scale})) {
        PostSelector(a_frame, a_target.output, a_target.light,
                     SelectorWithOperand(a_target.selector, i, *text));
      }
      table.Cell();
      if (RemoveButton(0)) {
        PostSelector(a_frame, a_target.output, a_target.light,
                     SelectorWithoutClause(a_target.selector, i));
      }
      ImGui::PopID();
    }
    table.End();
  }
}

}

struct ChannelButton {
  const char *label;
  bool ChannelSet::*bit;
  std::string_view help;
};
constexpr ChannelButton kChannelButtons[]{
    {"r", &ChannelSet::r, {}},
    {"g", &ChannelSet::g, {}},
    {"b", &ChannelSet::b, {}},
    {"a", &ChannelSet::a, {}},
};

std::optional<std::string> DrawChannels(const FormField &a_field) {
  Badge(a_field.kind);
  ImGui::SameLine();
  ChannelSet set = ChannelSet::Parse(a_field.text).value_or(ChannelSet{});
  const ChannelSet available = a_field.channelMask.value_or(ChannelSet{});
  int checked = 0;
  for (const ChannelButton &channel : kChannelButtons) {
    if (available.*channel.bit && set.*channel.bit) {
      ++checked;
    }
  }
  bool changed = false;
  bool first = true;
  for (const ChannelButton &channel : kChannelButtons) {
    if (!(available.*channel.bit)) {
      continue;
    }
    if (!first) {
      ImGui::SameLine();
    }
    first = false;
    bool &bit = set.*channel.bit;
    Disabled(bit && checked <= 1, [&] {
      if (SquareToggle(channel.label, bit, channel.help)) {
        changed = true;
      }
    });
  }
  return changed ? std::optional<std::string>{set.ToString()} : std::nullopt;
}

std::optional<std::string> FieldInput(const FormField &a_field, float a_scale,
                                      const Names &a_names,
                                      const Studio::Width &a_width) {
  const FieldScope fieldScope(a_field.name);
  const auto check = [&](const std::string &a_text) {
    return CheckField(a_field, a_text, a_names);
  };
  const TextCheck displayCheck =
      [&](const std::string &a_text) -> std::optional<std::string> {
    const std::optional<Diagnostic> diagnostic = check(a_text);
    return diagnostic ? std::optional<std::string>{ProblemText(diagnostic)}
                      : std::nullopt;
  };
  const FieldKindSpec *row = RowOf(kFieldKinds, a_field.kind);
  const FieldInputKind input = row ? row->input : FieldInputKind::kText;
  switch (input) {
  case FieldInputKind::kCombo:
    Badge(a_field.kind);
    return ReferenceCombo("value", a_field, {a_width, a_scale});
  case FieldInputKind::kChoice:
    Badge(a_field.kind);
    return ChoiceCombo("value", a_field.text, a_field.names,
                       {a_width, a_scale});
  case FieldInputKind::kToggle: {
    Badge(a_field.kind);
    bool on = a_field.text == "on";
    if (Toggle("##value", on, "")) {
      return std::string{on ? "on" : "off"};
    }
    return std::nullopt;
  }
  case FieldInputKind::kText:
    Badge(a_field.kind);
    return TextField("value", a_field.text, {a_width, a_scale}, displayCheck);
  case FieldInputKind::kPlain:
    return TextField("value", a_field.text, {a_width, a_scale}, displayCheck);
  case FieldInputKind::kValue:
    return ValueWidget("value", a_field, a_scale, displayCheck, a_width);
  case FieldInputKind::kChannels:
    return DrawChannels(a_field);
  }
  return std::nullopt;
}

void FocusCreatedSubject(const InspectorSubject &a_subject,
                         const Frame &a_frame) {
  if (const auto *mask = Get<MaskSubject>(a_subject)) {
    EditMaskAsTerms(TextRow{.name = mask->name, .text = "0"}, a_frame, true);
    return;
  }
  a_frame.state->pendingSelection = a_subject;
}

[[nodiscard]] bool PostCreate(const FormField &a_field,
                              const std::string &a_text, const Frame &a_frame) {
  if (std::ranges::find(a_field.creators, a_text) == a_field.creators.end()) {
    return false;
  }
  std::vector<RecipeEdit> edits =
      a_field.create ? a_field.create(a_text) : std::vector<RecipeEdit>{};
  if (edits.empty()) {
    RefuseCreate(a_field.name, a_text);
    return true;
  }
  const std::optional<InspectorSubject> created = CreatedSubjectOf(edits);
  Post(*a_frame.intents, EditRecipe{a_frame.recipe->id, std::move(edits)});
  if (created) {
    FocusCreatedSubject(*created, a_frame);
  }
  return true;
}

void PostField(const FormField &a_field, const std::string &a_text,
               const Frame &a_frame) {
  if (PostCreate(a_field, a_text, a_frame)) {
    return;
  }
  const std::optional<RecipeEdit> edit =
      a_field.bind ? a_field.bind(a_text) : std::nullopt;
  if (edit) {
    Post(*a_frame.intents,
         EditRecipe{a_frame.recipe->id, {*edit}, a_field.expectedRevision});
  } else {
    Refuse(a_field.name, a_text);
  }
}

void CommitField(const FormField &a_field, const std::string &a_text,
                 const Frame &a_frame) {
  if (a_text == kNewInputChoice) {
    OpenInputWizard();
  } else {
    PostField(a_field, a_text, a_frame);
  }
}

std::optional<std::string> TextInput(const FormField &a_field, float a_scale,
                                     const Names &a_names) {
  const TextCheck check =
      [&](const std::string &a_text) -> std::optional<std::string> {
    const std::optional<Diagnostic> diagnostic =
        CheckField(a_field, a_text, a_names);
    return diagnostic ? std::optional<std::string>{ProblemText(diagnostic)}
                      : std::nullopt;
  };
  return TextField("value", a_field.text, {Width::Fill(), a_scale}, check);
}

void DrawFieldInput(const FormField &a_field, const Frame &a_frame,
                    const Studio::Width &a_width = Studio::Width::Fill()) {
  if (FieldHasExpressionShelf(a_field)) {
    const FieldScope fieldScope(a_field.name);
    Badge(a_field.kind);
    DrawExpressionOpener(a_field, a_frame);
    if (const auto text = TextInput(a_field, a_frame.scale, *a_frame.names)) {
      CommitField(a_field, *text, a_frame);
    }
    return;
  }
  if (FieldHasNumberShelf(a_field, 2)) {
    const FieldScope fieldScope(a_field.name);
    DrawExpressionOpener(a_field, a_frame);
    ImGui::SameLine(0.0f, 0.0f);
  }
  if (const auto text =
          FieldInput(a_field, a_frame.scale, *a_frame.names, a_width)) {
    CommitField(a_field, *text, a_frame);
  }
}

void DrawRowField(const char *a_key, const FormField &a_field,
                  const Frame &a_frame) {
  if (a_frame.recipe == nullptr || a_frame.names == nullptr ||
      a_frame.intents == nullptr) {
    return;
  }
  ImGui::PushID(a_key);
  RevealProperty(a_field, a_frame);
  DrawFieldInput(a_field, a_frame);
  DrawInputWizard(a_frame, a_field);
  DrawTuning(a_field, a_frame);
  ImGui::PopID();
}

std::optional<std::size_t> DrawFieldTable(const char *a_id,
                                          std::span<const FormField> a_fields,
                                          const Frame &a_frame) {
  std::optional<std::size_t> open;
  if (a_frame.recipe == nullptr || a_frame.names == nullptr ||
      a_frame.intents == nullptr) {
    return open;
  }
  auto table = Table::Begin(a_id,
                            {{"field", Width::Fit()},
                             {"", Width::Px(ImGui::GetFrameHeight())},
                             {"value", Width::Fill()}},
                            Studio::kFormTable);
  if (!table.Open()) {
    return open;
  }
  for (std::size_t i = 0; i < a_fields.size(); ++i) {
    const FormField &field = a_fields[i];
    ImGui::PushID(field.name.c_str());
    table.Cell();
    ImGui::AlignTextToFramePadding();
    LabelWithHelp(field.name, field.help);
    if (!field.units.empty()) {
      ImGui::SameLine();
      Dim("(" + field.units + ")");
    }
    if (field.range) {
      Tooltip(std::format("Allowed range: {} to {}{}", field.range->first,
                          field.range->second,
                          field.integral ? "; whole numbers" : ""));
    } else if (field.workingRange) {
      Tooltip(std::format("Suggested tuning range: {} to {}; not a hard limit",
                          field.workingRange->first,
                          field.workingRange->second));
    }
    table.Cell();
    if (field.detail && DetailButton()) {
      open = i;
    }
    table.Cell();
    RevealProperty(field, a_frame);
    if (field.value) {
      ValueSwatch(*field.value);
      ImGui::SameLine();
    }
    if (FieldTunable(field, a_frame)) {
      DrawFieldInput(field, a_frame, Width::Fill(0.5f));
      DrawInputWizard(a_frame, field);
      ImGui::SameLine();
      DrawTuning(field, a_frame);
    } else {
      DrawFieldInput(field, a_frame);
      DrawInputWizard(a_frame, field);
    }
    ImGui::PopID();
  }
  table.End();
  return open;
}

std::optional<std::size_t> DrawForm(const char *a_id,
                                    std::span<const FormField> a_form,
                                    const Frame &a_frame,
                                    std::size_t a_columns) {
  if (a_columns <= 1) {
    return DrawFieldTable(a_id, a_form, a_frame);
  }
  std::optional<std::size_t> open;
  const std::size_t perColumn = (a_form.size() + a_columns - 1) / a_columns;
  std::vector<Column> columns(a_columns, Column{"", Width::Fill()});
  auto outer = Table::Begin(a_id, columns, Studio::kColumnsTable);
  if (!outer.Open()) {
    return open;
  }
  for (std::size_t c = 0; c < a_columns; ++c) {
    outer.Cell();
    const std::size_t first = (std::min)(c * perColumn, a_form.size());
    const std::size_t count = (std::min)(perColumn, a_form.size() - first);
    ImGui::PushID(static_cast<int>(c));
    if (const auto clicked =
            DrawFieldTable("column", a_form.subspan(first, count), a_frame)) {
      open = first + *clicked;
    }
    ImGui::PopID();
  }
  outer.End();
  return open;
}

namespace {
[[nodiscard]] std::optional<InspectorSubject>
ResolveReference(const std::string &a_name, const RecipeRow &a_recipe) {
  if (std::ranges::find(a_recipe.masks, a_name) != a_recipe.masks.end()) {
    return MaskSubject{a_name};
  }
  if (InspectorSubjectExists(SourceSubject{a_name}, a_recipe)) {
    return SourceSubject{a_name};
  }
  if (InspectorSubjectExists(SignalSubject{a_name}, a_recipe)) {
    return SignalSubject{a_name};
  }
  if (InspectorSubjectExists(CurveSubject{a_name}, a_recipe)) {
    return CurveSubject{a_name};
  }
  return std::nullopt;
}

void OpenFieldReferences(const FormField &a_field, const Frame &a_frame) {
  const auto program = Program::Parse(a_field.text);
  if (!program) {
    return;
  }
  std::optional<InspectorSubject> only;
  std::size_t count = 0;
  for (const std::string &read : program->References()) {
    if (const auto subject = ResolveReference(read, *a_frame.recipe)) {
      only = *subject;
      ++count;
    }
  }
  if (count == 0) {
    return;
  }
  if (count == 1) {
    NavigateFromInspector(a_frame, std::move(*only));
    return;
  }
  a_frame.state->referencePopup = a_field.text;
  ImGui::OpenPopup("field-references");
}

void DrawReferencePopup(const Frame &a_frame) {
  if (!ImGui::BeginPopup("field-references")) {
    return;
  }
  Dim("navigate to");
  const auto program = Program::Parse(a_frame.state->referencePopup);
  if (program) {
    for (const std::string &read : program->References()) {
      const auto subject = ResolveReference(read, *a_frame.recipe);
      if (!subject) {
        continue;
      }
      if (ImGui::Selectable(read.c_str())) {
        NavigateFromInspector(a_frame, *subject);
        ImGui::CloseCurrentPopup();
      }
    }
  }
  ImGui::EndPopup();
}
}

void NavigateFromInspector(const Frame &a_frame,
                           Studio::InspectorSubject a_subject,
                           std::optional<PropertyLocation> a_property) {
  if (a_frame.recipe == nullptr) {
    return;
  }
  a_frame.state->revealedProperty.reset();
  a_frame.state->navigation.scroll = ImGui::GetScrollY();
  const bool changed =
      a_property
          ? Studio::NavigateProperty(
                a_frame.state->navigation, a_frame.state->selection,
                std::move(a_subject), std::move(*a_property), *a_frame.recipe)
          : Studio::Navigate(a_frame.state->navigation,
                             a_frame.state->selection, std::move(a_subject),
                             *a_frame.recipe);
  if (changed) {
    ImGui::SetScrollY(a_frame.state->navigation.scroll);
  }
}

void DrawFormWithSignals(const char *a_id, std::span<const FormField> a_form,
                         const Frame &a_frame, std::size_t a_columns) {
  if (a_frame.recipe == nullptr) {
    return;
  }
  const auto open = DrawForm(a_id, a_form, a_frame, a_columns);
  if (open && *open < a_form.size()) {
    const FormField &field = a_form[*open];
    if (field.detail == FieldDetail::kSignal && IsWholeReference(field.text)) {
      NavigateFromInspector(a_frame, SignalSubject{ReferenceName(field.text)});
    } else if (field.detail == FieldDetail::kReferences) {
      OpenFieldReferences(field, a_frame);
    }
  }
  DrawReferencePopup(a_frame);
}

void FirePopup(const SignalRow &a_signal, const Frame &a_frame) {
  if (a_frame.state == nullptr || a_frame.geometry == nullptr ||
      a_frame.intents == nullptr) {
    return;
  }
  if (ImGui::SmallButton("Fire")) {
    ImGui::OpenPopup("fire");
  }
  if (!ImGui::BeginPopup("fire")) {
    return;
  }
  FiringDraft &draft = a_frame.state->firing;
  NextItemWidth(Width::Px(220.0f));
  if (ImGui::BeginCombo("node",
                        draft.node.empty() ? "(none)" : draft.node.c_str())) {
    if (ImGui::Selectable("(none)", draft.node.empty())) {
      draft.node.clear();
    }
    for (const BoneCoverage &bone : a_frame.geometry->bones) {
      if (ImGui::Selectable(bone.name.c_str(), bone.name == draft.node)) {
        draft.node = bone.name;
      }
    }
    ImGui::EndCombo();
  }
  float offset[3]{draft.offset.x, draft.offset.y, draft.offset.z};
  NextItemWidth(Width::Px(220.0f));
  if (ImGui::DragFloat3("offset", offset, 1.0f)) {
    draft.offset = Vec3{offset[0], offset[1], offset[2]};
  }
  NextItemWidth(Width::Px(220.0f));
  ImGui::DragFloat("random", &draft.random, 1.0f, 0.0f, 200.0f);
  NextItemWidth(Width::Px(220.0f));
  ImGui::DragFloat("value", &draft.value, 0.01f);
  if (ImGui::Button("Fire now")) {
    Post(*a_frame.intents,
         FireTrigger{ActorOf(a_frame), a_signal.event, draft.node, draft.offset,
                     draft.random, draft.value});
  }
  ImGui::EndPopup();
}

void DrawSelector(const SelectorView &a_selector, std::size_t a_output,
                  bool a_light, const Frame &a_frame) {
  if (a_frame.recipe == nullptr || a_frame.intents == nullptr) {
    return;
  }
  const Selector current = SelectorOf(a_selector);
  ImGui::PushID(a_light ? "light-selector" : "output-selector");
  const bool everyGeometry = a_selector.matchAll && a_selector.clauses.empty();
  static_cast<void>(Rule(
      Studio::RuleSpec{.text = "Applies to"}, ButtonWidth("Add match"),
      [&]() {
        if (ImGui::Button("Add match")) {
          PostSelector(a_frame, a_output, a_light,
                       SelectorWithClause(current, SelectorKind::kGeometry));
        }
        Tooltip("restrict the output to geometries whose addon, name or "
                "texture matches; with no match it applies to every geometry");
      },
      everyGeometry
          ? std::function<void()>{[]() { Dim("every geometry of the piece"); }}
          : std::function<void()>{}));
  if (!everyGeometry) {
    DrawSelectorClauses(a_selector, {current, a_output, a_light}, a_frame);
  }
  ImGui::PopID();
}

void DrawInspectorFields(const Studio::Inspector &a_inspector,
                         const Frame &a_frame) {
  const std::vector<FormField> form = InspectorForm(a_inspector);
  const auto opened = DrawForm("fields", form, a_frame);
  if (!opened || *opened >= form.size() || !form[*opened].detail) {
    DrawReferencePopup(a_frame);
    return;
  }
  std::optional<InspectorSubject> destination;
  switch (*form[*opened].detail) {
  case FieldDetail::kReferences:
    OpenFieldReferences(form[*opened], a_frame);
    break;
  case FieldDetail::kSource:
    if (IsWholeReference(form[*opened].text)) {
      const std::string name = ReferenceName(form[*opened].text);
      if (std::ranges::find(a_frame.recipe->masks, name) !=
          a_frame.recipe->masks.end()) {
        destination = MaskSubject{name};
      } else if (InspectorSubjectExists(SourceSubject{name}, *a_frame.recipe)) {
        destination = SourceSubject{name};
      } else {
        destination = SignalSubject{name};
      }
    }
    break;
  case FieldDetail::kMask:
    if (IsWholeReference(form[*opened].text)) {
      const std::string name = ReferenceName(form[*opened].text);
      destination = InspectorSubjectExists(MaskSubject{name}, *a_frame.recipe)
                        ? InspectorSubject{MaskSubject{name}}
                        : InspectorSubject{SourceSubject{name}};
    }
    break;
  case FieldDetail::kCurve:
    if (a_inspector.curve) {
      destination = CurveSubject{a_inspector.curve->name};
    }
    break;
  case FieldDetail::kOpacity:
  case FieldDetail::kColor:
  case FieldDetail::kSignal:
    if (IsWholeReference(form[*opened].text)) {
      destination = SignalSubject{ReferenceName(form[*opened].text)};
    }
    break;
  }
  if (destination) {
    NavigateFromInspector(a_frame, std::move(*destination));
  }
  DrawReferencePopup(a_frame);
}
}
