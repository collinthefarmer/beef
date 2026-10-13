// GPL-3.0-only with the additional permission in COPYING.md.
#include "mesh/MaterialDump.h"

#include "Core.h"

#include <nlohmann/json.hpp>

#include <array>
#include <format>
#include <utility>

namespace BetterEnchantmentEffects {
namespace {
using json = nlohmann::ordered_json;

constexpr std::size_t kTgaHeaderBytes = 18;
constexpr std::uint8_t kTgaTrueColour = 2;
constexpr std::uint8_t kTgaBitsPerPixel = 32;
constexpr std::uint8_t kTgaTopLeftEightAlphaBits = 0x28;

struct FlatTriangles {
  std::vector<std::array<std::uint32_t, 3>> corners;
  std::vector<std::uint32_t> partitionOf;
};

void PutShort(std::vector<std::uint8_t> &a_bytes, std::uint32_t a_value);
[[nodiscard]] std::size_t VertexCount(const MeshData &a_mesh) noexcept;
[[nodiscard]] std::size_t TriangleCount(const MeshData &a_mesh) noexcept;
[[nodiscard]] FlatTriangles Flatten(const MeshData &a_mesh);
[[nodiscard]] json UvsOf(const MeshData &a_mesh);
[[nodiscard]] json PartitionsOf(const MeshData &a_mesh);
[[nodiscard]] std::optional<std::string>
IslandsMissing(const MeshAnalysis &a_analysis, std::size_t a_vertices);
[[nodiscard]] json IdsOfTriangles(const FlatTriangles &a_triangles,
                                  const std::vector<std::uint16_t> &a_ofVertex);
[[nodiscard]] json MapOf(const DumpedMap &a_map);
[[nodiscard]] std::string Dump(const json &a_root, int a_indent);
[[nodiscard]] bool IsKept(char a_byte) noexcept;
[[nodiscard]] std::string Trimmed(std::string a_text);
}

std::string_view DumpMapName(DumpMap a_map) noexcept {
  return NameOf(kDumpMaps, a_map);
}

std::string_view DumpMapFile(DumpMap a_map) noexcept {
  const DumpMapSpec *row = RowOf(kDumpMaps, a_map);
  return row ? row->file : "";
}

std::expected<std::vector<std::uint8_t>, std::string>
EncodeTga(const RgbaImage &a_image) {
  if (a_image.width == 0 || a_image.height == 0 ||
      a_image.width > kMaxTgaSide || a_image.height > kMaxTgaSide) {
    return std::unexpected(std::format("a {}x{} image is outside 1 to {}",
                                       a_image.width, a_image.height,
                                       kMaxTgaSide));
  }
  const std::size_t texels =
      static_cast<std::size_t>(a_image.width) * a_image.height;
  if (a_image.rgba.size() != texels * 4) {
    return std::unexpected(std::format("a {}x{} image needs {} bytes, not {}",
                                       a_image.width, a_image.height,
                                       texels * 4, a_image.rgba.size()));
  }
  std::vector<std::uint8_t> bytes;
  bytes.reserve(kTgaHeaderBytes + texels * 4);
  bytes.insert(bytes.end(), {0, 0, kTgaTrueColour, 0, 0, 0, 0, 0, 0, 0, 0, 0});
  PutShort(bytes, a_image.width);
  PutShort(bytes, a_image.height);
  bytes.push_back(kTgaBitsPerPixel);
  bytes.push_back(kTgaTopLeftEightAlphaBits);
  for (std::size_t texel = 0; texel < texels; ++texel) {
    const std::uint8_t *rgba = a_image.rgba.data() + texel * 4;
    bytes.insert(bytes.end(), {rgba[2], rgba[1], rgba[0], rgba[3]});
  }
  return bytes;
}

std::expected<MeshDump, std::string>
MeshDumpJson(const MeshData &a_mesh, const MeshAnalysis &a_analysis) {
  const std::size_t vertices = VertexCount(a_mesh);
  const std::size_t triangles = TriangleCount(a_mesh);
  if (vertices > kMaxDumpVertices) {
    return std::unexpected(std::format("{} vertices exceed the limit of {}",
                                       vertices, kMaxDumpVertices));
  }
  if (triangles > kMaxDumpTriangles) {
    return std::unexpected(std::format("{} triangles exceed the limit of {}",
                                       triangles, kMaxDumpTriangles));
  }
  const FlatTriangles flat = Flatten(a_mesh);
  json root;
  root["format"] = 1;
  root["vertexCount"] = vertices;
  root["triangleCount"] = flat.corners.size();
  root["uv"] = UvsOf(a_mesh);
  root["triangles"] = flat.corners;
  root["partitionOfTriangle"] = flat.partitionOf;
  root["partitions"] = PartitionsOf(a_mesh);
  MeshDump out;
  out.counts.vertices = vertices;
  out.counts.triangles = flat.corners.size();
  out.counts.islandsMissing = IslandsMissing(a_analysis, vertices);
  if (out.counts.islandsMissing) {
    root["islandsMissing"] = *out.counts.islandsMissing;
  } else {
    root["charts"] = a_analysis.charts;
    root["parts"] = a_analysis.components;
    root["chartOfTriangle"] = IdsOfTriangles(flat, a_analysis.chartOf);
    root["partOfTriangle"] = IdsOfTriangles(flat, a_analysis.componentOf);
  }
  out.json = Dump(root, -1);
  return out;
}

namespace {
void PutShort(std::vector<std::uint8_t> &a_bytes, std::uint32_t a_value) {
  a_bytes.push_back(static_cast<std::uint8_t>(a_value & 0xFFu));
  a_bytes.push_back(static_cast<std::uint8_t>((a_value >> 8) & 0xFFu));
}

std::size_t VertexCount(const MeshData &a_mesh) noexcept {
  std::size_t count = 0;
  for (const MeshPartition &partition : a_mesh.partitions) {
    count += partition.vertices.size();
  }
  return count;
}

std::size_t TriangleCount(const MeshData &a_mesh) noexcept {
  std::size_t count = 0;
  for (const MeshPartition &partition : a_mesh.partitions) {
    count += partition.triangles.size();
  }
  return count;
}

FlatTriangles Flatten(const MeshData &a_mesh) {
  FlatTriangles flat;
  std::uint32_t base = 0;
  for (std::size_t index = 0; index < a_mesh.partitions.size(); ++index) {
    const MeshPartition &partition = a_mesh.partitions[index];
    const std::size_t count = partition.vertices.size();
    for (const std::array<std::uint32_t, 3> &triangle : partition.triangles) {
      if (triangle[0] < count && triangle[1] < count && triangle[2] < count) {
        flat.corners.push_back(
            {base + triangle[0], base + triangle[1], base + triangle[2]});
        flat.partitionOf.push_back(static_cast<std::uint32_t>(index));
      }
    }
    base += static_cast<std::uint32_t>(count);
  }
  return flat;
}

json UvsOf(const MeshData &a_mesh) {
  json uvs = json::array();
  for (const MeshPartition &partition : a_mesh.partitions) {
    for (const MeshVertex &vertex : partition.vertices) {
      uvs.push_back(json::array({vertex.uv.x, vertex.uv.y}));
    }
  }
  return uvs;
}

json PartitionsOf(const MeshData &a_mesh) {
  json partitions = json::array();
  for (const MeshPartition &partition : a_mesh.partitions) {
    json row;
    row["slot"] = partition.slot == MeshPartition::kNoSlot
                      ? json(nullptr)
                      : json(partition.slot);
    row["vertices"] = partition.vertices.size();
    row["triangles"] = partition.triangles.size();
    partitions.push_back(std::move(row));
  }
  return partitions;
}

std::optional<std::string> IslandsMissing(const MeshAnalysis &a_analysis,
                                          std::size_t a_vertices) {
  if (a_analysis.componentOf.empty() && a_analysis.chartOf.empty()) {
    return "the mesh analysis has no island ids";
  }
  if (a_analysis.componentOf.size() != a_vertices ||
      a_analysis.chartOf.size() != a_vertices) {
    return "the mesh analysis is of a different mesh";
  }
  return std::nullopt;
}

json IdsOfTriangles(const FlatTriangles &a_triangles,
                    const std::vector<std::uint16_t> &a_ofVertex) {
  json ids = json::array();
  for (const std::array<std::uint32_t, 3> &triangle : a_triangles.corners) {
    const std::uint16_t id =
        triangle[0] < a_ofVertex.size() ? a_ofVertex[triangle[0]] : kNoIsland;
    ids.push_back(id == kNoIsland ? -1 : static_cast<int>(id));
  }
  return ids;
}
}

std::string MetaDumpJson(const DumpMeta &a_meta) {
  json root;
  root["format"] = 1;
  root["geometry"] = a_meta.geometry;
  root["armor"] = {{"name", a_meta.armor.name},
                   {"form", a_meta.armor.form},
                   {"file", a_meta.armor.file}};
  root["build"] = a_meta.build;
  root["createdAt"] = a_meta.createdAt;
  root["maps"] = json::array();
  for (const DumpedMap &map : a_meta.maps) {
    root["maps"].push_back(MapOf(map));
  }
  if (a_meta.mesh) {
    json mesh;
    mesh["file"] = kDumpMeshFile;
    mesh["vertices"] = a_meta.mesh->vertices;
    mesh["triangles"] = a_meta.mesh->triangles;
    mesh["islands"] = !a_meta.mesh->islandsMissing.has_value();
    root["mesh"] = std::move(mesh);
  } else {
    root["mesh"] = nullptr;
  }
  root["skipped"] = json::array();
  for (const DumpSkip &skip : a_meta.skipped) {
    root["skipped"].push_back({{"item", skip.item}, {"reason", skip.reason}});
  }
  return Dump(root, 2) + "\n";
}

namespace {
json MapOf(const DumpedMap &a_map) {
  json row;
  row["name"] = DumpMapName(a_map.map);
  row["file"] = DumpMapFile(a_map.map);
  row["source"] = a_map.source;
  row["width"] = a_map.width;
  row["height"] = a_map.height;
  row["mip"] = a_map.mip;
  row["sourceWidth"] = a_map.sourceWidth;
  row["sourceHeight"] = a_map.sourceHeight;
  row["sourceMips"] = a_map.sourceMips;
  return row;
}

std::string Dump(const json &a_root, int a_indent) {
  return a_root.dump(a_indent, ' ', false, json::error_handler_t::replace);
}
}

std::string SanitisedName(std::string_view a_text) {
  std::string out;
  for (const char byte : a_text) {
    const char kept = IsKept(byte) ? byte : '_';
    if (kept == '_' && !out.empty() && out.back() == '_') {
      continue;
    }
    out.push_back(kept);
  }
  out = Trimmed(std::move(out));
  if (out.size() > kMaxDumpNameBytes) {
    out.resize(kMaxDumpNameBytes);
  }
  return Trimmed(std::move(out));
}

std::string DumpFolderName(std::string_view a_armor,
                           std::string_view a_geometry,
                           std::string_view a_stamp, std::uint32_t a_attempt) {
  const std::string armor = SanitisedName(a_armor);
  const std::string geometry = SanitisedName(a_geometry);
  std::string name = armor;
  if (!geometry.empty() && geometry != armor) {
    name += name.empty() ? geometry : "_" + geometry;
  }
  if (name.empty()) {
    name = "geometry";
  }
  const std::string stamp = SanitisedName(a_stamp);
  if (!stamp.empty()) {
    name += "-" + stamp;
  }
  if (a_attempt > 0) {
    name += std::format("-{}", a_attempt + 1);
  }
  return name;
}

namespace {
bool IsKept(char a_byte) noexcept {
  return (a_byte >= 'a' && a_byte <= 'z') || (a_byte >= 'A' && a_byte <= 'Z') ||
         (a_byte >= '0' && a_byte <= '9') || a_byte == '-' || a_byte == '_';
}

std::string Trimmed(std::string a_text) {
  const auto edge = [](char a_byte) { return a_byte == '_' || a_byte == '-'; };
  while (!a_text.empty() && edge(a_text.back())) {
    a_text.pop_back();
  }
  std::size_t start = 0;
  while (start < a_text.size() && edge(a_text[start])) {
    ++start;
  }
  return a_text.substr(start);
}
}
}
