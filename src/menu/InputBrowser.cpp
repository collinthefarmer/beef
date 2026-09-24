// GPL-3.0-only with the additional permission in COPYING.md.
#include "menu/InputBrowser.h"

#include "menu/MenuWidgets.h"
#include "studio/GameObjects.h"
#include "studio/InputCatalog.h"
#include "studio/InputConnections.h"
#include "studio/Intent.h"
#include "studio/Names.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include <SKSEMenuFramework.h>
#pragma clang diagnostic pop

#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ImGui = ImGuiMCP;

namespace BetterEnchantmentEffects::Menu {
namespace {
enum class WizardStep : std::uint8_t {
  kDriver,
  kActorValue,
  kMapping,
  kConfirm,
};

enum class Driver : std::uint8_t {
  kNone,
  kActorValue,
  kHit,
};

enum class Mapping : std::uint8_t {
  kMeasure,
  kFraction,
  kExhausted,
};

struct WizardState {
  bool advanced = false;
  WizardStep step = WizardStep::kDriver;
  Driver driver = Driver::kNone;
  std::string actorValue;
  Mapping mapping = Mapping::kMeasure;
  Measure measure = Measure::kCurrent;
  std::string error;
};

WizardState &Wizard() {
  static WizardState state;
  return state;
}

void ResetWizard() { Wizard() = WizardState{}; }

[[nodiscard]] Studio::InputConnectionSpec SpecOf(const WizardState &a_state) {
  if (a_state.driver == Driver::kHit) {
    return {Studio::InputConnectionKind::kHitResponse, {}, Measure::kCurrent};
  }
  if (a_state.mapping == Mapping::kFraction) {
    return {Studio::InputConnectionKind::kFraction, a_state.actorValue,
            Measure::kCurrent};
  }
  if (a_state.mapping == Mapping::kExhausted) {
    return {Studio::InputConnectionKind::kExhausted, a_state.actorValue,
            Measure::kCurrent};
  }
  return {Studio::InputConnectionKind::kMeasure, a_state.actorValue,
          a_state.measure};
}

[[nodiscard]] std::expected<Studio::EditBatch, std::string>
BuildEdits(const Frame &a_frame, const Studio::FormField *a_field,
           const Studio::InputConnectionSpec &a_spec) {
  if (a_field != nullptr) {
    return Studio::ConnectInput(*a_field, *a_frame.names, a_spec);
  }
  return Studio::CreateInput(*a_frame.names, a_spec);
}

[[nodiscard]] bool ApplySpec(const Frame &a_frame,
                             const Studio::FormField *a_field,
                             const Studio::InputConnectionSpec &a_spec,
                             std::string &a_error) {
  const auto edits = BuildEdits(a_frame, a_field, a_spec);
  if (!edits) {
    a_error = edits.error();
    return false;
  }
  const std::optional<std::uint64_t> revision =
      a_field != nullptr ? a_field->expectedRevision : std::nullopt;
  Studio::Post(*a_frame.intents,
               Studio::EditRecipe{a_frame.recipe->id, edits->edits, revision});
  return true;
}

struct ActorValueView {
  std::string name;
  std::string label;
  Studio::ActorValueHelp help;
  const Studio::ActorValueSample *sample = nullptr;
};

[[nodiscard]] const Studio::GameObjectCatalog *
ActorValueCatalog(const Frame &a_frame) {
  return a_frame.snapshot
      ->catalogs[static_cast<std::size_t>(Studio::GameObjectKind::kActorValue)]
      .get();
}

[[nodiscard]] ActorValueView
ViewOf(const Frame &a_frame, const Studio::GameObjectCandidate &a_candidate,
       std::size_t a_index) {
  const std::vector<Studio::ActorValueSample> &samples =
      a_frame.snapshot->actorValueSamples;
  const Studio::ActorValueSample *sample =
      a_index < samples.size() ? &samples[a_index] : nullptr;
  return {a_candidate.value, a_candidate.display,
          Studio::ActorValueHelpOf(a_candidate.value), sample};
}

[[nodiscard]] std::vector<ActorValueView> ActorValues(const Frame &a_frame) {
  std::vector<ActorValueView> views;
  const Studio::GameObjectCatalog *catalog = ActorValueCatalog(a_frame);
  if (!catalog) {
    return views;
  }
  views.reserve(catalog->candidates.size());
  for (std::size_t i = 0; i < catalog->candidates.size(); ++i) {
    views.push_back(ViewOf(a_frame, catalog->candidates[i], i));
  }
  return views;
}

[[nodiscard]] std::optional<ActorValueView>
FindActorValue(const Frame &a_frame, std::string_view a_name) {
  const Studio::GameObjectCatalog *catalog = ActorValueCatalog(a_frame);
  if (!catalog) {
    return std::nullopt;
  }
  for (std::size_t i = 0; i < catalog->candidates.size(); ++i) {
    if (catalog->candidates[i].value == a_name) {
      return ViewOf(a_frame, catalog->candidates[i], i);
    }
  }
  return std::nullopt;
}

[[nodiscard]] bool MatchesActorValue(const ActorValueView &a_view,
                                     std::string_view a_filter) {
  return Studio::NameMatches(a_view.name, a_filter) ||
         Studio::NameMatches(a_view.label, a_filter) ||
         Studio::NameMatches(a_view.help.description, a_filter);
}

[[nodiscard]] std::optional<float> SampleFor(const ActorValueView &a_view,
                                             Measure a_measure) {
  return a_view.sample ? Studio::SampleAt(*a_view.sample, a_measure)
                       : std::nullopt;
}

void DrawWearerHeader(const Frame &a_frame) {
  if (a_frame.snapshot->catalogEventActor == 0) {
    ImGui::TextWrapped("No wearer selected. Inputs can still be connected.");
  } else if (a_frame.piece != nullptr &&
             a_frame.piece->ref.actorID ==
                 a_frame.snapshot->catalogEventActor) {
    ImGui::Text("Live samples: %s", a_frame.piece->actorName.c_str());
  }
}

void ConnectButton(const char *a_label, const Frame &a_frame,
                   const Studio::FormField *a_field,
                   const Studio::InputConnectionSpec &a_spec) {
  if (!ImGui::Button(a_label)) {
    return;
  }
  std::string error;
  if (ApplySpec(a_frame, a_field, a_spec, error)) {
    ImGui::CloseCurrentPopup();
  } else {
    ImGui::TextWrapped("%s", error.c_str());
  }
}

void DrawMeasures(const ActorValueView &a_input, const Frame &a_frame,
                  const Studio::FormField *a_field) {
  for (const Studio::InputMeasureInfo &measure : Studio::kInputMeasures) {
    const std::string name{NameOf(kMeasures, measure.measure)};
    ImGui::PushID(name.c_str());
    ConnectButton(
        "Connect", a_frame, a_field,
        {Studio::InputConnectionKind::kMeasure, a_input.name, measure.measure});
    ImGui::SameLine();
    const auto sample = SampleFor(a_input, measure.measure);
    const std::string value =
        sample ? std::format("{:.4g}", *sample) : "unavailable";
    ImGui::Text("%s: %s %s", name.c_str(), value.c_str(),
                a_input.help.units.c_str());
    ImGui::TextWrapped("%.*s", static_cast<int>(measure.description.size()),
                       measure.description.data());
    ImGui::PopID();
  }
}

void DrawActorInput(const ActorValueView &a_input, const Frame &a_frame,
                    const Studio::FormField *a_field) {
  ImGui::PushID(a_input.name.c_str());
  const std::string label = std::format("{} ({})", a_input.label, a_input.name);
  if (ImGui::TreeNode(label.c_str())) {
    ImGui::TextWrapped("%s", a_input.help.description.c_str());
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

void DrawMatchingInputs(const Frame &a_frame, const Studio::FormField *a_field,
                        std::string_view a_filter) {
  const std::vector<ActorValueView> values = ActorValues(a_frame);
  for (const ActorValueView &input : values) {
    if (MatchesActorValue(input, a_filter)) {
      DrawActorInput(input, a_frame, a_field);
    }
  }
  if (values.empty()) {
    ImGui::TextWrapped("Actor-value catalog is not available yet.");
  }
}

void DrawAdvanced(const Frame &a_frame, const Studio::FormField *a_field) {
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
          ImGui::ImVec2{520.0f * a_frame.scale, 320.0f * a_frame.scale}, 0,
          0)) {
    DrawMatchingInputs(a_frame, a_field, filter);
  }
  ImGui::EndChild();
}

void DrawDriverStep(WizardState &a_state) {
  ImGui::TextWrapped("What should drive this input?");
  if (ImGui::Selectable("An actor value of the wearer",
                        a_state.driver == Driver::kActorValue)) {
    a_state.driver = Driver::kActorValue;
    a_state.step = WizardStep::kActorValue;
  }
  ImGui::TextWrapped("Health, magicka, stamina or any other value read from "
                     "the wearer.");
  if (ImGui::Selectable("A received hit", a_state.driver == Driver::kHit)) {
    a_state.driver = Driver::kHit;
    a_state.step = WizardStep::kConfirm;
  }
  ImGui::TextWrapped("Glows at impact and fades to zero over one second.");
}

void DrawActorValueStep(const Frame &a_frame, WizardState &a_state) {
  ImGui::TextWrapped("Pick an actor value.");
  const std::string filter{LiveTextField("wizard-filter", "Search actor values",
                                         Studio::Width::Px(430.0f),
                                         a_frame.scale)};
  if (ImGui::BeginChild(
          "wizard-inputs",
          ImGui::ImVec2{520.0f * a_frame.scale, 240.0f * a_frame.scale}, 0,
          0)) {
    const std::vector<ActorValueView> values = ActorValues(a_frame);
    if (values.empty()) {
      ImGui::TextWrapped("Actor-value catalog is not available yet.");
    }
    for (const ActorValueView &input : values) {
      if (!MatchesActorValue(input, filter)) {
        continue;
      }
      ImGui::PushID(input.name.c_str());
      const std::string label = std::format("{} ({})", input.label, input.name);
      if (ImGui::Selectable(label.c_str(), input.name == a_state.actorValue)) {
        a_state.actorValue = input.name;
        a_state.step = WizardStep::kMapping;
      }
      const auto sample = SampleFor(input, Measure::kCurrent);
      if (sample) {
        ImGui::Text("Now: %s %s", std::format("{:.4g}", *sample).c_str(),
                    input.help.units.c_str());
      }
      ImGui::TextWrapped("%s", input.help.description.c_str());
      ImGui::PopID();
    }
  }
  ImGui::EndChild();
}

void DrawMeasurePicker(const Frame &a_frame, WizardState &a_state) {
  const std::optional<ActorValueView> input =
      FindActorValue(a_frame, a_state.actorValue);
  for (const Studio::InputMeasureInfo &measure : Studio::kInputMeasures) {
    const std::string name{NameOf(kMeasures, measure.measure)};
    ImGui::PushID(name.c_str());
    if (ImGui::Selectable(name.c_str(), a_state.measure == measure.measure)) {
      a_state.measure = measure.measure;
    }
    if (input) {
      const auto sample = SampleFor(*input, measure.measure);
      const std::string value =
          sample ? std::format("{:.4g}", *sample) : "unavailable";
      ImGui::Text("Now: %s %s", value.c_str(), input->help.units.c_str());
    }
    ImGui::TextWrapped("%.*s", static_cast<int>(measure.description.size()),
                       measure.description.data());
    ImGui::PopID();
  }
}

void DrawMappingStep(const Frame &a_frame, WizardState &a_state) {
  ImGui::TextWrapped("How should %s map to a signal?",
                     a_state.actorValue.c_str());
  if (ImGui::Selectable("Raw measure", a_state.mapping == Mapping::kMeasure)) {
    a_state.mapping = Mapping::kMeasure;
  }
  ImGui::TextWrapped("Reads one measure of the value directly.");
  if (ImGui::Selectable("Current / max fraction",
                        a_state.mapping == Mapping::kFraction)) {
    a_state.mapping = Mapping::kFraction;
  }
  ImGui::TextWrapped("Ratio of current to maximum, guarded to zero when the "
                     "maximum is zero or negative.");
  if (ImGui::Selectable("Exhausted below zero",
                        a_state.mapping == Mapping::kExhausted)) {
    a_state.mapping = Mapping::kExhausted;
  }
  ImGui::TextWrapped("One while the value is depleted, otherwise zero.");
  if (a_state.mapping == Mapping::kMeasure) {
    ImGui::Separator();
    DrawMeasurePicker(a_frame, a_state);
  }
}

void DrawConfirmStep(WizardState &a_state, bool a_bind) {
  if (a_state.driver == Driver::kHit) {
    ImGui::TextWrapped("Creates a hit trigger and a fading glow signal.");
  } else if (a_state.mapping == Mapping::kFraction) {
    ImGui::TextWrapped("Creates current and maximum signals for %s and their "
                       "guarded ratio.",
                       a_state.actorValue.c_str());
  } else if (a_state.mapping == Mapping::kExhausted) {
    ImGui::TextWrapped("Creates a signal that is one while %s is depleted.",
                       a_state.actorValue.c_str());
  } else {
    const std::string measure{NameOf(kMeasures, a_state.measure)};
    ImGui::TextWrapped("Creates a signal reading the %s of %s.",
                       measure.c_str(), a_state.actorValue.c_str());
  }
  if (a_bind) {
    ImGui::TextWrapped("The created signal is connected to this property.");
  }
  if (!a_state.error.empty()) {
    ImGui::TextWrapped("%s", a_state.error.c_str());
  }
}

[[nodiscard]] const char *StepTitle(WizardStep a_step) {
  switch (a_step) {
  case WizardStep::kDriver:
    return "Step 1: driver";
  case WizardStep::kActorValue:
    return "Step 2: actor value";
  case WizardStep::kMapping:
    return "Step 3: mapping";
  case WizardStep::kConfirm:
    return "Step 4: confirm";
  }
  return "";
}

[[nodiscard]] WizardStep PreviousStep(const WizardState &a_state) {
  switch (a_state.step) {
  case WizardStep::kActorValue:
    return WizardStep::kDriver;
  case WizardStep::kMapping:
    return WizardStep::kActorValue;
  case WizardStep::kConfirm:
    return a_state.driver == Driver::kHit ? WizardStep::kDriver
                                          : WizardStep::kMapping;
  case WizardStep::kDriver:
    return WizardStep::kDriver;
  }
  return WizardStep::kDriver;
}

void DrawGuided(const Frame &a_frame, WizardState &a_state,
                const Studio::FormField *a_field) {
  static_cast<void>(Rule(Studio::RuleSpec{.text = StepTitle(a_state.step)}));
  switch (a_state.step) {
  case WizardStep::kDriver:
    DrawDriverStep(a_state);
    break;
  case WizardStep::kActorValue:
    DrawActorValueStep(a_frame, a_state);
    break;
  case WizardStep::kMapping:
    DrawMappingStep(a_frame, a_state);
    break;
  case WizardStep::kConfirm:
    DrawConfirmStep(a_state, a_field != nullptr);
    break;
  }
  ImGui::Separator();
  if (a_state.step != WizardStep::kDriver) {
    if (ImGui::Button("Back")) {
      a_state.error.clear();
      a_state.step = PreviousStep(a_state);
    }
    ImGui::SameLine();
  }
  if (a_state.step == WizardStep::kMapping) {
    if (ImGui::Button("Next")) {
      a_state.step = WizardStep::kConfirm;
    }
  } else if (a_state.step == WizardStep::kConfirm) {
    if (ImGui::Button(a_field != nullptr ? "Connect" : "Create input")) {
      if (ApplySpec(a_frame, a_field, SpecOf(a_state), a_state.error)) {
        ImGui::CloseCurrentPopup();
      }
    }
  }
}

void DrawWizardPopup(const Frame &a_frame, const char *a_id,
                     const char *a_title, const Studio::FormField *a_field) {
  if (!ImGui::BeginPopup(a_id)) {
    return;
  }
  ImGui::Text("%s", a_title);
  DrawWearerHeader(a_frame);
  WizardState &state = Wizard();
  ImGui::Checkbox("Advanced", &state.advanced);
  ImGui::Separator();
  if (state.advanced) {
    DrawAdvanced(a_frame, a_field);
  } else {
    DrawGuided(a_frame, state, a_field);
  }
  ImGui::EndPopup();
}
}

void OpenInputWizard() {
  ResetWizard();
  ImGui::OpenPopup("input-wizard");
}

void DrawInputWizard(const Frame &a_frame, const Studio::FormField &a_field) {
  if (!a_frame.snapshot || !a_frame.recipe || !a_frame.names ||
      !a_frame.intents || !Studio::CanConnectInput(a_field)) {
    return;
  }
  const std::string title = std::format("Connect to {}", a_field.name);
  DrawWizardPopup(a_frame, "input-wizard", title.c_str(), &a_field);
}

}
