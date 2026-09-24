// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <algorithm>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects {
template <typename Definition, typename Identity>
void AppendDefinition(std::vector<Definition> &a_definitions,
                      Definition a_definition, Identity a_identity) {
  std::erase_if(a_definitions, [&](const Definition &a_existing) {
    return a_identity(a_existing) == a_identity(a_definition);
  });
  a_definitions.push_back(std::move(a_definition));
}
}
