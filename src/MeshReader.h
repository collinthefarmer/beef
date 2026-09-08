#pragma once

// Reads a geometry's mesh out of the engine into MeshData: the skin
// partitions' vertex and index buffers (or the geometry's own when it is not
// skinned), from the CPU copy the engine keeps when it has one, else read
// back from the GPU through the lab. Every pointer and count is checked;
// a mesh that cannot be read is a problem string, never a fault. The cache
// below keeps one read per geometry, with the facts the snapshot shows and
// the bakes rasterised from it, for as long as the geometry is bound.

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

namespace WornEnchantmentPBR
{
	// The geometry's buffers decoded into a mesh, hashed. MeshCache::Get is
	// the caller and logs each read with the hash; the same hash across
	// reads means the engine's copy did not move.
	[[nodiscard]] std::expected<std::shared_ptr<const MeshData>, std::string> ReadMesh(RE::BSGeometry* a_geometry);

	// Where a read came from: the skin partition record, the renderer buffer
	// set of every partition, and the vertex count. A cached mesh whose
	// identity no longer matches the geometry's is read again.
	struct MeshIdentity
	{
		const void*              skinPartition = nullptr;
		std::vector<const void*> buffers;
		std::uint32_t            vertices = 0;

		[[nodiscard]] bool operator==(const MeshIdentity&) const = default;
	};
	// Nothing when the geometry has no renderer buffers to identify.
	[[nodiscard]] std::optional<MeshIdentity> IdentityOf(RE::BSGeometry* a_geometry);

	// The CPU copy against a GPU readback of the same buffers, for the
	// stale-copy diagnostic; empty when the geometry has no CPU copy or the
	// readback failed.
	struct GpuComparison
	{
		std::size_t differing = 0;
		std::size_t total = 0;
	};
	[[nodiscard]] std::optional<GpuComparison> CompareWithGpu(RE::BSGeometry* a_geometry);

	// A node's position in the geometry's bind-pose (skin) space: from the
	// skin data when the node is one of the geometry's bones, else the
	// node's current position relative to the actor's root. Empty when the
	// node is not found.
	[[nodiscard]] std::optional<Vec3> NodeBindPosition(RE::BSGeometry* a_geometry, RE::NiAVObject* a_root, std::string_view a_node);

	// A world position in the actor root's frame, which is bind-pose skin
	// space for a standing actor.
	[[nodiscard]] Vec3 ToRootSpace(RE::NiAVObject* a_root, const Vec3& a_world);

	// ------------------------------------------------------------- the cache

	// One geometry's read: the mesh (null with the problem when it could not
	// be read), its facts and analysis (components and charts, computed with
	// the read), and the bakes rasterised from it keyed by definition and
	// size (BakeKeyOf and friends). The entry holds the geometry, so the
	// pointer it is keyed by cannot be reused while it lives.
	struct MeshEntry
	{
		RE::NiPointer<RE::BSGeometry>                                        geometry;
		MeshIdentity                                                         identity;
		std::shared_ptr<const MeshData>                                      mesh;
		std::string                                                          problem;  // why mesh is null
		Studio::MeshFacts                                                    facts;
		MeshAnalysis                                                         analysis;
		std::unordered_map<std::string, std::shared_ptr<TextureLab::RenderTarget>> bakes;
		std::uint32_t                                                        lastUsedMS = 0;
	};

	// Game thread only; the snapshot that reads Cached is built on the tick
	// (Threads in ARCHITECTURE.md). Get reads a geometry once and returns the entry from
	// then on; a changed identity reads again and drops the entry's bakes.
	class MeshCache
	{
	public:
		[[nodiscard]] std::expected<std::shared_ptr<MeshEntry>, std::string> Get(RE::BSGeometry* a_geometry, std::uint32_t a_nowMS, bool a_verbose);
		// The entry as it stands, or null; never reads.
		[[nodiscard]] std::shared_ptr<const MeshEntry> Cached(RE::BSGeometry* a_geometry) const noexcept;
		// Drops entries unused for a_maxAgeMS whose geometry is not in a_keep.
		void Sweep(std::uint32_t a_nowMS, std::uint32_t a_maxAgeMS, std::span<RE::BSGeometry* const> a_keep, bool a_verbose);
		void Clear() noexcept;

	private:
		std::unordered_map<RE::BSGeometry*, std::shared_ptr<MeshEntry>> entries_;
	};
}
