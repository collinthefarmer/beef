// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/Navigation.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
namespace {
[[nodiscard]] bool IsCascade(const RecipeEdit &a_edit) {
  const auto *signal = Get<RemoveSignal>(a_edit);
  const auto *curve = Get<RemoveCurve>(a_edit);
  const auto *mask = Get<RemoveMask>(a_edit);
  const auto *source = Get<RemoveSource>(a_edit);
  return (signal && signal->cascade) || (curve && curve->cascade) ||
         (mask && mask->cascade) || (source && source->cascade);
}

bool PropertyMatches(const InspectorSubject &a_subject,
                     const PropertyLocation &a_property) {
  return Match(
      a_property.owner,
      [&](const OutputOwner &owner) {
        const auto *subject = Get<OutputSubject>(a_subject);
        return subject && subject->output == owner.output;
      },
      [&](const LayerOwner &owner) {
        const auto *subject = Get<LayerSubject>(a_subject);
        return subject && subject->output == owner.output &&
               subject->layer == owner.layer;
      },
      [&](const ShellOwner &) { return Is<ShellSubject>(a_subject); },
      [&](const VariantOwner &) { return Is<RecipeSubject>(a_subject); },
      [&](const ResourceRef &owner) {
        switch (owner.kind) {
        case ResourceKind::kSignal: {
          const auto *subject = Get<SignalSubject>(a_subject);
          return subject && subject->name == owner.name;
        }
        case ResourceKind::kSource: {
          const auto *subject = Get<SourceSubject>(a_subject);
          return subject && subject->name == owner.name;
        }
        case ResourceKind::kMask: {
          const auto *subject = Get<MaskSubject>(a_subject);
          return subject && subject->name == owner.name;
        }
        case ResourceKind::kCurve: {
          const auto *subject = Get<CurveSubject>(a_subject);
          return subject && subject->name == owner.name;
        }
        default:
          return false;
        }
      });
}

bool PropertyExists(const PropertyLocation &a_property,
                    const RecipeRow &a_recipe) {
  return std::ranges::any_of(
      a_recipe.relationships,
      [&](const Relationship &link) { return link.consumer == a_property; });
}

[[nodiscard]] const OutputRow *OutputAt(const RecipeRow &a_recipe,
                                        std::size_t a_index) {
  return FindBy(a_recipe.outputs, a_index, &OutputRow::index);
}

void AlignOutputSelection(Selection &a_selection, const RecipeRow &a_recipe) {
  const auto *outputSubject = Get<OutputSubject>(a_selection.subject);
  const auto *layerSubject = Get<LayerSubject>(a_selection.subject);
  if (!outputSubject && !layerSubject) {
    return;
  }
  const std::size_t index =
      outputSubject ? outputSubject->output : layerSubject->output;
  const OutputRow *output = OutputAt(a_recipe, index);
  if (!output) {
    return;
  }
  a_selection.target = output->target;
  a_selection.slot = output->target == Target::kLight
                         ? std::nullopt
                         : std::optional<Slot>{output->slot};
  a_selection.layer = layerSubject
                          ? std::optional<std::size_t>{layerSubject->layer}
                          : std::nullopt;
}

[[nodiscard]] std::optional<std::size_t>
IndexedOutputOf(const InspectorSubject &a_subject) {
  if (const auto *output = Get<OutputSubject>(a_subject)) {
    return output->output;
  }
  if (const auto *layer = Get<LayerSubject>(a_subject)) {
    return layer->output;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<InspectorSubject>
SubjectAfter(const InspectorSubject &a_subject,
             const Renumbering &a_renumbering) {
  if (const auto *output = Get<OutputSubject>(a_subject)) {
    const std::optional<std::size_t> to =
        OutputAfter(a_renumbering, output->output);
    return to ? std::optional<InspectorSubject>{OutputSubject{*to}}
              : std::nullopt;
  }
  if (const auto *layer = Get<LayerSubject>(a_subject)) {
    const std::optional<LayerPlace> to =
        LayerAfter(a_renumbering, {layer->output, layer->layer});
    return to ? std::optional<InspectorSubject>{LayerSubject{to->output,
                                                             to->layer}}
              : std::nullopt;
  }
  return a_subject;
}

[[nodiscard]] std::optional<PropertyLocation>
PropertyAfter(PropertyLocation a_property, const Renumbering &a_renumbering) {
  if (auto *output = Get<OutputOwner>(a_property.owner)) {
    const std::optional<std::size_t> to =
        OutputAfter(a_renumbering, output->output);
    if (!to) {
      return std::nullopt;
    }
    output->output = *to;
  }
  if (auto *layer = Get<LayerOwner>(a_property.owner)) {
    const std::optional<LayerPlace> to =
        LayerAfter(a_renumbering, {layer->output, layer->layer});
    if (!to) {
      return std::nullopt;
    }
    *layer = LayerOwner{to->output, to->layer};
  }
  return a_property;
}

[[nodiscard]] std::optional<std::size_t>
StackLayerAfter(const Selection &a_selection,
                const Renumbering &a_renumbering) {
  const std::optional<std::size_t> output =
      IndexedOutputOf(a_selection.subject);
  if (!output || !a_selection.layer) {
    return std::nullopt;
  }
  const std::optional<LayerPlace> to =
      LayerAfter(a_renumbering, {*output, *a_selection.layer});
  return to ? std::optional{to->layer} : std::nullopt;
}

bool RenumberSelection(Selection &a_selection, std::string_view a_recipeID,
                       const Renumbering &a_renumbering) {
  if (a_selection.recipeID != a_recipeID) {
    return true;
  }
  a_selection.layer = StackLayerAfter(a_selection, a_renumbering);
  if (a_selection.property) {
    a_selection.property =
        PropertyAfter(std::move(*a_selection.property), a_renumbering);
  }
  std::optional<InspectorSubject> subject =
      SubjectAfter(a_selection.subject, a_renumbering);
  if (!subject) {
    a_selection.subject = RecipeSubject{};
    a_selection.property.reset();
    a_selection.layer.reset();
    return false;
  }
  a_selection.subject = std::move(*subject);
  return true;
}

void PushVisit(std::vector<InspectorVisit> &a_stack, InspectorVisit a_visit) {
  if (a_stack.size() >= kMaxInspectorHistory) {
    a_stack.erase(a_stack.begin());
  }
  a_stack.push_back(std::move(a_visit));
}

bool CommitNavigation(Navigation &a_navigation, const Selection &a_previous,
                      Selection &a_selection, const RecipeRow &a_recipe) {
  PushVisit(a_navigation.back, {a_previous, a_navigation.scroll});
  a_navigation.forward.clear();
  AlignOutputSelection(a_selection, a_recipe);
  a_navigation.scroll = 0.0f;
  return true;
}
}

bool InspectorSubjectExists(const InspectorSubject &a_subject,
                            const RecipeRow &a_recipe) {
  return Match(
      a_subject, [](const RecipeSubject &) { return true; },
      [](const ShellSubject &) { return true; },
      [&](const OutputSubject &a_output) {
        return OutputAt(a_recipe, a_output.output) != nullptr;
      },
      [&](const LayerSubject &a_layer) {
        const OutputRow *output = OutputAt(a_recipe, a_layer.output);
        return output && output->target != Target::kLight &&
               a_layer.layer < output->layers.size();
      },
      [&](const SignalSubject &a_signal) {
        return FindByName(a_recipe.signals, a_signal.name) != nullptr;
      },
      [&](const SourceSubject &a_source) {
        return FindByName(a_recipe.sourceRows, a_source.name) != nullptr;
      },
      [&](const MaskSubject &a_mask) {
        return FindByName(a_recipe.maskRows, a_mask.name) != nullptr;
      },
      [&](const CurveSubject &a_curve) {
        return FindByName(a_recipe.curves, a_curve.name) != nullptr;
      });
}

bool ResolveInspectorSubject(Selection &a_selection,
                             const RecipeRow *a_recipe) {
  if (!a_recipe || a_recipe->id != a_selection.recipeID ||
      !InspectorSubjectExists(a_selection.subject, *a_recipe)) {
    a_selection.subject = RecipeSubject{};
    a_selection.property.reset();
    a_selection.layer.reset();
    return false;
  }
  if (a_selection.property &&
      (!PropertyMatches(a_selection.subject, *a_selection.property) ||
       !PropertyExists(*a_selection.property, *a_recipe))) {
    a_selection.property.reset();
  }
  AlignOutputSelection(a_selection, *a_recipe);
  return true;
}

bool Navigate(Navigation &a_navigation, Selection &a_selection,
              InspectorSubject a_subject, const RecipeRow &a_recipe) {
  if (a_selection.recipeID != a_recipe.id ||
      !InspectorSubjectExists(a_subject, a_recipe) ||
      (a_selection.subject == a_subject && !a_selection.property)) {
    return false;
  }
  const Selection previous = a_selection;
  a_selection.subject = std::move(a_subject);
  a_selection.property.reset();
  return CommitNavigation(a_navigation, previous, a_selection, a_recipe);
}

bool ResolvePendingSubject(Navigation &a_navigation, Selection &a_selection,
                           std::optional<PendingSubject> &a_pending,
                           const RecipeRow *a_recipe) {
  if (!a_pending || !a_recipe) {
    return false;
  }
  if (a_recipe->id != a_selection.recipeID ||
      a_pending->recipeID != a_selection.recipeID) {
    a_pending.reset();
    return false;
  }
  if (!InspectorSubjectExists(a_pending->subject, *a_recipe)) {
    return false;
  }
  const bool navigated =
      Navigate(a_navigation, a_selection, a_pending->subject, *a_recipe);
  a_pending.reset();
  return navigated;
}

bool NavigateProperty(Navigation &a_navigation, Selection &a_selection,
                      PropertyTarget a_target, const RecipeRow &a_recipe) {
  if (a_selection.recipeID != a_recipe.id ||
      !InspectorSubjectExists(a_target.subject, a_recipe) ||
      !PropertyMatches(a_target.subject, a_target.property) ||
      !PropertyExists(a_target.property, a_recipe) ||
      (a_selection.subject == a_target.subject &&
       a_selection.property == a_target.property)) {
    return false;
  }
  const Selection previous = a_selection;
  a_selection.subject = std::move(a_target.subject);
  a_selection.property = std::move(a_target.property);
  return CommitNavigation(a_navigation, previous, a_selection, a_recipe);
}

std::optional<InspectorVisit> ReachableVisit(InspectorVisit a_visit,
                                             const Selection &a_current,
                                             const RecipeRow &a_recipe) {
  if (a_visit.selection.recipeID != a_recipe.id ||
      a_visit.selection.piece != a_current.piece ||
      !InspectorSubjectExists(a_visit.selection.subject, a_recipe)) {
    return std::nullopt;
  }
  AlignOutputSelection(a_visit.selection, a_recipe);
  if (a_visit.selection.property &&
      !PropertyExists(*a_visit.selection.property, a_recipe)) {
    a_visit.selection.property.reset();
  }
  if (a_visit.selection == a_current) {
    return std::nullopt;
  }
  return a_visit;
}

bool StepHistory(Navigation &a_navigation, HistoryDirection a_direction,
                 Selection &a_selection, const RecipeRow &a_recipe) {
  const bool back = a_direction == HistoryDirection::kBack;
  std::vector<InspectorVisit> &from =
      back ? a_navigation.back : a_navigation.forward;
  std::vector<InspectorVisit> &to =
      back ? a_navigation.forward : a_navigation.back;
  while (!from.empty()) {
    InspectorVisit candidate = std::move(from.back());
    from.pop_back();
    std::optional<InspectorVisit> visit =
        ReachableVisit(std::move(candidate), a_selection, a_recipe);
    if (!visit) {
      continue;
    }
    PushVisit(to, {a_selection, a_navigation.scroll});
    a_selection = std::move(visit->selection);
    a_navigation.scroll = visit->scroll;
    return true;
  }
  return false;
}

bool GoBack(Navigation &a_navigation, Selection &a_selection,
            const RecipeRow &a_recipe) {
  return StepHistory(a_navigation, HistoryDirection::kBack, a_selection,
                     a_recipe);
}

bool GoForward(Navigation &a_navigation, Selection &a_selection,
               const RecipeRow &a_recipe) {
  return StepHistory(a_navigation, HistoryDirection::kForward, a_selection,
                     a_recipe);
}

void RenumberIndexedSubjects(Navigation &a_navigation, Selection &a_selection,
                             std::string_view a_recipeID,
                             const Renumbering &a_renumbering) {
  if (!RenumberSelection(a_selection, a_recipeID, a_renumbering)) {
    a_navigation.scroll = 0.0f;
  }
  const auto lost = [&](InspectorVisit &a_visit) {
    return !RenumberSelection(a_visit.selection, a_recipeID, a_renumbering);
  };
  std::erase_if(a_navigation.back, lost);
  std::erase_if(a_navigation.forward, lost);
}

bool CanRenumber(std::span<const RecipeEdit> a_edits) {
  return std::ranges::any_of(a_edits, [](const RecipeEdit &a_edit) {
    return IsCascade(a_edit) || Is<AddLayer>(a_edit) ||
           Is<RemoveLayer>(a_edit) || Is<MoveLayer>(a_edit) ||
           Is<ClearLayers>(a_edit) || Is<ResetOutput>(a_edit) ||
           Is<AddOutput>(a_edit) || Is<RemoveOutput>(a_edit) ||
           Is<ClearOutputs>(a_edit) || Is<ClearRecipe>(a_edit) ||
           Is<AddLight>(a_edit);
  });
}
}
