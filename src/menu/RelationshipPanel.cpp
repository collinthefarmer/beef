// GPL-3.0-only with the additional permission in COPYING.md.
#include "menu/RelationshipPanel.h"

#include "menu/FormDraw.h"
#include "menu/MenuWidgets.h"
#include "studio/Relationships.h"

#include <algorithm>
#include <array>
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

const Studio::SignalRow *FiredSignal(const Frame &a_frame,
                                     const ResourceRef &a_driver) {
  if (a_driver.kind != ResourceKind::kSignal) {
    return nullptr;
  }
  const Studio::SignalRow *signal =
      FindByName(a_frame.recipe->signals, a_driver.name);
  return signal && !signal->event.empty() ? signal : nullptr;
}

struct RelationBlock {
  const char *title;
  const char *id;
  std::array<const char *, 3> columns;
  bool (*shows)(const Studio::Relationship &, const Studio::InspectorSubject &);
  void (*draw)(const Frame &, const Studio::Relationship &, Table &);
};

constexpr std::size_t kVisibleRows = 3;

bool IsDriverOf(const Studio::Relationship &a_link,
                const Studio::InspectorSubject &a_subject);
bool IsUsedBy(const Studio::Relationship &a_link,
              const Studio::InspectorSubject &a_subject);
void DrawDriverRow(const Frame &a_frame, const Studio::Relationship &a_link,
                   Table &a_table);
void DrawConsumerRow(const Frame &a_frame, const Studio::Relationship &a_link,
                     Table &a_table);
std::size_t RowCount(const Frame &a_frame, const RelationBlock &a_block,
                     const Studio::InspectorSubject &a_subject);
float BlockHeight(std::size_t a_rows);
void DrawBlock(const Frame &a_frame, const RelationBlock &a_block,
               const Studio::InspectorSubject &a_subject);
void DrawBlockTable(const Frame &a_frame, const RelationBlock &a_block,
                    const Studio::InspectorSubject &a_subject);

constexpr RelationBlock kRelationBlocks[]{
    {"Driven by",
     "driven-by",
     {"Driver", "Property", "Value"},
     IsDriverOf,
     DrawDriverRow},
    {"Used by",
     "used-by",
     {"Consumer", "Property", "Component"},
     IsUsedBy,
     DrawConsumerRow},
};

void Follow(Studio::LinkKind a_kind, const std::string &a_label,
            Studio::InspectorSubject a_subject, const Frame &a_frame,
            std::optional<PropertyLocation> a_property = std::nullopt) {
  if (TypedLink({a_kind, a_label})) {
    NavigateFromInspector(a_frame, std::move(a_subject), std::move(a_property));
  }
}

bool IsDriverOf(const Studio::Relationship &a_link,
                const Studio::InspectorSubject &a_subject) {
  return SubjectOf(a_link.consumer.owner) == a_subject;
}

bool IsUsedBy(const Studio::Relationship &a_link,
              const Studio::InspectorSubject &a_subject) {
  return SubjectOf(a_link.driver) == a_subject;
}
}

bool HasRelationships(const Frame &a_frame) {
  return RelationshipsHeight(a_frame) > 0.0f;
}

float RelationshipsHeight(const Frame &a_frame) {
  if (!a_frame.recipe) {
    return 0.0f;
  }
  const Studio::InspectorSubject subject = SelectionOf(a_frame).subject;
  float height = 0.0f;
  for (const RelationBlock &block : kRelationBlocks) {
    const std::size_t rows = RowCount(a_frame, block, subject);
    if (rows > 0) {
      height += RuleHeight() + BlockHeight(rows);
    }
  }
  return height;
}

void DrawRelationships(const Frame &a_frame) {
  if (!a_frame.recipe) {
    return;
  }
  const Studio::InspectorSubject subject = SelectionOf(a_frame).subject;
  for (const RelationBlock &block : kRelationBlocks) {
    DrawBlock(a_frame, block, subject);
  }
}

namespace {
std::size_t RowCount(const Frame &a_frame, const RelationBlock &a_block,
                     const Studio::InspectorSubject &a_subject) {
  return static_cast<std::size_t>(std::ranges::count_if(
      a_frame.recipe->relationships, [&](const Studio::Relationship &a_link) {
        return a_block.shows(a_link, a_subject);
      }));
}

float BlockHeight(std::size_t a_rows) {
  return ImGui::GetFrameHeightWithSpacing() *
         static_cast<float>((std::min)(a_rows, kVisibleRows) + 1);
}

void DrawBlock(const Frame &a_frame, const RelationBlock &a_block,
               const Studio::InspectorSubject &a_subject) {
  const std::size_t rows = RowCount(a_frame, a_block, a_subject);
  if (rows == 0) {
    return;
  }
  static_cast<void>(Rule(Studio::RuleSpec{.text = a_block.title}));
  if (ImGui::BeginChild(a_block.id, ImGui::ImVec2{0.0f, BlockHeight(rows)}, 0,
                        0)) {
    DrawBlockTable(a_frame, a_block, a_subject);
  }
  ImGui::EndChild();
}

void DrawBlockTable(const Frame &a_frame, const RelationBlock &a_block,
                    const Studio::InspectorSubject &a_subject) {
  Table table = Table::Begin(a_block.id,
                             {{a_block.columns[0], Studio::Width::Fit()},
                              {a_block.columns[1], Studio::Width::Fill()},
                              {a_block.columns[2], Studio::Width::Fill()}},
                             Studio::kRelationTable);
  if (!table.Open()) {
    return;
  }
  int index = 0;
  for (const Studio::Relationship &link : a_frame.recipe->relationships) {
    if (!a_block.shows(link, a_subject)) {
      continue;
    }
    ImGui::PushID(index++);
    a_block.draw(a_frame, link, table);
    ImGui::PopID();
  }
  table.End();
}

void DrawDriverRow(const Frame &a_frame, const Studio::Relationship &a_link,
                   Table &a_table) {
  a_table.Cell();
  ImGui::AlignTextToFramePadding();
  Follow(Studio::LinkKindOf(a_link.driver.kind), a_link.driver.name,
         SubjectOf(a_link.driver), a_frame);
  a_table.Cell();
  TextOrDash(a_link.consumer.property);
  a_table.Cell();
  if (const Studio::SignalRow *trigger = FiredSignal(a_frame, a_link.driver)) {
    FirePopup(*trigger, a_frame);
    ImGui::SameLine();
  }
  TextOrDash(ResourceValueText(*a_frame.recipe, a_link.driver));
}

void DrawConsumerRow(const Frame &a_frame, const Studio::Relationship &a_link,
                     Table &a_table) {
  a_table.Cell();
  ImGui::AlignTextToFramePadding();
  Follow(Studio::LinkKindOf(a_link.consumer.owner),
         OwnerName(a_link.consumer.owner, *a_frame.recipe),
         SubjectOf(a_link.consumer.owner), a_frame, a_link.consumer);
  a_table.Cell();
  TextOrDash(a_link.consumer.property);
  a_table.Cell();
  TextOrDash(a_link.consumer.component
                 ? std::format("[{}]", *a_link.consumer.component + 1)
                 : std::string{});
}
}
}
