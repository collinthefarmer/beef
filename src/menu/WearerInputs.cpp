// GPL-3.0-only with the additional permission in COPYING.md.
#include "menu/WearerInputs.h"

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

#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ImGui = ImGuiMCP;

namespace BetterEnchantmentEffects::Menu {
namespace {
constexpr const char *kPopup = "wearer-value";

struct Request {
  const Studio::FormField *field = nullptr;
  int frame = -1;
};

struct PopupState {
  std::string actorValue;
  std::string error;
};

struct Candidate {
  std::string name;
  std::string label;
  Studio::ActorValueHelp help;
  const Studio::ActorValueSample *sample = nullptr;
};

Request &PendingRequest();
PopupState &Popup();
bool TakeRequest(const Studio::FormField &a_field);
void DrawWearerLine(const Frame &a_frame);
void DrawCandidates(const Frame &a_frame, PopupState &a_state);
void DrawReadAs(const Frame &a_frame, const Studio::FormField &a_field,
                PopupState &a_state);
bool Connect(const Frame &a_frame, const Studio::FormField &a_field,
             const Studio::InputConnectionSpec &a_spec, std::string &a_error);
std::vector<Candidate> Candidates(const Frame &a_frame);
const Studio::ActorValueSample *SampleOf(const Frame &a_frame,
                                         std::string_view a_name);
std::string NowText(std::optional<float> a_value, std::string_view a_units);
}

void RequestWearerValue(const Studio::FormField &a_field) {
  PendingRequest() = {&a_field, ImGui::GetFrameCount()};
}

void DrawWearerValue(const Frame &a_frame, const Studio::FormField &a_field) {
  if (!a_frame.snapshot || !a_frame.recipe || !a_frame.names ||
      !a_frame.intents || !Studio::CanConnectInput(a_field)) {
    return;
  }
  if (TakeRequest(a_field)) {
    Popup() = {};
    ImGui::OpenPopup(kPopup);
  }
  if (!ImGui::BeginPopup(kPopup)) {
    return;
  }
  PopupState &state = Popup();
  ImGui::Text("Drive %s from a value of the wearer", a_field.name.c_str());
  DrawWearerLine(a_frame);
  ImGui::Separator();
  DrawCandidates(a_frame, state);
  if (!state.actorValue.empty()) {
    ImGui::Separator();
    DrawReadAs(a_frame, a_field, state);
  }
  if (!state.error.empty()) {
    Problem(state.error);
  }
  ImGui::EndPopup();
}

void ConnectWhenHit(const Frame &a_frame, const Studio::FormField &a_field) {
  if (!a_frame.recipe || !a_frame.names || !a_frame.intents) {
    return;
  }
  std::string error;
  if (!Connect(a_frame, a_field, Studio::HitResponse(), error)) {
    logger::warn("{} not connected to a hit: {}", a_field.name, error);
  }
}

void DrawActorValueReadout(const Frame &a_frame,
                           const ActorValueSignal &a_signal) {
  if (!a_frame.snapshot) {
    return;
  }
  const Studio::ActorValueHelp help =
      Studio::ActorValueHelpOf(a_signal.actorValue);
  const std::vector<Studio::ReadAsOption> options = Studio::ReadAsOptions(
      a_signal.actorValue, SampleOf(a_frame, a_signal.actorValue));
  DrawWearerLine(a_frame);
  for (const Studio::ReadAsOption &option : options) {
    if (option.spec.kind != Studio::InputConnectionKind::kMeasure) {
      continue;
    }
    const bool chosen = option.spec.measure == a_signal.measure;
    const std::string line =
        std::format("{}{}: {}", chosen ? "> " : "  ", option.label,
                    NowText(option.now, help.units));
    if (chosen) {
      ImGui::TextUnformatted(line.c_str());
    } else {
      Dim(line);
    }
  }
}

namespace {
Request &PendingRequest() {
  static Request request;
  return request;
}

PopupState &Popup() {
  static PopupState state;
  return state;
}

bool TakeRequest(const Studio::FormField &a_field) {
  Request &request = PendingRequest();
  if (request.field != &a_field || request.frame != ImGui::GetFrameCount()) {
    return false;
  }
  request = {};
  return true;
}

void DrawWearerLine(const Frame &a_frame) {
  if (a_frame.snapshot->catalogEventActor == 0) {
    Dim("No wearer selected, so no live values.");
    return;
  }
  if (a_frame.piece != nullptr &&
      a_frame.piece->ref.actorID == a_frame.snapshot->catalogEventActor) {
    Dim(std::format("Live values: {}", a_frame.piece->actorName));
  }
}

void DrawCandidates(const Frame &a_frame, PopupState &a_state) {
  const std::string filter{LiveTextField("filter", "Search wearer values",
                                         Studio::Width::Px(420.0f),
                                         a_frame.scale)};
  if (!ImGui::BeginChild(
          "values",
          ImGui::ImVec2{440.0f * a_frame.scale, 220.0f * a_frame.scale}, 0,
          0)) {
    ImGui::EndChild();
    return;
  }
  const std::vector<Candidate> candidates = Candidates(a_frame);
  if (candidates.empty()) {
    Dim("The wearer value list is not available yet.");
  }
  for (const Candidate &candidate : candidates) {
    if (!Studio::NameMatches(candidate.name, filter) &&
        !Studio::NameMatches(candidate.label, filter) &&
        !Studio::NameMatches(candidate.help.description, filter)) {
      continue;
    }
    ImGui::PushID(candidate.name.c_str());
    const std::optional<float> now =
        candidate.sample
            ? Studio::SampleAt(*candidate.sample, Measure::kCurrent)
            : std::nullopt;
    const std::string label = std::format("{}  ({})", candidate.label,
                                          NowText(now, candidate.help.units));
    if (ImGui::Selectable(label.c_str(),
                          candidate.name == a_state.actorValue)) {
      a_state.actorValue = candidate.name;
      a_state.error.clear();
    }
    if (!candidate.help.description.empty()) {
      Tooltip(candidate.help.description);
    }
    ImGui::PopID();
  }
  ImGui::EndChild();
}

void DrawReadAs(const Frame &a_frame, const Studio::FormField &a_field,
                PopupState &a_state) {
  const Studio::ActorValueHelp help =
      Studio::ActorValueHelpOf(a_state.actorValue);
  ImGui::Text("Read %s as", a_state.actorValue.c_str());
  const std::vector<Studio::ReadAsOption> options = Studio::ReadAsOptions(
      a_state.actorValue, SampleOf(a_frame, a_state.actorValue));
  for (const Studio::ReadAsOption &option : options) {
    ImGui::PushID(option.label.c_str());
    const std::string label =
        std::format("{}  ({})", option.label, NowText(option.now, help.units));
    if (ImGui::Selectable(label.c_str(), false) &&
        Connect(a_frame, a_field, option.spec, a_state.error)) {
      ImGui::CloseCurrentPopup();
    }
    Tooltip(option.description);
    ImGui::PopID();
  }
}

bool Connect(const Frame &a_frame, const Studio::FormField &a_field,
             const Studio::InputConnectionSpec &a_spec, std::string &a_error) {
  const auto edits = Studio::ConnectInput(a_field, *a_frame.names, a_spec);
  if (!edits) {
    a_error = edits.error();
    return false;
  }
  Studio::Post(*a_frame.intents,
               Studio::EditRecipe{a_frame.recipe->id, edits->edits,
                                  a_field.expectedRevision});
  return true;
}

std::vector<Candidate> Candidates(const Frame &a_frame) {
  std::vector<Candidate> candidates;
  const Studio::GameObjectCatalog *catalog =
      a_frame.snapshot
          ->catalogs[static_cast<std::size_t>(
              Studio::GameObjectKind::kActorValue)]
          .get();
  if (!catalog) {
    return candidates;
  }
  candidates.reserve(catalog->candidates.size());
  for (const Studio::GameObjectCandidate &candidate : catalog->candidates) {
    candidates.push_back({candidate.value, candidate.display,
                          Studio::ActorValueHelpOf(candidate.value),
                          SampleOf(a_frame, candidate.value)});
  }
  return candidates;
}

const Studio::ActorValueSample *SampleOf(const Frame &a_frame,
                                         std::string_view a_name) {
  return Studio::SampleNamed(a_frame.snapshot->actorValueSamples, a_name);
}

std::string NowText(std::optional<float> a_value, std::string_view a_units) {
  if (!a_value) {
    return "no live value";
  }
  return a_units.empty() ? std::format("now {:.4g}", *a_value)
                         : std::format("now {:.4g} {}", *a_value, a_units);
}
}
}
