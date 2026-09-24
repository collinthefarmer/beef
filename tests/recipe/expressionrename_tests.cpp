// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/Expression.h"
#include "test_support.h"

#include <array>
#include <span>
#include <string>

using namespace BetterEnchantmentEffects;
using test::Check;

int main() {
  const std::array swap{ExpressionRename{"a", "b"}, ExpressionRename{"b", "a"}};
  Check(RenameInExpression("@a - @b + @a", swap, false) == "@b - @a + @b",
        "swapped references are renamed from the original tokens only");
  const std::array chain{ExpressionRename{"a", "b"},
                         ExpressionRename{"b", "c"}};
  Check(RenameInExpression("@a - @b", chain, false) == "@b - @c",
        "a replacement cannot be renamed again by a later mapping");
  const std::array aliases{ExpressionRename{"a", "shared"},
                           ExpressionRename{"b", "shared"}};
  Check(RenameInExpression("@a + @b", aliases, false) == "@shared + @shared",
        "distinct aliases may intentionally reuse one source");
  Check(RenameInExpression("@a + @ab + @a_2 + @z", swap, false) ==
            "@b + @ab + @a_2 + @z",
        "remapping changes whole matching identifiers only");
  Check(RenameInExpression("@a + @a \t\n(@b)", swap, false) ==
            "@b + @a \t\n(@a)",
        "source remapping preserves curve names with arbitrary whitespace");
  Check(RenameInExpression("@a + @a \t\n(@b)", swap, true) ==
            "@a + @b \t\n(@b)",
        "curve remapping preserves the argument's source reference");
  Check(RenameInExpression("@a + @ab", "a", "b", false) == "@b + @ab",
        "the existing single-name API retains whole-token behavior");
  Check(RenameInExpression("@a + 1", std::span<const ExpressionRename>{},
                           false) == "@a + 1",
        "an empty mapping preserves the expression");
  return test::Finish("recipe_expressionrename");
}
