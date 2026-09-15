#include "menu/PatternChooser.h"

#include "engine/RecipeStore.h"
#include "menu/FormDraw.h"
#include "menu/MenuWidgets.h"
#include "studio/Names.h"
#include "studio/PaintSession.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <string>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include <SKSEMenuFramework.h>
#pragma clang diagnostic pop

namespace ImGui = ImGuiMCP;

namespace BetterEnchantmentEffects::Menu {
namespace {
constexpr Studio::TableStyle kOfferStyle{
    .borders = Studio::TableBorders::kInnerHorizontal,
    .stretch = true,
    .headers = false,
    .rowBackground = false};

[[nodiscard]] const Studio::GeometryRow &
OfferGeometry(const Studio::TermOffer &a_offer, const Frame &a_frame) {
  const auto found = std::ranges::find(
      a_frame.recipe->geometries, a_offer.geometry, &Studio::GeometryRow::name);
  return found == a_frame.recipe->geometries.end() ? *a_frame.geometry : *found;
}

[[nodiscard]] std::string OfferLabel(const Studio::TermOffer &a_offer) {
  const auto *group = RowOf(Studio::kOfferGroups, a_offer.group);
  const std::string_view groupName = group ? group->name : std::string_view{};
  return std::format("{} / {}{}", groupName, a_offer.name,
                     a_offer.geometry.empty() ? std::string{}
                                              : " / " + a_offer.geometry);
}

[[nodiscard]] bool OfferMatches(const Studio::TermOffer &a_offer,
                                std::string_view a_filter) {
  const auto *group = RowOf(Studio::kOfferGroups, a_offer.group);
  const std::string_view groupName = group ? group->name : std::string_view{};
  return Studio::NameMatches(a_offer.name, a_filter) ||
         Studio::NameMatches(a_offer.detail, a_filter) ||
         Studio::NameMatches(groupName, a_filter);
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
  if (picture == pictures.end() ||
      picture->texture == Studio::TextureHandle{}) {
    Dim("No live texture preview is available for this input.");
    return;
  }
  Dim("Current input texture");
  Thumbnail({picture->texture, picture->channel, picture->animated, 128.0f});
  if (!picture->problem.empty()) {
    Problem(picture->problem);
  }
}

void AddPattern(const Studio::TermKind &a_kind,
                const Studio::GeometryRow &a_geometry, const Frame &a_frame) {
  Studio::BuiltTerm built = Studio::BuildTerm(
      a_kind, LoadedPresets(),
      Studio::PaintSources(*a_frame.state, *a_frame.recipe, *a_frame.intents));
  Studio::Post(
      *a_frame.intents,
      Studio::AddTerm{
          Studio::Term{Studio::TermOp::kAnd, std::move(built.expression),
                       Studio::TermLabelOf(a_kind, LoadedPresets(), a_geometry),
                       a_kind},
          std::move(built.edits)});
}

void DrawOfferRow(Table &a_table, const Studio::TermOffer &a_offer, bool a_full,
                  const Frame &a_frame) {
  const Studio::GeometryRow &geometry = OfferGeometry(a_offer, a_frame);
  a_table.Cell();
  Disabled(a_full || a_offer.unavailable.has_value(), [&] {
    if (ImGui::SmallButton("Add")) {
      AddPattern(a_offer.kind, geometry, a_frame);
    }
  });
  Tooltip(a_offer.unavailable.value_or(
      "Add to the mask draft with default settings; tune the placed term in "
      "the preview."));
  a_table.Cell();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(OfferLabel(a_offer).c_str());
  a_table.Cell();
  ImGui::AlignTextToFramePadding();
  if (a_offer.unavailable) {
    Problem(*a_offer.unavailable);
  } else {
    std::string detail = a_offer.detail;
    if (a_offer.coverage) {
      const std::string share =
          std::format("{:.1f}%", *a_offer.coverage * 100.0f);
      detail = detail.empty() ? share : detail + ", " + share;
    }
    Dim(detail);
  }
  a_table.Cell();
  const auto title = std::format("{}###offer-preview", OfferLabel(a_offer));
  if (DetailButton()) {
    ImGui::OpenPopup(title.c_str());
  }
  DetailModal(title.c_str(), [&] {
    Dim(a_offer.detail);
    if (!a_offer.geometry.empty()) {
      Dim("Armor geometry: " + a_offer.geometry);
    }
    if (a_offer.coverage) {
      Dim(std::format("Coverage: {:.1f}%", *a_offer.coverage * 100.0f));
    }
    if (a_offer.unavailable) {
      Problem(*a_offer.unavailable);
    }
    DrawPatternPreview(a_offer, geometry);
  });
}

void DrawOfferTable(std::span<const Studio::TermOffer> a_offers,
                    std::string_view a_filter, bool a_full,
                    const Frame &a_frame) {
  auto table = Table::Begin("offers",
                            {{"", Studio::Width::Fit()},
                             {"pattern", Studio::Width::Fit()},
                             {"detail", Studio::Width::Fill()},
                             {"", Studio::Width::Px(RowButtonWidth())}},
                            kOfferStyle);
  if (!table.Open()) {
    return;
  }
  std::size_t index = 0;
  for (const Studio::TermOffer &offer : a_offers) {
    if (!OfferMatches(offer, a_filter)) {
      continue;
    }
    ImGui::PushID(static_cast<int>(index++));
    DrawOfferRow(table, offer, a_full, a_frame);
    ImGui::PopID();
  }
  table.End();
}
}

void DrawPatternChooser(std::span<const Studio::TermOffer> a_offers,
                        std::string_view a_filter, bool a_full,
                        const Frame &a_frame) {
  if (!a_frame.state || !a_frame.state->paint || !a_frame.recipe ||
      !a_frame.geometry || !a_frame.names || !a_frame.intents) {
    return;
  }
  ImGui::PushID("pattern-chooser");
  if (ImGui::BeginChild("pattern-list", ImGuiMCP::ImVec2{0.0f, 180.0f}, 0, 0)) {
    DrawOfferTable(a_offers, a_filter, a_full, a_frame);
  }
  ImGui::EndChild();
  ImGui::PopID();
}
}
