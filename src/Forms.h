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
	};

	// The detail modal a field opens: the row its text names, shown with its
	// picture or its own editor.
	enum class FieldDetail
	{
		kSource,
		kCurve,
		kOpacity,
		kColor,
		kMask,
	};
	[[nodiscard]] std::string_view FieldDetailName(FieldDetail a_detail) noexcept;

	// Committed text becomes an edit, or nothing when it does not parse.
	using FieldBinding = std::function<std::optional<RecipeEdit>(const std::string&)>;

	// One field of a form: what the page draws as a row of the field table.
	// `detail` is set only when the modal would show something, so a detail
	// button appears only where there is content.
	struct FieldSpec
	{
		std::string                name;
		FieldKind                  kind = FieldKind::kScalar;
		std::string                text;   // the value as written
		std::vector<std::string>   names;  // what the signal or reference combo offers
		bool                       allowEmpty = false;
		std::optional<FieldDetail> detail;
		std::optional<Value>       value;  // the live value, shown as a swatch before the input
		FieldBinding               bind;
	};

	// The selected layer's fields: source, curve, opacity, colour, mask,
	// channels, in that order.
	[[nodiscard]] std::vector<FieldSpec> InspectorForm(const Inspector& a_inspector);
	// The stack's slot scalars, one field each, in the slot's order.
	[[nodiscard]] std::vector<FieldSpec> ScalarForm(const StackView& a_stack);
	// A signal's value as one field of the signal table, shown as its kind
	// (a constant's number or colour, an expr's text) and read as what is
	// typed: a number keeps or makes a scalar constant, three numbers a
	// colour constant, anything else that parses an expression. Nothing for
	// a row edited in the file (efsh, trigger, vec2 ...).
	[[nodiscard]] std::optional<FieldSpec>  SignalForm(const SignalRow& a_signal);
	[[nodiscard]] std::optional<RecipeEdit> SignalValueEdit(const std::string& a_signal, const std::string& a_text);
	// The light's panel: colour, intensity, size, cutoff, offset, shadow,
	// bones as a kind, then the kind's own settings. Empty when the recipe
	// has no light.
	[[nodiscard]] std::vector<FieldSpec> LightForm(const LightRow& a_light, const SignalNames& a_names);
	// The shell's settings: material kind, blend, depth bias, alpha test,
	// alpha, rim power, emissive, and the pose (inflate, offset, scale and
	// its point, spin and its axis).
	[[nodiscard]] std::vector<FieldSpec> ShellForm(const ShellRow& a_shell, const SignalNames& a_names);

	// ---------------------------------------------------------------- colours

	// A colour typed as text: three numbers, or one for all three. Nothing
	// for a reference or text that does not parse. The text form is what the
	// colour picker writes into a field.
	[[nodiscard]] std::optional<Vec3> LiteralColor(std::string_view a_text);
	[[nodiscard]] std::string         LiteralColorText(const Vec3& a_color);

}
