// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/Create.h"

#include "studio/Names.h"

#include <string_view>
#include <utility>

namespace BetterEnchantmentEffects::Studio {
namespace {
[[nodiscard]] std::string UniqueOf(std::string_view a_stem,
                                   const RecipeRow &a_recipe) {
  return UniqueName(a_stem, ReservedNames(NamesOf(a_recipe)));
}

[[nodiscard]] const OutputRow *OutputAt(const RecipeRow &a_recipe,
                                        std::size_t a_index) {
  return FindBy(a_recipe.outputs, a_index, &OutputRow::index);
}
}

Created Create(const Creation &a_request, const RecipeRow &a_recipe) {
  return Match(
      a_request,
      [&](const NewOutput &a_new) -> Created {
        return {.edits = {AddOutput{a_new.surface, a_new.slot, a_new.selector}},
                .subject = OutputSubject{a_recipe.outputs.size()}};
      },
      [&](const NewLight &) -> Created {
        return {.edits = {AddLight{}},
                .subject = OutputSubject{a_recipe.outputs.size()}};
      },
      [&](const NewLayer &a_new) -> Created {
        const OutputRow *output = OutputAt(a_recipe, a_new.output);
        const std::size_t at = output ? output->layers.size() : 0;
        return {.edits = {AddLayer{a_new.output, DefaultLayer(), at}},
                .subject = LayerSubject{a_new.output, at}};
      },
      [&](const NewSignal &a_new) -> Created {
        const std::string name = UniqueOf(a_new.stem, a_recipe);
        std::vector<RecipeEdit> edits{AddSignal{name}};
        if (!Is<ConstantSignal>(a_new.kind)) {
          edits.emplace_back(SetSignal{name, a_new.kind});
        }
        return {.edits = std::move(edits), .subject = SignalSubject{name}};
      },
      [&](const NewSource &a_new) -> Created {
        const std::string name = UniqueOf(a_new.stem, a_recipe);
        return {.edits = {AddSource{name, a_new.kind}},
                .subject = SourceSubject{name}};
      },
      [&](const NewMask &a_new) -> Created {
        const std::string name = UniqueOf(a_new.stem, a_recipe);
        return {.edits = {AddMask{name}}, .subject = MaskSubject{name}};
      },
      [&](const NewCurve &a_new) -> Created {
        const std::string name = UniqueOf(a_new.stem, a_recipe);
        return {.edits = {AddCurve{name}}, .subject = CurveSubject{name}};
      });
}

std::optional<std::string> ResourceName(const InspectorSubject &a_subject) {
  if (const auto *signal = Get<SignalSubject>(a_subject)) {
    return signal->name;
  }
  if (const auto *source = Get<SourceSubject>(a_subject)) {
    return source->name;
  }
  if (const auto *mask = Get<MaskSubject>(a_subject)) {
    return mask->name;
  }
  if (const auto *curve = Get<CurveSubject>(a_subject)) {
    return curve->name;
  }
  return std::nullopt;
}
}
