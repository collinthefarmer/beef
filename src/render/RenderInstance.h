// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/RenderExecution.h"
#include "render/Compositor.h"

namespace BetterEnchantmentEffects {
struct TextureView {
  TextureRef texture;
  TextureLab::LayerInput sampling;
  float normalize = 1;
  std::shared_ptr<TextureLab::RenderTarget> target;
};
struct RenderFiring {
  std::optional<Vec3> origin;
  float startTime = 0;
};
struct RenderFirings {
  std::vector<RenderFiring> firings;
};
struct RenderTransform {
  RE::NiPointer<RE::NiAVObject> root;
};
struct StackResult {
  TextureRef texture;
};
using RenderValue =
    std::variant<Value, TextureView, std::shared_ptr<MeshEntry>, MaterialInputs,
                 RenderTransform, RenderFirings,
                 std::shared_ptr<const BakeBuffers>,
                 std::shared_ptr<const MaterialSample>,
                 std::shared_ptr<const MaterialAnalysis>,
                 std::shared_ptr<TextureLab::Lookup>, LayerFilter, StackResult>;
struct RenderScratch {
  std::weak_ptr<TextureLab::RenderTarget> target;
};
class RenderInstance {
public:
  RenderInstance(RenderPlan plan, GeometryInputs inputs,
                 std::vector<std::shared_ptr<const RecipeGraph>> graphs);
  [[nodiscard]] std::expected<void, std::string>
  UpdateInput(RenderInputId input, RenderValue value, bool mutated = false);
  [[nodiscard]] const RenderPlan &Plan() const noexcept;
  [[nodiscard]] std::expected<void, std::string>
  Update(const RecipeGraph &graph, std::size_t instance,
         const SignalState &signals);
  [[nodiscard]] std::expected<StackResult, std::string>
  Render(StepOutputRef output, const LayerFilter &filter,
         const StackBase &base);
  [[nodiscard]] std::optional<TextureView> Texture(RenderValueRef output) const;
  [[nodiscard]] std::optional<TextureView> Inspect(const RecipeGraph &graph,
                                                   OutputRef output,
                                                   std::size_t instance) const;

private:
  [[nodiscard]] std::expected<RenderValue, std::string>
  Execute(const RenderStep &step,
          std::span<const ResolvedRenderInput<RenderValue>> inputs,
          RenderScratch &scratch);

  std::vector<std::shared_ptr<const RecipeGraph>> graphs_;
  GeometryInputs geometry_;
  RenderExecution<RenderValue, RenderScratch> execution_;
};
}
