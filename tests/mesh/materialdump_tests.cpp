// GPL-3.0-only with the additional permission in COPYING.md.
#include "mesh/MaterialDump.h"
#include "test_support.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Equal;
using json = nlohmann::json;

namespace {
MeshVertex At(float a_x, float a_y, float a_u, float a_v) {
  MeshVertex vertex;
  vertex.position = Vec3{a_x, a_y, 0.0f};
  vertex.uv = Vec2{a_u, a_v};
  return vertex;
}

MeshData TwoPartitions() {
  MeshPartition first;
  first.slot = 32;
  first.vertices = {At(0.0f, 0.0f, 0.0f, 0.0f), At(1.0f, 0.0f, 0.25f, 0.0f),
                    At(0.0f, 1.0f, 0.0f, 0.25f)};
  first.triangles = {{0, 1, 2}};
  MeshPartition second;
  second.vertices = {At(50.0f, 0.0f, 0.5f, 0.5f), At(51.0f, 0.0f, 0.75f, 0.5f),
                     At(50.0f, 1.0f, 0.5f, 0.75f)};
  second.triangles = {{0, 1, 2}, {0, 1, 9}};
  MeshData mesh;
  mesh.partitions = {first, second};
  return mesh;
}

void TgaHeaderAndPixels() {
  const RgbaImage image{2, 1, {10, 20, 30, 40, 50, 60, 70, 80}};
  const auto bytes = EncodeTga(image);
  Check(bytes.has_value(), "a 2x1 image encodes");
  if (!bytes) {
    return;
  }
  Equal(bytes->size(), std::size_t{18 + 8}, "header plus four bytes a texel");
  Equal(int{(*bytes)[2]}, 2, "uncompressed true colour");
  Equal(int{(*bytes)[12]}, 2, "width low byte");
  Equal(int{(*bytes)[13]}, 0, "width high byte");
  Equal(int{(*bytes)[14]}, 1, "height low byte");
  Equal(int{(*bytes)[16]}, 32, "32 bits a pixel");
  Equal(int{(*bytes)[17]}, 0x28, "top-left origin with eight alpha bits");
  const std::vector<std::uint8_t> pixels(bytes->begin() + 18, bytes->end());
  Check(pixels == std::vector<std::uint8_t>{30, 20, 10, 40, 70, 60, 50, 80},
        "texels are written blue, green, red, alpha in row order");

  const RgbaImage wide{300, 1, std::vector<std::uint8_t>(300 * 4, 0)};
  const auto wideBytes = EncodeTga(wide);
  Check(wideBytes && (*wideBytes)[12] == 44 && (*wideBytes)[13] == 1,
        "a width over 255 spans both header bytes");
}

void TgaRefusesBadInput() {
  Check(!EncodeTga(RgbaImage{}), "an empty image is refused");
  Check(!EncodeTga(RgbaImage{2, 2, std::vector<std::uint8_t>(15, 0)}),
        "too few bytes are refused");
  Check(!EncodeTga(RgbaImage{2, 2, std::vector<std::uint8_t>(17, 0)}),
        "too many bytes are refused");
  Check(!EncodeTga(RgbaImage{
            kMaxTgaSide + 1, 1,
            std::vector<std::uint8_t>((std::size_t{kMaxTgaSide} + 1) * 4, 0)}),
        "a side over the limit is refused");
  Check(!EncodeTga(RgbaImage{0xFFFFFFFFu, 0xFFFFFFFFu, {}}),
        "a huge size is refused before any arithmetic on it");
}

void MeshWithIslands() {
  const MeshData mesh = TwoPartitions();
  const MeshAnalysis analysis = AnalyseMesh(mesh);
  const auto dumped = MeshDumpJson(mesh, analysis);
  Check(dumped.has_value(), "a small mesh dumps");
  if (!dumped) {
    return;
  }
  const json root = json::parse(dumped->json);
  Equal(root["vertexCount"].get<int>(), 6, "every vertex is counted");
  Equal(root["triangleCount"].get<int>(), 2,
        "a triangle past its partition's vertices is dropped");
  Equal(dumped->counts.triangles, std::size_t{2}, "the counts agree");
  Equal(root["uv"].size(), std::size_t{6}, "one uv a vertex");
  Check(root["uv"][1] == json::array({0.25, 0.0}), "uvs keep their values");
  Check(root["triangles"][1] == json::array({3, 4, 5}),
        "later partitions' indices are offset by earlier vertices");
  Check(root["partitionOfTriangle"] == json::array({0, 1}),
        "each triangle names its partition");
  Equal(root["partitions"][0]["slot"].get<int>(), 32, "the slot is kept");
  Check(root["partitions"][1]["slot"].is_null(), "no slot is null");
  Check(!root.contains("islandsMissing"), "the analysis matches");
  Equal(root["chartOfTriangle"].size(), std::size_t{2}, "a chart a triangle");
  Equal(root["partOfTriangle"].size(), std::size_t{2}, "a part a triangle");
  Check(root["partOfTriangle"][0] != root["partOfTriangle"][1],
        "separate pieces are separate parts");
}

void MeshWithoutIslands() {
  const MeshData mesh = TwoPartitions();
  const auto bare = MeshDumpJson(mesh, MeshAnalysis{});
  Check(bare && bare->counts.islandsMissing.has_value(),
        "no analysis is recorded, not invented");
  if (bare) {
    const json root = json::parse(bare->json);
    Check(!root.contains("chartOfTriangle"), "no chart ids without analysis");
  }
  MeshAnalysis partial;
  partial.componentOf = {0};
  partial.chartOf = {0};
  const auto mismatched = MeshDumpJson(mesh, partial);
  Check(mismatched &&
            mismatched->counts.islandsMissing ==
                std::string{"the mesh analysis is of a different mesh"},
        "an analysis of another mesh is refused, not indexed past its end");
}

void MeshOverTheCap() {
  MeshPartition partition;
  partition.vertices = {At(0, 0, 0, 0)};
  partition.triangles.assign(kMaxDumpTriangles + 1, {0, 0, 0});
  MeshData mesh;
  mesh.partitions = {partition};
  const auto refused = MeshDumpJson(mesh, MeshAnalysis{});
  Check(!refused && refused.error().find("exceed") != std::string::npos,
        "a mesh over the triangle cap is refused with the reason");
}

void MetaRecordsEverything() {
  DumpMeta meta;
  meta.geometry = "Cuirass";
  meta.armor = {"Bonemold \"Cuirass\"", "0x0001234A", "Skyrim.esm"};
  meta.build = "abc-Release";
  meta.createdAt = "20261013-120000";
  meta.maps.push_back(DumpedMap{DumpMap::kRmaos, "textures\\a_rmaos.dds", 512,
                                512, 2.0f, 2048, 2048, 12});
  meta.mesh = DumpedMesh{6, 2, std::nullopt};
  meta.skipped.push_back({"normal", "the map is missing"});
  const json root = json::parse(MetaDumpJson(meta));
  Equal(root["armor"]["name"].get<std::string>(),
        std::string{"Bonemold \"Cuirass\""}, "quotes survive");
  Equal(root["maps"][0]["file"].get<std::string>(), std::string{"rmaos.tga"},
        "the map names its file");
  Equal(root["maps"][0]["mip"].get<float>(), 2.0f, "the mip read is kept");
  Equal(root["mesh"]["triangles"].get<int>(), 2, "the mesh is summarised");
  Equal(root["skipped"][0]["reason"].get<std::string>(),
        std::string{"the map is missing"}, "skips keep their reason");

  DumpMeta invalid;
  invalid.geometry = std::string{"bad\xff\xfe"};
  Check(!MetaDumpJson(invalid).empty(),
        "a name that is not UTF-8 is replaced, not thrown");
  Check(json::parse(MetaDumpJson(invalid))["mesh"].is_null(),
        "no mesh is null");
}

void FolderNames() {
  Equal(SanitisedName("Bonemold Cuirass (M)"),
        std::string{"Bonemold_Cuirass_M"}, "spaces and brackets fold");
  Equal(SanitisedName("../../Windows"), std::string{"Windows"},
        "dots and separators cannot climb out");
  Equal(SanitisedName("C:\\x"), std::string{"C_x"}, "drive letters fold");
  Equal(SanitisedName(std::string(200, 'a')).size(), kMaxDumpNameBytes,
        "long names are cut");
  Equal(SanitisedName("\xc3\xa9t\xc3\xa9"), std::string{"t"},
        "non-ASCII bytes fold");
  Equal(DumpFolderName("Bonemold Cuirass", "Cuirass:0", "20261013-120000", 0),
        std::string{"Bonemold_Cuirass_Cuirass_0-20261013-120000"},
        "armor, geometry and stamp");
  Equal(DumpFolderName("", "", "20261013-120000", 2),
        std::string{"geometry-20261013-120000-3"},
        "an unnamed geometry still gets a folder, and retries count up");
  Equal(DumpFolderName("Hood", "Hood", "s", 0), std::string{"Hood-s"},
        "a geometry named as its armor is not repeated");
  for (const std::string &name :
       {DumpFolderName("..", "/", "..", 0), DumpFolderName(".", ".", ".", 1)}) {
    Check(name.find('.') == std::string::npos &&
              name.find('/') == std::string::npos &&
              name.find('\\') == std::string::npos && !name.empty(),
          "a hostile name yields one plain folder");
  }
}
}

int main() {
  TgaHeaderAndPixels();
  TgaRefusesBadInput();
  MeshWithIslands();
  MeshWithoutIslands();
  MeshOverTheCap();
  MetaRecordsEverything();
  FolderNames();
  return test::Finish("materialdump");
}
