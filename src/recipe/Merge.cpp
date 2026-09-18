#include "recipe/Merge.h"

#include <algorithm>

namespace BetterEnchantmentEffects {
namespace {
std::vector<std::size_t> PriorityOrder(std::span<const PlacedRecipe> a_placed) {
  std::vector<std::size_t> order(a_placed.size());
  for (std::size_t i = 0; i < order.size(); ++i) {
    order[i] = i;
  }
  std::ranges::stable_sort(order, [&](std::size_t a_lhs, std::size_t a_rhs) {
    return a_placed[a_lhs].priority < a_placed[a_rhs].priority;
  });
  return order;
}

const SurfaceOutput *SurfaceOutputAt(const PlacedRecipe &a_placed,
                                     std::size_t a_index) noexcept {
  if (!a_placed.recipe || a_index >= a_placed.recipe->outputs.size()) {
    return nullptr;
  }
  return Get<SurfaceOutput>(a_placed.recipe->outputs[a_index]);
}

const SurfaceOutput *SlotOutputAt(std::span<const PlacedRecipe> a_placed,
                                  SlotContribution a_contribution) noexcept {
  const std::size_t placed = IndexOf(a_contribution.placed);
  if (placed >= a_placed.size()) {
    return nullptr;
  }
  return SurfaceOutputAt(a_placed[placed], a_contribution.output);
}

bool NamesScalar(const SlotScalars &a_scalars, ScalarField a_field) noexcept {
  if (a_field == ScalarField::kColor) {
    return a_scalars.color.has_value();
  }
  const std::optional<Param> *param = ScalarOf(a_scalars, a_field);
  return param && param->has_value();
}

template <class Contribution> struct Flagged {
  Contribution contribution;
  bool replaces = false;
};

template <class Contribution>
void CutAtReplace(std::span<const Flagged<Contribution>> a_flagged,
                  std::vector<Contribution> &a_ordered,
                  std::vector<Contribution> &a_replaced,
                  std::optional<Contribution> &a_replacer) {
  std::size_t start = 0;
  for (std::size_t i = 0; i < a_flagged.size(); ++i) {
    if (a_flagged[i].replaces) {
      start = i;
      a_replacer = a_flagged[i].contribution;
    }
  }
  a_replaced.assign(a_ordered.begin(),
                    a_ordered.begin() + static_cast<std::ptrdiff_t>(start));
  a_ordered.erase(a_ordered.begin(),
                  a_ordered.begin() + static_cast<std::ptrdiff_t>(start));
}

SlotPlan &SlotPlanFor(GeometryPlan &a_plan, const SurfaceOutput &a_output) {
  if (SlotPlan *found = FindIf(a_plan.slots, [&](const SlotPlan &a_slot) {
        return a_slot.surface == a_output.surface &&
               a_slot.slot == a_output.slot;
      })) {
    return *found;
  }
  a_plan.slots.push_back(
      SlotPlan{a_output.surface, a_output.slot, {}, {}, std::nullopt, {}});
  return a_plan.slots.back();
}

void PlanScalars(SlotPlan &a_slot, std::span<const PlacedRecipe> a_placed) {
  std::vector<const SurfaceOutput *> outputs;
  outputs.reserve(a_slot.chain.size());
  for (const SlotContribution &c : a_slot.chain) {
    outputs.push_back(SlotOutputAt(a_placed, c));
  }
  for (const ScalarField field : ScalarsOf(a_slot.slot)) {
    if (const std::optional<std::size_t> at =
            ScalarSource(a_slot.slot, field, outputs)) {
      a_slot.scalars.push_back(ScalarOwner{field, a_slot.chain[*at]});
    }
  }
}

const LightOutput *FirstLight(const Recipe &a_recipe, std::size_t &a_index,
                              const OutputFilter &a_filter) {
  for (std::size_t i = 0; i < a_recipe.outputs.size(); ++i) {
    if (const LightOutput *light = Get<LightOutput>(a_recipe.outputs[i]);
        light && (!a_filter || a_filter(a_recipe, i))) {
      a_index = i;
      return light;
    }
  }
  return nullptr;
}
}

std::optional<std::size_t>
ScalarSource(Slot a_slot, ScalarField a_field,
             std::span<const SurfaceOutput *const> a_outputs) {
  const std::span<const ScalarField> fields = ScalarsOf(a_slot);
  if (!std::ranges::contains(fields, a_field)) {
    return std::nullopt;
  }
  for (std::size_t i = a_outputs.size(); i-- > 0;) {
    if (a_outputs[i] && NamesScalar(a_outputs[i]->scalars, a_field)) {
      return i;
    }
  }
  return std::nullopt;
}

GeometryPlan PlanGeometry(std::span<const PlacedRecipe> a_placed) {
  GeometryPlan plan;
  for (const std::size_t placed : PriorityOrder(a_placed)) {
    for (const std::size_t index : a_placed[placed].outputs) {
      const SurfaceOutput *output = SurfaceOutputAt(a_placed[placed], index);
      if (!output) {
        continue;
      }
      SlotPlanFor(plan, *output)
          .chain.push_back(
              SlotContribution{FromIndex<SlotContributor>(placed), index});
    }
  }
  for (SlotPlan &slot : plan.slots) {
    std::vector<Flagged<SlotContribution>> flagged;
    for (const SlotContribution &c : slot.chain) {
      const SurfaceOutput *output = SlotOutputAt(a_placed, c);
      flagged.push_back(
          Flagged<SlotContribution>{c, output && output->replace});
    }
    CutAtReplace<SlotContribution>(flagged, slot.chain, slot.replaced,
                                   slot.replacer);
    PlanScalars(slot, a_placed);
  }
  return plan;
}

LightPlan PlanLights(std::span<const PlacedRecipe> a_placed,
                     const OutputFilter &a_filter) {
  LightPlan plan;
  std::vector<Flagged<LightContribution>> flagged;
  for (const std::size_t placed : PriorityOrder(a_placed)) {
    const Recipe *recipe = a_placed[placed].recipe;
    if (!recipe) {
      continue;
    }
    std::size_t index = 0;
    const LightOutput *light = FirstLight(*recipe, index, a_filter);
    if (!light) {
      continue;
    }
    const LightContribution contribution{FromIndex<LightContributor>(placed),
                                         index};
    plan.shown.push_back(contribution);
    flagged.push_back(Flagged<LightContribution>{contribution, light->replace});
  }
  CutAtReplace<LightContribution>(flagged, plan.shown, plan.replaced,
                                  plan.replacer);
  return plan;
}

const SlotPlan *SlotPlanOf(const GeometryPlan &a_plan, Surface a_surface,
                           Slot a_slot) noexcept {
  return FindIf(a_plan.slots, [&](const SlotPlan &a_candidate) {
    return a_candidate.surface == a_surface && a_candidate.slot == a_slot;
  });
}

std::optional<SlotContribution> ScalarOwnerOf(const SlotPlan &a_plan,
                                              ScalarField a_field) noexcept {
  if (const ScalarOwner *owner =
          FindBy(a_plan.scalars, a_field, &ScalarOwner::field)) {
    return owner->from;
  }
  return std::nullopt;
}

std::optional<std::size_t>
ReplacerOf(const SlotPlan &a_plan, SlotContribution a_contribution) noexcept {
  if (a_plan.replacer &&
      std::ranges::contains(a_plan.replaced, a_contribution)) {
    return IndexOf(a_plan.replacer->placed);
  }
  return std::nullopt;
}

std::optional<std::size_t>
ReplacerOf(const LightPlan &a_plan, LightContribution a_contribution) noexcept {
  if (a_plan.replacer &&
      std::ranges::contains(a_plan.replaced, a_contribution)) {
    return IndexOf(a_plan.replacer->placed);
  }
  return std::nullopt;
}
}
