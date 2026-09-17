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

[[nodiscard]] const Studio::GeometryRow &
OfferGeometry(const Studio::TermOffer &a_offer, const Frame &a_frame) {
  const auto found = std::ranges::find(
      a_frame.recipe->geometries, a_offer.geometry, &Studio::GeometryRow::name);
  return found == a_frame.recipe->geometries.end() ? *a_frame.geometry : *found;
}

[[nodiscard]] std::string OfferKey(const Studio::TermOffer &a_offer) {
  return std::format("{}:{}:{}", static_cast<int>(a_offer.group), a_offer.name,
                     a_offer.geometry);
}

[[nodiscard]] bool OfferMatches(const Studio::TermOffer &a_offer,
                                std::string_view a_filter) {
  const auto *group = RowOf(Studio::kOfferGroups, a_offer.group);
  const std::string_view groupName = group ? group->name : std::string_view{};
  return Studio::NameMatches(a_offer.name, a_filter) ||
         Studio::NameMatches(a_offer.detail, a_filter) ||
         Studio::NameMatches(groupName, a_filter);
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

void DrawOfferTable(std::span<const Studio::TermOffer> a_offers,
                    std::string_view a_filter, bool a_full,
                    const Frame &a_frame) {
  for (const Studio::OfferGroupSpec &spec : Studio::kOfferGroups) {
    std::vector<const Studio::TermOffer *> rows;
    for (const Studio::TermOffer &offer : a_offers) {
      if (offer.group == spec.value && OfferMatches(offer, a_filter)) {
        rows.push_back(&offer);
      }
    }
    if (rows.empty()) {
      continue;
    }
    std::ranges::sort(
        rows, [](const Studio::TermOffer *a, const Studio::TermOffer *b) {
          return a->coverage.value_or(0.0f) > b->coverage.value_or(0.0f);
        });
    ImGui::PushID(static_cast<int>(spec.value));
    const auto section = Rule(Studio::RuleSpec{
        .text = spec.name, .collapsible = true, .leadingSpace = false});
    if (section.open) {
      auto table = Table::Begin("offers",
                                {{"", Studio::Width::Fit()},
                                 {"pattern", Studio::Width::Fit()},
                                 {"detail", Studio::Width::Fill()},
                                 {"", Studio::Width::Fit()}},
                                Studio::kFormTable);
      if (table.Open()) {
        std::size_t index = 0;
        for (const Studio::TermOffer *offer : rows) {
          ImGui::PushID(static_cast<int>(index++));
          DrawOfferRow(table, *offer, a_full, a_frame);
          ImGui::PopID();
        }
        table.End();
      }
    }
    ImGui::PopID();
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
