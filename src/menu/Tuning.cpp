#include "menu/Tuning.h"

#include "engine/Manager.h"
#include "menu/MenuWidgets.h"
#include "studio/FieldCheck.h"
#include "studio/Fields.h"

#include <cmath>
#include <format>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include <SKSEMenuFramework.h>
#pragma clang diagnostic pop

namespace ImGui = ImGuiMCP;

namespace BetterEnchantmentEffects::Menu {
namespace {
std::optional<float> FieldValue(const Studio::FormField &a_field) {
  const auto value = ParseParam(a_field.text);
  if (!value) {
    return std::nullopt;
  }
  const auto *number = Get<float>(*value);
  return number && std::isfinite(*number) ? std::optional<float>{*number}
                                          : std::nullopt;
}

std::optional<float> TunableValue(const Studio::FormField &a_field) {
  if (!a_field.bind || (a_field.kind != Studio::FieldKind::kScalar &&
                        a_field.kind != Studio::FieldKind::kSignalValue)) {
    return std::nullopt;
  }
  return FieldValue(a_field);
}

struct TuneRange {
  std::pair<float, float> span;
  bool adjustable;
};

TuneRange ResolveTuningRange(const Studio::FormField &a_field,
                             Studio::MenuState &a_state, Studio::FieldKey a_key,
                             float a_value) {
  if (a_field.workingRange) {
    return {*a_field.workingRange, false};
  }
  if (a_field.range) {
    return {*a_field.range, false};
  }
  if (const auto found = a_state.tuningRanges.find(a_key);
      found != a_state.tuningRanges.end()) {
    return {found->second, true};
  }
  return {Studio::ValueRelativeRange(a_value), true};
}

void DrawAdjustRange(Studio::MenuState &a_state, Studio::FieldKey a_key,
                     std::pair<float, float> a_span) {
  const bool custom = a_state.tuningRanges.contains(a_key);
  if (custom) {
    const ImGui::ImVec4 *active =
        ImGui::GetStyleColorVec4(ImGui::ImGuiCol_ButtonActive);
    ImGui::PushStyleColor(ImGui::ImGuiCol_Button,
                          active ? *active
                                 : ImGui::ImVec4{0.35f, 0.35f, 0.35f, 1.0f});
  }
  if (ImGui::Button("Range")) {
    if (custom) {
      a_state.tuningRanges.erase(a_key);
    } else {
      a_state.numberBuffers[a_key] = {a_span.first, a_span.second, 0.0f};
      ImGui::OpenPopup("tuning-range");
    }
  }
  if (custom) {
    ImGui::PopStyleColor();
  }
  Tooltip("The slider spans a range around the current value. Set exact "
          "limits; exact input stays unrestricted.");
  if (ImGui::BeginPopup("tuning-range")) {
    auto &range = a_state.numberBuffers[a_key];
    ImGui::InputFloat("Minimum", &range[0]);
    ImGui::InputFloat("Maximum", &range[1]);
    const bool valid = std::isfinite(range[0]) && std::isfinite(range[1]) &&
                       range[0] < range[1];
    Disabled(!valid, [&] {
      if (ImGui::SmallButton("Use range")) {
        a_state.tuningRanges[a_key] = {range[0], range[1]};
        ImGui::CloseCurrentPopup();
      }
    });
    ImGui::EndPopup();
  }
}

struct SliderScope {
  const Studio::FormField &field;
  Studio::MenuState &state;
  Studio::FieldKey key = Studio::kNoField;
  TuneRange range{};
  bool disabled = false;
};

void DrawRangeSlider(const SliderScope &a_scope, float a_value,
                     const std::function<void(float)> &a_change,
                     const std::function<void()> &a_observe) {
  const float openerWidth =
      a_scope.range.adjustable ? ButtonWidth("Range") + ItemSpacingX() : 0.0f;
  Disabled(a_scope.disabled, [&] {
    const float slider = ImGui::GetContentRegionAvail().x - openerWidth;
    NextItemWidth(Studio::Width::Px(slider));
    if (ImGui::SliderFloat("##tune", &a_value, a_scope.range.span.first,
                           a_scope.range.span.second,
                           a_scope.field.integral ? "%.0f" : "%.3f")) {
      if (a_scope.field.integral) {
        a_value = std::round(a_value);
      }
      a_change(a_value);
    }
    Tooltip("Drag to preview; release to keep one undo step. Escape cancels. "
            "Exact input is above.");
    a_observe();
  });
  if (a_scope.range.adjustable) {
    ImGui::SameLine();
    DrawAdjustRange(a_scope.state, a_scope.key, a_scope.range.span);
  }
}

void UpdateTuning(const Studio::FormField &a_field, const Frame &a_frame,
                  Studio::FieldKey a_key, float a_value) {
  Manager *manager = Manager::GetSingleton();
  if (!manager) {
    return;
  }
  Studio::MenuState &state = *a_frame.state;
  const std::string property = std::to_string(a_key);
  if (!state.tuning) {
    const std::uint64_t id = manager->Editor().BeginGesture(
        a_frame.recipe->id,
        a_field.expectedRevision.value_or(a_frame.recipe->documentRevision),
        property);
    if (id == 0) {
      return;
    }
    state.tuning = Studio::TuningGesture{.id = id,
                                         .field = a_key,
                                         .recipeID = a_frame.recipe->id,
                                         .subject = state.selection.subject,
                                         .value = a_value,
                                         .bind = a_field.bind};
  }
  auto &gesture = *state.tuning;
  if (gesture.field != a_key || gesture.finishing) {
    return;
  }
  gesture.value = a_value;
  const std::string text = ParamText(Param{a_value});
  if (Studio::CheckField(a_field, text, *a_frame.names)) {
    return;
  }
  if (const auto edit = gesture.bind ? gesture.bind(text) : std::nullopt) {
    manager->Editor().UpdateGesture(gesture.id, property, {{*edit}});
  }
}
}

void FinishTuning(Studio::MenuState &a_state, bool a_commit) {
  if (!a_state.tuning || a_state.tuning->finishing) {
    return;
  }
  if (Manager *manager = Manager::GetSingleton()) {
    manager->Editor().EndGesture(a_state.tuning->id, a_commit);
    a_state.tuning->finishing = true;
  }
}

void BeginTuningFrame(Studio::MenuState &a_state,
                      const Studio::Snapshot &a_snapshot) {
  if (!a_state.tuning) {
    return;
  }
  const auto &result = a_snapshot.gesture;
  if (result && result->gestureID == a_state.tuning->id &&
      result->state != Studio::GesturePhase::kActive) {
    a_state.tuning.reset();
    return;
  }
  a_state.tuning->seen = false;
  if (a_state.selection.recipeID != a_state.tuning->recipeID ||
      a_state.selection.subject != a_state.tuning->subject) {
    FinishTuning(a_state, true);
  }
}

void EndTuningFrame(Studio::MenuState &a_state) {
  if (a_state.tuning && !a_state.tuning->seen) {
    FinishTuning(a_state, true);
  }
}

namespace {
void ObserveTuningItem(Studio::MenuState &a_state, Studio::FieldKey a_key) {
  if (!a_state.tuning || a_state.tuning->field != a_key) {
    return;
  }
  a_state.tuning->seen = true;
  if (Manager *manager = Manager::GetSingleton()) {
    manager->Editor().TouchGesture(a_state.tuning->id);
  }
  if (ImGui::IsKeyPressed(ImGuiMCP::ImGuiKey_Escape, false)) {
    FinishTuning(a_state, false);
  } else if (!ImGui::IsItemActive()) {
    FinishTuning(a_state, true);
  }
}

void DrawRecipeTuning(const Studio::FormField &a_field, const Frame &a_frame) {
  const auto initial = TunableValue(a_field);
  if (!initial || !a_frame.state || !a_frame.recipe || !a_frame.names ||
      (a_frame.state->paint && a_frame.state->mode == Studio::Mode::kPaint)) {
    return;
  }
  Studio::MenuState &state = *a_frame.state;
  const auto key =
      Studio::HashFieldKey(Studio::State().fieldScope, a_field.name, "tune");
  const bool owned = state.tuning && state.tuning->field == key &&
                     state.tuning->recipeID == a_frame.recipe->id;
  float value = owned ? state.tuning->value : *initial;
  const TuneRange range = ResolveTuningRange(a_field, state, key, *initial);
  const bool disabled = state.tuning && (!owned || state.tuning->finishing);
  DrawRangeSlider(
      SliderScope{a_field, state, key, range, disabled}, value,
      [&](float a_value) { UpdateTuning(a_field, a_frame, key, a_value); },
      [&] { ObserveTuningItem(state, key); });
  if (a_frame.snapshot->gesture && owned && a_frame.snapshot->gesture->error) {
    Problem(*a_frame.snapshot->gesture->error);
  }
}

void DrawSinkTuning(const Studio::FormField &a_field, const Frame &a_frame,
                    const TuningSink &a_sink) {
  const auto initial = FieldValue(a_field);
  if (!initial || !a_frame.state || !a_frame.recipe || !a_frame.names) {
    return;
  }
  Studio::MenuState &state = *a_frame.state;
  const auto key =
      Studio::HashFieldKey(Studio::State().fieldScope, a_field.name, "tune");
  float value = *initial;
  const TuneRange range = ResolveTuningRange(a_field, state, key, value);
  DrawRangeSlider(
      SliderScope{a_field, state, key, range, false}, value,
      [&](float a_value) { a_sink.commit(a_value); }, [] {});
}
}

void DrawTuning(const Studio::FormField &a_field, const Frame &a_frame,
                const std::optional<TuningSink> &a_sink) {
  if (a_sink) {
    DrawSinkTuning(a_field, a_frame, *a_sink);
  } else {
    DrawRecipeTuning(a_field, a_frame);
  }
}

bool FieldTunable(const Studio::FormField &a_field, const Frame &a_frame) {
  return TunableValue(a_field).has_value() && a_frame.state && a_frame.recipe &&
         a_frame.names &&
         !(a_frame.state->paint && a_frame.state->mode == Studio::Mode::kPaint);
}
}
