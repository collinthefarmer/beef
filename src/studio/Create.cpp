// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/Create.h"

#include "studio/Names.h"
#include "studio/Rows.h"

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

[[nodiscard]] Created Unchanged(const InspectorSubject &a_subject) {
  return {.edits = {}, .subject = a_subject};
}

[[nodiscard]] Created CopySignal(const SignalSubject &a_of,
                                 const RecipeRow &a_recipe) {
  const SignalRow *row = FindByName(a_recipe.signals, a_of.name);
  if (!row) {
    return Unchanged(a_of);
  }
  const std::string name = UniqueOf(row->name, a_recipe);
  std::vector<RecipeEdit> edits{AddSignal{name},
                                SetSignal{name, row->definition}};
  if (!row->curve.empty()) {
    edits.emplace_back(SetSignalCurve{name, CurveRef{row->curve}});
  }
  return {.edits = std::move(edits), .subject = SignalSubject{name}};
}

[[nodiscard]] Created CopySource(const SourceSubject &a_of,
                                 const RecipeRow &a_recipe) {
  const SourceRow *row = FindByName(a_recipe.sourceRows, a_of.name);
  const std::optional<SourceKind> kind =
      row ? SourceKindOf(*row) : std::nullopt;
  if (!kind) {
    return Unchanged(a_of);
  }
  const std::string name = UniqueOf(row->name, a_recipe);
  return {.edits = {AddSource{name, *kind}}, .subject = SourceSubject{name}};
}

[[nodiscard]] Created CopyMask(const MaskSubject &a_of,
                               const RecipeRow &a_recipe) {
  const TextRow *row = FindByName(a_recipe.maskRows, a_of.name);
  if (!row) {
    return Unchanged(a_of);
  }
  const std::string name = UniqueOf(row->name, a_recipe);
  return {.edits = {AddMask{name}, SetMask{name, row->text}},
          .subject = MaskSubject{name}};
}

[[nodiscard]] Created CopyCurve(const CurveSubject &a_of,
                                const RecipeRow &a_recipe) {
  const TextRow *row = FindByName(a_recipe.curves, a_of.name);
  if (!row) {
    return Unchanged(a_of);
  }
  const std::string name = UniqueOf(row->name, a_recipe);
  return {.edits = {AddCurve{name}, SetCurve{name, row->text}},
          .subject = CurveSubject{name}};
}

[[nodiscard]] Created Copy(const InspectorSubject &a_of,
                           const RecipeRow &a_recipe) {
  if (const auto *signal = Get<SignalSubject>(a_of)) {
    return CopySignal(*signal, a_recipe);
  }
  if (const auto *source = Get<SourceSubject>(a_of)) {
    return CopySource(*source, a_recipe);
  }
  if (const auto *mask = Get<MaskSubject>(a_of)) {
    return CopyMask(*mask, a_recipe);
  }
  if (const auto *curve = Get<CurveSubject>(a_of)) {
    return CopyCurve(*curve, a_recipe);
  }
  return Unchanged(a_of);
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
      },
      [&](const NewCopy &a_new) -> Created {
        return Copy(a_new.of, a_recipe);
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

std::optional<RecipeEdit> ResourceRename(const InspectorSubject &a_subject,
                                         std::string a_to) {
  if (const auto *signal = Get<SignalSubject>(a_subject)) {
    return RenameSignal{signal->name, std::move(a_to)};
  }
  if (const auto *source = Get<SourceSubject>(a_subject)) {
    return RenameSource{source->name, std::move(a_to)};
  }
  if (const auto *mask = Get<MaskSubject>(a_subject)) {
    return RenameMask{mask->name, std::move(a_to)};
  }
  if (const auto *curve = Get<CurveSubject>(a_subject)) {
    return RenameCurve{curve->name, std::move(a_to)};
  }
  return std::nullopt;
}
}
