// GPL-3.0-only with the additional permission in COPYING.md.
#include "render/Compositor.h"
#include "render/RenderInstance.h"
#include "render/SourceSampling.h"

namespace BetterEnchantmentEffects {
namespace {
bool MeasureFlatDisplacement(const TextureRef &texture) {
  if (!IsNonPlaceholderTexture(texture))
    return true;
  const float mean =
      TextureLab::GetSingleton()->MeanChannel(texture.get(), ShaderChannel::kR);
  return mean <= 0.02f || mean >= 0.98f;
}
}
MaterialInputs MaterialInputs::From(const PbrMaterial &a_material) {
  MaterialInputs in;
  if (!a_material.Attached()) {
    return in;
  }
  in.diffuse = a_material.layout_->diffuseTexture;
  in.normal = a_material.layout_->normalTexture;
  in.rmaos = a_material.layout_->rmaosTexture;
  in.displacement = a_material.layout_->displacementTexture;
  in.flatDisplacement = MeasureFlatDisplacement(in.displacement);
  return in;
}

std::optional<PreparedSource>
Compositor::InspectSource(const Recipe &recipe, const RecipeGraph &graph,
                          std::string_view name,
                          const GeometryInputs &inputs) const {
  const auto node = graph.FindNodeIndex(name);
  if (!node || (!recipe.FindSource(name) && !recipe.FindMask(name)))
    return std::nullopt;
  PreparedSource prepared;
  prepared.animated = graph.MayChangeOverTime(name);
  if (inputs.render)
    if (const auto view =
            inputs.render->Inspect(graph, {*node, 0}, inputs.applicationContext,
                                   inputs.geometry.get())) {
      prepared.texture = view->texture;
      prepared.sampling = view->sampling;
      prepared.normalize = view->normalize;
      return prepared;
    }
  prepared.problem = kNotRendered;
  return prepared;
}
std::optional<PreparedMask>
Compositor::InspectMask(const Recipe &recipe, const RecipeGraph &graph,
                        std::string_view name,
                        const GeometryInputs &inputs) const {
  if (!recipe.FindMask(name))
    return std::nullopt;
  const auto source = InspectSource(recipe, graph, name, inputs);
  if (!source)
    return std::nullopt;
  return PreparedMask{source->texture, ShaderChannel::kR, source->animated,
                      source->problem};
}
}
