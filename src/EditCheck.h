#pragma once

// Validation before apply: a field's text checked against the recipe's row
// names and types while it is typed, with the same parsers the file uses,
// so a field draws red until its text would be accepted and an unknown
// @name never reaches EditRecipe. What only the runtime knows (a texture
// file, a bone on this skeleton) still shows on the row after apply.

#include "Core.h"
#include "Forms.h"
#include "Snapshot.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace WornEnchantmentPBR::Studio
{
	// The names a field's text may reference, by kind, with the texel or
	// tick type of each. Masks read as scalars; the compositor types them by
	// their expression and reports a mismatch on the row.
	struct Names
	{
		std::vector<std::pair<std::string, ValueType>> signals;
		std::vector<std::string>                       curves;
		std::vector<std::pair<std::string, ValueType>> sources;
		std::vector<std::string>                       masks;
	};
	[[nodiscard]] Names NamesOf(const RecipeRow& a_recipe, const GeometryRow& a_geometry);

	// Why the text would be refused for a field of that kind, or nothing
	// when it parses and every reference it makes is one the field may make:
	// a whole `@name` must be one of the field's own combo names; a
	// component reference in a colour or vector a scalar signal; an
	// expression's references signals (a signal's), or sources, masks and
	// signals (a mask's); a curve's `@name` a declared curve.
	[[nodiscard]] std::optional<std::string> CheckField(const FormField& a_field, std::string_view a_text, const Names& a_names);
	// The texts the tables hold outside a form: a signal's value (a number,
	// a colour, or an expression over signals), a curve's expression (in x,
	// over signals), a mask's expression (per texel, over sources, masks
	// and signals).
	[[nodiscard]] std::optional<std::string> CheckSignalValue(std::string_view a_text, const Names& a_names);
	[[nodiscard]] std::optional<std::string> CheckCurveText(std::string_view a_text, const Names& a_names);
	[[nodiscard]] std::optional<std::string> CheckMaskText(std::string_view a_text, const Names& a_names);
}
