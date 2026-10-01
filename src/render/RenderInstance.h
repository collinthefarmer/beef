// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/RenderExecution.h"
#include "planners/RenderFirings.h"
#include "render/Compositor.h"

namespace BetterEnchantmentEffects {
struct TextureView {
  TextureRef texture;
  TextureLab::LayerInput sampling;
  float normalize = 1;
  std::shared_ptr<TextureLab::RenderTarget> target;
};
struct RenderTransform {
  RE::NiPointer<RE::NiAVObject> root;
};
struct StackResult {
  TextureRef texture;
  ChangeVersion contentVersion = 0;
};
struct StackPending {};
using StackOutcome = std::variant<StackResult, StackPending>;
using RenderValue =
    std::variant<Value, TextureView, std::shared_ptr<MeshEntry>, MaterialInputs,
                 RenderTransform, RenderFirings,
                 std::shared_ptr<const BakeBuffers>,
                 std::shared_ptr<const MaterialSample>,
                 std::shared_ptr<const MaterialAnalysis>,
                 std::shared_ptr<TextureLab::Lookup>, LayerFilter, StackResult>;
struct LayerFieldPack {
  std::vector<std::size_t> fields;
  PackedLayerFields packed;
};
struct RenderScratch {
  std::weak_ptr<TextureLab::RenderTarget> target;
  TextureLab::ReductionReadback reduction;
  TextureLab::MaterialReadback material;
  std::optional<LayerFieldPack> layerFieldPack;
};
class RenderInstance {
public:
  RenderInstance(RenderPlan plan, std::vector<GeometryInputs> geometries,
                 std::vector<std::shared_ptr<const RecipeGraph>> graphs);
  [[nodiscard]] std::expected<void, std::string>
  UpdateInput(RenderInputId input, RenderValue value);
  [[nodiscard]] const RenderPlan &Plan() const noexcept;
  bool BeginFrame(std::uint64_t frame, std::uint64_t nowMS);
  [[nodiscard]] std::expected<void, std::string>
  Update(const RecipeGraph &graph, std::size_t instance,
         const SignalState &signals);
  [[nodiscard]] std::expected<StackOutcome, std::string>
  Render(StepOutputRef output, const LayerFilter &filter,
         const StackBase &base);
  [[nodiscard]] std::optional<TextureView> Texture(RenderValueRef output);
  [[nodiscard]] std::optional<TextureView>
  Inspect(const RecipeGraph &graph, OutputRef output, std::size_t instance,
          const RE::BSGeometry *geometry);

private:
  [[nodiscard]] std::expected<ResolvedRenderInput<RenderValue>, std::string>
  Demand(RenderValueRef output);
  void CollectReadbacks();
  void CollectReadback(RenderInputId input);
  [[nodiscard]] bool AwaitingFirstReadback() const;

  std::vector<std::shared_ptr<const RecipeGraph>> graphs_;
  [[nodiscard]] const GeometryInputs *GeometryOf(GeometryId id) const noexcept;

  std::vector<GeometryInputs> geometries_;
  std::optional<std::uint64_t> frame_;
  RenderExecution<RenderValue, RenderScratch> execution_;
  std::uint64_t restoresReported_ = 0;
  std::uint64_t nowMS_ = 0;
};
}
