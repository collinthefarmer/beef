#include "studio/Navigation.h"

#include <algorithm>
#include <utility>

namespace BetterEnchantmentEffects::Studio {
namespace {
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
  const auto found =
      std::ranges::find(a_recipe.outputs, a_index, &OutputRow::index);
  return found == a_recipe.outputs.end() ? nullptr : &*found;
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

bool InvalidateIndexedSelection(Selection &a_selection,
                                std::string_view a_recipeID) {
  if (a_selection.recipeID != a_recipeID) {
    return false;
  }
  a_selection.layer.reset();
  if (a_selection.property && (Is<OutputOwner>(a_selection.property->owner) ||
                               Is<LayerOwner>(a_selection.property->owner) ||
                               Is<VariantOwner>(a_selection.property->owner))) {
    a_selection.property.reset();
  }
  if (!Is<OutputSubject>(a_selection.subject) &&
      !Is<LayerSubject>(a_selection.subject)) {
    return false;
  }
  a_selection.subject = RecipeSubject{};
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
        return std::ranges::find(a_recipe.signals, a_signal.name,
                                 &SignalRow::name) != a_recipe.signals.end();
      },
      [&](const SourceSubject &a_source) {
        return std::ranges::find(a_recipe.sourceRows, a_source.name,
                                 &SourceRow::name) != a_recipe.sourceRows.end();
      },
      [&](const MaskSubject &a_mask) {
        return std::ranges::find(a_recipe.maskRows, a_mask.name,
                                 &TextRow::name) != a_recipe.maskRows.end();
      },
      [&](const CurveSubject &a_curve) {
        return std::ranges::find(a_recipe.curves, a_curve.name,
                                 &TextRow::name) != a_recipe.curves.end();
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
  if (a_navigation.back.size() >= kMaxInspectorHistory) {
    a_navigation.back.erase(a_navigation.back.begin());
  }
  a_navigation.back.push_back({a_selection, a_navigation.scroll});
  a_selection.subject = std::move(a_subject);
  a_selection.property.reset();
  AlignOutputSelection(a_selection, a_recipe);
  a_navigation.scroll = 0.0f;
  return true;
}

bool NavigateProperty(Navigation &a_navigation, Selection &a_selection,
                      InspectorSubject a_subject, PropertyLocation a_property,
                      const RecipeRow &a_recipe) {
  if (a_selection.recipeID != a_recipe.id ||
      !InspectorSubjectExists(a_subject, a_recipe) ||
      !PropertyMatches(a_subject, a_property) ||
      !PropertyExists(a_property, a_recipe) ||
      (a_selection.subject == a_subject &&
       a_selection.property == a_property)) {
    return false;
  }
  if (a_navigation.back.size() >= kMaxInspectorHistory) {
    a_navigation.back.erase(a_navigation.back.begin());
  }
  a_navigation.back.push_back({a_selection, a_navigation.scroll});
  a_selection.subject = std::move(a_subject);
  a_selection.property = std::move(a_property);
  AlignOutputSelection(a_selection, a_recipe);
  a_navigation.scroll = 0.0f;
  return true;
}

bool GoBack(Navigation &a_navigation, Selection &a_selection,
            const RecipeRow &a_recipe) {
  while (!a_navigation.back.empty()) {
    InspectorVisit visit = std::move(a_navigation.back.back());
    a_navigation.back.pop_back();
    if (visit.selection.recipeID != a_recipe.id ||
        visit.selection.piece != a_selection.piece ||
        !InspectorSubjectExists(visit.selection.subject, a_recipe)) {
      continue;
    }
    AlignOutputSelection(visit.selection, a_recipe);
    if (visit.selection.property &&
        !PropertyExists(*visit.selection.property, a_recipe)) {
      visit.selection.property.reset();
    }
    if (visit.selection == a_selection) {
      continue;
    }
    a_selection = std::move(visit.selection);
    a_navigation.scroll = visit.scroll;
    return true;
  }
  return false;
}

void InvalidateIndexedSubjects(Navigation &a_navigation, Selection &a_selection,
                               std::string_view a_recipeID) {
  if (InvalidateIndexedSelection(a_selection, a_recipeID)) {
    a_navigation.scroll = 0.0f;
  }
  std::erase_if(a_navigation.back, [&](InspectorVisit &a_visit) {
    return InvalidateIndexedSelection(a_visit.selection, a_recipeID);
  });
}

bool ShouldInvalidateIndexedSubjects(std::span<const RecipeEdit> a_edits) {
  return std::ranges::any_of(a_edits, [](const RecipeEdit &a_edit) {
    return Is<AddLayer>(a_edit) || Is<RemoveLayer>(a_edit) ||
           Is<MoveLayer>(a_edit) || Is<ClearLayers>(a_edit) ||
           Is<AddOutput>(a_edit) || Is<RemoveOutput>(a_edit) ||
           Is<ClearOutputs>(a_edit) || Is<ClearRecipe>(a_edit) ||
           Is<AddLight>(a_edit);
  });
}

void ResolvePreviewPin(std::optional<PreviewPin> &a_pin,
                       const Selection &a_selection, const RecipeRow *a_recipe,
                       std::uint64_t a_resetID) {
  if (!a_pin) {
    return;
  }
  if (!a_recipe || a_pin->selection.piece != a_selection.piece ||
      a_pin->selection.recipeID != a_selection.recipeID ||
      a_pin->selection.recipeID != a_recipe->id ||
      a_pin->resetID != a_resetID ||
      !InspectorSubjectExists(a_pin->selection.subject, *a_recipe) ||
      std::ranges::find(a_recipe->geometries, a_pin->selection.geometry,
                        &GeometryRow::name) == a_recipe->geometries.end()) {
    a_pin.reset();
  }
}

void InvalidatePreviewPin(std::optional<PreviewPin> &a_pin,
                          std::string_view a_recipeID) {
  if (a_pin && a_pin->selection.recipeID == a_recipeID &&
      (Is<OutputSubject>(a_pin->selection.subject) ||
       Is<LayerSubject>(a_pin->selection.subject))) {
    a_pin.reset();
  }
}
}
