// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "PCH.h"
#include "mesh/Islands.h"
#include "mesh/MaterialClusters.h"
#include "mesh/Mesh.h"
#include "mesh/MeshFacts.h"
#include "mesh/TextureSize.h"
#include "planners/RenderPlan.h"
#include "planners/RetainedCache.h"
#include "planners/StackPlan.h"
#include "planners/TextureDemand.h"
#include "recipe/Expression.h"
#include "recipe/GraphOperations.h"
#include "recipe/Merge.h"
#include "recipe/Recipe.h"
#include "recipe/Signals.h"
#include "render/MeshCache.h"
#include "render/PBRMaterial.h"
#include "render/TextureLab.h"
#include "render/TextureRef.h"

#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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
struct GeometryInputs {
  std::shared_ptr<RenderInstance> render;
  std::size_t applicationContext = 0;
  RE::FormID actor = 0;
  MaterialInputs material;
  RE::NiPointer<RE::BSGeometry> geometry;
  RE::NiPointer<RE::NiAVObject> root;
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

  [[nodiscard]] TextureSize StackSize(const SurfaceOutput &output,
                                      const GeometryInputs &inputs,
                                      TextureSize requested,
                                      TextureSize maximum) const;
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
  MeshOf(RE::BSGeometry *a_geometry);
  [[nodiscard]] std::shared_ptr<const MeshEntry>
  CachedMesh(RE::BSGeometry *a_geometry) const noexcept;
  static constexpr std::uint32_t kMeshSweepMS = 5000;
  static constexpr std::uint32_t kMeshMaxAgeMS = 30000;
  [[nodiscard]] bool MeshSweepDue(std::uint32_t a_nowMS) const noexcept;
  void SweepMeshes(std::uint32_t a_nowMS,
                   std::span<RE::BSGeometry *const> a_bound);
  void ClearMeshes() noexcept;

  using MaterialKey = std::pair<RE::NiSourceTexture *, RE::NiSourceTexture *>;
  static constexpr std::uint32_t kMaterialMaxAgeMS = 30000;
  static constexpr std::size_t kMaxUnusedMaterials = 64;
  struct MaterialRecord {
    TextureRef rmaos;
    TextureRef diffuse;
    std::shared_ptr<const MaterialSample> sample;
    std::shared_ptr<const MaterialAnalysis> analysis;
    std::string problem;
  };
  [[nodiscard]] const MaterialRecord &
  AnalyseMaterial(const MaterialInputs &a_material);
  [[nodiscard]] const MaterialRecord *
  CachedMaterial(const MaterialInputs &a_material) const noexcept;
  void ClearMaterials() noexcept;
  void SweepMaterials(std::uint32_t a_nowMS,
                      std::span<const MaterialKey> a_keep);

private:
  friend class RenderInstance;
  std::shared_ptr<TextureLab::RenderTarget> NeutralHeight();
  std::shared_ptr<TextureLab::RenderTarget> neutralHeight_;

  std::unordered_map<std::string, TextureRef> images_;
  MeshCache meshes_;
  RetainedCache<MaterialKey, MaterialRecord> materials_;
  std::uint32_t nowMS_ = 0;
  std::uint32_t lastSweepMS_ = 0;
};

[[nodiscard]] ValueBindings TextureValueBindings(const RecipeGraph &graph,
                                                 const GeometryInputs &inputs);

[[nodiscard]] std::string DescribeTexture(const TextureRef &a_texture);
}
