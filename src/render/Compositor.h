#pragma once

#include "PCH.h"
#include "engine/MeshReader.h"
#include "mesh/Islands.h"
#include "mesh/MaterialClusters.h"
#include "mesh/Mesh.h"
#include "mesh/MeshFacts.h"
#include "mesh/TextureSize.h"
#include "planners/StackPlan.h"
#include "recipe/Expression.h"
#include "recipe/Merge.h"
#include "recipe/Recipe.h"
#include "recipe/Signals.h"
#include "render/PBRMaterial.h"
#include "render/RuntimeTextures.h"

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
  RE::NiPointer<RE::NiSourceTexture> diffuse;
  RE::NiPointer<RE::NiSourceTexture> normal;
  RE::NiPointer<RE::NiSourceTexture> rmaos;
  RE::NiPointer<RE::NiSourceTexture> displacement;
  bool flatDisplacement = true;

  [[nodiscard]] static MaterialInputs From(const PBRMaterialLayout &a_material);
};

class RenderedMask;
class RenderedRipple;

struct PreparedSource {
  RE::NiPointer<RE::NiSourceTexture> texture;
  TextureLab::LayerInput sampling;
  std::optional<Vec2Param> scroll;
  std::optional<Vec2Param> tile;
  bool animated = false;
  float normalize = 1.0f;
  std::string problem;
  std::shared_ptr<RenderedMask> rendered;
  std::shared_ptr<RenderedRipple> ripple;
};

struct PreparedMask {
  RE::NiPointer<RE::NiSourceTexture> texture;
  ShaderChannel channel = ShaderChannel::kR;
  bool animated = false;
  std::string problem;
  std::shared_ptr<RenderedMask> rendered;
};

using MaskCache =
    std::unordered_map<std::string, std::shared_ptr<RenderedMask>>;
using RippleCache =
    std::unordered_map<std::string, std::shared_ptr<RenderedRipple>>;

struct DerivedMaps {
  std::shared_ptr<TextureLab::RenderTarget> normalSlope;
  std::string problem;
  bool tried = false;
  std::shared_ptr<TextureLab::RenderTarget> clusters;
  ClusterSettings clusterSettings;
  std::string clustersProblem;
  bool clustersTried = false;
};

struct GeometryInputs {
  MaterialInputs material;
  RE::NiPointer<RE::BSGeometry> geometry;
  RE::NiPointer<RE::NiAVObject> root;
  std::shared_ptr<MaskCache> masks = std::make_shared<MaskCache>();
  std::shared_ptr<RippleCache> ripples = std::make_shared<RippleCache>();
  std::shared_ptr<DerivedMaps> derived = std::make_shared<DerivedMaps>();
};

class RenderedRipple {
public:
  [[nodiscard]] RE::NiSourceTexture *Texture() const noexcept;

private:
  friend class Compositor;
  std::shared_ptr<TextureLab::RenderTarget> target_;
  std::shared_ptr<TextureLab::RenderTarget> positions_;
  RippleSource source_;
  Vec3 fallbackOrigin_;
  RE::NiPointer<RE::BSGeometry> geometry_;
  RE::NiPointer<RE::NiAVObject> root_;
  bool hadFirings_ = false;
  std::uint64_t renderedTick_ = 0;
};

class RenderedMask {
public:
  [[nodiscard]] RE::NiSourceTexture *Texture() const noexcept;
  [[nodiscard]] bool Animated() const noexcept;
  [[nodiscard]] bool Vector() const noexcept;
  [[nodiscard]] const std::string &Problem() const noexcept;

private:
  friend class Compositor;
  struct RefBinding {
    bool isTexture = false;
    std::uint32_t texture = 0;
    std::string signal;
  };
  std::optional<Program> program_;
  std::vector<RefBinding> refs_;
  std::vector<PreparedSource> textures_;
  std::vector<std::shared_ptr<RenderedMask>> dependencies_;
  std::vector<std::shared_ptr<TextureLab::Lookup>> curves_;
  std::shared_ptr<TextureLab::RenderTarget> target_;
  bool animated_ = false;
  bool vector_ = false;
  bool renderedOnce_ = false;
  std::uint64_t renderedTick_ = 0;
  std::string problem_;
};

struct PreparedLayer {
  const Layer *layer = nullptr;
  std::size_t index = 0;
  std::optional<PreparedSource> source;
  std::optional<PreparedMask> mask;
  std::shared_ptr<TextureLab::Lookup> curve;
};

struct LayerFilter {
  std::vector<std::size_t> hidden;

  [[nodiscard]] bool Hides(std::size_t a_index) const noexcept;
  [[nodiscard]] bool operator==(const LayerFilter &) const = default;
};

struct StackBase {
  RE::NiSourceTexture *texture = nullptr;
  bool animated = false;
};

class RenderedStack {
public:
  [[nodiscard]] RE::NiSourceTexture *Texture() const noexcept;
  [[nodiscard]] bool Animated() const noexcept;
  [[nodiscard]] TextureSize Size() const noexcept;
  [[nodiscard]] std::span<const PreparedLayer> Layers() const noexcept;
  [[nodiscard]] std::span<const Diagnostic> Diagnostics() const noexcept;

private:
  friend class Compositor;
  std::vector<PreparedLayer> layers_;
  RE::NiPointer<RE::NiSourceTexture> base_;
  std::shared_ptr<TextureLab::RenderTarget> neutral_;
  std::shared_ptr<TextureLab::RenderTarget> target_;
  TextureLab::RenderTarget *latest_ = nullptr;
  RE::NiSourceTexture *renderedBase_ = nullptr;
  TextureSize size_{TextureSize::kMin};
  bool animated_ = false;
  bool renderedOnce_ = false;
  LayerFilter filter_;
  std::vector<Diagnostic> diagnostics_;
};

struct MeshEntry {
  RE::NiPointer<RE::BSGeometry> geometry;
  MeshIdentity identity;
  std::shared_ptr<const MeshData> mesh;
  std::string problem;
  MeshFacts facts;
  MeshAnalysis analysis;
  std::unordered_map<std::string, std::shared_ptr<TextureLab::RenderTarget>>
      bakes;
  std::uint32_t lastUsedMS = 0;
};

class MeshCache {
public:
  [[nodiscard]] std::expected<std::shared_ptr<MeshEntry>, std::string>
  Get(RE::BSGeometry *a_geometry, std::uint32_t a_nowMS, bool a_verbose);
  [[nodiscard]] std::shared_ptr<const MeshEntry>
  Cached(RE::BSGeometry *a_geometry) const noexcept;
  void Sweep(std::uint32_t a_nowMS, std::uint32_t a_maxAgeMS,
             std::span<RE::BSGeometry *const> a_keep, bool a_verbose);
  void Clear() noexcept;

private:
  std::unordered_map<RE::BSGeometry *, std::shared_ptr<MeshEntry>> entries_;
};

class Compositor {
public:
  [[nodiscard]] static Compositor *GetSingleton();

  void BeginTick(std::uint32_t a_nowMS) noexcept;

  [[nodiscard]] std::unique_ptr<RenderedStack>
  Prepare(const Recipe &a_recipe, const SurfaceOutput &a_output,
          const GeometryInputs &a_inputs, TextureSize a_size,
          TextureSize a_maxSize);

  void Render(RenderedStack &a_stack, const SignalState &a_signals,
              float a_time, const LayerFilter &a_filter,
              const StackBase &a_base = {});

  [[nodiscard]] RE::NiPointer<RE::NiSourceTexture>
  LoadImage(std::string_view a_path);

  static constexpr std::string_view kNotRendered =
      "not rendered on this geometry; add a layer that reads it";
  [[nodiscard]] std::optional<PreparedSource>
  InspectSource(const Recipe &a_recipe, std::string_view a_name,
                const GeometryInputs &a_inputs) const;
  [[nodiscard]] std::optional<PreparedMask>
  InspectMask(const Recipe &a_recipe, std::string_view a_name,
              const GeometryInputs &a_inputs) const;

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

  struct MaterialRecord {
    RE::NiPointer<RE::NiSourceTexture> rmaos;
    RE::NiPointer<RE::NiSourceTexture> diffuse;
    std::shared_ptr<const MaterialSample> sample;
    std::shared_ptr<const MaterialAnalysis> analysis;
    std::string problem;
  };
  [[nodiscard]] const MaterialRecord &
  AnalyseMaterial(const MaterialInputs &a_material);
  [[nodiscard]] const MaterialRecord *
  CachedMaterial(const MaterialInputs &a_material) const noexcept;
  void ClearMaterials() noexcept;

private:
  std::shared_ptr<TextureLab::RenderTarget> NeutralHeight();
  std::shared_ptr<TextureLab::RenderTarget> neutralHeight_;

  std::optional<PreparedSource>
  PrepareSource(const Recipe &a_recipe, const Ref &a_ref,
                const GeometryInputs &a_inputs, TextureSize a_size,
                std::vector<Diagnostic> &a_out, const std::string &a_where,
                std::uint32_t a_depth = 0);
  std::optional<PreparedMask>
  PrepareMask(const Recipe &a_recipe, const Ref &a_ref,
              const GeometryInputs &a_inputs, TextureSize a_size,
              std::vector<Diagnostic> &a_out, const std::string &a_where);
  std::shared_ptr<RenderedMask>
  PrepareRenderedMask(const Recipe &a_recipe, std::string_view a_name,
                      const GeometryInputs &a_inputs, TextureSize a_size,
                      std::uint32_t a_depth);
  void RenderMask(RenderedMask &a_mask, const SignalState &a_signals,
                  float a_time);
  std::expected<std::shared_ptr<TextureLab::RenderTarget>, std::string>
  BakeInto(MeshEntry &a_entry, const std::string &a_key, TextureSize a_size,
           const std::function<BakeBuffers()> &a_buffers);
  std::expected<std::shared_ptr<TextureLab::RenderTarget>, std::string>
  PrepareBake(const BakeSource &a_bake, const GeometryInputs &a_inputs,
              TextureSize a_size);
  std::expected<std::shared_ptr<TextureLab::RenderTarget>, std::string>
  PrepareDistance(const DistanceSource &a_distance,
                  const GeometryInputs &a_inputs, TextureSize a_size);
  std::expected<std::shared_ptr<RenderedRipple>, std::string>
  PrepareRipple(const Source &a_source, const RippleSource &a_ripple,
                const GeometryInputs &a_inputs, TextureSize a_size);
  void RenderRipple(RenderedRipple &a_ripple, const SignalState &a_signals,
                    float a_time);
  std::shared_ptr<TextureLab::Lookup>
  BakeCurve(const Recipe &a_recipe, const CurveRef &a_curve,
            const std::optional<PreparedSource> &a_source,
            std::vector<Diagnostic> &a_out, const std::string &a_where);

  std::unordered_map<std::string, RE::NiPointer<RE::NiSourceTexture>> images_;
  MeshCache meshes_;
  std::map<std::pair<RE::NiSourceTexture *, RE::NiSourceTexture *>,
           MaterialRecord>
      materials_;
  std::uint64_t tick_ = 1;
  std::uint32_t nowMS_ = 0;
  std::uint32_t lastSweepMS_ = 0;
};
}
