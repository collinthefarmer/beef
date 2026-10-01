// GPL-3.0-only with the additional permission in COPYING.md.
#include "mesh/Mesh.h"
#include "test_support.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Near;

namespace {
void PutFloat(std::vector<std::uint8_t> &a_bytes, std::size_t a_at,
              float a_value) {
  std::memcpy(a_bytes.data() + a_at, &a_value, sizeof(float));
}

void PutU16(std::vector<std::uint8_t> &a_bytes, std::size_t a_at,
            std::uint16_t a_value) {
  std::memcpy(a_bytes.data() + a_at, &a_value, sizeof(std::uint16_t));
}

VertexLayout Layout32() {
  VertexLayout layout;
  layout.stride = 32;
  layout.position = 0;
  layout.uv = 12;
  layout.normal = 16;
  layout.skinning = 20;
  return layout;
}

std::vector<std::uint8_t> VertexBuffer(const std::vector<Vec3> &a_positions) {
  std::vector<std::uint8_t> bytes(a_positions.size() * 32, 0);
  for (std::size_t i = 0; i < a_positions.size(); ++i) {
    const std::size_t base = i * 32;
    PutFloat(bytes, base + 0, a_positions[i].x);
    PutFloat(bytes, base + 4, a_positions[i].y);
    PutFloat(bytes, base + 8, a_positions[i].z);
    PutU16(bytes, base + 12, 0x3C00);
    PutU16(bytes, base + 14, 0x0000);
    bytes[base + 16] = 128;
    bytes[base + 17] = 128;
    bytes[base + 18] = 255;
    PutU16(bytes, base + 20, 0x3C00);
    bytes[base + 28] = 0;
    bytes[base + 29] = 1;
    bytes[base + 30] = 2;
    bytes[base + 31] = 3;
  }
  return bytes;
}

std::vector<std::uint8_t>
IndexBuffer(const std::vector<std::uint16_t> &a_indices) {
  std::vector<std::uint8_t> bytes(a_indices.size() * 2, 0);
  for (std::size_t i = 0; i < a_indices.size(); ++i) {
    PutU16(bytes, i * 2, a_indices[i]);
  }
  return bytes;
}

MeshData TriangleMesh() {
  MeshPartition partition;
  partition.vertices = {
      MeshVertex{Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f},
                 Vec2{0.0f, 0.0f}},
      MeshVertex{Vec3{128.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f},
                 Vec2{1.0f, 0.0f}},
      MeshVertex{Vec3{-128.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f},
                 Vec2{0.0f, 1.0f}},
  };
  partition.triangles = {{0, 1, 2}};
  MeshData mesh;
  mesh.partitions.push_back(partition);
  return mesh;
}
}

void LayoutHashCoversWhatBakesRead() {
  MeshData mesh;
  mesh.partitions.push_back(MeshPartition{});
  mesh.partitions.front().slot = 32;
  mesh.partitions.front().boneNames = {"NPC Spine2 [Spn2]"};
  mesh.radius = 10.0f;
  const std::uint64_t base = HashLayout(mesh, kHashBasis);
  Check(HashLayout(mesh, kHashBasis) == base, "the layout hash is stable");
  MeshData slot = mesh;
  slot.partitions.front().slot = 33;
  Check(HashLayout(slot, kHashBasis) != base, "the partition slot is hashed");
  MeshData bones = mesh;
  bones.partitions.front().boneNames = {"NPC Spine1 [Spn1]"};
  Check(HashLayout(bones, kHashBasis) != base, "the bone names are hashed");
  MeshData split = mesh;
  split.partitions.front().boneNames = {"NPC Spine2", " [Spn2]"};
  Check(HashLayout(split, kHashBasis) != base,
        "bone name boundaries are hashed");
  MeshData bound = mesh;
  bound.radius = 11.0f;
  Check(HashLayout(bound, kHashBasis) != base, "the model bound is hashed");
}

int main() {
  LayoutHashCoversWhatBakesRead();
  Check(Near(HalfToFloat(0x0000), 0.0f), "half 0x0000 is zero");
  Check(Near(HalfToFloat(0x3C00), 1.0f), "half 0x3C00 is one");
  Check(Near(HalfToFloat(0x4000), 2.0f), "half 0x4000 is two");
  Check(Near(HalfToFloat(0xBC00), -1.0f), "half 0xBC00 is minus one");
  Check(Near(HalfToFloat(0xC000), -2.0f), "half 0xC000 is minus two");

  Check(HashBytes(std::span<const std::uint8_t>{}) == kHashBasis,
        "hashing no bytes leaves the basis");
  const std::uint8_t a = 0x61;
  Check(HashBytes(std::span<const std::uint8_t>{&a, 1}) ==
            0xaf63dc4c8601ec8cull,
        "FNV-1a of 'a' matches the published vector");
  const std::uint8_t ab[2] = {0x61, 0x62};
  const std::uint64_t chained =
      HashBytes(std::span<const std::uint8_t>{ab + 1, 1},
                HashBytes(std::span<const std::uint8_t>{ab, 1}));
  Check(HashBytes(std::span<const std::uint8_t>{ab, 2}) == chained,
        "seeding one hash with another equals hashing the whole");

  const auto vertexBytes =
      VertexBuffer({Vec3{0.0f, 0.0f, 0.0f}, Vec3{128.0f, 0.0f, 0.0f}});
  const VertexLayout layout = Layout32();
  const auto decoded = DecodeVertex(vertexBytes, layout, 0);
  Check(decoded.has_value(), "a whole vertex decodes");
  Check(decoded && decoded->position == Vec3{0.0f, 0.0f, 0.0f},
        "the position decodes");
  Check(decoded && Near(decoded->uv.x, 1.0f) && Near(decoded->uv.y, 0.0f),
        "the uv decodes from halfs");
  Check(decoded && Near(decoded->normal.z, 1.0f),
        "the normal z decodes biased");
  Check(decoded && Near(decoded->weights[0], 1.0f),
        "the first bone weight decodes");
  Check(decoded && decoded->bones == std::array<std::uint16_t, 4>{0, 1, 2, 3},
        "the bone indices decode");
  Check(DecodeVertex(vertexBytes, layout, 1).has_value(),
        "the second vertex decodes");
  Check(!DecodeVertex(vertexBytes, layout, 2).has_value(),
        "an index past the buffer decodes to nothing");
  Check(!DecodeVertex(std::span<const std::uint8_t>{vertexBytes.data(), 31},
                      layout, 0)
             .has_value(),
        "a truncated vertex decodes to nothing");
  VertexLayout noPosition = layout;
  noPosition.position.reset();
  Check(!DecodeVertex(vertexBytes, noPosition, 0).has_value(),
        "a layout without a position decodes to nothing");
  VertexLayout noStride = layout;
  noStride.stride = 0;
  Check(!DecodeVertex(vertexBytes, noStride, 0).has_value(),
        "a zero stride decodes to nothing");

  const auto triangleIndices = IndexBuffer({0, 1, 0});
  RawPartition raw;
  raw.vertexBytes = vertexBytes;
  raw.indexBytes = triangleIndices;
  raw.layout = layout;
  raw.vertexCount = 2;
  raw.triangleCount = 1;
  raw.slot = 37;
  raw.boneNames = {"spine"};
  const auto partition = DecodePartition(raw);
  Check(partition.has_value(), "a well-formed raw partition decodes");
  Check(partition && partition->vertices.size() == 2,
        "every vertex is decoded");
  Check(partition && partition->triangles.size() == 1 &&
            partition->triangles[0] == std::array<std::uint32_t, 3>{0, 1, 0},
        "the triangle is decoded");
  Check(partition && partition->slot == 37, "the slot carries through");
  Check(partition && partition->boneNames == std::vector<std::string>{"spine"},
        "the bone names carry through");

  RawPartition empty;
  const auto emptyDecoded = DecodePartition(empty);
  Check(emptyDecoded.has_value() && emptyDecoded->vertices.empty() &&
            emptyDecoded->triangles.empty(),
        "zero counts decode to an empty partition");

  RawPartition shortVertices = raw;
  shortVertices.vertexBytes =
      std::span<const std::uint8_t>{vertexBytes.data(), 32};
  shortVertices.vertexCount = 2;
  Check(!DecodePartition(shortVertices).has_value(),
        "a vertex count past the buffer decodes to nothing");

  RawPartition shortIndices = raw;
  shortIndices.indexBytes =
      std::span<const std::uint8_t>{raw.indexBytes.data(), 2};
  shortIndices.triangleCount = 1;
  Check(!DecodePartition(shortIndices).has_value(),
        "a triangle count past the index buffer decodes to nothing");

  RawPartition strayTriangle = raw;
  const auto strayIndices = IndexBuffer({0, 1, 5});
  strayTriangle.indexBytes = strayIndices;
  strayTriangle.triangleCount = 1;
  const auto strayDecoded = DecodePartition(strayTriangle);
  Check(strayDecoded && strayDecoded->triangles.empty(),
        "a triangle naming a missing vertex is dropped");

  const std::array<std::uint32_t, 3> within[]{{0, 1, 2}, {0, 1, 3}};
  const auto kept = TrianglesWithin(within, 3);
  Check(kept.size() == 1 && kept[0] == std::array<std::uint32_t, 3>{0, 1, 2},
        "TrianglesWithin keeps only in-range triangles");

  const MeshData mesh = TriangleMesh();

  const auto positionBake = BuildBake(mesh, PositionBake{});
  Check(positionBake.vector && positionBake.problem.empty(),
        "a position bake is a vector bake");
  Check(positionBake.vertices.size() == 3 && positionBake.indices.size() == 3,
        "a position bake carries every vertex and triangle");
  Check(Near(positionBake.vertices[0].value[0], 0.5f) &&
            Near(positionBake.vertices[1].value[0], 1.0f) &&
            Near(positionBake.vertices[2].value[0], 0.0f),
        "position x maps into the shared frame");
  Check(Near(positionBake.vertices[0].u, 0.0f) &&
            Near(positionBake.vertices[1].u, 1.0f),
        "the bake carries the uv");

  const auto localBake = BuildBake(mesh, LocalPositionBake{});
  Check(localBake.vector &&
            localBake.problem == "the mesh has no bound to map positions into",
        "a local bake without a bound reports it");

  const auto partitionBake = BuildBake(mesh, PartitionBake{BipedSlot{32}});
  Check(partitionBake.problem == "no partition in biped slot 32 (body)",
        "a partition bake naming an absent slot reports it with the slot name");

  const auto componentBake = BuildBake(mesh, ComponentIdBake{});
  Check(!componentBake.vector &&
            componentBake.problem == "componentId needs the mesh analysis",
        "a componentId bake defers to the analysis");

  const auto emptyBake = BuildBake(MeshData{}, PositionBake{});
  Check(emptyBake.problem == "the mesh has no triangles to bake",
        "a bake of an empty mesh reports no triangles");

  const auto distanceBake = BuildDistanceBake(mesh, Vec3{0.0f, 0.0f, 0.0f});
  Check(!distanceBake.vector && distanceBake.vertices.size() == 3,
        "a distance bake is a scalar bake");
  Check(Near(distanceBake.vertices[0].value[0], 0.0f) &&
            Near(distanceBake.vertices[1].value[0], 0.5f),
        "distance maps against the distance frame");

  const auto uvBake = BuildBake(mesh, UvBake{});
  Check(uvBake.vector, "a uv bake is vector-valued");
  Check(Near(uvBake.vertices[0].value[0], 0.0f) &&
            Near(uvBake.vertices[1].value[0], 1.0f) &&
            Near(uvBake.vertices[1].value[2], 0.0f),
        "a uv bake carries u and v with a zero third component");

  MeshData upMesh = TriangleMesh();
  upMesh.partitions[0].vertices[1].normal = Vec3{1.0f, 0.0f, 0.0f};
  upMesh.partitions[0].vertices[2].normal = Vec3{0.0f, 0.0f, -1.0f};
  const auto normalBake = BuildBake(upMesh, NormalBake{});
  Check(normalBake.vector, "a normal bake is vector-valued");
  Check(Near(normalBake.vertices[1].value[0], 1.0f) &&
            Near(normalBake.vertices[1].value[1], 0.5f) &&
            Near(normalBake.vertices[1].value[2], 0.5f) &&
            Near(normalBake.vertices[2].value[2], 0.0f),
        "a normal bake maps each normal axis into 0..1");

  MeshData boundMesh = TriangleMesh();
  boundMesh.partitions[0].vertices[0].position = Vec3{-2.0f, 0.0f, 0.0f};
  boundMesh.partitions[0].vertices[1].position = Vec3{6.0f, 0.0f, 0.0f};
  boundMesh.partitions[0].vertices[2].position = Vec3{2.0f, 3.0f, 0.0f};
  const MeshBound bound = MeasureBound(boundMesh.partitions);
  Check(Near(bound.center.x, 2.0f) && Near(bound.center.y, 1.5f) &&
            Near(bound.radius, std::sqrt(18.25f)),
        "MeasureBound centres the box and encloses every vertex");
  Check(MeasureBound({}).radius == 0.0f, "an empty mesh measures no bound");

  MeshData weightedMesh = TriangleMesh();
  auto &weighted = weightedMesh.partitions[0];
  weighted.boneNames = {"NPC Head", "NPC Spine"};
  weighted.vertices[0].bones = {0, 1, 0, 0};
  weighted.vertices[0].weights = {0.25f, 0.5f, 0.0f, 0.0f};
  weighted.vertices[1].bones = {1, 1, 1, 1};
  weighted.vertices[1].weights = {0.4f, 0.4f, 0.4f, 0.4f};
  const auto weightBake = BuildBake(weightedMesh, BoneWeightBake{{"NPC Head"}});
  Check(Near(weightBake.vertices[0].value[0], 0.25f),
        "a boneWeight bake sums the named bones' weights");
  Check(Near(weightBake.vertices[1].value[0], 0.0f),
        "a boneWeight bake ignores other bones");
  const auto spineBake = BuildBake(weightedMesh, BoneWeightBake{{"NPC Spine"}});
  Check(Near(spineBake.vertices[0].value[0], 0.5f) &&
            Near(spineBake.vertices[1].value[0], 1.0f),
        "a boneWeight bake clamps the sum at one");

  Check(DefinitionOf(BakeKind{PositionBake{}}) == "bake position",
        "a position bake names itself");
  Check(DefinitionOf(BakeKind{BoneWeightBake{{"b", "a"}}}) ==
            "bake boneWeight [a, b]",
        "a bone-weight bake sorts its bones");
  Check(DefinitionOf(DistanceSource{"NPC Root"}) ==
            "distance from node NPC Root",
        "a distance names its node");
  Check(DefinitionOf(BakeKind{UvBake{}}) == "bake uv",
        "a uv bake names itself");

  const BakeKey key =
      KeyOf(DefinitionOf(BakeKind{PositionBake{}}), TextureSize{512});
  Check(key.definition == "bake position" && key.pixels == 512,
        "a bake key carries its definition and size as fields");
  Check(key == KeyOf("bake position", TextureSize{512}) &&
            key != KeyOf("bake position", TextureSize{256}),
        "bake keys compare by both fields");

  return test::Finish("mesh");
}
