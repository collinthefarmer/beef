#include "menu/PatternChooser.h"

#include "engine/RecipeStore.h"
#include "menu/FormDraw.h"
#include "menu/MenuWidgets.h"
#include "studio/Names.h"
#include "studio/PaintSession.h"

#include <algorithm>
#include <format>
#include <optional>
#include <string>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include <SKSEMenuFramework.h>
#pragma clang diagnostic pop

namespace ImGui = ImGuiMCP;

namespace BetterEnchantmentEffects::Menu {
namespace {
struct PatternChoice {
  std::uint64_t sessionID = 0;
  std::string editing;
  std::optional<Studio::TermOffer> offer;
  Studio::TermKind kind;
  std::string problem;
};

bool SameOffer(const Studio::TermOffer &a_left,
               const Studio::TermOffer &a_right) {
  return a_left.group == a_right.group && a_left.name == a_right.name &&
         a_left.geometry == a_right.geometry && a_left.kind == a_right.kind;
}

void DrawPatternList(std::span<const Studio::TermOffer> a_offers,
                     std::string_view a_filter, PatternChoice &a_choice) {
  if (ImGui::BeginChild("pattern-list", ImGuiMCP::ImVec2{0.0f, 180.0f}, 0, 0)) {
    std::size_t index = 0;
    for (const Studio::TermOffer &offer : a_offers) {
      const auto *group = RowOf(Studio::kOfferGroups, offer.group);
      const std::string_view groupName =
          group ? group->name : std::string_view{};
      if (!Studio::NameMatches(offer.name, a_filter) &&
          !Studio::NameMatches(offer.detail, a_filter) &&
          !Studio::NameMatches(groupName, a_filter)) {
        continue;
      }
      ImGui::PushID(static_cast<int>(index++));
      const std::string label = std::format(
          "{} / {}{}", groupName, offer.name,
          offer.geometry.empty() ? std::string{} : " / " + offer.geometry);
      if (ImGui::Selectable(label.c_str(),
                            a_choice.offer &&
                                SameOffer(*a_choice.offer, offer))) {
        a_choice.offer = offer;
        a_choice.kind = offer.kind;
        a_choice.problem.clear();
      }
      Tooltip(offer.unavailable.value_or(offer.detail));
      ImGui::PopID();
    }
  }
  ImGui::EndChild();
}

void DrawPatternControls(PatternChoice &a_choice,
                         const Studio::GeometryRow &a_geometry,
                         const Frame &a_frame) {
  const auto fields =
      Studio::TermForm(a_choice.kind, LoadedPresets(), a_geometry);
  for (const Studio::TermField &field : fields) {
    ImGui::PushID(field.field.name.c_str());
    Dim(field.field.name);
    if (const auto text =
            FieldInput(field.field, a_frame.scale, *a_frame.names)) {
      const auto changed = field.apply ? field.apply(*text) : std::nullopt;
      if (changed) {
        a_choice.kind = *changed;
        a_choice.problem.clear();
      } else {
        a_choice.problem = "The pattern setting could not be applied.";
      }
    }
    ImGui::PopID();
  }
}

void DrawPatternPreview(const Studio::TermOffer &a_offer,
                        const Studio::GeometryRow &a_geometry) {
  const auto *reference = Get<Studio::ReferenceTerm>(a_offer.kind);
  if (!reference) {
    Dim("Add to the mask draft to preview its coverage on the armor.");
    return;
  }
  const auto &pictures = a_offer.group == Studio::OfferGroup::kMasks
                             ? a_geometry.masks
                             : a_geometry.sources;
  const auto picture =
      std::ranges::find(pictures, reference->name, &Studio::PictureRow::name);
  if (picture == pictures.end() || !picture->texture) {
    Dim("No live texture preview is available for this input.");
    return;
  }
  Dim("Current input texture");
  Thumbnail({picture->texture, picture->channel, picture->animated, 128.0f});
  if (!picture->problem.empty()) {
    Problem(picture->problem);
  }
}

void AddPattern(const PatternChoice &a_choice,
                const Studio::GeometryRow &a_geometry, const Frame &a_frame) {
  Studio::BuiltTerm built = Studio::BuildTerm(
      a_choice.kind, LoadedPresets(),
      Studio::PaintSources(*a_frame.state, *a_frame.recipe, *a_frame.intents));
  Studio::Post(
      *a_frame.intents,
      Studio::AddTerm{
          Studio::Term{
              Studio::TermOp::kAnd, std::move(built.expression),
              Studio::TermLabelOf(a_choice.kind, LoadedPresets(), a_geometry),
              a_choice.kind},
          std::move(built.edits)});
}

void DrawPatternDetails(PatternChoice &a_choice, bool a_full,
                        const Frame &a_frame) {
  if (!a_choice.offer) {
    Dim("Choose a supported pattern, source, or armor region.");
    return;
  }
  const Studio::TermOffer &offer = *a_choice.offer;
  const auto found = std::ranges::find(
      a_frame.recipe->geometries, offer.geometry, &Studio::GeometryRow::name);
  const Studio::GeometryRow &geometry =
      found == a_frame.recipe->geometries.end() ? *a_frame.geometry : *found;
  ImGui::PushID(offer.name.c_str());
  ImGui::PushID(offer.geometry.c_str());
  ImGui::PushID(static_cast<int>(offer.group));
  Dim(offer.detail);
  if (!offer.geometry.empty()) {
    Dim("Armor geometry: " + offer.geometry);
  }
  if (offer.coverage) {
    Dim(std::format("Coverage: {:.1f}%", *offer.coverage * 100.0f));
  }
  if (offer.unavailable) {
    Problem(*offer.unavailable);
  }
  DrawPatternPreview(offer, geometry);
  DrawPatternControls(a_choice, geometry, a_frame);
  if (!a_choice.problem.empty()) {
    Problem(a_choice.problem);
  }
  Disabled(a_full || offer.unavailable.has_value() || !a_choice.problem.empty(),
           [&] {
             if (ImGui::Button("Add to mask draft")) {
               AddPattern(a_choice, geometry, a_frame);
             }
           });
  Tooltip(
      "Adds a term to the draft; tune or undo it before Keep writes the mask.");
  ImGui::PopID();
  ImGui::PopID();
  ImGui::PopID();
}
}

void DrawPatternChooser(std::span<const Studio::TermOffer> a_offers,
                        std::string_view a_filter, bool a_full,
                        const Frame &a_frame) {
  if (!a_frame.state || !a_frame.state->paint || !a_frame.recipe ||
      !a_frame.geometry || !a_frame.names || !a_frame.intents) {
    return;
  }
  static PatternChoice choice;
  const std::uint64_t session = a_frame.state->paint->sessionID;
  if (choice.sessionID != session ||
      choice.editing != a_frame.state->mask.editing) {
    choice = PatternChoice{};
    choice.sessionID = session;
    choice.editing = a_frame.state->mask.editing;
  }
  if (choice.offer) {
    const auto found =
        std::ranges::find_if(a_offers, [&](const Studio::TermOffer &offer) {
          return SameOffer(offer, *choice.offer);
        });
    if (found == a_offers.end()) {
      choice.offer.reset();
    } else {
      choice.offer = *found;
    }
  }
  ImGui::PushID("pattern-chooser");
  DrawPatternList(a_offers, a_filter, choice);
  DrawPatternDetails(choice, a_full, a_frame);
  ImGui::PopID();
}
}
