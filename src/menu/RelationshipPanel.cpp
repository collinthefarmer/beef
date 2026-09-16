#include "menu/RelationshipPanel.h"

#include "menu/FormDraw.h"
#include "menu/MenuWidgets.h"
#include "studio/Relationships.h"

#include <algorithm>
#include <format>
#include <string>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include <SKSEMenuFramework.h>
#pragma clang diagnostic pop

namespace ImGui = ImGuiMCP;

namespace BetterEnchantmentEffects::Menu {
namespace {
Studio::InspectorSubject SubjectOf(const ResourceRef &a_resource) {
  switch (a_resource.kind) {
  case ResourceKind::kSource:
    return Studio::SourceSubject{a_resource.name};
  case ResourceKind::kMask:
    return Studio::MaskSubject{a_resource.name};
  case ResourceKind::kCurve:
    return Studio::CurveSubject{a_resource.name};
  default:
    return Studio::SignalSubject{a_resource.name};
  }
}

Studio::InspectorSubject SubjectOf(const RelationshipOwner &a_owner) {
  return Match(
      a_owner, [](const ResourceRef &a_ref) { return SubjectOf(a_ref); },
      [](const OutputOwner &a_output) -> Studio::InspectorSubject {
        return Studio::OutputSubject{a_output.output};
      },
      [](const LayerOwner &a_layer) -> Studio::InspectorSubject {
        return Studio::LayerSubject{a_layer.output, a_layer.layer};
      },
      [](const ShellOwner &) -> Studio::InspectorSubject {
        return Studio::ShellSubject{};
      },
      [](const VariantOwner &) -> Studio::InspectorSubject {
        return Studio::RecipeSubject{};
      });
}

std::string OwnerName(const RelationshipOwner &a_owner) {
  return Match(
      a_owner,
      [](const ResourceRef &a_ref) {
        return std::format(
            "{} {}", kResourceKindNames[static_cast<std::size_t>(a_ref.kind)],
            a_ref.name);
      },
      [](const OutputOwner &a_output) {
        return std::format("Output {}", a_output.output + 1);
      },
      [](const LayerOwner &a_layer) {
        return std::format("Output {} / layer {}", a_layer.output + 1,
                           a_layer.layer + 1);
      },
      [](const ShellOwner &) -> std::string { return "Shell"; },
      [](const VariantOwner &a_variant) {
        return std::format("Variant {}", a_variant.index + 1);
      });
}

void Follow(const std::string &a_label, Studio::InspectorSubject a_subject,
            const Frame &a_frame,
            std::optional<PropertyLocation> a_property = std::nullopt) {
  if (ImGui::SmallButton(a_label.c_str())) {
    NavigateFromInspector(a_frame, std::move(a_subject), std::move(a_property));
  }
}
}

void DrawDrivenBy(const Frame &a_frame,
                  const Studio::InspectorSubject &a_subject) {
  const auto driven = [&](const auto &a_link) {
    return SubjectOf(a_link.consumer.owner) == a_subject;
  };
  if (std::ranges::none_of(a_frame.recipe->relationships, driven)) {
    return;
  }
  if (!Rule(Studio::RuleSpec{.text = "Driven by",
                             .collapsible = true,
                             .leadingSpace = false})
           .open) {
    return;
  }
  Table table = Table::Begin(
      "driven-by",
      {{"Property", Studio::Width::Fill()}, {"Driver", Studio::Width::Fill()}},
      Studio::kRelationTable);
  if (!table.Open()) {
    return;
  }
  std::size_t index = 0;
  for (const auto &link : a_frame.recipe->relationships) {
    if (!driven(link)) {
      continue;
    }
    ImGui::PushID(static_cast<int>(index++));
    table.Cell();
    Dim(link.consumer.property);
    table.Cell();
    Follow("@" + link.driver.name, SubjectOf(link.driver), a_frame);
    ImGui::PopID();
  }
  table.End();
}

void DrawUsedBy(const Frame &a_frame,
                const Studio::InspectorSubject &a_subject) {
  const auto uses = [&](const auto &a_link) {
    return SubjectOf(a_link.driver) == a_subject;
  };
  if (std::ranges::none_of(a_frame.recipe->relationships, uses)) {
    return;
  }
  if (!Rule(Studio::RuleSpec{
                .text = "Used by", .collapsible = true, .leadingSpace = false})
           .open) {
    return;
  }
  Table table = Table::Begin("used-by",
                             {{"Consumer", Studio::Width::Fill()},
                              {"Property", Studio::Width::Fill()},
                              {"Component", Studio::Width::Fit()}},
                             Studio::kRelationTable);
  if (!table.Open()) {
    return;
  }
  std::size_t index = 0;
  for (const auto &link : a_frame.recipe->relationships) {
    if (!uses(link)) {
      continue;
    }
    ImGui::PushID(static_cast<int>(index++));
    table.Cell();
    Follow(OwnerName(link.consumer.owner), SubjectOf(link.consumer.owner),
           a_frame, link.consumer);
    table.Cell();
    Dim(link.consumer.property);
    table.Cell();
    const std::string component =
        link.consumer.component
            ? std::format("[{}]", *link.consumer.component + 1)
            : std::string{};
    Dim(component);
    ImGui::PopID();
  }
  table.End();
}

bool HasRelationships(const Frame &a_frame) {
  if (!a_frame.recipe) {
    return false;
  }
  const auto subject = SelectionOf(a_frame).subject;
  return std::ranges::any_of(a_frame.recipe->relationships,
                             [&](const auto &a_link) {
                               return SubjectOf(a_link.consumer.owner) ==
                                      subject;
                             }) ||
         std::ranges::any_of(a_frame.recipe->relationships,
                             [&](const auto &a_link) {
                               return SubjectOf(a_link.driver) == subject;
                             });
}

void DrawRelationships(const Frame &a_frame) {
  const auto subject = SelectionOf(a_frame).subject;
  DrawDrivenBy(a_frame, subject);
  DrawUsedBy(a_frame, subject);
}
}
