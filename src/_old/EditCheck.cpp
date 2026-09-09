#include "EditCheck.h"

#include "Expression.h"
#include "Recipe.h"

#include <algorithm>
#include <format>

namespace BetterEnchantmentEffects::Studio
{
	namespace
	{
		[[nodiscard]] bool Listed(std::span<const std::string> a_names, std::string_view a_name)
		{
			return std::ranges::find(a_names, a_name) != a_names.end();
		}

		[[nodiscard]] std::optional<ValueType> SignalType(const Names& a_names, std::string_view a_name)
		{
			for (const auto& [name, type] : a_names.signals) {
				if (name == a_name) {
					return type;
				}
			}
			return std::nullopt;
		}

		[[nodiscard]] std::optional<ValueType> TexelType(const Names& a_names, std::string_view a_name)
		{
			for (const auto& [name, type] : a_names.sources) {
				if (name == a_name) {
					return type;
				}
			}
			if (Listed(a_names.masks, a_name)) {
				return ValueType::kScalar;
			}
			return SignalType(a_names, a_name);
		}

		[[nodiscard]] std::optional<std::string> CheckWholeReference(const FormField& a_field, std::string_view a_name)
		{
			if (Listed(a_field.names, a_name)) {
				return std::nullopt;
			}
			return std::format("'@{}' is not one of the rows this field takes", a_name);
		}

		[[nodiscard]] std::optional<std::string> CheckComponent(const Names& a_names, const Param& a_part)
		{
			const auto* ref = Get<Ref>(a_part);
			if (!ref) {
				return std::nullopt;
			}
			const auto type = SignalType(a_names, ref->name);
			if (!type) {
				return std::format("'@{}' is not a signal", ref->name);
			}
			if (*type != ValueType::kScalar) {
				return std::format("'@{}' is not a scalar signal", ref->name);
			}
			return std::nullopt;
		}

		[[nodiscard]] std::optional<std::string> CheckScalar(const FormField& a_field, std::string_view a_text)
		{
			const auto param = ParseParam(a_text);
			if (!param) {
				return "a number, or @signal";
			}
			if (const auto* ref = Get<Ref>(*param)) {
				return CheckWholeReference(a_field, ref->name);
			}
			const auto* number = Get<float>(*param);
			if (a_field.range && number && (*number < a_field.range->first || *number > a_field.range->second)) {
				return std::format("{} to {}", ParamText(a_field.range->first), ParamText(a_field.range->second));
			}
			return std::nullopt;
		}

		[[nodiscard]] std::optional<std::string> CheckLayerSource(const FormField& a_field, std::string_view a_text)
		{
			if (a_text.starts_with('@')) {
				return CheckWholeReference(a_field, a_text.substr(1));
			}
			return LiteralColor(a_text) ? std::nullopt : std::optional<std::string>{ "@source, @mask, or r, g, b" };
		}

		[[nodiscard]] std::optional<std::string> CheckVec3(const FormField& a_field, std::string_view a_text, const Names& a_names)
		{
			const auto param = ParseVec3Param(a_text);
			if (!param) {
				return "three numbers, one number, or @signal";
			}
			if (const auto* ref = Get<Ref>(*param)) {
				return CheckWholeReference(a_field, ref->name);
			}
			for (const auto& part : *Get<std::array<Param, 3>>(*param)) {
				if (auto problem = CheckComponent(a_names, part)) {
					return problem;
				}
			}
			return std::nullopt;
		}

		[[nodiscard]] std::optional<std::string> CheckExpression(std::string_view a_text, const Names& a_names, bool a_texel, bool a_curve)
		{
			const auto program = Program::Parse(a_text);
			if (!program) {
				return program.error();
			}
			for (const auto& name : program->References()) {
				const bool known = a_texel ? TexelType(a_names, name).has_value() : SignalType(a_names, name).has_value();
				if (!known) {
					return a_texel ? std::format("'@{}' is not a source, mask or signal", name) : std::format("'@{}' is not a signal", name);
				}
			}
			for (const auto& name : program->Curves()) {
				if (!Listed(a_names.curves, name)) {
					return std::format("'@{}(' is not a declared curve", name);
				}
			}
			if (program->UsesX() && !a_curve) {
				return "'x' is only defined inside a curve";
			}
			const auto type = program->Check([&](std::string_view a_name) { return a_texel ? TexelType(a_names, a_name) : SignalType(a_names, a_name); });
			if (!type) {
				return type.error();
			}
			return std::nullopt;
		}

		[[nodiscard]] std::optional<std::string> CheckVec2(const FormField& a_field, std::string_view a_text, const Names& a_names)
		{
			const auto param = ParseVec2Param(a_text);
			if (!param) {
				return "two numbers, one number, or @signal";
			}
			if (const auto* ref = Get<Ref>(*param)) {
				return CheckWholeReference(a_field, ref->name);
			}
			for (const auto& part : *Get<std::array<Param, 2>>(*param)) {
				if (auto problem = CheckComponent(a_names, part)) {
					return problem;
				}
			}
			return std::nullopt;
		}

		[[nodiscard]] std::optional<std::string> CheckCurve(const FormField& a_field, std::string_view a_text, const Names& a_names)
		{
			const CurveRef ref{ std::string{ a_text } };
			if (const auto name = ref.Named()) {
				return Listed(a_field.names, *name) ? std::nullopt : std::optional{ std::format("'@{}' is not a declared curve", *name) };
			}
			return CheckExpression(a_text, a_names, false, true);
		}

		[[nodiscard]] std::optional<std::string> CheckReference(const FormField& a_field, std::string_view a_text)
		{
			return a_text.starts_with('@') && a_text.size() > 1 ? CheckWholeReference(a_field, a_text.substr(1)) : std::optional<std::string>{ "@name of a row" };
		}

		[[nodiscard]] std::optional<std::string> CheckChannels(std::string_view a_text)
		{
			return ChannelSet::Parse(a_text) ? std::nullopt : std::optional<std::string>{ "any of r g b a" };
		}

		[[nodiscard]] std::optional<std::string> CheckChoice(const FormField& a_field, std::string_view a_text)
		{
			return Listed(a_field.names, a_text) ? std::nullopt : std::optional<std::string>{ "one of the listed values" };
		}

		[[nodiscard]] std::optional<std::string> CheckName(const FormField& a_field, std::string_view a_text)
		{
			if (!IsName(a_text)) {
				return "letters, digits and underscores, not starting with a digit";
			}
			if (a_text != a_field.text && Listed(a_field.names, a_text)) {
				return std::format("another row is named '{}'", a_text);
			}
			return std::nullopt;
		}
	}

	std::vector<std::string> TakenNames(RowKind a_kind, const Names& a_names)
	{
		std::vector<std::string> taken;
		switch (a_kind) {
		case RowKind::kSignal:
			for (const auto& [name, type] : a_names.signals) {
				taken.push_back(name);
			}
			break;
		case RowKind::kCurve:
			taken = a_names.curves;
			break;
		case RowKind::kSource:
		case RowKind::kMask:
			for (const auto& [name, type] : a_names.sources) {
				taken.push_back(name);
			}
			taken.insert(taken.end(), a_names.masks.begin(), a_names.masks.end());
			break;
		}
		return taken;
	}

	Names NamesOf(const RecipeRow& a_recipe, const GeometryRow& a_geometry)
	{
		Names names;
		for (const auto& signal : a_recipe.signals) {
			names.signals.emplace_back(signal.name, signal.type);
		}
		for (const auto& curve : a_recipe.curves) {
			names.curves.push_back(curve.name);
		}
		for (const auto& source : a_geometry.sources) {
			names.sources.emplace_back(source.name, source.type);
		}
		for (const auto& mask : a_geometry.masks) {
			names.masks.push_back(mask.name);
		}
		return names;
	}

	std::optional<std::string> CheckSignalValue(std::string_view a_text, const Names& a_names)
	{
		if (a_text.empty()) {
			return "cannot be empty";
		}
		if (const auto param = ParseParam(a_text); param && Get<float>(*param)) {
			return std::nullopt;
		}
		if (const auto colour = ParseVec3Param(a_text)) {
			if (const auto* parts = Get<std::array<Param, 3>>(*colour); parts && std::ranges::all_of(*parts, [](const Param& p) { return Get<float>(p) != nullptr; })) {
				return std::nullopt;
			}
		}
		return CheckExpression(a_text, a_names, false, false);
	}

	std::optional<std::string> CheckCurveText(std::string_view a_text, const Names& a_names)
	{
		if (a_text.empty()) {
			return "cannot be empty";
		}
		return CheckExpression(a_text, a_names, false, true);
	}

	std::optional<std::string> CheckMaskText(std::string_view a_text, const Names& a_names)
	{
		if (a_text.empty()) {
			return "cannot be empty";
		}
		return CheckExpression(a_text, a_names, true, false);
	}

	std::optional<std::string> CheckField(const FormField& a_field, std::string_view a_text, const Names& a_names)
	{
		if (a_text.empty()) {
			return a_field.allowEmpty ? std::nullopt : std::optional<std::string>{ "cannot be empty" };
		}
		const auto* row = RowOf(kFieldKinds, a_field.kind);
		if (!row) {
			return std::nullopt;
		}
		switch (row->check) {
		case FieldCheckKind::kScalar:
			return CheckScalar(a_field, a_text);
		case FieldCheckKind::kColorOrVector:
			return CheckVec3(a_field, a_text, a_names);
		case FieldCheckKind::kVec2:
			return CheckVec2(a_field, a_text, a_names);
		case FieldCheckKind::kReference:
			return CheckReference(a_field, a_text);
		case FieldCheckKind::kExpression:
			return CheckExpression(a_text, a_names, false, false);
		case FieldCheckKind::kMask:
			return CheckExpression(a_text, a_names, true, false);
		case FieldCheckKind::kCurve:
			return CheckCurve(a_field, a_text, a_names);
		case FieldCheckKind::kChannels:
			return CheckChannels(a_text);
		case FieldCheckKind::kChoice:
			return CheckChoice(a_field, a_text);
		case FieldCheckKind::kName:
			return CheckName(a_field, a_text);
		case FieldCheckKind::kSignalValue:
			return CheckSignalValue(a_text, a_names);
		case FieldCheckKind::kLayerSource:
			return CheckLayerSource(a_field, a_text);
		case FieldCheckKind::kNone:
			return std::nullopt;
		}
		return std::nullopt;
	}
}
