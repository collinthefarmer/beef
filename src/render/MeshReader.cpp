#include "render/MeshReader.h"

#include "render/TextureLab.h"

#include <algorithm>
#include <format>
#include <limits>
#include <span>
#include <vector>

namespace BetterEnchantmentEffects {
namespace {
using Vertex = RE::BSGraphics::Vertex;

VertexLayout LayoutOf(RE::BSGraphics::VertexDesc a_desc) {
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

const RE::NiSkinPartition *SkinPartitionOf(RE::BSGeometry *a_geometry) {
  const RE::NiSkinInstance *skin =
      a_geometry->GetGeometryRuntimeData().skinInstance.get();
  const RE::NiSkinPartition *partition =
      skin ? skin->skinPartition.get() : nullptr;
  return partition && partition->numPartitions > 0 &&
                 partition->partitions.data()
             ? partition
             : nullptr;
}

struct BufferSet {
  const RE::BSGraphics::TriShape *shape = nullptr;
  std::uint32_t vertices = 0;
  std::uint32_t triangles = 0;
  VertexLayout layout;
};

std::expected<std::vector<BufferSet>, std::string>
BufferSetsOf(RE::BSGeometry *a_geometry) {
  if (!a_geometry) {
    return std::unexpected("no geometry");
  }
  RE::BSTriShape *triShape = a_geometry->AsTriShape();
  if (!triShape) {
    return std::unexpected("not a tri shape");
  }
  std::vector<BufferSet> sets;
  if (const RE::NiSkinPartition *skinPartition = SkinPartitionOf(a_geometry)) {
    for (std::uint32_t p = 0; p < skinPartition->numPartitions; ++p) {
      const RE::NiSkinPartition::Partition &source =
          skinPartition->partitions[p];
      sets.push_back({source.buffData, source.vertices, source.triangles,
                      LayoutOf(source.buffData ? source.buffData->vertexDesc
                                               : source.vertexDesc)});
    }
  } else {
    const auto &rt = a_geometry->GetGeometryRuntimeData();
    const auto &shapeData = triShape->GetTrishapeRuntimeData();
    sets.push_back({rt.rendererData, shapeData.vertexCount,
                    shapeData.triangleCount, LayoutOf(rt.vertexDesc)});
  }
  return sets;
}

struct RawBytes {
  std::vector<std::uint8_t> vertices;
  std::vector<std::uint8_t> indices;
  bool fromGpu = false;
};

std::expected<RawBytes, std::string> ReadBuffers(const BufferSet &a_set,
                                                 bool a_forceGpu) {
  const RE::BSGraphics::TriShape *shape = a_set.shape;
  if (!shape) {
    return std::unexpected("no renderer buffers");
  }
  const std::size_t indexCount = static_cast<std::size_t>(a_set.triangles) * 3;
  if (a_set.vertices == 0 || indexCount == 0 || a_set.layout.stride == 0) {
    return std::unexpected("empty buffers");
  }
  const std::size_t vertexBytes =
      static_cast<std::size_t>(a_set.vertices) * a_set.layout.stride;
  const std::size_t indexBytes =
      static_cast<std::size_t>(indexCount) * sizeof(std::uint16_t);
  if (vertexBytes > std::numeric_limits<std::uint32_t>::max() ||
      indexBytes > std::numeric_limits<std::uint32_t>::max()) {
    return std::unexpected("renderer buffer size exceeds the D3D11 byte range");
  }
  RawBytes out;
  if (!a_forceGpu && shape->rawVertexData && shape->rawIndexData) {
    out.vertices.assign(shape->rawVertexData,
                        shape->rawVertexData + vertexBytes);
    const std::uint8_t *indexStart =
        reinterpret_cast<const std::uint8_t *>(shape->rawIndexData);
    out.indices.assign(indexStart, indexStart + indexBytes);
    return out;
  }
  TextureLab *lab = TextureLab::GetSingleton();
  out.vertices = lab->ReadBuffer(
      reinterpret_cast<REX::W32::ID3D11Buffer *>(shape->vertexBuffer),
      static_cast<std::uint32_t>(vertexBytes));
  out.indices = lab->ReadBuffer(
      reinterpret_cast<REX::W32::ID3D11Buffer *>(shape->indexBuffer),
      static_cast<std::uint32_t>(indexBytes));
  if (out.vertices.size() != vertexBytes || out.indices.size() != indexBytes) {
    return std::unexpected("the GPU buffers could not be read back");
  }
  out.fromGpu = true;
  return out;
}

std::string BoneName(const RE::NiSkinInstance &a_skin, std::uint32_t a_index) {
  if (!a_skin.bones || !a_skin.skinData || a_index >= a_skin.skinData->bones) {
    return {};
  }
  const RE::NiAVObject *bone = a_skin.bones[a_index];
  return bone && bone->name.c_str() ? bone->name.c_str() : "";
}
}

std::expected<std::shared_ptr<const MeshData>, std::string>
ReadMesh(RE::BSGeometry *a_geometry) {
  const std::expected<std::vector<BufferSet>, std::string> sets =
      BufferSetsOf(a_geometry);
  if (!sets) {
    return std::unexpected(sets.error());
  }
  auto mesh = std::make_shared<MeshData>();
  const auto &bound = a_geometry->GetModelData().modelBound;
  mesh->center = Vec3{bound.center.x, bound.center.y, bound.center.z};
  mesh->radius = bound.radius;

  const RE::NiSkinInstance *skin =
      a_geometry->GetGeometryRuntimeData().skinInstance.get();
  const RE::NiSkinPartition *skinPartition = SkinPartitionOf(a_geometry);
  const RE::BSDismemberSkinInstance *dismember =
      skinPartition ? netimmerse_cast<const RE::BSDismemberSkinInstance *>(skin)
                    : nullptr;
  const RE::BSDismemberSkinInstance::Data *slots =
      dismember ? dismember->GetRuntimeData().partitions : nullptr;
  const std::int32_t slotCount =
      dismember ? dismember->GetRuntimeData().numPartitions : 0;

  bool anyGpu = false;
  std::uint64_t hash = kHashBasis;
  for (std::size_t p = 0; p < sets->size(); ++p) {
    const BufferSet &set = (*sets)[p];
    const std::expected<RawBytes, std::string> bytes = ReadBuffers(set, false);
    if (!bytes) {
      return std::unexpected(
          skinPartition ? std::format("partition {}: {}", p, bytes.error())
                        : bytes.error());
    }
    RawPartition raw;
    raw.vertexBytes = bytes->vertices;
    raw.indexBytes = bytes->indices;
    raw.layout = set.layout;
    raw.vertexCount = set.vertices;
    raw.triangleCount = set.triangles;
    if (skinPartition && p < skinPartition->numPartitions) {
      const RE::NiSkinPartition::Partition &source =
          skinPartition->partitions[p];
      if (slots && static_cast<std::int32_t>(p) < slotCount) {
        raw.slot = slots[p].slot;
      }
      if (source.bones && skin) {
        for (std::uint16_t b = 0; b < source.numBones; ++b) {
          raw.boneNames.push_back(BoneName(*skin, source.bones[b]));
        }
      }
    }
    const std::optional<MeshPartition> partition = DecodePartition(raw);
    if (!partition) {
      return std::unexpected(
          skinPartition
              ? std::format(
                    "partition {}: the buffers do not fit the vertex layout "
                    "(stride {})",
                    p, set.layout.stride)
              : std::format(
                    "the buffers do not fit the vertex layout (stride {})",
                    set.layout.stride));
    }
    anyGpu = anyGpu || bytes->fromGpu;
    hash = HashBytes(bytes->vertices, hash);
    hash = HashBytes(bytes->indices, hash);
    mesh->partitions.push_back(*partition);
  }
  if (mesh->partitions.empty()) {
    return std::unexpected("no partitions");
  }
  if (mesh->radius <= 0.0f) {
    const MeshBound measured = MeasureBound(mesh->partitions);
    mesh->center = measured.center;
    mesh->radius = measured.radius;
  }
  mesh->origin = anyGpu ? "gpu readback" : "cpu copy";
  mesh->hash = hash;
  return mesh;
}

std::optional<MeshIdentity> IdentityOf(RE::BSGeometry *a_geometry) {
  MeshIdentity identity;
  const std::expected<std::vector<BufferSet>, std::string> sets =
      BufferSetsOf(a_geometry);
  if (!sets) {
    return std::nullopt;
  }
  identity.skinPartition = SkinPartitionOf(a_geometry);
  for (const BufferSet &set : *sets) {
    identity.buffers.push_back(set.shape);
    identity.buffers.push_back(set.shape ? set.shape->vertexBuffer : nullptr);
    identity.vertices += set.vertices;
  }
  return identity;
}

std::optional<GpuComparison> CompareWithGpu(RE::BSGeometry *a_geometry) {
  const std::expected<std::vector<BufferSet>, std::string> sets =
      BufferSetsOf(a_geometry);
  if (!sets) {
    return std::nullopt;
  }
  GpuComparison comparison;
  for (const BufferSet &set : *sets) {
    if (!set.shape || !set.shape->rawVertexData || !set.shape->rawIndexData) {
      return std::nullopt;
    }
    const std::expected<RawBytes, std::string> cpu = ReadBuffers(set, false);
    const std::expected<RawBytes, std::string> gpu = ReadBuffers(set, true);
    if (!cpu || !gpu) {
      return std::nullopt;
    }
    const auto count = [&](std::span<const std::uint8_t> a,
                           std::span<const std::uint8_t> b) {
      const std::size_t shared = std::min(a.size(), b.size());
      for (std::size_t i = 0; i < shared; ++i) {
        comparison.differing += a[i] != b[i] ? 1 : 0;
      }
      comparison.differing += std::max(a.size(), b.size()) - shared;
      comparison.total += std::max(a.size(), b.size());
    };
    count(cpu->vertices, gpu->vertices);
    count(cpu->indices, gpu->indices);
  }
  return comparison;
}

Vec3 ToRootSpace(RE::NiAVObject *a_root, const Vec3 &a_world) {
  if (!a_root) {
    return a_world;
  }
  const RE::NiPoint3 local =
      a_root->world.Invert() * RE::NiPoint3{a_world.x, a_world.y, a_world.z};
  return Vec3{local.x, local.y, local.z};
}

std::optional<Vec3> NodeBindPosition(RE::BSGeometry *a_geometry,
                                     RE::NiAVObject *a_root,
                                     std::string_view a_node) {
  if (a_geometry) {
    const RE::NiSkinInstance *skin =
        a_geometry->GetGeometryRuntimeData().skinInstance.get();
    if (skin && skin->bones && skin->skinData && skin->skinData->boneData) {
      for (std::uint32_t i = 0; i < skin->skinData->bones; ++i) {
        const RE::NiAVObject *bone = skin->bones[i];
        if (bone && bone->name.c_str() && a_node == bone->name.c_str()) {
          const RE::NiPoint3 origin =
              skin->skinData->boneData[i].skinToBone.Invert() *
              RE::NiPoint3{0.0f, 0.0f, 0.0f};
          return Vec3{origin.x, origin.y, origin.z};
        }
      }
    }
  }
  if (a_root) {
    if (RE::NiAVObject *node =
            a_root->GetObjectByName(RE::BSFixedString{std::string{a_node}})) {
      return ToRootSpace(a_root,
                         Vec3{node->world.translate.x, node->world.translate.y,
                              node->world.translate.z});
    }
  }
  return std::nullopt;
}
}
