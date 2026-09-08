#pragma once

// A term's settings are its data; its expression is derived from them.
// Each recipe below is one template: BuildTerm (Regions) turns it into the
// sources it needs and the expression text, ReadTerm recovers the template
// a text fits, and a text no template fits is a raw term. Per-texel
// refinements (thresholds, posterize, softness, invert) live in the
// expression; whole-map ones (clusters, components) are source parameters.

#include "Analysis.h"
#include "Recipe.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace WornEnchantmentPBR::Studio
{
	// Typed text, or a text no template fits.
	struct RawTerm
	{
		[[nodiscard]] bool operator==(const RawTerm&) const = default;
	};
	// "@name": a mask or a source of the recipe.
	struct ReferenceTerm
	{
		std::string        name;
		[[nodiscard]] bool operator==(const ReferenceTerm&) const = default;
	};
	// One material channel between low and high, with softness on both
	// edges; posterize > 1 quantises the channel to that many levels first.
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
	// A what preset from the shipped file, by name.
	struct WhatPresetTerm
	{
		std::string        preset;
		[[nodiscard]] bool operator==(const WhatPresetTerm&) const = default;
	};
	// The biped partition: a partition bake.
	struct PartitionTerm
	{
		std::uint32_t      slot = 32;
		[[nodiscard]] bool operator==(const PartitionTerm&) const = default;
	};
	// The summed weight of these bones: a boneWeight bake.
	struct BoneTerm
	{
		std::vector<std::string> bones;
		[[nodiscard]] bool       operator==(const BoneTerm&) const = default;
	};
	// One region of the mesh analysis: the id map bake tested for the id.
	struct IslandTerm
	{
		IslandSource       source = IslandSource::kComponent;
		std::uint16_t      id = 0;
		[[nodiscard]] bool operator==(const IslandTerm&) const = default;
	};
	// One cluster of the material analysis: the cluster map tested for the id.
	struct ClusterTerm
	{
		ClusterSettings    settings;
		std::uint8_t       id = 0;
		[[nodiscard]] bool operator==(const ClusterTerm&) const = default;
	};

	using TermKind = std::variant<RawTerm, ReferenceTerm, ThresholdTerm, WhatPresetTerm, PartitionTerm, BoneTerm, IslandTerm, ClusterTerm>;

	// "raw", "reference", "threshold", "preset", "partition", "bones", "component", "cluster".
	[[nodiscard]] std::string_view TermKindName(const TermKind& a_recipe) noexcept;
}
