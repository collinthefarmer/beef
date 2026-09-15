#include "menu/ExpressionShelf.h"

#include "menu/FormDraw.h"
#include "menu/MenuWidgets.h"
#include "recipe/Expression.h"
#include "studio/FieldParsing.h"

#include <format>
#include <string>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include <SKSEMenuFramework.h>
#pragma clang diagnostic pop

namespace ImGui = ImGuiMCP;

namespace BetterEnchantmentEffects::Menu {
namespace {
bool HasExpression(const Studio::FormField &a_field) {
  return a_field.kind == Studio::FieldKind::kExpression ||
         a_field.kind == Studio::FieldKind::kMask ||
         a_field.kind == Studio::FieldKind::kCurve ||
         (a_field.kind == Studio::FieldKind::kSignalValue &&
          !ParseParam(a_field.text) && !Studio::LiteralColor(a_field.text));
}

Studio::FieldBinding LiteralBinding(const Studio::FormField &a_field,
                                    std::size_t a_index) {
  return [field = a_field,
          selection = NumericLiteralSelection{a_field.text, a_index}](
             const std::string &a_text) -> std::optional<Studio::RecipeEdit> {
    const auto replaced = ReplaceNumericLiteral(field.text, selection, a_text);
    return replaced && field.bind ? field.bind(*replaced) : std::nullopt;
  };
}

void PromoteLiteral(const Studio::FormField &a_field,
                    const NumericLiteral &a_literal, std::size_t a_index,
                    const Frame &a_frame) {
  if (!ImGui::SmallButton("Promote to signal")) {
    return;
  }
  std::vector<std::string> names;
  names.reserve(a_frame.recipe->signals.size() +
                a_frame.recipe->sourceRows.size() +
                a_frame.recipe->maskRows.size());
  for (const auto &signal : a_frame.recipe->signals) {
    names.push_back(signal.name);
  }
  for (const auto &source : a_frame.recipe->sourceRows) {
    names.push_back(source.name);
  }
  for (const auto &mask : a_frame.recipe->maskRows) {
    names.push_back(mask.name);
  }
  const std::string name = Studio::UniqueName("amount", names);
  const auto edit = LiteralBinding(a_field, a_index)("@" + name);
  if (edit) {
    Studio::Post(
        *a_frame.intents,
        Studio::EditRecipe{a_frame.recipe->id,
                           {Studio::AddSignal{name},
                            Studio::SetConstant{name, a_literal.value}, *edit},
                           a_field.expectedRevision});
  }
}

Studio::FormField ExpressionDraft(const Studio::FormField &a_field,
                                  const Frame &a_frame,
                                  Studio::FieldKey a_key) {
  Studio::MenuState &state = *a_frame.state;
  if (state.activeField == Studio::kNoField && !state.tuning) {
    if (state.expressionDrafts.size() >= 128) {
      state.expressionDrafts.clear();
    }
    state.expressionDrafts[a_key] = {a_field.text,
                                     a_frame.recipe->documentRevision};
  }
  const auto found = state.expressionDrafts.find(a_key);
  Studio::FormField expression = a_field;
  if (found != state.expressionDrafts.end()) {
    expression.text = found->second.text;
    expression.expectedRevision = found->second.revision;
  } else {
    expression.expectedRevision = a_frame.recipe->documentRevision;
  }
  return expression;
}

void DrawExpressionNumbers(const Studio::FormField &a_expression,
                           const Program &a_program, const Frame &a_frame) {
  std::size_t index = 0;
  for (const NumericLiteral &literal : a_program.NumericLiterals()) {
    ImGui::PushID(static_cast<int>(index));
    Dim(std::format("Number {} at character {}", index + 1,
                    literal.offset + 1));
    Studio::FormField field;
    field.name = "number";
    field.kind = Studio::FieldKind::kScalar;
    field.text = std::format("{:.9g}", literal.value);
    field.bind = LiteralBinding(a_expression, index);
    field.expectedRevision = a_expression.expectedRevision;
    DrawRowField("operand", field, a_frame);
    PromoteLiteral(a_expression, literal, index, a_frame);
    ImGui::PopID();
    ++index;
  }
}
}

bool DrawExpressionShelf(const Studio::FormField &a_field,
                         const Frame &a_frame) {
  if (!HasExpression(a_field) || !a_field.bind ||
      Studio::IsWholeReference(a_field.text)) {
    return false;
  }
  const auto key =
      Studio::HashFieldKey(Studio::State().fieldScope, a_field.name, "expr");
  const Studio::FormField expression = ExpressionDraft(a_field, a_frame, key);
  const auto program = Program::Parse(expression.text);
  if (!program || program->NumericLiterals().empty()) {
    return false;
  }
  const std::string popup = std::format("expression-numbers-{}", key);
  if (ImGui::SmallButton("123")) {
    ImGui::OpenPopup(popup.c_str());
  }
  Tooltip("Edit or promote the numbers written in this expression.");
  if (ImGui::BeginPopup(popup.c_str())) {
    DrawExpressionNumbers(expression, *program, a_frame);
    ImGui::EndPopup();
  }
  return true;
}
}
