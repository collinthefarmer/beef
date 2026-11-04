// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "PCH.h"
#include "mesh/Islands.h"
#include "mesh/MaterialClusters.h"
#include "mesh/Mesh.h"
#include "mesh/MeshFacts.h"
#include "mesh/TextureSize.h"
#include "planners/IdentityFacts.h"
#include "planners/MaterialGrouper.h"
#include "planners/RenderPlan.h"
#include "planners/RetainedStore.h"
#include "planners/StackPlan.h"
#include "planners/TextureDemand.h"
#include "recipe/Expression.h"
#include "recipe/GraphOperations.h"
#include "recipe/Merge.h"
#include "recipe/Recipe.h"
#include "recipe/Signals.h"
#include "render/MeshCache.h"
#include "render/PBRMaterial.h"
#include "render/StepOutputCache.h"
#include "render/TextureLab.h"
#include "render/TextureRef.h"

#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace BetterEnchantmentEffects {
struct MaterialInputs {
  TextureRef diffuse;
  TextureRef normal;
  TextureRef rmaos;
  TextureRef displacement;
  bool flatDisplacement = true;

  [[nodiscard]] static MaterialInputs From(const PbrMaterial &a_material);
  void CollectTargets(HolderSet &a_set) const;
};

class RenderInstance;
struct PreparedSource {
  TextureRef texture;
  TextureLab::LayerInput sampling;
  bool animated = false;
  float normalize = 1;
  std::string problem;
};
struct PreparedMask {
  TextureRef texture;
  ShaderChannel channel = ShaderChannel::kR;
  bool animated = false;
  std::string problem;
};
struct ApplicationScope {
  std::size_t context = 0;
  RE::FormID actor = 0;
};
struct EnchantmentValues {
  std::array<float, kEnchantmentFieldCount> fields{};
};
struct GeometryInputs {
  std::shared_ptr<RenderInstance> render;
  ApplicationScope scope;
  MaterialInputs material;
  RE::NiPointer<RE::BSGeometry> geometry;
  RE::NiPointer<RE::NiAVObject> root;
  float itemHash = 0.0f;
  std::uint32_t geometryIndex = 0;
  std::uint32_t geometryCount = 1;
  EnchantmentValues enchantment;
  MatchedShaderFacts shader;
  std::string name;
};

struct StackTextureRequest {
  const Recipe *recipe = nullptr;
  const RecipeGraph *graph = nullptr;
  const SurfaceOutput *output = nullptr;
  std::size_t outputIndex = 0;
  PlacementId placement{};
  GeometryInputs inputs;
  TextureSize size{TextureSize::kMin};
};

struct LayerFilter {
  std::vector<std::size_t> hidden;

  [[nodiscard]] bool Hides(std::size_t a_index) const noexcept;
  [[nodiscard]] bool operator==(const LayerFilter &) const = default;
};

struct StackBase {
  TextureRef texture;
  std::uint64_t contentVersion = 0;
};

enum class StackRender { kRendered, kPending, kFailed };

class RenderOutput {
public:
  RenderOutput(const std::shared_ptr<RenderInstance> &render,
               StepOutputRef result, TextureSize size, bool animated);
  [[nodiscard]] TextureRef Texture() const noexcept;
  [[nodiscard]] std::uint64_t ContentVersion() const noexcept;
  [[nodiscard]] TextureRef LayerTexture(std::size_t layer) const;
  [[nodiscard]] bool Animated() const noexcept;
  [[nodiscard]] TextureSize Size() const noexcept;
  [[nodiscard]] std::span<const Diagnostic> Diagnostics() const noexcept;
  [[nodiscard]] StepOutputRef Result() const noexcept;
  [[nodiscard]] std::unique_ptr<RenderOutput>
  DrawnBy(const std::shared_ptr<RenderInstance> &a_render) const;

private:
  friend class Compositor;
  std::weak_ptr<RenderInstance> render_;
  StepOutputRef result_;
  TextureRef latest_;
  std::uint64_t latestVersion_ = 0;
  TextureSize size_;
  bool animated_ = false;
  std::vector<Diagnostic> diagnostics_;
};

class Compositor {
public:
  [[nodiscard]] static Compositor *GetSingleton();

  void BeginTick(std::uint32_t a_nowMS) noexcept;
  [[nodiscard]] std::uint64_t ResidentTargets() const noexcept;
  [[nodiscard]] std::uint64_t ResidentBytes() const noexcept;

  StackRender Render(RenderOutput &a_stack, const LayerFilter &a_filter,
                     const StackBase &a_base = {});

  [[nodiscard]] TextureRef LoadImage(std::string_view a_path);

  static constexpr std::string_view kNotRendered =
      "not rendered on this geometry; add a layer that reads it";
  [[nodiscard]] std::optional<PreparedSource>
  InspectSource(const Recipe &a_recipe, const RecipeGraph &a_graph,
                std::string_view a_name, const GeometryInputs &a_inputs) const;
  [[nodiscard]] std::optional<PreparedMask>
  InspectMask(const Recipe &a_recipe, const RecipeGraph &a_graph,
              std::string_view a_name, const GeometryInputs &a_inputs) const;

  [[nodiscard]] std::expected<std::shared_ptr<MeshEntry>, std::string>
  MeshOf(RE::BSGeometry *a_geometry, MeshUse a_use = MeshUse::kData);
  [[nodiscard]] std::shared_ptr<const MeshEntry>
  CachedMesh(RE::BSGeometry *a_geometry) const noexcept;
  static constexpr std::uint32_t kMeshSweepMS = 5000;
  static constexpr std::uint32_t kMeshMaxAgeMS = 30000;
  [[nodiscard]] bool MeshSweepDue(std::uint32_t a_nowMS) const noexcept;
  void SweepMeshes(std::uint32_t a_nowMS,
                   std::span<RE::BSGeometry *const> a_bound);
  void ClearMeshes() noexcept;

  using MaterialKey = std::tuple<std::string, std::string, std::uint64_t>;
  [[nodiscard]] static MaterialKey
  MaterialKeyOf(const MaterialInputs &a_material, std::uint64_t a_meshHash);
  [[nodiscard]] std::uint64_t
  MeshHashOf(RE::BSGeometry *a_geometry) const noexcept;
  static constexpr RetentionPolicy kMaterialRecords{
      .maxCount = 64, .maxBytes = kNoByteBound, .maxIdle = 30000};
  struct MaterialRecord {
    std::optional<GroupingTicket> ticket;
    std::shared_ptr<const MaterialGrouping> grouping;
    std::string problem;
  };
  [[nodiscard]] const MaterialRecord &
  AnalyseMaterial(const MaterialInputs &a_material,
                  const std::shared_ptr<MeshEntry> &a_mesh);
  [[nodiscard]] const MaterialRecord *
  CachedMaterial(const MaterialInputs &a_material, std::uint64_t a_meshHash);
  void ClearMaterials();
  void SweepMaterials(std::uint32_t a_nowMS,
                      std::span<const MaterialKey> a_keep);

  [[nodiscard]] std::optional<TextureView>
  FindStepOutput(const std::string &a_key, std::uint64_t a_nowMS);
  void PublishStepOutput(const std::string &a_key, const TextureView &a_view,
                         std::uint64_t a_nowMS);
  void SweepStepOutputs(std::uint64_t a_nowMS);
  void ClearStepOutputs() noexcept;
  void CollectCachedTargets(HolderSet &a_set) const;
  void CollectResidentTargets(HolderSet &a_set) const;

private:
  friend class RenderInstance;
  std::shared_ptr<TextureLab::RenderTarget> NeutralHeight();
  std::shared_ptr<TextureLab::RenderTarget> neutralHeight_;

  std::unordered_map<std::string, TextureRef> images_;
  MeshCache meshes_;
  RetainedStore<MaterialKey, MaterialRecord> materials_{kMaterialRecords};
  StepOutputCache stepOutputs_;
  std::uint32_t nowMS_ = 0;
  std::uint32_t lastSweepMS_ = 0;
};

[[nodiscard]] std::optional<float> ItemFactOf(const GeometryInputs &inputs,
                                              ItemField field);
[[nodiscard]] IdentityFacts IdentityFactsOf(const RecipeGraph &graph,
                                            const GeometryInputs &inputs);
[[nodiscard]] ValueBindings TextureValueBindings(const RecipeGraph &graph,
                                                 const GeometryInputs &inputs);
[[nodiscard]] std::string MaterialIdentityOf(const MaterialInputs &material);
[[nodiscard]] std::optional<std::string>
MeshHashIdentityOf(const GeometryInputs &inputs);
[[nodiscard]] std::string MeshIdentityOf(const GeometryInputs &inputs);

[[nodiscard]] std::string DescribeTexture(const TextureRef &a_texture);
}
