#include "MeshReader.h"

#include "RuntimeTextures.h"

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

		// The bytes of one renderer buffer set: the CPU copy when the engine
		// kept it, else a GPU read back.
		struct Buffers
		{
			std::vector<std::uint8_t>  vertices;
			std::vector<std::uint16_t> indices;
			bool                       fromGpu = false;
		};

		std::expected<Buffers, std::string> ReadBuffers(const RE::BSGraphics::TriShape* a_shape, std::uint32_t a_vertexCount, std::uint32_t a_indexCount, std::uint32_t a_stride)
		{
			if (!a_shape) {
				return std::unexpected("no renderer buffers");
			}
			if (a_vertexCount == 0 || a_indexCount == 0 || a_stride == 0) {
				return std::unexpected("empty buffers");
			}
			const std::size_t vertexBytes = static_cast<std::size_t>(a_vertexCount) * a_stride;
			const std::size_t indexBytes = static_cast<std::size_t>(a_indexCount) * sizeof(std::uint16_t);
			Buffers           out;
			if (a_shape->rawVertexData && a_shape->rawIndexData) {
				out.vertices.assign(a_shape->rawVertexData, a_shape->rawVertexData + vertexBytes);
				out.indices.assign(a_shape->rawIndexData, a_shape->rawIndexData + a_indexCount);
				return out;
			}
			auto* lab = TextureLab::GetSingleton();
			out.vertices = lab->ReadBuffer(reinterpret_cast<REX::W32::ID3D11Buffer*>(a_shape->vertexBuffer), static_cast<std::uint32_t>(vertexBytes));
			const auto indexRaw = lab->ReadBuffer(reinterpret_cast<REX::W32::ID3D11Buffer*>(a_shape->indexBuffer), static_cast<std::uint32_t>(indexBytes));
			if (out.vertices.size() != vertexBytes || indexRaw.size() != indexBytes) {
				return std::unexpected("the GPU buffers could not be read back");
			}
			out.indices.resize(a_indexCount);
			std::memcpy(out.indices.data(), indexRaw.data(), indexBytes);
			out.fromGpu = true;
			return out;
		}

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
			for (std::size_t i = 0; i + 2 < a_buffers.indices.size(); i += 3) {
				partition.triangles.push_back({ a_buffers.indices[i], a_buffers.indices[i + 1], a_buffers.indices[i + 2] });
			}
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
	}

	std::expected<std::shared_ptr<const MeshData>, std::string> ReadMesh(RE::BSGeometry* a_geometry)
	{
		if (!a_geometry) {
			return std::unexpected("no geometry");
		}
		auto* triShape = a_geometry->AsTriShape();
		if (!triShape) {
			return std::unexpected("not a tri shape");
		}
		auto mesh = std::make_shared<MeshData>();
		const auto& bound = a_geometry->GetModelData().modelBound;
		mesh->center = Vec3{ bound.center.x, bound.center.y, bound.center.z };
		mesh->radius = bound.radius;

		const auto& rt = a_geometry->GetGeometryRuntimeData();
		const auto* skin = rt.skinInstance.get();
		const auto* skinPartition = skin ? skin->skinPartition.get() : nullptr;
		bool        anyGpu = false;
		if (skinPartition && skinPartition->numPartitions > 0 && skinPartition->partitions.data()) {
			// Skinned: one buffer set per partition, bones through the partition's palette.
			const auto* dismember = netimmerse_cast<const RE::BSDismemberSkinInstance*>(skin);
			const auto* slots = dismember ? dismember->GetRuntimeData().partitions : nullptr;
			const auto  slotCount = dismember ? dismember->GetRuntimeData().numPartitions : 0;
			for (std::uint32_t p = 0; p < skinPartition->numPartitions; ++p) {
				const auto& source = skinPartition->partitions[p];
				const auto  layout = LayoutOf(source.buffData ? source.buffData->vertexDesc : source.vertexDesc);
				auto        buffers = ReadBuffers(source.buffData, source.vertices, static_cast<std::uint32_t>(source.triangles) * 3, layout.stride);
				if (!buffers) {
					return std::unexpected(std::format("partition {}: {}", p, buffers.error()));
				}
				auto partition = DecodePartition(*buffers, layout, source.vertices);
				if (!partition) {
					return std::unexpected(std::format("partition {}: {}", p, partition.error()));
				}
				anyGpu = anyGpu || buffers->fromGpu;
				if (slots && static_cast<std::int32_t>(p) < slotCount) {
					partition->slot = slots[p].slot;
				}
				if (source.bones) {
					for (std::uint16_t b = 0; b < source.numBones; ++b) {
						partition->boneNames.push_back(BoneName(*skin, source.bones[b]));
					}
				}
				mesh->partitions.push_back(std::move(*partition));
			}
		} else {
			const auto& shapeData = triShape->GetTrishapeRuntimeData();
			const auto  layout = LayoutOf(rt.vertexDesc);
			auto        buffers = ReadBuffers(rt.rendererData, shapeData.vertexCount, static_cast<std::uint32_t>(shapeData.triangleCount) * 3, layout.stride);
			if (!buffers) {
				return std::unexpected(buffers.error());
			}
			auto partition = DecodePartition(*buffers, layout, shapeData.vertexCount);
			if (!partition) {
				return std::unexpected(partition.error());
			}
			anyGpu = buffers->fromGpu;
			mesh->partitions.push_back(std::move(*partition));
		}
		if (mesh->partitions.empty()) {
			return std::unexpected("no partitions");
		}
		mesh->origin = anyGpu ? "gpu readback" : "cpu copy";
		return mesh;
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
}

