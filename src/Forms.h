#pragma once

#include "Edits.h"
#include "Snapshot.h"
#include "Studio.h"

#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace WornEnchantmentPBR::Studio
{

	enum class FieldKind
	{
		kScalar,
		kColor,
		kVector,
		kReference,
		kExpression,
		kCurve,
		kMask,
		kChannels,
		kToggle,
		kChoice,
		kText,
		kVec2,
		kName,
		kSignalValue,
	};
	inline constexpr std::size_t kFieldKindCount = 14;

	enum class FieldInputKind
	{
		kCombo,
		kChoice,
		kToggle,
		kText,
		kPlain,
		kValue,
	};

	enum class FieldCheckKind
	{
		kScalar,
		kColorOrVector,
		kVec2,
		kReference,
		kExpression,
		kMask,
		kCurve,
		kChannels,
		kChoice,
		kName,
		kSignalValue,
		kNone,
	};

	enum class Swatch
	{
		kNone,
		kAlways,
		kWhenColour,
	};

	struct FieldKindSpec
	{
		FieldKind      value;
		const char*    glyph;
		bool           takesSignal;
		const char*    rule;
		FieldInputKind input;
		FieldCheckKind check;
		Swatch         swatch;
	};

	inline constexpr FieldKindSpec kFieldKinds[]{
		{ FieldKind::kScalar, "#", true, "scalar: a number, or @signal of scalar type", FieldInputKind::kValue, FieldCheckKind::kScalar, Swatch::kNone },
		{ FieldKind::kColor, "c", true, "colour: r, g, b in 0..1, or one number for all three, or @signal of colour type", FieldInputKind::kValue, FieldCheckKind::kColorOrVector, Swatch::kAlways },
		{ FieldKind::kVector, "v", true, "vector: x, y, z (a position, direction or scale), or one number for all three, or @signal of vector type", FieldInputKind::kValue, FieldCheckKind::kColorOrVector, Swatch::kNone },
		{ FieldKind::kReference, "@", false, "reference: @name of a row of the recipe", FieldInputKind::kCombo, FieldCheckKind::kReference, Swatch::kNone },
		{ FieldKind::kExpression, "=", false, "expression: numbers, [r, g, b], @signals, + - * /, comparisons, and/or/not, if(c, a, b), abs min max clamp saturate floor ceil frac sqrt pow sin cos step smoothstep lerp, time, pi", FieldInputKind::kText, FieldCheckKind::kExpression, Swatch::kNone },
		{ FieldKind::kCurve, "x", false, "curve: an expression in x (mean is the source's mean), or @curve", FieldInputKind::kValue, FieldCheckKind::kCurve, Swatch::kNone },
		{ FieldKind::kMask, "m", false, "mask: an expression per texel where @source and @mask names are images and @signals are this tick's values", FieldInputKind::kText, FieldCheckKind::kMask, Swatch::kNone },
		{ FieldKind::kChannels, "ch", false, "channels: any of r g b a, in any order", FieldInputKind::kText, FieldCheckKind::kChannels, Swatch::kNone },
		{ FieldKind::kToggle, "?", false, "on or off", FieldInputKind::kToggle, FieldCheckKind::kNone, Swatch::kNone },
		{ FieldKind::kChoice, "o", false, "one of the listed values", FieldInputKind::kChoice, FieldCheckKind::kChoice, Swatch::kNone },
		{ FieldKind::kText, "\"", false, "text", FieldInputKind::kText, FieldCheckKind::kNone, Swatch::kNone },
		{ FieldKind::kVec2, "v", true, "vec2: x, y, or one number for both, or @signal of vec2 type", FieldInputKind::kValue, FieldCheckKind::kVec2, Swatch::kNone },
		{ FieldKind::kName, "n", false, "name: letters, digits and underscores, not starting with a digit, and not another row's", FieldInputKind::kPlain, FieldCheckKind::kName, Swatch::kNone },
		{ FieldKind::kSignalValue, "=", false, "value: a number keeps or makes a scalar constant, r, g, b a colour constant, anything else that parses an expression over signals", FieldInputKind::kValue, FieldCheckKind::kSignalValue, Swatch::kWhenColour },
	};
	static_assert(std::size(kFieldKinds) == kFieldKindCount);

	enum class FieldDetail
	{
		kSource,
		kCurve,
		kOpacity,
		kColor,
		kMask,
		kSignal,
	};
	[[nodiscard]] std::string_view FieldDetailName(FieldDetail a_detail) noexcept;

	using FieldBinding = std::function<std::optional<RecipeEdit>(const std::string&)>;
	using FieldCreator = std::function<std::vector<RecipeEdit>(const std::string&)>;

	struct FormField
	{
		std::string                name;
		FieldKind                  kind = FieldKind::kScalar;
		std::string                text;
		std::vector<std::string>   names;
		bool                       allowEmpty = false;
		std::optional<FieldDetail> detail;
		std::optional<Value>       value;
		FieldBinding               bind;
		std::vector<std::string>   creators;
		FieldCreator               create;
	};

	enum class RowKind
	{
		kSignal,
		kCurve,
		kSource,
		kMask,
	};
	[[nodiscard]] FormField RowNameField(RowKind a_kind, const std::string& a_name, std::vector<std::string> a_taken);
	[[nodiscard]] FormField CurveTextField(const std::string& a_curve, const std::string& a_text);
	[[nodiscard]] FormField MaskTextField(const std::string& a_mask, const std::string& a_text);

	[[nodiscard]] std::vector<FormField> InspectorForm(const Inspector& a_inspector);
	[[nodiscard]] std::vector<FormField> ScalarForm(const LayerStack& a_stack);
	[[nodiscard]] std::optional<FormField>  SignalForm(const SignalRow& a_signal);
	[[nodiscard]] std::optional<RecipeEdit> SignalValueEdit(const std::string& a_signal, const std::string& a_text);
	[[nodiscard]] std::vector<FormField> SourceForm(const SourceRow& a_source, const SignalNames& a_names);
	[[nodiscard]] std::vector<FormField> LightForm(const LightRow& a_light, const SignalNames& a_names);
	[[nodiscard]] std::vector<FormField> ShellForm(const ShellRow& a_shell, const SignalNames& a_names);

	[[nodiscard]] std::optional<Vec3> LiteralColor(std::string_view a_text);
	[[nodiscard]] std::string         LiteralColorText(const Vec3& a_color);

}
