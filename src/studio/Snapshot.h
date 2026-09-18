#pragma once

#include "Core.h"
#include "mesh/Islands.h"
#include "mesh/MaterialClusters.h"
#include "mesh/MeshFacts.h"
#include "recipe/Recipe.h"
#include "studio/ApplicationRecord.h"
#include "studio/EditResult.h"
#include "studio/FileOperation.h"
#include "studio/GameObjects.h"
#include "studio/Gesture.h"
#include "studio/InputCatalog.h"
#include "studio/PaintCommit.h"
#include "studio/Relationships.h"
#include "studio/View.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
enum class TextureHandle : std::uintptr_t {};

struct SignalRow {
  std::string name;
  SignalKindId kind = SignalKindId::kConstant;
  ValueType type = ValueType::kScalar;
  Value value;
  bool inert = false;
  std::optional<Value> constant;
  std::string text;
  std::string curve;
  std::string problem;
  std::string event;
  std::size_t references = 0;
  SignalKind definition = ConstantSignal{};
  bool live = false;
};

struct TextRow {
  std::string name;
  std::string text;
  std::size_t references = 0;
};

struct LayerRow {
  std::string source;
  std::string mask;
  Blend blend = Blend::kReplace;
  float opacity = 1.0f;
  std::string opacityText;
  std::string color;
  std::string curve;
  std::string channels;
  std::string problem;
  TextureHandle texture{};
};

struct ScalarRow {
  std::string name;
  Value value;
  std::string text;
};

struct OutputRow {
  std::size_t index = 0;
  Target target = Target::kMaterial;
  Surface surface = Surface::kMaterial;
  Slot slot = Slot::kEmissive;
  bool replace = false;
  bool merged = false;
  std::size_t merge = 0;
  bool animated = false;
  std::uint32_t size = 0;
  std::string selector;
  Selector selection;
  std::string problem;
  std::vector<ScalarRow> scalars;
  std::vector<LayerRow> layers;
  TextureHandle texture{};
};

struct PictureRow {
  std::string name;
  std::string description;
  ValueType type = ValueType::kScalar;
  TextureHandle texture{};
  ShaderChannel channel = ShaderChannel::kRgb;
  bool animated = false;
  std::string problem;
};

struct SlotRow {
  Slot slot = Slot::kEmissive;
  std::string original;
  std::string written;
  std::string problem;
};

struct GeometryRow {
  std::string name;
  bool privateMaterial = false;
  bool meshRead = false;
  std::vector<SlotCoverage> partitions;
  std::vector<BoneCoverage> bones;
  std::vector<MeshIsland> islands;
  std::vector<MaterialCluster> clusters;
  std::string shell;
  std::vector<SlotRow> materialSlots;
  std::vector<SlotRow> shellSlots;
  std::vector<PictureRow> sources;
  std::vector<PictureRow> masks;
  std::vector<OutputRow> outputs;
};

struct ImageSourceRow {
  std::string path;
  std::string channel;
  std::string space;
  std::string scroll;
  std::string tile;
  std::string mirrorU;
  std::string mirrorV;
  std::string transpose;
  std::string mip;
};

struct MaterialSourceRow {
  std::string material;
};

struct BakeSourceRow {
  std::string bake;
  std::string partition;
  std::string bones;
};

struct UvSourceRow {
  std::string axis;
};

struct DistanceSourceRow {
  std::string from;
};

struct RippleSourceRow {
  std::string trigger;
  std::string speed;
  std::string width;
  std::string decay;
  std::string shape;
};

struct MaterialClustersSourceRow {
  std::string clusters;
  std::string weights;
  std::string seed;
  std::string iterations;
};

using SourceRowKind =
    std::variant<ImageSourceRow, MaterialSourceRow, BakeSourceRow, UvSourceRow,
                 DistanceSourceRow, RippleSourceRow, MaterialClustersSourceRow>;
static_assert(std::variant_size_v<SourceRowKind> == kSourceKindCount);
static_assert(std::is_same_v<std::variant_alternative_t<0, SourceRowKind>,
                             ImageSourceRow> &&
                  std::is_same_v<std::variant_alternative_t<1, SourceRowKind>,
                                 MaterialSourceRow> &&
                  std::is_same_v<std::variant_alternative_t<2, SourceRowKind>,
                                 BakeSourceRow> &&
                  std::is_same_v<std::variant_alternative_t<3, SourceRowKind>,
                                 UvSourceRow> &&
                  std::is_same_v<std::variant_alternative_t<4, SourceRowKind>,
                                 DistanceSourceRow> &&
                  std::is_same_v<std::variant_alternative_t<5, SourceRowKind>,
                                 RippleSourceRow> &&
                  std::is_same_v<std::variant_alternative_t<6, SourceRowKind>,
                                 MaterialClustersSourceRow>,
              "SourceRowKind alternative order must match SourceKindId");

[[nodiscard]] inline SourceKindId
SourceRowKindId(const SourceRowKind &a_kind) noexcept {
  return static_cast<SourceKindId>(a_kind.index());
}

struct SourceRow {
  std::string name;
  ValueType type = ValueType::kScalar;
  SourceRowKind kind = MaterialSourceRow{};
  std::size_t references = 0;
};

struct LightRow {
  bool present = false;
  std::size_t output = 0;
  std::string color;
  std::string intensity;
  std::string size;
  std::string cutoff;
  std::string offset;
  bool shadow = false;
  bool replace = false;
  std::string selector;
  Selector selection;
  std::string bones;
  std::string bonesMax;
  std::string bonesMinShare;
  std::string bonesNames;
};

struct ShellRow {
  ShellMaterial material = ShellMaterial::kPbrCopy;
  ShellBlend blend = ShellBlend::kAdditive;
  bool depthBias = true;
  float alphaTest = 0.0f;
  std::string alpha;
  std::string rimPower;
  std::string emissive;
  std::string inflate;
  std::string offset;
  std::string scale;
  std::string spin;
  Vec3 scalePoint;
  Vec3 spinAxis{0.0f, 0.0f, 1.0f};
};

struct RecipeRow {
  std::string id;
  RecipeKey matchedKey;
  std::vector<RecipeKey> keys;
  int priority = 0;
  float clockSpeed = 1.0f;
  float time = 0.0f;
  bool dirty = false;
  bool pinned = false;
  ShellMaterial shellMaterial = ShellMaterial::kPbrCopy;
  std::vector<SignalRow> signals;
  std::vector<TextRow> curves;
  std::vector<std::string> masks;
  std::vector<TextRow> maskRows;
  std::vector<SourceRow> sourceRows;
  std::vector<GeometryRow> geometries;
  std::vector<OutputRow> outputs;
  std::string light;
  std::optional<std::size_t> lightOutput;
  std::vector<Diagnostic> problems;
  bool heldBack = false;
  std::size_t undoDepth = 0;
  std::size_t redoDepth = 0;
  LightRow lightRow;
  std::vector<LightRow> lights;
  ShellRow shellRow;
  std::uint64_t documentRevision = 0;
  std::vector<Relationship> relationships;
};

struct KeyChoice {
  PieceKey key;
  std::string text;
};

struct PieceRow {
  PieceRef ref;
  bool isPlayer = false;
  std::string actorName;
  std::string armorName;
  std::vector<KeyChoice> keys;
  std::vector<RecipeRow> recipes;
};

struct LoadedRecipeRow {
  std::string id;
  std::vector<RecipeKey> keys;
  std::size_t signals = 0;
  std::size_t curves = 0;
  std::size_t sources = 0;
  std::size_t masks = 0;
  std::size_t outputs = 0;
  bool imported = false;
  std::vector<Diagnostic> diagnostics;
  std::string path;
};

struct Status {
  bool emissivePath = false;
  bool layoutVerified = false;
  bool textureLab = false;
  std::uint32_t actors = 0;
  std::uint32_t pieces = 0;
  std::uint32_t recipes = 0;
  std::uint32_t geometries = 0;
  std::uint32_t shells = 0;
  std::uint32_t lights = 0;
  std::size_t loadedFiles = 0;
  std::size_t withErrors = 0;
};

struct Snapshot {
  std::vector<ApplicationRecord> applications;
  std::vector<FileOperationResult> fileOperations;
  std::vector<RecipeEditResult> editResults;
  std::uint64_t version = 0;
  std::uint32_t tickMS = 0;
  Status status;
  View view;
  std::optional<PaintCommitResult> paintCommit;
  std::optional<PaintUpdateResult> paintUpdate;
  std::vector<PieceRow> pieces;
  std::vector<std::string> loaded;
  std::vector<LoadedRecipeRow> loadedRecipes;
  std::vector<RecipeRow> documents;
  std::optional<GestureResult> gesture;
  std::array<std::shared_ptr<const GameObjectCatalog>, kGameObjectKindCount>
      catalogs{};
  FormID catalogEventActor = 0;
  std::vector<ActorValueSample> actorValueSamples;
};
}
