#pragma once

#include "Analysis.h"
#include "Recipe.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace WornEnchantmentPBR::Studio
{
	struct RawTerm
	{
		[[nodiscard]] bool operator==(const RawTerm&) const = default;
	};
	struct ReferenceTerm
	{
		std::string        name;
		[[nodiscard]] bool operator==(const ReferenceTerm&) const = default;
	};
	struct ThresholdTerm
	{
		MaterialChannel    channel = MaterialChannel::kMetallic;
		float              low = 0.0f;
		float              high = 1.0f;
		float              softness = 0.05f;
		std::uint8_t       posterize = 0;
		bool               invert = false;
		[[nodiscard]] bool operator==(const ThresholdTerm&) const = default;
	};
	struct WhatPresetTerm
	{
		std::string        preset;
		[[nodiscard]] bool operator==(const WhatPresetTerm&) const = default;
	};
	struct PartitionTerm
	{
		std::uint32_t      slot = 32;
		[[nodiscard]] bool operator==(const PartitionTerm&) const = default;
	};
	struct BoneTerm
	{
		std::vector<std::string> bones;
		[[nodiscard]] bool       operator==(const BoneTerm&) const = default;
	};
	struct IslandTerm
	{
		IslandSource       source = IslandSource::kComponent;
		std::uint16_t      id = 0;
		[[nodiscard]] bool operator==(const IslandTerm&) const = default;
	};
	struct ClusterTerm
	{
		ClusterSettings    settings;
		std::uint8_t       id = 0;
		[[nodiscard]] bool operator==(const ClusterTerm&) const = default;
	};

	using TermKind = std::variant<RawTerm, ReferenceTerm, ThresholdTerm, WhatPresetTerm, PartitionTerm, BoneTerm, IslandTerm, ClusterTerm>;

	[[nodiscard]] std::string_view TermKindName(const TermKind& a_recipe) noexcept;
}
