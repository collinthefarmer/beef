// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/MaterialDump.h"

#include "Identity.h"
#include "engine/EngineForms.h"
#include "engine/TextFile.h"
#include "mesh/MaterialDump.h"
#include "render/Compositor.h"
#include "render/TextureLab.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <span>
#include <string_view>
#include <system_error>

namespace BetterEnchantmentEffects {
namespace {
using Seconds =
    std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>;

[[nodiscard]] DumpArmor ArmorOf(const LivePiece &a_piece);
[[nodiscard]] std::expected<std::filesystem::path, std::string>
CreateFolder(const DumpMeta &a_meta, Seconds a_now);
void DumpMapInto(const std::filesystem::path &a_folder, DumpMap a_map,
                 const MaterialInputs &a_material, DumpMeta &a_meta);
void DumpMeshInto(const std::filesystem::path &a_folder,
                  const LiveGeometry &a_geometry, DumpMeta &a_meta);
[[nodiscard]] RE::NiSourceTexture *TextureOf(const MaterialInputs &a_material,
                                             DumpMap a_map);
[[nodiscard]] std::string SourcePathOf(const RE::NiSourceTexture *a_texture);
[[nodiscard]] bool WriteBytes(const std::filesystem::path &a_path,
                              std::span<const std::uint8_t> a_bytes);
[[nodiscard]] std::string Shown(const std::filesystem::path &a_path);
}

std::expected<Studio::MaterialDumpWritten, std::string>
WriteMaterialDump(const LivePiece &a_piece, const LiveGeometry &a_geometry,
                  std::string_view a_build) {
  const Seconds now = std::chrono::floor<std::chrono::seconds>(
      std::chrono::system_clock::now());
  DumpMeta meta;
  meta.geometry = a_geometry.name;
  meta.armor = ArmorOf(a_piece);
  meta.build = a_build;
  meta.createdAt = std::format("{:%FT%TZ}", now);
  const std::expected<std::filesystem::path, std::string> folder =
      CreateFolder(meta, now);
  if (!folder) {
    return std::unexpected(folder.error());
  }
  for (const DumpMapSpec &spec : kDumpMaps) {
    DumpMapInto(*folder, spec.value, a_geometry.inputs.material, meta);
  }
  DumpMeshInto(*folder, a_geometry, meta);
  if (!WriteText(*folder / kDumpMetaFile, MetaDumpJson(meta))) {
    return std::unexpected(
        std::format("{} could not be written", Shown(*folder / kDumpMetaFile)));
  }
  return Studio::MaterialDumpWritten{Shown(*folder), meta.skipped.size()};
}

namespace {
DumpArmor ArmorOf(const LivePiece &a_piece) {
  DumpArmor armor;
  armor.name = a_piece.armorName;
  armor.form = std::format("{:08X}", a_piece.armor);
  if (const RE::TESForm *form = RE::TESForm::LookupByID(a_piece.armor)) {
    armor.file = FormKeyFor(*form).file;
  }
  return armor;
}

std::expected<std::filesystem::path, std::string>
CreateFolder(const DumpMeta &a_meta, Seconds a_now) {
  const std::filesystem::path root = Identity::DumpFolder();
  std::error_code error;
  std::filesystem::create_directories(root, error);
  if (error) {
    return std::unexpected(std::format("{} could not be created: {}",
                                       Shown(root), error.message()));
  }
  const std::string stamp = std::format("{:%Y%m%d-%H%M%S}", a_now);
  for (std::uint32_t attempt = 0; attempt < kMaxDumpFolderAttempts; ++attempt) {
    std::filesystem::path folder =
        root /
        DumpFolderName(a_meta.armor.name, a_meta.geometry, stamp, attempt);
    if (std::filesystem::create_directory(folder, error)) {
      return folder;
    }
    if (error) {
      return std::unexpected(std::format("{} could not be created: {}",
                                         Shown(folder), error.message()));
    }
  }
  return std::unexpected("every dump folder name for this second is taken");
}

void DumpMapInto(const std::filesystem::path &a_folder, DumpMap a_map,
                 const MaterialInputs &a_material, DumpMeta &a_meta) {
  const std::string name{DumpMapName(a_map)};
  RE::NiSourceTexture *texture = TextureOf(a_material, a_map);
  if (!texture) {
    a_meta.skipped.push_back({name, "the material has no such map"});
    return;
  }
  TextureLab *lab = TextureLab::GetSingleton();
  if (!lab) {
    a_meta.skipped.push_back({name, "the texture lab is unavailable"});
    return;
  }
  const TextureLab::MapPixelsResult pixels =
      lab->CopyMapPixels(texture, kDumpMapSide);
  if (!pixels) {
    a_meta.skipped.push_back({name, pixels.error()});
    return;
  }
  const auto tga = EncodeTga(pixels->image);
  if (!tga) {
    a_meta.skipped.push_back({name, tga.error()});
    return;
  }
  if (!WriteBytes(a_folder / DumpMapFile(a_map), *tga)) {
    a_meta.skipped.push_back({name, "the file could not be written"});
    return;
  }
  a_meta.maps.push_back(DumpedMap{a_map, SourcePathOf(texture),
                                  pixels->image.width, pixels->image.height,
                                  pixels->mip, pixels->source.width,
                                  pixels->source.height, pixels->source.mips});
}

void DumpMeshInto(const std::filesystem::path &a_folder,
                  const LiveGeometry &a_geometry, DumpMeta &a_meta) {
  const std::string item{kDumpMeshFile};
  Compositor *compositor = Compositor::GetSingleton();
  RE::BSGeometry *geometry = a_geometry.geometry.get();
  if (!compositor || !geometry) {
    a_meta.skipped.push_back({item, "the geometry is no longer loaded"});
    return;
  }
  const auto entry = compositor->MeshOf(geometry, MeshUse::kData);
  if (!entry) {
    a_meta.skipped.push_back({item, entry.error()});
    return;
  }
  if (!*entry || !(*entry)->mesh) {
    a_meta.skipped.push_back({item, "the mesh cache holds no mesh data"});
    return;
  }
  const std::expected<MeshDump, std::string> dumped =
      MeshDumpJson(*(*entry)->mesh, (*entry)->analysis);
  if (!dumped) {
    a_meta.skipped.push_back({item, dumped.error()});
    return;
  }
  if (!WriteText(a_folder / kDumpMeshFile, dumped->json)) {
    a_meta.skipped.push_back({item, "the file could not be written"});
    return;
  }
  a_meta.mesh = dumped->counts;
}

RE::NiSourceTexture *TextureOf(const MaterialInputs &a_material,
                               DumpMap a_map) {
  switch (a_map) {
  case DumpMap::kDiffuse:
    return a_material.diffuse.get();
  case DumpMap::kRmaos:
    return a_material.rmaos.get();
  case DumpMap::kNormal:
    return a_material.normal.get();
  }
  return nullptr;
}

std::string SourcePathOf(const RE::NiSourceTexture *a_texture) {
  const char *name = a_texture ? a_texture->name.c_str() : nullptr;
  return name ? std::string{name} : std::string{};
}

bool WriteBytes(const std::filesystem::path &a_path,
                std::span<const std::uint8_t> a_bytes) {
  return WriteText(
      a_path, std::string_view{reinterpret_cast<const char *>(a_bytes.data()),
                               a_bytes.size()});
}

std::string Shown(const std::filesystem::path &a_path) {
  const std::u8string text = a_path.generic_u8string();
  return std::string{text.begin(), text.end()};
}
}
}
