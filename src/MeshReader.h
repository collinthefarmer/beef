#pragma once

#include "Analysis.h"
#include "Mesh.h"
#include "PCH.h"
#include "Studio.h"
#include "RuntimeTextures.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace BetterEnchantmentEffects
{
	[[nodiscard]] std::expected<std::shared_ptr<const MeshData>, std::string> ReadMesh(RE::BSGeometry* a_geometry);

	struct MeshIdentity
	{
		const void*              skinPartition = nullptr;
		std::vector<const void*> buffers;
		std::uint32_t            vertices = 0;

		[[nodiscard]] bool operator==(const MeshIdentity&) const = default;
	};
	[[nodiscard]] std::optional<MeshIdentity> IdentityOf(RE::BSGeometry* a_geometry);

	struct GpuComparison
	{
		std::size_t differing = 0;
		std::size_t total = 0;
	};
	[[nodiscard]] std::optional<GpuComparison> CompareWithGpu(RE::BSGeometry* a_geometry);

	[[nodiscard]] std::optional<Vec3> NodeBindPosition(RE::BSGeometry* a_geometry, RE::NiAVObject* a_root, std::string_view a_node);

	[[nodiscard]] Vec3 ToRootSpace(RE::NiAVObject* a_root, const Vec3& a_world);

	struct MeshEntry
	{
		RE::NiPointer<RE::BSGeometry>                                        geometry;
		MeshIdentity                                                         identity;
		std::shared_ptr<const MeshData>                                      mesh;
		std::string                                                          problem;
		Studio::MeshFacts                                                    facts;
		MeshAnalysis                                                         analysis;
		std::unordered_map<std::string, std::shared_ptr<TextureLab::RenderTarget>> bakes;
		std::uint32_t                                                        lastUsedMS = 0;
	};

	class MeshCache
	{
	public:
		[[nodiscard]] std::expected<std::shared_ptr<MeshEntry>, std::string> Get(RE::BSGeometry* a_geometry, std::uint32_t a_nowMS, bool a_verbose);
		[[nodiscard]] std::shared_ptr<const MeshEntry> Cached(RE::BSGeometry* a_geometry) const noexcept;
		void Sweep(std::uint32_t a_nowMS, std::uint32_t a_maxAgeMS, std::span<RE::BSGeometry* const> a_keep, bool a_verbose);
		void Clear() noexcept;

	private:
		std::unordered_map<RE::BSGeometry*, std::shared_ptr<MeshEntry>> entries_;
	};
}
