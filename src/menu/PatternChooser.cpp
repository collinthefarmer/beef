// GPL-3.0-only with the additional permission in COPYING.md.
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
#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include <SKSEMenuFramework.h>
#pragma clang diagnostic pop

namespace ImGui = ImGuiMCP;

namespace BetterEnchantmentEffects::Menu {
namespace {

[[nodiscard]] const Studio::GeometryRow &
OfferGeometry(const Studio::TermOffer &a_offer, const Frame &a_frame) {
  const Studio::GeometryRow *found =
      FindByName(a_frame.recipe->geometries, a_offer.geometry);
  return found ? *found : *a_frame.geometry;
}

[[nodiscard]] std::string OfferKey(const Studio::TermOffer &a_offer) {
  return std::format("{}:{}:{}", static_cast<int>(a_offer.group), a_offer.name,
                     a_offer.geometry);
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
  ImGui::AlignTextToFramePadding();
  Dim(a_offer.coverage ? std::format("{:.1f}%", *a_offer.coverage * 100.0f)
                       : std::string{});
  a_table.Cell();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(a_offer.name.c_str());
  if (!a_offer.geometry.empty()) {
    Tooltip("geometry: " + a_offer.geometry);
  }
  a_table.Cell();
  ImGui::AlignTextToFramePadding();
  if (a_offer.unavailable) {
    Problem(*a_offer.unavailable);
  } else {
    Dim(a_offer.detail);
  }
  a_table.Cell();
  const std::string key = OfferKey(a_offer);
  const bool peeking = a_frame.state->paint && a_frame.state->paint->peek &&
                       a_frame.state->paint->peek->offer == key;
  bool peek = peeking;
  Disabled(a_offer.unavailable.has_value(), [&] {
    if (PeekButton(peek)) {
      if (peeking) {
        Studio::Post(*a_frame.intents, Studio::SetPeek{std::nullopt});
      } else {
        Studio::BuiltTerm built = Studio::BuildTerm(
            a_offer.kind, LoadedPresets(),
            Studio::PaintSources(*a_frame.state, *a_frame.recipe,
                                 *a_frame.intents));
        Studio::Post(*a_frame.intents,
                     Studio::SetPeek{Studio::PaintPeek{
                         .offer = key,
                         .expression = std::move(built.expression),
                         .sources = std::move(built.edits)}});
      }
    }
  });
  ImGui::SameLine();
  Disabled(a_full || a_offer.unavailable.has_value(), [&] {
    if (ImGui::Button("Add")) {
      AddPattern(a_offer.kind, geometry, a_frame);
      if (peeking) {
        Studio::Post(*a_frame.intents, Studio::SetPeek{std::nullopt});
      }
    }
  });
  Tooltip(a_offer.unavailable.value_or(
      "Add to the mask draft with default settings; tune the placed term in "
      "the preview."));
}

Table BeginOfferTable() {
  return Table::Begin("offers",
                      {{"", Studio::Width::Fit()},
                       {"pattern", Studio::Width::Fit()},
                       {"detail", Studio::Width::Fill()},
                       {"", Studio::Width::Fit()}},
                      Studio::kFormTable);
}

void DrawOfferGroup(const Studio::OfferGroupSpec &a_spec,
                    std::span<const Studio::TermOffer *const> a_rows,
                    bool a_full, const Frame &a_frame) {
  ImGui::PushID(static_cast<int>(a_spec.value));
  const auto section = Rule(Studio::RuleSpec{
      .text = a_spec.name, .collapsible = true, .leadingSpace = false});
  if (section.open) {
    Table table = BeginOfferTable();
    if (table.Open()) {
      std::size_t index = 0;
      for (const Studio::TermOffer *offer : a_rows) {
        ImGui::PushID(static_cast<int>(index++));
        DrawOfferRow(table, *offer, a_full, a_frame);
        ImGui::PopID();
      }
      table.End();
    }
  }
  ImGui::PopID();
}

void DrawOfferTable(std::span<const Studio::TermOffer> a_offers,
                    std::string_view a_filter, bool a_full,
                    const Frame &a_frame) {
  for (const Studio::OfferGroupSpec &spec : Studio::kOfferGroups) {
    const std::vector<const Studio::TermOffer *> rows =
        Studio::OffersInGroup(a_offers, spec.value, a_filter);
    if (!rows.empty()) {
      DrawOfferGroup(spec, rows, a_full, a_frame);
    }
  }
}
}

void DrawPatternChooser(std::span<const Studio::TermOffer> a_offers,
                        std::string_view a_filter, bool a_full,
                        const Frame &a_frame) {
  if (!a_frame.state || !a_frame.state->paint || !a_frame.recipe ||
      !a_frame.geometry || !a_frame.names || !a_frame.intents) {
    return;
  }
  const std::optional<Studio::PaintPeek> &peek = a_frame.state->paint->peek;
  if (peek && !peek->offer.empty() && !a_offers.empty() &&
      std::ranges::none_of(a_offers, [&](const Studio::TermOffer &a_offer) {
        return OfferKey(a_offer) == peek->offer;
      })) {
    Studio::Post(*a_frame.intents, Studio::SetPeek{std::nullopt});
  }
  ImGui::PushID("pattern-chooser");
  DrawOfferTable(a_offers, a_filter, a_full, a_frame);
  ImGui::PopID();
}
}
