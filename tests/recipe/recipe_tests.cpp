#include "recipe/Recipe.h"
#include "recipe/Words.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using test::Check;

int main()
{
	Recipe recipe;
	recipe.id = "example";
	recipe.signals.push_back(Signal{ "glow", ConstantSignal{ 1.0f }, std::nullopt });
	Check(recipe.signals.size() == 1, "a signal is stored");

	const SignalKind kind = recipe.signals.front().kind;
	const bool       isConstant = Match(
        kind,
        [](const ConstantSignal&) { return true; },
        [](const auto&) { return false; });
	Check(isConstant, "the signal holds a constant");

	Check(kKeyKindCount == std::size(kKeyKinds), "the key-kind table has a row per kind");
	Check(kSignalKindCount == std::size(kSignalKinds), "the signal-kind table has a row per kind");
	Check(kSlotCount == std::size(kSlots), "the slot table has a row per slot");
	Check(NameOf(kBlends, Blend::kAdd) == "add", "blend add names itself");
	Check(FromName(kBlends, "screen") == Blend::kScreen, "blend screen parses back");

	Check(std::size(kBipedSlots) == 11, "the biped-slot table is exposed");
	Check(kBipedSlots[2].slot == 32 && kBipedSlots[2].name == "body", "slot 32 is body");

	Check(Recipe{} == Recipe{}, "two empty recipes compare equal");

	return test::Finish("recipe");
}
