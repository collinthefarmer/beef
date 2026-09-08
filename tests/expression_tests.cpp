#include "Expression.h"
#include "test_support.h"

#include <format>
#include <random>

using namespace WornEnchantmentPBR;
using test::Check;
using test::Near;

namespace
{
	float Eval(const char* a_text, std::span<const Value> a_refs = {}, float a_time = 0.0f)
	{
		const auto p = Program::Parse(a_text);
		if (!p) {
			std::printf("  parse error in '%s': %s\n", a_text, p.error().c_str());
			return std::numeric_limits<float>::quiet_NaN();
		}
		Program::Inputs in;
		in.refs = a_refs;
		in.time = a_time;
		return AsScalar(p->Evaluate(in));
	}

	Value EvalValue(const char* a_text, std::span<const Value> a_refs = {})
	{
		const auto p = Program::Parse(a_text);
		if (!p) {
			return 0.0f;
		}
		Program::Inputs in;
		in.refs = a_refs;
		return p->Evaluate(in);
	}

	bool Fails(const char* a_text, const char* a_fragment = "")
	{
		const auto p = Program::Parse(a_text);
		return !p && p.error().find(a_fragment) != std::string::npos;
	}

	void Arithmetic()
	{
		Check(Near(Eval("1 + 2 * 3"), 7.0f), "precedence");
		Check(Near(Eval("(1 + 2) * 3"), 9.0f), "parentheses");
		Check(Near(Eval("-2 * -3"), 6.0f), "unary minus");
		Check(Near(Eval("10 / 4"), 2.5f), "division");
		Check(Near(Eval("1 / 0"), 0.0f), "division by zero is 0");
		Check(Near(Eval("pow(2, 10)"), 1024.0f), "pow");
		Check(Near(Eval("pow(-8, 0.5)"), 0.0f), "pow that would be NaN is 0");
		Check(Near(Eval("clamp(5, 0, 1) + min(2, 3) + max(2, 3) + abs(-1)"), 7.0f), "clamp min max abs");
		Check(Near(Eval("saturate(-3) + floor(1.7) + ceil(1.2) + frac(2.25)"), 3.25f), "saturate floor ceil frac");
		Check(Near(Eval("sqrt(16) + sqrt(-1)"), 4.0f), "sqrt, negative is 0");
		Check(Near(Eval("step(0.5, 0.7) + step(0.5, 0.3)"), 1.0f), "step(edge, x)");
		Check(Near(Eval("smoothstep(0, 1, 0.5)"), 0.5f), "smoothstep");
		Check(Near(Eval("lerp(2, 4, 0.25)"), 2.5f), "lerp");
		Check(Near(Eval("sin(pi / 2) + cos(0)"), 2.0f), "sin cos pi");
		Check(Near(Eval("time * 2", {}, 1.5f), 3.0f), "time");
		Check(Near(Eval(".5 + 1."), 1.5f), "numbers with a leading or trailing dot");
	}

	void Logic()
	{
		Check(Near(Eval("1 < 2"), 1.0f) && Near(Eval("2 < 1"), 0.0f), "less than");
		Check(Near(Eval("2 > 1"), 1.0f) && Near(Eval("1 >= 1"), 1.0f) && Near(Eval("1 <= 0"), 0.0f), "greater, or-equal");
		Check(Near(Eval("1 == 1"), 1.0f) && Near(Eval("1 != 1"), 0.0f) && Near(Eval("1 != 2"), 1.0f), "equality");
		Check(Near(Eval("1 < 2 and 2 < 3"), 1.0f) && Near(Eval("1 < 2 and 3 < 2"), 0.0f), "and");
		Check(Near(Eval("1 > 2 or 2 < 3"), 1.0f) && Near(Eval("1 > 2 or 3 < 2"), 0.0f), "or");
		Check(Near(Eval("not 0"), 1.0f) && Near(Eval("not 5"), 0.0f), "not");
		Check(Near(Eval("1 < 2 or 0 and 0"), 1.0f), "and binds tighter than or");
		Check(Near(Eval("if(1 < 2, 10, 20)"), 10.0f) && Near(Eval("if(0, 10, 20)"), 20.0f), "if");
		Check(Near(Eval("if(1 - step(0.25, 0.2), 1, 0)"), 1.0f), "if over step");
		Check(Near(Eval("1 + 1 == 2"), 1.0f), "comparison binds looser than arithmetic");
	}

	void Vectors()
	{
		const auto v = EvalValue("[1, 2, 3] * 2 + [1, 1, 1]");
		const auto* v3 = Get<Vec3>(v);
		Check(v3 && *v3 == Vec3{ 3, 5, 7 }, "vec3 arithmetic with a broadcast scalar");
		const auto w = EvalValue("[1, 2] / [2, 4]");
		const auto* v2 = Get<Vec2>(w);
		Check(v2 && *v2 == Vec2{ 0.5f, 0.5f }, "vec2 arithmetic");
		const auto lerped = EvalValue("lerp([0, 0, 0], [1, 0, 0], 0.5)");
		const auto* l3 = Get<Vec3>(lerped);
		Check(l3 && Near(l3->x, 0.5f) && Near(l3->y, 0.0f), "lerp between colours");
		const auto chosen = EvalValue("if(1, [1, 0, 0], [0, 0, 1])");
		Check(Get<Vec3>(chosen) && Get<Vec3>(chosen)->x == 1.0f, "if chooses a colour");
		Check(Near(AsScalar(EvalValue("[0, 0, 0] + [1, 1]")), 0.0f), "vec2 with vec3 evaluates to 0 rather than anything else");
		Check(Near(Eval("[0.2, 0.7, 0.1] > 0.5"), 1.0f), "comparison of a colour uses its luminance");
		Check(Near(Eval("[1, 2, 3]"), 0.2126f * 1 + 0.7152f * 2 + 0.0722f * 3, 1e-4f), "AsScalar of a colour is luminance");
		const Value refs[]{ Vec3{ 1, 0, 0 }, 0.5f };
		const auto  tinted = EvalValue("@hue * @level", refs);
		Check(Get<Vec3>(tinted) && Near(Get<Vec3>(tinted)->x, 0.5f), "reference to a colour scaled by a scalar reference");
	}

	void ReferencesAndCurves()
	{
		const auto p = Program::Parse("@b + @a * @b");
		Check(p && p->References().size() == 2 && p->References()[0] == "b" && p->References()[1] == "a", "references deduplicated in first-use order");
		const Value refs[]{ 4.0f, 0.5f };
		Check(Near(Eval("@b + @a * @b", refs), 6.0f), "references evaluate by index");
		Check(Program::Parse("2 * 3")->Constant() && !Program::Parse("time")->Constant() && !Program::Parse("@x")->Constant() && !Program::Parse("x")->Constant(), "constant detection");

		const auto curve = ParseCurve("pow(1 - x, 2)");
		Check(curve.has_value(), "curve parses");
		Check(curve && Near(ApplyCurve(*curve, 0.5f), 0.25f) && Near(ApplyCurve(*curve, 1.0f), 0.0f), "curve applied");
		const auto contrast = ParseCurve("(x - mean) * 3 + 0.5");
		Check(contrast && Near(ApplyCurve(*contrast, 0.9f, 0.8f), 0.8f), "curve reads mean");
		Check(!ParseCurve("x + @a").has_value(), "a curve reads no rows");
		Check(!ParseCurve("@other(x)").has_value(), "a curve calls no curve");

		const auto caller = Program::Parse("@flash(@hurt) * 2");
		Check(caller && caller->Curves().size() == 1 && caller->Curves()[0] == "flash" && caller->References().size() == 1, "curve calls are separate from references");
		const Value    refs2[]{ 0.5f };
		const Program* curves[]{ &*curve };
		Program::Inputs in;
		in.refs = refs2;
		in.curves = curves;
		Check(caller && Near(AsScalar(caller->Evaluate(in)), 0.5f), "curve call evaluates the curve at the argument");
		const Program* none[]{ nullptr };
		in.curves = none;
		Check(caller && Near(AsScalar(caller->Evaluate(in)), 1.0f), "a missing curve passes its argument through");
		Check(p && p->OpCount() == 5, "op count");
	}

	void TypeCheck()
	{
		const RefTyper types = [](std::string_view name) -> std::optional<ValueType> {
			if (name == "hue") return ValueType::kVec3;
			if (name == "uv") return ValueType::kVec2;
			if (name == "level") return ValueType::kScalar;
			return std::nullopt;
		};
		const auto type = [&](const char* text) -> std::string {
			const auto p = Program::Parse(text);
			if (!p) return "parse:" + p.error();
			const auto t = p->Check(types);
			return t ? Name(*t) : "error:" + t.error();
		};
		Check(type("@level * 2") == "scalar", "scalar result");
		Check(type("@hue * @level") == "vec3", "vec3 times scalar is vec3");
		Check(type("[1, 2] + @uv") == "vec2", "vec2 result");
		Check(type("lerp(@hue, [1, 1, 1], @level)") == "vec3", "lerp of colours");
		Check(type("if(@level > 0.5, @hue, [0, 0, 0])") == "vec3", "if of colours");
		Check(type("@hue + @uv").starts_with("error:") && type("@hue + @uv").find("mixes vec3 with vec2") != std::string::npos, "vec3 with vec2 rejected: " + type("@hue + @uv"));
		Check(type("@hue < 1").starts_with("error:"), "comparison on a vector rejected");
		Check(type("if(@hue, 1, 0)").starts_with("error:"), "vector condition rejected");
		Check(type("@nothing").find("unknown row '@nothing'") != std::string::npos, "unknown reference reported");
		Check(type("@flash(@hue)").starts_with("error:"), "curve on a vector rejected");
		Check(type("[@level, @hue]").starts_with("error:"), "vector components must be scalars");
	}

	void Errors()
	{
		Check(Fails("", "empty"), "empty");
		Check(Fails("(1 + 2", "expected ')'"), "unbalanced parenthesis");
		Check(Fails("foo(1)", "unknown name 'foo'"), "unknown function");
		Check(Fails("min(1)", "takes 2"), "wrong arity");
		Check(Fails("1 +", "unexpected end"), "dangling operator");
		Check(Fails("1 2", "unexpected '2'"), "juxtaposition");
		Check(Fails("2 ^ 3", "no power operator"), "^ is refused with advice");
		Check(Fails("health * 2", "rows are written @health"), "bare name advises the sigil");
		Check(Fails("@", "must be followed by a name"), "lone @");
		Check(Fails("[1]", "two or three"), "one-component vector");
		Check(Fails("[1, 2, 3, 4]", "[a, b]"), "four-component vector");
		Check(Fails("orbit", "unknown name 'orbit'"), "keyword prefix is not a keyword");
		Check(Fails("1 < 2 < 3", "unexpected '<'"), "comparisons do not chain");
		Check(Fails("if(1, 2)", "takes 3"), "if arity");
	}

	void Garbage()
	{
		std::string deep(kMaxExpressionDepth + 5, '(');
		deep += "1";
		deep += std::string(kMaxExpressionDepth + 5, ')');
		Check(Fails(deep.c_str(), "nested deeper"), "nesting limit");
		std::string minus(kMaxExpressionDepth + 5, '-');
		minus += "1";
		Check(Fails(minus.c_str(), "nested deeper"), "unary chain limit");
		std::string wide = "1";
		for (std::size_t i = 0; i < kMaxExpressionOps; ++i) {
			wide += " + 1";
		}
		Check(Fails(wide.c_str(), "more than"), "op limit");
		std::string longText(kMaxExpressionLength + 1, '1');
		Check(Fails(longText.c_str(), "longer than"), "length limit");

		std::mt19937 rng(7);
		const char   alphabet[] = "0123456789.+-*/()[],<>=!@abcdefx_ ";
		int          parsed = 0;
		for (int i = 0; i < 2000; ++i) {
			std::string text;
			const int   n = static_cast<int>(rng() % 40);
			for (int j = 0; j < n; ++j) {
				text += alphabet[rng() % (sizeof(alphabet) - 1)];
			}
			const auto p = Program::Parse(text);
			if (p) {
				++parsed;
				const Value refs[]{ 1.0f, Vec3{ 1, 2, 3 }, Vec2{ 1, 2 } };
				Program::Inputs in;
				in.refs = std::span{ refs, std::min<std::size_t>(p->References().size(), 3) };
				(void)p->Evaluate(in);
				(void)p->Check([](std::string_view) { return ValueType::kScalar; });
			}
		}
		Check(parsed > 0, std::format("fuzz: {} of 2000 random strings parsed and evaluated without incident", parsed));
		for (int i = 0; i < 200; ++i) {
			std::string bytes;
			for (int j = 0; j < 64; ++j) {
				bytes += static_cast<char>(rng() % 256);
			}
			(void)Program::Parse(bytes);
		}
		Check(true, "raw bytes never crash");
	}
}

int main()
{
	Arithmetic();
	Logic();
	Vectors();
	ReferencesAndCurves();
	TypeCheck();
	Errors();
	Garbage();
	return test::Finish("expression");
}
