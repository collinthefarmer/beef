// GPL-3.0-only with the additional permission in COPYING.md.
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

std::string_view OwnerType(const RelationshipOwner &a_owner) {
  return Match(
      a_owner,
      [](const ResourceRef &a_ref) -> std::string_view {
        return kResourceKindNames[IndexOf(a_ref.kind)];
      },
      [](const OutputOwner &) -> std::string_view { return "output"; },
      [](const LayerOwner &) -> std::string_view { return "layer"; },
      [](const ShellOwner &) -> std::string_view { return "shell"; },
      [](const VariantOwner &) -> std::string_view { return "variant"; });
}

std::string OutputLabel(const Studio::RecipeRow &a_recipe,
                        std::size_t a_output) {
  if (a_output >= a_recipe.outputs.size()) {
    return std::format("output {}", a_output + 1);
  }
  const Studio::OutputRow &output = a_recipe.outputs[a_output];
  if (output.target == Target::kLight) {
    return std::format("light {}", a_output + 1);
  }
  return std::format("{} {}", SurfaceName(output.surface),
                     SlotName(output.slot));
}

std::string OwnerName(const RelationshipOwner &a_owner,
                      const Studio::RecipeRow &a_recipe) {
  return Match(
      a_owner, [](const ResourceRef &a_ref) { return a_ref.name; },
      [&](const OutputOwner &a_output) {
        return OutputLabel(a_recipe, a_output.output);
      },
      [&](const LayerOwner &a_layer) {
        return std::format("{} / layer {}",
                           OutputLabel(a_recipe, a_layer.output),
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
  Table table = Table::Begin("driven-by",
                             {{"Type", Studio::Width::Fit()},
                              {"Driver", Studio::Width::Fit()},
                              {"Property", Studio::Width::Fit()},
                              {"Value", Studio::Width::Fill()}},
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
    Dim(kResourceKindNames[IndexOf(link.driver.kind)]);
    table.Cell();
    Follow(link.driver.name, SubjectOf(link.driver), a_frame);
    table.Cell();
    Dim(link.consumer.property);
    table.Cell();
    DimFitted(ResourceValueText(*a_frame.recipe, link.driver));
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
                             {{"Type", Studio::Width::Fit()},
                              {"Consumer", Studio::Width::Fit()},
                              {"Property", Studio::Width::Fit()},
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
    Dim(OwnerType(link.consumer.owner));
    table.Cell();
    Follow(OwnerName(link.consumer.owner, *a_frame.recipe),
           SubjectOf(link.consumer.owner), a_frame, link.consumer);
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
