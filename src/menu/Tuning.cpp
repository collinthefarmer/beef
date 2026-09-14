#include "menu/Tuning.h"

#include "engine/Manager.h"
#include "menu/MenuWidgets.h"
#include "studio/FieldCheck.h"

#include <cmath>
#include <format>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include <SKSEMenuFramework.h>
#pragma clang diagnostic pop

namespace ImGui = ImGuiMCP;

namespace BetterEnchantmentEffects::Menu {
namespace {
std::optional<float> TunableValue(const Studio::FormField &a_field) {
  if (!a_field.bind || (a_field.kind != Studio::FieldKind::kScalar &&
                        a_field.kind != Studio::FieldKind::kSignalValue)) {
    return std::nullopt;
  }
  const auto value = ParseParam(a_field.text);
  if (!value) {
    return std::nullopt;
  }
  const auto *number = Get<float>(*value);
  return number && std::isfinite(*number) ? std::optional<float>{*number}
                                          : std::nullopt;
}

std::optional<std::pair<float, float>>
TuningRange(const Studio::FormField &a_field, Studio::MenuState &a_state,
            Studio::FieldKey a_key, float a_value) {
  if (a_field.workingRange) {
    return a_field.workingRange;
  }
  if (a_field.range) {
    return a_field.range;
  }
  if (const auto found = a_state.tuningRanges.find(a_key);
      found != a_state.tuningRanges.end()) {
    return found->second;
  }
  if (ImGui::SmallButton("Set slider range")) {
    a_state.numberBuffers[a_key] = {a_value, a_value, 0.0f};
    ImGui::OpenPopup("tuning-range");
  }
  Tooltip("No expected range is known. Choose limits for this slider; exact "
          "input remains unrestricted.");
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
  return std::nullopt;
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
  const std::string text = std::format("{:.9g}", a_value);
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
}

void DrawTuning(const Studio::FormField &a_field, const Frame &a_frame) {
  const auto initial = TunableValue(a_field);
  if (!initial || !a_frame.state || !a_frame.recipe || !a_frame.names ||
      (a_frame.state->paint && a_frame.state->mode == Studio::Mode::kPaint)) {
    return;
  }
  Studio::MenuState &state = *a_frame.state;
  const auto key = ImGui::GetID("tune");
  const bool owned = state.tuning && state.tuning->field == key &&
                     state.tuning->recipeID == a_frame.recipe->id;
  float value = owned ? state.tuning->value : *initial;
  const auto range = TuningRange(a_field, state, key, value);
  if (!range) {
    return;
  }
  Disabled(state.tuning && (!owned || state.tuning->finishing), [&] {
    NextItemWidth(Studio::Width::Fill());
    if (ImGui::SliderFloat("##tune", &value, range->first, range->second,
                           a_field.integral ? "%.0f" : "%.3g")) {
      if (a_field.integral) {
        value = std::round(value);
      }
      UpdateTuning(a_field, a_frame, key, value);
    }
    ObserveTuningItem(state, key);
  });
  Tooltip("Drag to preview; release to keep one undo step. Escape cancels. "
          "Exact input is above.");
  if (a_frame.snapshot->gesture && owned && a_frame.snapshot->gesture->error) {
    Problem(*a_frame.snapshot->gesture->error);
  }
}
}
