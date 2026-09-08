#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace WornEnchantmentPBR
{
	struct Vec2
	{
		float x = 0.0f;
		float y = 0.0f;
		[[nodiscard]] bool operator==(const Vec2&) const = default;
	};

	struct Vec3
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		[[nodiscard]] bool operator==(const Vec3&) const = default;
	};

	using Value = std::variant<float, Vec2, Vec3>;

	enum class ValueType
	{
		kScalar,
		kVec2,
		kVec3,
	};

	enum class ShaderChannel : std::uint32_t
	{
		kR = 0,
		kG = 1,
		kB = 2,
		kA = 3,
		kRgb = 4,
		kLuma = 5,
	};

	[[nodiscard]] inline ValueType TypeOf(const Value& a_value) noexcept
	{
		return static_cast<ValueType>(a_value.index());
	}

	[[nodiscard]] inline const char* Name(ValueType a_type) noexcept
	{
		switch (a_type) {
		case ValueType::kScalar:
			return "scalar";
		case ValueType::kVec2:
			return "vec2";
		case ValueType::kVec3:
			return "vec3";
		}
		return "?";
	}

	[[nodiscard]] inline float AsScalar(const Value& a_value) noexcept
	{
		if (const auto* f = std::get_if<float>(&a_value)) {
			return *f;
		}
		if (const auto* v = std::get_if<Vec3>(&a_value)) {
			return 0.2126f * v->x + 0.7152f * v->y + 0.0722f * v->z;
		}
		return std::get<Vec2>(a_value).x;
	}

	[[nodiscard]] inline Vec3 AsVec3(const Value& a_value) noexcept
	{
		if (const auto* v = std::get_if<Vec3>(&a_value)) {
			return *v;
		}
		if (const auto* v = std::get_if<Vec2>(&a_value)) {
			return Vec3{ v->x, v->y, 0.0f };
		}
		const float f = std::get<float>(a_value);
		return Vec3{ f, f, f };
	}

	[[nodiscard]] inline Vec2 AsVec2(const Value& a_value) noexcept
	{
		if (const auto* v = std::get_if<Vec2>(&a_value)) {
			return *v;
		}
		if (const auto* v = std::get_if<Vec3>(&a_value)) {
			return Vec2{ v->x, v->y };
		}
		const float f = std::get<float>(a_value);
		return Vec2{ f, f };
	}

	[[nodiscard]] inline float Clamp01(float a_x) noexcept
	{
		return a_x < 0.0f ? 0.0f : (a_x > 1.0f ? 1.0f : a_x);
	}

	struct Ref
	{
		std::string        name;
		[[nodiscard]] bool operator==(const Ref&) const = default;
	};

	template <class... Fs>
	struct Overloaded : Fs...
	{
		using Fs::operator()...;
	};

	template <class Variant, class... Fs>
	[[nodiscard]] decltype(auto) Match(Variant&& a_variant, Fs&&... a_cases)
	{
		return std::visit(Overloaded<std::decay_t<Fs>...>{ std::forward<Fs>(a_cases)... }, std::forward<Variant>(a_variant));
	}

	template <class T, class Variant>
	[[nodiscard]] const T* Get(const Variant& a_variant) noexcept
	{
		return std::get_if<T>(&a_variant);
	}

	template <class T, class Variant>
	[[nodiscard]] T* Get(Variant& a_variant) noexcept
	{
		return std::get_if<T>(&a_variant);
	}

	template <class T, class Variant>
	[[nodiscard]] bool Is(const Variant& a_variant) noexcept
	{
		return std::holds_alternative<T>(a_variant);
	}

	template <class E>
	struct Named
	{
		E                value;
		std::string_view name;
	};

	template <class Row, std::size_t N, class E>
	[[nodiscard]] constexpr std::string_view NameOf(const Row (&a_table)[N], E a_value) noexcept
	{
		for (const auto& row : a_table) {
			if (row.value == a_value) {
				return row.name;
			}
		}
		return "?";
	}

	template <class Row, std::size_t N>
	[[nodiscard]] constexpr std::optional<decltype(Row::value)> FromName(const Row (&a_table)[N], std::string_view a_name) noexcept
	{
		for (const auto& row : a_table) {
			if (row.name == a_name) {
				return row.value;
			}
		}
		return std::nullopt;
	}

	template <class Row, std::size_t N, class E>
	[[nodiscard]] constexpr const Row* RowOf(const Row (&a_table)[N], E a_value) noexcept
	{
		for (const auto& row : a_table) {
			if (row.value == a_value) {
				return &row;
			}
		}
		return nullptr;
	}

	template <class Row, std::size_t N>
	[[nodiscard]] std::string Choices(const Row (&a_table)[N])
	{
		std::string out;
		for (const auto& row : a_table) {
			out += (out.empty() ? "" : ", ") + std::string{ row.name };
		}
		return out;
	}

	template <std::size_t N>
	[[nodiscard]] std::vector<std::string> WordsOf(const std::string_view (&a_words)[N])
	{
		return std::vector<std::string>(std::begin(a_words), std::end(a_words));
	}

	template <class Variant, std::size_t... I>
	[[nodiscard]] std::optional<Variant> AlternativeAt(std::size_t a_index, std::index_sequence<I...>)
	{
		std::optional<Variant> made;
		((a_index == I ? (made.emplace(std::in_place_index<I>), true) : false) || ...);
		return made;
	}

	template <class Variant>
	[[nodiscard]] std::optional<Variant> AlternativeAt(std::size_t a_index)
	{
		return AlternativeAt<Variant>(a_index, std::make_index_sequence<std::variant_size_v<Variant>>{});
	}

	template <class Row, std::size_t N>
	[[nodiscard]] std::vector<std::string> WordsOf(const Row (&a_table)[N])
	{
		std::vector<std::string> out;
		for (const auto& row : a_table) {
			out.emplace_back(row.name);
		}
		return out;
	}
}
