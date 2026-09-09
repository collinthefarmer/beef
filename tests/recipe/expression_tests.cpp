#include "recipe/Expression.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using test::Check;

int main()
{
	Check(kMaxExpressionDepth == 32, "the expression depth bound is declared");
	Check(kMaxExpressionOps == 256, "the expression op bound is declared");
	Check(kMaxExpressionLength == 4096, "the expression length bound is declared");

	const Program empty;
	Check(empty.OpCount() == 0, "a default program has no code");
	Check(empty.Constant(), "a default program reads no rows");
	Check(!empty.UsesTime() && !empty.UsesX() && !empty.UsesMean(), "a default program uses no free variables");

	return test::Finish("expression");
}
