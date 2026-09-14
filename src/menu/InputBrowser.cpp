#include "menu/InputBrowser.h"

#include "menu/MenuWidgets.h"
#include "studio/InputCatalog.h"
#include "studio/InputConnections.h"
#include "studio/Intent.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include <SKSEMenuFramework.h>
#pragma clang diagnostic pop

#include <format>
#include <string>
#include <string_view>

namespace ImGui = ImGuiMCP;

namespace BetterEnchantmentEffects::Menu {
namespace {
void ConnectButton(const char *a_label, const Frame &a_frame,
                   const Studio::FormField &a_field,
                   const Studio::InputConnectionSpec &a_spec) {
  if (!ImGui::Button(a_label)) {
    return;
  }
  const auto edits = Studio::ConnectInput(a_field, *a_frame.names, a_spec);
  if (edits) {
    Studio::Post(*a_frame.intents,
                 Studio::EditRecipe{a_frame.recipe->id, edits->edits,
                                    a_field.expectedRevision});
    ImGui::CloseCurrentPopup();
  } else {
    ImGui::TextWrapped("%s", edits.error().c_str());
  }
}

void DrawMeasures(const Studio::ActorInputInfo &a_input, const Frame &a_frame,
                  const Studio::FormField &a_field) {
  for (const Studio::InputMeasureInfo &measure : Studio::kInputMeasures) {
    const std::string name{NameOf(kMeasures, measure.measure)};
    ImGui::PushID(name.c_str());
    ConnectButton(
        "Connect", a_frame, a_field,
        {Studio::InputConnectionKind::kMeasure, a_input.name, measure.measure});
    ImGui::SameLine();
    const auto sample = Studio::InputSample(a_input, measure.measure);
    const std::string value =
        sample ? std::format("{:.4g}", *sample) : "unavailable";
    ImGui::Text("%s: %s %s", name.c_str(), value.c_str(),
                a_input.units.c_str());
    ImGui::TextWrapped("%.*s", static_cast<int>(measure.description.size()),
                       measure.description.data());
    ImGui::PopID();
  }
}

void DrawActorInput(const Studio::ActorInputInfo &a_input, const Frame &a_frame,
                    const Studio::FormField &a_field) {
  ImGui::PushID(a_input.name.c_str());
  const std::string label = std::format("{} ({})", a_input.label, a_input.name);
  if (ImGui::TreeNode(label.c_str())) {
    ImGui::TextWrapped("%s", a_input.description.c_str());
    ImGui::TextWrapped("No fixed range is assumed. Live samples describe the "
                       "selected wearer.");
    DrawMeasures(a_input, a_frame, a_field);
    ImGui::Separator();
    ConnectButton("Connect current / max", a_frame, a_field,
                  {Studio::InputConnectionKind::kFraction, a_input.name,
                   Measure::kCurrent});
    ImGui::TextWrapped("Ratio of current to maximum; returns zero when maximum "
                       "is zero or negative.");
    ConnectButton("Connect exhausted condition", a_frame, a_field,
                  {Studio::InputConnectionKind::kExhausted, a_input.name,
                   Measure::kCurrent});
    ImGui::TextWrapped("One while current is zero or below, otherwise zero. "
                       "Stays active while depleted.");
    ImGui::TreePop();
  }
  ImGui::PopID();
}
void DrawMatchingInputs(const Frame &a_frame, const Studio::FormField &a_field,
                        std::string_view a_filter) {
  for (const Studio::ActorInputInfo &input : a_frame.snapshot->actorInputs) {
    if (Studio::InputMatches(input, a_filter)) {
      DrawActorInput(input, a_frame, a_field);
    }
  }
  if (a_frame.snapshot->actorInputs.empty()) {
    ImGui::TextWrapped("Actor-value catalog is not available yet.");
  }
}

}

void DrawInputBrowser(const Frame &a_frame, const Studio::FormField &a_field) {
  if (!a_frame.snapshot || !a_frame.recipe || !a_frame.names ||
      !a_frame.intents || !Studio::CanConnectInput(a_field)) {
    return;
  }
  ImGui::PushID(a_field.name.c_str());
  if (ImGui::Button("Connect input")) {
    ImGui::OpenPopup("input-browser");
  }
  if (ImGui::BeginPopup("input-browser")) {
    ImGui::Text("Connect to %s", a_field.name.c_str());
    if (a_frame.snapshot->actorInputActorID == 0) {
      ImGui::TextWrapped("No wearer selected. Inputs can still be connected.");
    } else if (a_frame.piece && a_frame.piece->ref.actorID ==
                                    a_frame.snapshot->actorInputActorID) {
      ImGui::Text("Live samples: %s", a_frame.piece->actorName.c_str());
    }
    ConnectButton(
        "Glow after a received hit", a_frame, a_field,
        {Studio::InputConnectionKind::kHitResponse, {}, Measure::kCurrent});
    ImGui::TextWrapped("One at impact, fading to zero over one second. Tune "
                       "the created hit signal to change duration.");
    const std::string filter{LiveTextField("filter", "Search actor values",
                                           Studio::Width::Px(430.0f),
                                           a_frame.scale)};
    if (ImGui::BeginChild(
            "inputs",
            ImGui::ImVec2{520.0f * a_frame.scale, 360.0f * a_frame.scale}, 0,
            0)) {
      DrawMatchingInputs(a_frame, a_field, filter);
    }
    ImGui::EndChild();
    ImGui::EndPopup();
  }
  ImGui::PopID();
}
}
