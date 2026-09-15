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
constexpr TableStyle kFormStyle{.borders = TableBorders::kInnerHorizontal,
                                .stretch = true,
                                .headers = false,
                                .rowBackground = false};
constexpr TableStyle kColumnsStyle{.borders = TableBorders::kNone,
                                   .stretch = true,
                                   .headers = false,
                                   .rowBackground = false};

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
                            kFormStyle);
  if (table.Open()) {
    for (std::size_t i = 0; i < a_view.clauses.size(); ++i) {
      const SelectorClauseRow &clause = a_view.clauses[i];
      ImGui::PushID(static_cast<int>(i));
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

void DrawSignalReads(const std::string &a_text, const Frame &a_frame,
                     int a_depth) {
  if (a_text.empty() || a_depth >= kMaxSignalModalDepth) {
    return;
  }
  const auto program = Program::Parse(a_text);
  if (!program) {
    return;
  }
  const RecipeRow &recipe = *a_frame.recipe;
  bool any = false;
  for (const std::string &read : program->References()) {
    if (std::ranges::find(recipe.signals, read, &SignalRow::name) ==
        recipe.signals.end()) {
      continue;
    }
    if (!any) {
      Dim("reads");
      any = true;
    }
    ImGui::SameLine();
    DrawSignalModal(read, a_frame, a_depth + 1);
  }
}

void DrawSignalEditorInline(const SignalRow &a_signal, const Frame &a_frame) {
  const auto form = SignalForm(a_signal, SignalNamesOf(*a_frame.recipe));
  const bool tunable = a_signal.kind == SignalKindId::kConstant ||
                       a_signal.kind == SignalKindId::kExpr;
  const auto value =
      tunable ? std::ranges::find(form, "value", &FormField::name) : form.end();
  const auto title = std::format("signal {}###signal-settings", a_signal.name);
  if (DetailButton()) {
    ImGui::OpenPopup(title.c_str());
  }
  DetailModal(title.c_str(), [&]() {
    [[maybe_unused]] const auto detail = DrawForm("form", form, a_frame);
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
}

std::optional<std::string> FieldInput(const FormField &a_field, float a_scale,
                                      const Names &a_names) {
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
    return ReferenceCombo("value", a_field, {Width::Fill(), a_scale});
  case FieldInputKind::kChoice:
    Badge(a_field.kind);
    return ChoiceCombo("value", a_field.text, a_field.names,
                       {Width::Fill(), a_scale});
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
    return TextField("value", a_field.text, {Width::Fill(), a_scale},
                     displayCheck);
  case FieldInputKind::kPlain:
    return TextField("value", a_field.text, {Width::Fill(), a_scale},
                     displayCheck);
  case FieldInputKind::kValue:
    return ValueWidget("value", a_field, a_scale, displayCheck);
  }
  return std::nullopt;
}

void AuthorCreated(const InspectorSubject &a_subject, const Frame &a_frame) {
  if (const auto *mask = Get<MaskSubject>(a_subject)) {
    EditMaskAsTerms(TextRow{.name = mask->name, .text = "0"}, a_frame);
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
    AuthorCreated(*created, a_frame);
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

void DrawRowField(const char *a_key, const FormField &a_field,
                  const Frame &a_frame) {
  if (a_frame.recipe == nullptr || a_frame.names == nullptr ||
      a_frame.intents == nullptr) {
    return;
  }
  ImGui::PushID(a_key);
  RevealProperty(a_field, a_frame);
  if (DrawExpressionShelf(a_field, a_frame)) {
    ImGui::SameLine(0.0f, 0.0f);
  }
  if (const auto text = FieldInput(a_field, a_frame.scale, *a_frame.names)) {
    CommitField(a_field, *text, a_frame);
  }
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
                            kFormStyle);
  if (!table.Open()) {
    return open;
  }
  for (std::size_t i = 0; i < a_fields.size(); ++i) {
    const FormField &field = a_fields[i];
    ImGui::PushID(field.name.c_str());
    table.Cell();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(field.name.c_str());
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
    if (DrawExpressionShelf(field, a_frame)) {
      ImGui::SameLine(0.0f, 0.0f);
    }
    if (const auto text = FieldInput(field, a_frame.scale, *a_frame.names)) {
      CommitField(field, *text, a_frame);
    }
    DrawInputWizard(a_frame, field);
    DrawTuning(field, a_frame);
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
  auto outer = Table::Begin(a_id, columns, kColumnsStyle);
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

void DrawFormWithSignals(const char *a_id, std::span<const FormField> a_form,
                         const Frame &a_frame, std::size_t a_columns) {
  if (a_frame.recipe == nullptr) {
    return;
  }
  const auto open = DrawForm(a_id, a_form, a_frame, a_columns);
  for (std::size_t i = 0; i < a_form.size(); ++i) {
    if (a_form[i].detail != FieldDetail::kSignal) {
      continue;
    }
    ImGui::PushID(static_cast<int>(i));
    const auto title = std::format("{}###signal-modal-0", a_form[i].text);
    if (open == i) {
      ImGui::OpenPopup(title.c_str());
    }
    DetailModal(title.c_str(),
                [&]() { DrawSignalDetail(a_form[i].text, a_frame, 0); });
    ImGui::PopID();
  }
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

void DrawSignalDetail(const std::string &a_text, const Frame &a_frame,
                      int a_depth) {
  if (a_frame.recipe == nullptr) {
    return;
  }
  const RecipeRow &recipe = *a_frame.recipe;
  const auto name = ReferenceName(a_text);
  const auto it = std::ranges::find(recipe.signals, name, &SignalRow::name);
  if (a_text.empty() || !a_text.starts_with('@') ||
      it == recipe.signals.end()) {
    Dim("a literal; choose a @signal to tune it here");
    return;
  }
  ImGui::PushID(it->name.c_str());
  ImGui::Text("%s (%s)", ReferenceText(it->name).c_str(),
              std::string{SignalKindName(it->kind)}.c_str());
  ImGui::SameLine();
  if (it->live) {
    ValueSwatch(it->value);
  } else {
    Dim("Not live");
  }
  DrawSignalEditorInline(*it, a_frame);
  if (it->inert) {
    Problem(it->problem.empty() ? "inert" : "inert: " + it->problem);
  }
  DrawSignalReads(it->text, a_frame, a_depth);
  ImGui::PopID();
}

void DrawSignalModal(const std::string &a_name, const Frame &a_frame,
                     int a_depth) {
  if (a_depth > kMaxSignalModalDepth) {
    return;
  }
  ImGui::PushID(a_name.c_str());
  const auto title =
      std::format("{}###signal-modal-{}", ReferenceText(a_name), a_depth);
  if (ImGui::SmallButton(ReferenceText(a_name).c_str())) {
    ImGui::OpenPopup(title.c_str());
  }
  DetailModal(title.c_str(), [&]() {
    DrawSignalDetail(ReferenceText(a_name), a_frame, a_depth);
  });
  ImGui::PopID();
}

void DrawSelector(const SelectorView &a_selector, std::size_t a_output,
                  bool a_light, const Frame &a_frame) {
  if (a_frame.recipe == nullptr || a_frame.intents == nullptr) {
    return;
  }
  const Selector current = SelectorOf(a_selector);
  ImGui::PushID(a_light ? "light-selector" : "output-selector");
  if (a_selector.matchAll && a_selector.clauses.empty()) {
    Dim("applies to every geometry of the piece");
  } else {
    DrawSelectorClauses(a_selector, {current, a_output, a_light}, a_frame);
  }
  if (ImGui::SmallButton("Add match")) {
    PostSelector(a_frame, a_output, a_light,
                 SelectorWithClause(current, SelectorKind::kGeometry));
  }
  Tooltip("restrict the output to geometries whose addon, name or texture "
          "matches; with no match it applies to every geometry");
  ImGui::PopID();
}
}
