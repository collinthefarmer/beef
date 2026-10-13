// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "mesh/Islands.h"
#include "mesh/Mesh.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects {
inline constexpr std::uint32_t kDumpMapSide = 512;
inline constexpr std::uint32_t kMaxTgaSide = 4096;
inline constexpr std::size_t kMaxDumpTriangles = 500000;
inline constexpr std::size_t kMaxDumpVertices = 1500000;
inline constexpr std::size_t kMaxDumpNameBytes = 48;
inline constexpr std::uint32_t kMaxDumpFolderAttempts = 9;

enum class DumpMap : std::uint8_t { kDiffuse, kRmaos, kNormal };
struct DumpMapSpec {
  DumpMap value;
  std::string_view name;
  std::string_view file;
};
inline constexpr DumpMapSpec kDumpMaps[]{
    {DumpMap::kDiffuse, "diffuse", "diffuse.tga"},
    {DumpMap::kRmaos, "rmaos", "rmaos.tga"},
    {DumpMap::kNormal, "normal", "normal.tga"},
};
inline constexpr std::string_view kDumpMeshFile = "mesh.json";
inline constexpr std::string_view kDumpMetaFile = "meta.json";

struct RgbaImage {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> rgba;
};

struct DumpedMap {
  DumpMap map = DumpMap::kDiffuse;
  std::string source;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  float mip = 0.0f;
  std::uint32_t sourceWidth = 0;
  std::uint32_t sourceHeight = 0;
  std::uint32_t sourceMips = 0;
};

struct DumpedMesh {
  std::size_t vertices = 0;
  std::size_t triangles = 0;
  std::optional<std::string> islandsMissing;
};

struct DumpSkip {
  std::string item;
  std::string reason;
};

struct DumpArmor {
  std::string name;
  std::string form;
  std::string file;
};

struct DumpMeta {
  std::string geometry;
  DumpArmor armor;
  std::string build;
  std::string createdAt;
  std::vector<DumpedMap> maps;
  std::optional<DumpedMesh> mesh;
  std::vector<DumpSkip> skipped;
};

struct MeshDump {
  std::string json;
  DumpedMesh counts;
};

[[nodiscard]] std::string_view DumpMapName(DumpMap a_map) noexcept;
[[nodiscard]] std::string_view DumpMapFile(DumpMap a_map) noexcept;

[[nodiscard]] std::expected<std::vector<std::uint8_t>, std::string>
EncodeTga(const RgbaImage &a_image);

[[nodiscard]] std::expected<MeshDump, std::string>
MeshDumpJson(const MeshData &a_mesh, const MeshAnalysis &a_analysis);

[[nodiscard]] std::string MetaDumpJson(const DumpMeta &a_meta);

[[nodiscard]] std::string SanitisedName(std::string_view a_text);
[[nodiscard]] std::string DumpFolderName(std::string_view a_armor,
                                         std::string_view a_geometry,
                                         std::string_view a_stamp,
                                         std::uint32_t a_attempt);
}
