#pragma once

// The forms the studio draws: a field is a name, the kind of value it takes
// (which decides its badge, its rule and whether a @signal may stand in),
// the value as text, what its combo offers, and a binding that turns
// committed text into a recipe edit. Built by pure functions from the view
// models; the page draws them through one widget per kind.

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
	// ------------------------------------------------------------------ forms

	// What a field requires: the kind of value decides the badge it wears,
	// the rule its tooltip states, and whether a @signal may stand in for the
	// value (scalar, colour and vector).
	enum class FieldKind
	{
		kScalar,      // a number, or @signal of scalar type
		kColor,       // r, g, b (one number for all three), or @signal of colour type
		kVector,      // x, y, z as a position, direction or scale, or @signal of vector type
		kReference,   // @name of a row
		kExpression,  // the recipe language
		kCurve,       // an expression in x, or @curve
		kMask,        // an expression per texel over sources and masks
		kChannels,    // a subset of rgba
		kToggle,      // on or off; the text is "on" or "off"
		kChoice,      // one of the names, as written
		kText,        // plain text (a list of bone names)
		kVec2,        // x, y (one number for both), or @signal of vec2 type
	};
	inline constexpr std::size_t kFieldKindCount = 12;

	// The widget a field's kind draws: a combo over its names (reference), a
	// combo over its names spelled as written (choice), a checkbox (toggle),
	// a badge plus a text field (expression, mask, channels, plain text), or
	// a value field that switches between text and a signal combo (scalar,
	// colour, vector, vec2, curve).
	enum class FieldInputKind
	{
		kCombo,
		kChoice,
		kToggle,
		kText,
		kValue,
	};

	// The shape EditCheck::CheckField runs for a kind: several kinds share a
	// shape (colour and vector are both three numbers or @signal), and
	// toggle and text take no check beyond emptiness.
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
		kNone,
	};

	// One row per kind, engine-free: the badge's glyph, whether a @signal may
	// stand in for the value, the tooltip's rule, the widget that draws it
	// and the shape of its check. `StyleOf` in MenuWidgets.cpp reads this and
	// adds the ImVec4 colour, keyed by kind, since ImGui types have no home
	// here; `FieldInput` (ComposePage.cpp) and `CheckField` (EditCheck.cpp)
	// dispatch on `input` and `check` instead of switching on the kind.
	struct FieldKindSpec
	{
		FieldKind      value;
		const char*    glyph;
		bool           takesSignal;
		const char*    rule;
		FieldInputKind input;
		FieldCheckKind check;
	};

	inline constexpr FieldKindSpec kFieldKinds[]{
		{ FieldKind::kScalar, "#", true, "scalar: a number, or @signal of scalar type", FieldInputKind::kValue, FieldCheckKind::kScalar },
		{ FieldKind::kColor, "c", true, "colour: r, g, b in 0..1, or one number for all three, or @signal of colour type", FieldInputKind::kValue, FieldCheckKind::kColorOrVector },
		{ FieldKind::kVector, "v", true, "vector: x, y, z (a position, direction or scale), or one number for all three, or @signal of vector type", FieldInputKind::kValue, FieldCheckKind::kColorOrVector },
		{ FieldKind::kReference, "@", false, "reference: @name of a row of the recipe", FieldInputKind::kCombo, FieldCheckKind::kReference },
		{ FieldKind::kExpression, "=", false, "expression: numbers, [r, g, b], @signals, + - * /, comparisons, and/or/not, if(c, a, b), abs min max clamp saturate floor ceil frac sqrt pow sin cos step smoothstep lerp, time, pi", FieldInputKind::kText, FieldCheckKind::kExpression },
		{ FieldKind::kCurve, "x", false, "curve: an expression in x (mean is the source's mean), or @curve", FieldInputKind::kValue, FieldCheckKind::kCurve },
		{ FieldKind::kMask, "m", false, "mask: an expression per texel where @source and @mask names are images and @signals are this tick's values", FieldInputKind::kText, FieldCheckKind::kMask },
		{ FieldKind::kChannels, "ch", false, "channels: any of r g b a, in any order", FieldInputKind::kText, FieldCheckKind::kChannels },
		{ FieldKind::kToggle, "?", false, "on or off", FieldInputKind::kToggle, FieldCheckKind::kNone },
		{ FieldKind::kChoice, "o", false, "one of the listed values", FieldInputKind::kChoice, FieldCheckKind::kChoice },
		{ FieldKind::kText, "\"", false, "text", FieldInputKind::kText, FieldCheckKind::kNone },
		{ FieldKind::kVec2, "v", true, "vec2: x, y, or one number for both, or @signal of vec2 type", FieldInputKind::kValue, FieldCheckKind::kVec2 },
	};
	static_assert(std::size(kFieldKinds) == kFieldKindCount);

	// The detail modal a field opens: the row its text names, shown with its
	// picture or its own editor.
	enum class FieldDetail
	{
		kSource,
		kCurve,
		kOpacity,
		kColor,
		kMask,
		kSignal,  // the signal the field's text names, in a modal that follows references
	};
	[[nodiscard]] std::string_view FieldDetailName(FieldDetail a_detail) noexcept;

	// Committed text becomes an edit, or nothing when it does not parse.
	using FieldBinding = std::function<std::optional<RecipeEdit>(const std::string&)>;
	// A creator entry of the field's combo ("new image", "promote") becomes
	// the edits that make the row and bind the field to it, in order.
	using FieldCreator = std::function<std::vector<RecipeEdit>(const std::string&)>;

	// One field of a form: what the page draws as a row of the field table.
	// `detail` is set only when the modal would show something, so a detail
	// button appears only where there is content.
	struct FormField
	{
		std::string                name;
		FieldKind                  kind = FieldKind::kScalar;
		std::string                text;   // the value as written
		std::vector<std::string>   names;  // what the signal or reference combo offers
		bool                       allowEmpty = false;
		std::optional<FieldDetail> detail;
		std::optional<Value>       value;  // the live value, shown as a swatch before the input
		FieldBinding               bind;
		std::vector<std::string>   creators;  // entries after the names: what the field can make in place
		FieldCreator               create;
	};

	// The selected layer's fields: source, curve, opacity, colour, mask,
	// channels, in that order.
	[[nodiscard]] std::vector<FormField> InspectorForm(const Inspector& a_inspector);
	// The stack's slot scalars, one field each, in the slot's order.
	[[nodiscard]] std::vector<FormField> ScalarForm(const StackView& a_stack);
	// A signal's value as one field of the signal table, shown as its kind
	// (a constant's number or colour, an expr's text) and read as what is
	// typed: a number keeps or makes a scalar constant, three numbers a
	// colour constant, anything else that parses an expression. Nothing for
	// a row edited in the file (efsh, trigger, vec2 ...).
	[[nodiscard]] std::optional<FormField>  SignalForm(const SignalRow& a_signal);
	[[nodiscard]] std::optional<RecipeEdit> SignalValueEdit(const std::string& a_signal, const std::string& a_text);
	// A source's form: the kind first (a change starts the kind at its
	// defaults), then the kind's own settings, each rebuilding the whole
	// kind from the row's texts.
	[[nodiscard]] std::vector<FormField> SourceForm(const SourceRow& a_source, const SignalNames& a_names);
	// The light's panel: colour, intensity, size, cutoff, offset, shadow,
	// bones as a kind, then the kind's own settings. Empty when the recipe
	// has no light.
	[[nodiscard]] std::vector<FormField> LightForm(const LightRow& a_light, const SignalNames& a_names);
	// The shell's settings: material kind, blend, depth bias, alpha test,
	// alpha, rim power, emissive, and the pose (inflate, offset, scale and
	// its point, spin and its axis).
	[[nodiscard]] std::vector<FormField> ShellForm(const ShellRow& a_shell, const SignalNames& a_names);

	// ---------------------------------------------------------------- colours

	// A colour typed as text: three numbers, or one for all three. Nothing
	// for a reference or text that does not parse. The text form is what the
	// colour picker writes into a field.
	[[nodiscard]] std::optional<Vec3> LiteralColor(std::string_view a_text);
	[[nodiscard]] std::string         LiteralColorText(const Vec3& a_color);

}
