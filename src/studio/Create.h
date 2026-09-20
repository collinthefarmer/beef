#pragma once

#include "Core.h"
#include "recipe/Recipe.h"
#include "studio/Edits.h"
#include "studio/Selection.h"
#include "studio/Snapshot.h"

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
struct NewOutput {
  Surface surface = Surface::kMaterial;
  Slot slot = Slot::kEmissive;
  Selector selector = {};
};
struct NewLight {};
struct NewLayer {
  std::size_t output = 0;
};
struct NewSignal {
  std::string stem = "signal";
  SignalKind kind = ConstantSignal{};
};
struct NewSource {
  std::string stem = "source";
  SourceKind kind = MaterialSource{};
};
struct NewMask {
  std::string stem = "mask";
};
struct NewCurve {
  std::string stem = "curve";
};
using Creation = std::variant<NewOutput, NewLight, NewLayer, NewSignal,
                              NewSource, NewMask, NewCurve>;

struct Created {
  std::vector<RecipeEdit> edits;
  InspectorSubject subject;
};

[[nodiscard]] Created Create(const Creation &a_request,
                             const RecipeRow &a_recipe);
[[nodiscard]] std::optional<std::string>
ResourceName(const InspectorSubject &a_subject);
}
