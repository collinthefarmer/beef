#pragma once

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
	struct Names
	{
		std::vector<std::pair<std::string, ValueType>> signals;
		std::vector<std::string>                       curves;
		std::vector<std::pair<std::string, ValueType>> sources;
		std::vector<std::string>                       masks;
	};
	[[nodiscard]] Names NamesOf(const RecipeRow& a_recipe, const GeometryRow& a_geometry);

	[[nodiscard]] std::vector<std::string> TakenNames(RowKind a_kind, const Names& a_names);

	[[nodiscard]] std::optional<std::string> CheckField(const FormField& a_field, std::string_view a_text, const Names& a_names);
	[[nodiscard]] std::optional<std::string> CheckSignalValue(std::string_view a_text, const Names& a_names);
	[[nodiscard]] std::optional<std::string> CheckCurveText(std::string_view a_text, const Names& a_names);
	[[nodiscard]] std::optional<std::string> CheckMaskText(std::string_view a_text, const Names& a_names);
}
