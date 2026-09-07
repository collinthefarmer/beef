#include "MeshReader.h"

#include "RuntimeTextures.h"

#include <algorithm>
#include <cstring>
#include <format>

namespace WornEnchantmentPBR
{
	namespace
	{
		using Vertex = RE::BSGraphics::Vertex;

		VertexLayout LayoutOf(RE::BSGraphics::VertexDesc a_desc)
		{
			VertexLayout layout;
			layout.stride = a_desc.GetSize();
			if (a_desc.HasFlag(Vertex::VF_VERTEX)) {
				layout.position = a_desc.GetAttributeOffset(Vertex::VA_POSITION);
			}
			if (a_desc.HasFlag(Vertex::VF_UV)) {
				layout.uv = a_desc.GetAttributeOffset(Vertex::VA_TEXCOORD0);
			}
			if (a_desc.HasFlag(Vertex::VF_NORMAL)) {
				layout.normal = a_desc.GetAttributeOffset(Vertex::VA_NORMAL);
			}
			if (a_desc.HasFlag(Vertex::VF_SKINNED)) {
				layout.skinning = a_desc.GetAttributeOffset(Vertex::VA_SKINNING);
			}
			return layout;
		}

		// The skin partition record when the geometry is skinned into at
		// least one partition, else null.
		const RE::NiSkinPartition* SkinPartitionOf(RE::BSGeometry* a_geometry)
		{
			const auto* skin = a_geometry->GetGeometryRuntimeData().skinInstance.get();
			const auto* partition = skin ? skin->skinPartition.get() : nullptr;
			return partition && partition->numPartitions > 0 && partition->partitions.data() ? partition : nullptr;
		}

		// One renderer buffer set with its counts: one per skin partition, or
		// the shape's own when it is not skinned.
		struct BufferSet
		{
			const RE::BSGraphics::TriShape* shape = nullptr;
			std::uint32_t                   vertices = 0;
			std::uint32_t                   triangles = 0;
			VertexLayout                    layout;
		};

		std::expected<std::vector<BufferSet>, std::string> BufferSetsOf(RE::BSGeometry* a_geometry)
		{
			if (!a_geometry) {
				return std::unexpected("no geometry");
			}
			auto* triShape = a_geometry->AsTriShape();
			if (!triShape) {
				return std::unexpected("not a tri shape");
			}
			std::vector<BufferSet> sets;
			if (const auto* skinPartition = SkinPartitionOf(a_geometry)) {
				for (std::uint32_t p = 0; p < skinPartition->numPartitions; ++p) {
					const auto& source = skinPartition->partitions[p];
					sets.push_back({ source.buffData, source.vertices, source.triangles, LayoutOf(source.buffData ? source.buffData->vertexDesc : source.vertexDesc) });
				}
			} else {
				const auto& rt = a_geometry->GetGeometryRuntimeData();
				const auto& shapeData = triShape->GetTrishapeRuntimeData();
				sets.push_back({ rt.rendererData, shapeData.vertexCount, shapeData.triangleCount, LayoutOf(rt.vertexDesc) });
			}
			return sets;
		}

		// The bytes of one renderer buffer set: the CPU copy when the engine
		// kept it, else a GPU read back.
		struct Buffers
		{
			std::vector<std::uint8_t>  vertices;
			std::vector<std::uint16_t> indices;
			bool                       fromGpu = false;
		};

		std::expected<Buffers, std::string> ReadBuffers(const BufferSet& a_set, bool a_forceGpu)
		{
			const auto* shape = a_set.shape;
			if (!shape) {
				return std::unexpected("no renderer buffers");
			}
			const std::uint32_t indexCount = a_set.triangles * 3;
			if (a_set.vertices == 0 || indexCount == 0 || a_set.layout.stride == 0) {
				return std::unexpected("empty buffers");
			}
			// The counts are the engine's own uint16 fields, so a copy is at
			// most 65535 vertices and triangles; the CPU copy's length is the
			// engine's invariant and cannot be checked from here.
			const std::size_t vertexBytes = static_cast<std::size_t>(a_set.vertices) * a_set.layout.stride;
			const std::size_t indexBytes = static_cast<std::size_t>(indexCount) * sizeof(std::uint16_t);
			Buffers           out;
			if (!a_forceGpu && shape->rawVertexData && shape->rawIndexData) {
				out.vertices.assign(shape->rawVertexData, shape->rawVertexData + vertexBytes);
				out.indices.assign(shape->rawIndexData, shape->rawIndexData + indexCount);
				return out;
			}
			auto* lab = TextureLab::GetSingleton();
			out.vertices = lab->ReadBuffer(reinterpret_cast<REX::W32::ID3D11Buffer*>(shape->vertexBuffer), static_cast<std::uint32_t>(vertexBytes));
			const auto indexRaw = lab->ReadBuffer(reinterpret_cast<REX::W32::ID3D11Buffer*>(shape->indexBuffer), static_cast<std::uint32_t>(indexBytes));
			if (out.vertices.size() != vertexBytes || indexRaw.size() != indexBytes) {
				return std::unexpected("the GPU buffers could not be read back");
			}
			out.indices.resize(indexCount);
			std::memcpy(out.indices.data(), indexRaw.data(), indexBytes);
			out.fromGpu = true;
			return out;
		}

		std::span<const std::uint8_t> BytesOf(const std::vector<std::uint16_t>& a_indices) noexcept
		{
			return { reinterpret_cast<const std::uint8_t*>(a_indices.data()), a_indices.size() * sizeof(std::uint16_t) };
		}

		// Vertices decoded and every triangle that indexes past them dropped,
		// so MeshData carries the invariant the bakes rely on.
		std::expected<MeshPartition, std::string> DecodePartition(const Buffers& a_buffers, const VertexLayout& a_layout, std::uint32_t a_vertexCount)
		{
			MeshPartition partition;
			partition.vertices.reserve(a_vertexCount);
			for (std::uint32_t i = 0; i < a_vertexCount; ++i) {
				const auto vertex = DecodeVertex(a_buffers.vertices, a_layout, i);
				if (!vertex) {
					return std::unexpected(std::format("vertex {} does not fit the layout (stride {})", i, a_layout.stride));
				}
				partition.vertices.push_back(*vertex);
			}
			std::vector<std::array<std::uint32_t, 3>> triangles;
			triangles.reserve(a_buffers.indices.size() / 3);
			for (std::size_t i = 0; i + 2 < a_buffers.indices.size(); i += 3) {
				triangles.push_back({ a_buffers.indices[i], a_buffers.indices[i + 1], a_buffers.indices[i + 2] });
			}
			partition.triangles = TrianglesWithin(triangles, partition.vertices.size());
			return partition;
		}

		std::string BoneName(const RE::NiSkinInstance& a_skin, std::uint32_t a_index)
		{
			if (!a_skin.bones || !a_skin.skinData || a_index >= a_skin.skinData->bones) {
				return {};
			}
			const auto* bone = a_skin.bones[a_index];
			return bone && bone->name.c_str() ? bone->name.c_str() : "";
		}

		const char* NameOf(RE::BSGeometry* a_geometry) noexcept
		{
			return a_geometry && a_geometry->name.c_str() ? a_geometry->name.c_str() : "?";
		}
	}

	std::expected<std::shared_ptr<const MeshData>, std::string> ReadMesh(RE::BSGeometry* a_geometry)
	{
		const auto sets = BufferSetsOf(a_geometry);
		if (!sets) {
			return std::unexpected(sets.error());
		}
		auto mesh = std::make_shared<MeshData>();
		const auto& bound = a_geometry->GetModelData().modelBound;
		mesh->center = Vec3{ bound.center.x, bound.center.y, bound.center.z };
		mesh->radius = bound.radius;

		const auto* skin = a_geometry->GetGeometryRuntimeData().skinInstance.get();
		const auto* skinPartition = SkinPartitionOf(a_geometry);
		// Skinned: bones through the partition's palette, the biped slot from
		// the dismember record when the geometry has one.
		const auto* dismember = skinPartition ? netimmerse_cast<const RE::BSDismemberSkinInstance*>(skin) : nullptr;
		const auto* slots = dismember ? dismember->GetRuntimeData().partitions : nullptr;
		const auto  slotCount = dismember ? dismember->GetRuntimeData().numPartitions : 0;
		bool          anyGpu = false;
		std::uint64_t hash = kHashBasis;
		for (std::size_t p = 0; p < sets->size(); ++p) {
			const auto& set = (*sets)[p];
			const auto  buffers = ReadBuffers(set, false);
			if (!buffers) {
				return std::unexpected(skinPartition ? std::format("partition {}: {}", p, buffers.error()) : buffers.error());
			}
			auto partition = DecodePartition(*buffers, set.layout, set.vertices);
			if (!partition) {
				return std::unexpected(skinPartition ? std::format("partition {}: {}", p, partition.error()) : partition.error());
			}
			anyGpu = anyGpu || buffers->fromGpu;
			hash = HashBytes(buffers->vertices, hash);
			hash = HashBytes(BytesOf(buffers->indices), hash);
			if (skinPartition && p < skinPartition->numPartitions) {
				const auto& source = skinPartition->partitions[p];
				if (slots && static_cast<std::int32_t>(p) < slotCount) {
					partition->slot = slots[p].slot;
				}
				if (source.bones && skin) {
					for (std::uint16_t b = 0; b < source.numBones; ++b) {
						partition->boneNames.push_back(BoneName(*skin, source.bones[b]));
					}
				}
			}
			mesh->partitions.push_back(std::move(*partition));
		}
		if (mesh->partitions.empty()) {
			return std::unexpected("no partitions");
		}
		mesh->origin = anyGpu ? "gpu readback" : "cpu copy";
		mesh->hash = hash;
		return mesh;
	}

	std::optional<MeshIdentity> IdentityOf(RE::BSGeometry* a_geometry)
	{
		MeshIdentity identity;
		const auto   sets = BufferSetsOf(a_geometry);
		if (!sets) {
			return std::nullopt;
		}
		identity.skinPartition = SkinPartitionOf(a_geometry);
		for (const auto& set : *sets) {
			identity.buffers.push_back(set.shape);
			identity.buffers.push_back(set.shape ? set.shape->vertexBuffer : nullptr);
			identity.vertices += set.vertices;
		}
		return identity;
	}

	std::optional<GpuComparison> CompareWithGpu(RE::BSGeometry* a_geometry)
	{
		const auto sets = BufferSetsOf(a_geometry);
		if (!sets) {
			return std::nullopt;
		}
		GpuComparison comparison;
		for (const auto& set : *sets) {
			if (!set.shape || !set.shape->rawVertexData || !set.shape->rawIndexData) {
				return std::nullopt;
			}
			const auto cpu = ReadBuffers(set, false);
			const auto gpu = ReadBuffers(set, true);
			if (!cpu || !gpu) {
				return std::nullopt;
			}
			const auto count = [&](std::span<const std::uint8_t> a, std::span<const std::uint8_t> b) {
				const auto shared = std::min(a.size(), b.size());
				for (std::size_t i = 0; i < shared; ++i) {
					comparison.differing += a[i] != b[i] ? 1 : 0;
				}
				comparison.differing += std::max(a.size(), b.size()) - shared;
				comparison.total += std::max(a.size(), b.size());
			};
			count(cpu->vertices, gpu->vertices);
			count(BytesOf(cpu->indices), BytesOf(gpu->indices));
		}
		return comparison;
	}

	Vec3 ToRootSpace(RE::NiAVObject* a_root, const Vec3& a_world)
	{
		if (!a_root) {
			return a_world;
		}
		const auto local = a_root->world.Invert() * RE::NiPoint3{ a_world.x, a_world.y, a_world.z };
		return Vec3{ local.x, local.y, local.z };
	}

	std::optional<Vec3> NodeBindPosition(RE::BSGeometry* a_geometry, RE::NiAVObject* a_root, std::string_view a_node)
	{
		if (a_geometry) {
			const auto* skin = a_geometry->GetGeometryRuntimeData().skinInstance.get();
			if (skin && skin->bones && skin->skinData && skin->skinData->boneData) {
				for (std::uint32_t i = 0; i < skin->skinData->bones; ++i) {
					const auto* bone = skin->bones[i];
					if (bone && bone->name.c_str() && a_node == bone->name.c_str()) {
						// skinToBone maps skin space into the bone; its inverse puts the bone's origin in skin space.
						const auto origin = skin->skinData->boneData[i].skinToBone.Invert() * RE::NiPoint3{ 0.0f, 0.0f, 0.0f };
						return Vec3{ origin.x, origin.y, origin.z };
					}
				}
			}
		}
		if (a_root) {
			if (auto* node = a_root->GetObjectByName(RE::BSFixedString{ std::string{ a_node } })) {
				return ToRootSpace(a_root, Vec3{ node->world.translate.x, node->world.translate.y, node->world.translate.z });
			}
		}
		return std::nullopt;
	}

	// ------------------------------------------------------------- the cache

	namespace
	{
		// The read's log line with its hash, and under verbose logging (a
		// geometry given) the CPU-copy-versus-GPU comparison.
		void LogRead(const char* a_name, const MeshData& a_mesh, RE::BSGeometry* a_compare)
		{
			std::size_t vertices = 0, triangles = 0;
			for (const auto& p : a_mesh.partitions) {
				vertices += p.vertices.size();
				triangles += p.triangles.size();
			}
			logger::info("mesh '{}': {}, {} partitions, {} vertices, {} triangles, hash {:016x}", a_name, a_mesh.origin, a_mesh.partitions.size(), vertices, triangles, a_mesh.hash);
			if (a_compare) {
				if (const auto compared = CompareWithGpu(a_compare)) {
					logger::info("mesh '{}': cpu copy vs gpu readback: {} of {} bytes differ", a_name, compared->differing, compared->total);
				}
			}
		}
	}

	std::expected<std::shared_ptr<MeshEntry>, std::string> MeshCache::Get(RE::BSGeometry* a_geometry, std::uint32_t a_nowMS, bool a_verbose)
	{
		if (!a_geometry) {
			return std::unexpected("no geometry to read");
		}
		const auto* name = NameOf(a_geometry);
		auto        identity = IdentityOf(a_geometry);
		if (!identity) {
			return std::unexpected("no renderer buffers to read");
		}
		if (const auto it = entries_.find(a_geometry); it != entries_.end() && it->second) {
			auto& entry = *it->second;
			if (entry.identity == *identity) {
				if (a_verbose && entry.lastUsedMS != a_nowMS) {
					logger::info("mesh '{}': cached", name);
				}
				entry.lastUsedMS = a_nowMS;
				if (!entry.mesh) {
					return std::unexpected(std::format("the mesh could not be read: {}", entry.problem));
				}
				return it->second;
			}
			logger::info("mesh '{}': buffers changed, reading again", name);
		}
		auto entry = std::make_shared<MeshEntry>();
		entry->geometry = RE::NiPointer{ a_geometry };
		entry->identity = std::move(*identity);
		entry->lastUsedMS = a_nowMS;
		auto read = ReadMesh(a_geometry);
		if (read) {
			entry->mesh = *read;
			entry->facts = Studio::FactsOf(*entry->mesh);
			LogRead(name, *entry->mesh, a_verbose ? a_geometry : nullptr);
		} else {
			entry->problem = read.error();
		}
		entries_[a_geometry] = entry;
		if (!entry->mesh) {
			return std::unexpected(std::format("the mesh could not be read: {}", entry->problem));
		}
		return entry;
	}

	std::shared_ptr<const MeshEntry> MeshCache::Cached(RE::BSGeometry* a_geometry) const noexcept
	{
		const auto it = entries_.find(a_geometry);
		return it != entries_.end() ? it->second : nullptr;
	}

	void MeshCache::Sweep(std::uint32_t a_nowMS, std::uint32_t a_maxAgeMS, std::span<RE::BSGeometry* const> a_keep, bool a_verbose)
	{
		const auto dropped = std::erase_if(entries_, [&](const auto& a_pair) {
			const auto& [geometry, entry] = a_pair;
			if (!entry) {
				return true;
			}
			if (std::ranges::find(a_keep, geometry) != a_keep.end()) {
				entry->lastUsedMS = a_nowMS;
				return false;
			}
			return a_nowMS - entry->lastUsedMS > a_maxAgeMS;
		});
		if (a_verbose && dropped > 0) {
			logger::info("mesh cache: dropped {} unused entries, {} kept", dropped, entries_.size());
		}
	}

	void MeshCache::Clear() noexcept
	{
		entries_.clear();
	}
}
