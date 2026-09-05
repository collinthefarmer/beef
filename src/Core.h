#pragma once

// Value types shared by every recipe module, and the two helpers that keep
// std::variant mechanics out of domain code: Match (visit with lambdas) and
// Get (a checked pointer to one alternative).

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace WornEnchantmentPBR
{
	struct Vec2
	{
		float x = 0.0f;
		float y = 0.0f;
		[[nodiscard]] bool operator==(const Vec2&) const = default;
	};

	// A colour is a Vec3 by another name: r, g, b in x, y, z.
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

	// One number for any value: itself, or a colour's luminance, or x.
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

	// A reference to another row of the recipe, written "@name" in files.
	struct Ref
	{
		std::string        name;
		[[nodiscard]] bool operator==(const Ref&) const = default;
	};

	// Match(value, [](const A&) {...}, [](const B&) {...}) visits a variant
	// with one lambda per alternative (or a generic one for the rest).
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

	// Get<T>(variant) is the alternative T, or null: never throws.
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
}
