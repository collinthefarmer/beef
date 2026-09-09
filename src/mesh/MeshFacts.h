#pragma once

#include "mesh/Mesh.h"
#include "recipe/Words.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects
{
	struct SlotCoverage
	{
		std::uint32_t      slot = 0;
		std::string        name;
		std::size_t        triangles = 0;
		[[nodiscard]] bool operator==(const SlotCoverage&) const = default;
	};

	struct BoneCoverage
	{
		std::string        name;
		float              coverage = 0.0f;
		[[nodiscard]] bool operator==(const BoneCoverage&) const = default;
	};

	struct MeshFacts
	{
		std::vector<SlotCoverage> slots;
		std::vector<BoneCoverage> bones;
		[[nodiscard]] bool        operator==(const MeshFacts&) const = default;
	};

	[[nodiscard]] std::vector<SlotCoverage> SlotsOf(const MeshData& a_mesh);
	[[nodiscard]] std::vector<BoneCoverage> BonesOf(const MeshData& a_mesh);
	[[nodiscard]] MeshFacts                 FactsOf(const MeshData& a_mesh);
}
