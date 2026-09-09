#include "recipe/Signals.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using test::Check;

int main()
{
	NullEnvironment environment;
	Check(environment.ActorValue("Health", Measure::kCurrent) == 0.0f, "the null environment reads zero");

	FormRef record;
	record.text = "EnchArmorMagickaFXS";
	Check(!environment.EffectShader(record).has_value(), "the null environment has no effect shader");

	const SignalGraph graph;
	Check(graph.Size() == 0, "a default graph has no nodes");

	EventRecord event;
	event.id = "hit.received";
	event.payload.value = 1.0f;
	Check(event.payload.value == 1.0f, "an event carries a payload value");

	return test::Finish("signals");
}
