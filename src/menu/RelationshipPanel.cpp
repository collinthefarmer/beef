#include "menu/RelationshipPanel.h"

#include "menu/MenuWidgets.h"
#include "studio/Relationships.h"

#include <format>

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
    a_frame.state->revealedProperty.reset();
    a_frame.state->navigation.scroll = ImGui::GetScrollY();
    if (a_property) {
      (void)Studio::NavigateProperty(
          a_frame.state->navigation, a_frame.state->selection,
          std::move(a_subject), std::move(*a_property), *a_frame.recipe);
    } else {
      (void)Studio::Navigate(a_frame.state->navigation,
                             a_frame.state->selection, std::move(a_subject),
                             *a_frame.recipe);
    }
  }
}
}

void DrawRelationships(const Frame &a_frame) {
  const auto subject = SelectionOf(a_frame).subject;
  bool drivers = false;
  bool consumers = false;
  std::size_t index = 0;
  for (const auto &link : a_frame.recipe->relationships) {
    ImGui::PushID(static_cast<int>(index++));
    if (SubjectOf(link.consumer.owner) == subject) {
      if (!drivers) {
        Rule();
        Dim("Driven by");
        drivers = true;
      }
      Dim(link.consumer.property);
      ImGui::SameLine();
      Follow("@" + link.driver.name, SubjectOf(link.driver), a_frame);
    }
    ImGui::PopID();
  }
  for (const auto &link : a_frame.recipe->relationships) {
    ImGui::PushID(static_cast<int>(index++));
    if (SubjectOf(link.driver) == subject) {
      if (!consumers) {
        Rule();
        Dim("Used by");
        consumers = true;
      }
      const std::string component =
          link.consumer.component
              ? std::format(" [{}]", *link.consumer.component + 1)
              : "";
      Follow(OwnerName(link.consumer.owner) + " / " + link.consumer.property +
                 component,
             SubjectOf(link.consumer.owner), a_frame, link.consumer);
    }
    ImGui::PopID();
  }
}
}
