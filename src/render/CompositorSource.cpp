#include "render/Compositor.h"

#include "engine/MeshReader.h"
#include "mesh/MaterialClusters.h"
#include "mesh/Mesh.h"
#include "recipe/Expression.h"
#include "recipe/Recipe.h"
#include "recipe/Signals.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>

namespace BetterEnchantmentEffects {
namespace {
std::string Lower(std::string_view a_text) {
  std::string out{a_text};
  for (auto &c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

bool RealTexture(const RE::NiPointer<RE::NiSourceTexture> &a_texture) {
  const auto extent = TextureLab::ExtentOf(a_texture.get());
  return extent && extent->width > 4 && extent->height > 4;
}

std::string
DescribeTexture(const RE::NiPointer<RE::NiSourceTexture> &a_texture) {
  if (!a_texture) {
    return "the material has no texture in this slot";
  }
  const auto *data = reinterpret_cast<const RE::NiTexture::RendererData *>(
      a_texture->rendererTexture);
  const char *name = a_texture->name.c_str() ? a_texture->name.c_str() : "";
  if (!data) {
    return std::format("'{}' is not resident (no renderer data)", name);
  }
  if (!data->resourceView) {
    return std::format("'{}' has no shader resource view", name);
  }
  const auto extent = TextureLab::ExtentOf(a_texture.get());
  if (!extent) {
    return std::format("'{}' is not a 2D texture", name);
  }
  return std::format("'{}' is {}x{}, a placeholder", name, extent->width,
                     extent->height);
}

RE::NiPointer<RE::NiSourceTexture> MapOf(MaterialMap a_map,
                                         const MaterialInputs &a_material) {
  switch (a_map) {
  case MaterialMap::kDiffuse:
    return a_material.diffuse;
  case MaterialMap::kNormal:
    return a_material.normal;
  case MaterialMap::kRmaos:
    return a_material.rmaos;
  case MaterialMap::kDisplacement:
    return a_material.displacement;
  case MaterialMap::kNone:
    return nullptr;
  }
  return nullptr;
}

bool MeasureFlatDisplacement(
    const RE::NiPointer<RE::NiSourceTexture> &a_displacement) {
  if (!RealTexture(a_displacement)) {
    return true;
  }
  const float mean = TextureLab::GetSingleton()->MeanChannel(
      a_displacement.get(), ShaderChannel::kR);
  return mean <= 0.02f || mean >= 0.98f;
}

std::shared_ptr<TextureLab::RenderTarget>
RenderNormalSlope(const MaterialInputs &a_material, std::string &a_problem) {
  if (!RealTexture(a_material.normal)) {
    a_problem = "normal map: " + DescribeTexture(a_material.normal);
    return nullptr;
  }
  const auto extent = TextureLab::ExtentOf(a_material.normal.get());
  auto *lab = TextureLab::GetSingleton();
  auto target = lab->Acquire(
      TextureSize(extent ? std::max(extent->width, extent->height) : 0));
  if (!target) {
    a_problem = "no render target for the normal slope";
    return nullptr;
  }
  TextureLab::LayerParams params;
  params.mode = TextureLab::Mode::kChannel;
  params.map = {a_material.normal.get(), TextureLab::MapReading::kNormalSlope};
  params.channel.slope = true;
  if (!lab->Render(*target, nullptr, params)) {
    a_problem = "the normal slope pass failed";
    return nullptr;
  }
  return target;
}

std::shared_ptr<TextureLab::RenderTarget>
RenderClusterMap(const GeometryInputs &a_inputs,
                 const ClusterSettings &a_settings) {
  const MaterialInputs &material = a_inputs.material;
  DerivedMaps &derived = *a_inputs.derived;
  if (derived.clusters && derived.clusterSettings == a_settings) {
    return derived.clusters;
  }
  derived.clustersTried = true;
  derived.clusters = nullptr;
  derived.clusterSettings = a_settings;
  derived.clustersProblem.clear();
  const auto &record = Compositor::GetSingleton()->AnalyseMaterial(material);
  if (!record.sample || !record.analysis) {
    derived.clustersProblem = record.problem.empty()
                                  ? "the material could not be sampled"
                                  : record.problem;
    return nullptr;
  }
  const bool defaults = record.analysis->settings == a_settings;
  const MaterialAnalysis analysis =
      defaults ? *record.analysis : ClusterMaterial(*record.sample, a_settings);
  if (analysis.clusters.empty()) {
    derived.clustersProblem = "the material sample clustered into nothing";
    return nullptr;
  }
  const auto extent = TextureLab::ExtentOf(material.rmaos.get());
  auto *lab = TextureLab::GetSingleton();
  auto target = lab->Acquire(
      TextureSize(extent ? std::max(extent->width, extent->height) : 0));
  if (!target) {
    derived.clustersProblem = "no render target for the cluster map";
    return nullptr;
  }
  if (!lab->RenderClusters(*target, material.rmaos.get(),
                           material.diffuse.get(), analysis)) {
    derived.clustersProblem = lab->ClassifyAvailable()
                                  ? "the classify pass failed"
                                  : "the classify pass is unavailable";
    return nullptr;
  }
  derived.clusters = target;
  return target;
}

struct MaterialChannelPick {
  RE::NiPointer<RE::NiSourceTexture> texture;
  ShaderChannel channel = ShaderChannel::kRgb;
  std::string problem;
};

MaterialChannelPick PickMaterialChannel(MaterialChannel a_channel,
                                        const GeometryInputs &a_inputs,
                                        bool a_mayRender) {
  const MaterialInputs &a_material = a_inputs.material;
  const auto map = MaterialMapOf(a_channel);
  if (map != MaterialMap::kNone) {
    return {MapOf(map, a_material), ShaderChannelOf(a_channel), {}};
  }
  switch (a_channel) {
  case MaterialChannel::kRelief:
    if (!a_material.flatDisplacement) {
      return {a_material.displacement,
              ShaderChannelOf(MaterialChannel::kDisplacement),
              {}};
    }
    return {a_material.rmaos, ShaderChannelOf(MaterialChannel::kOcclusion), {}};
  case MaterialChannel::kNormalSlope: {
    auto &derived = *a_inputs.derived;
    if (!derived.normalSlope && a_mayRender && !derived.tried) {
      derived.tried = true;
      derived.normalSlope = RenderNormalSlope(a_material, derived.problem);
    }
    if (derived.normalSlope) {
      return {
          RE::NiPointer<RE::NiSourceTexture>{derived.normalSlope->Texture()},
          ShaderChannelOf(a_channel),
          {}};
    }
    return {nullptr, ShaderChannelOf(a_channel),
            derived.tried ? derived.problem
                          : std::string{Compositor::kNotRendered}};
  }
  default:
    return {nullptr, ShaderChannel::kR, "unknown channel"};
  }
}

std::optional<MaterialChannel> SingleChannelOf(const Recipe &a_recipe,
                                               const Mask &a_mask) {
  const auto program = Program::Parse(a_mask.text);
  const auto *source =
      program && program->OpCount() == 1 && program->References().size() == 1
          ? a_recipe.FindSource(program->References()[0])
          : nullptr;
  const auto *channel = source ? Get<MaterialSource>(source->kind) : nullptr;
  return channel ? std::optional{channel->channel} : std::nullopt;
}

template <class T>
std::shared_ptr<T>
LargestOf(const std::unordered_map<std::string, std::shared_ptr<T>> &a_cache,
          std::string_view a_definition) {
  std::shared_ptr<T> best;
  std::uint32_t bestSize = 0;
  for (const auto &[key, value] : a_cache) {
    if (KeyDefinition(key) != a_definition) {
      continue;
    }
    const auto size = KeySize(key).value_or(0);
    if (!best || size > bestSize) {
      best = value;
      bestSize = size;
    }
  }
  return best;
}

std::shared_ptr<RenderedMask> CachedMask(const GeometryInputs &a_inputs,
                                         const Recipe &a_recipe,
                                         std::string_view a_name) {
  return a_inputs.masks
             ? LargestRecipeTexture(*a_inputs.masks, a_recipe.id, a_name)
             : nullptr;
}

std::shared_ptr<TextureLab::RenderTarget>
CachedBake(const MeshEntry *a_entry, std::string_view a_definition) {
  return a_entry ? LargestOf(a_entry->bakes, a_definition) : nullptr;
}

TextureLab::LayerInput SamplingNow(const PreparedSource &a_source,
                                   const SignalState &a_signals) {
  auto input = a_source.sampling;
  if (a_source.scroll) {
    const auto scroll = a_signals.Resolve(*a_source.scroll);
    input.transform.uOffset = scroll.x;
    input.transform.vOffset = scroll.y;
  }
  if (a_source.tile) {
    const auto tile = a_signals.Resolve(*a_source.tile);
    input.transform.tileU = std::max(tile.x, 0.01f);
    input.transform.tileV = std::max(tile.y, 0.01f);
  }
  return input;
}
}

MaterialInputs MaterialInputs::From(const PbrMaterial &a_material) {
  MaterialInputs in;
  if (!a_material.Attached()) {
    return in;
  }
  in.diffuse = a_material.material_->diffuseTexture;
  in.normal = a_material.material_->normalTexture;
  in.rmaos = a_material.material_->rmaosTexture;
  in.displacement = a_material.material_->displacementTexture;
  in.flatDisplacement = MeasureFlatDisplacement(in.displacement);
  return in;
}

RE::NiSourceTexture *RenderedRipple::Texture() const noexcept {
  return target_ ? target_->Texture() : nullptr;
}

RE::NiSourceTexture *RenderedMask::Texture() const noexcept {
  return target_ ? target_->Texture() : nullptr;
}

bool RenderedMask::Animated() const noexcept { return animated_; }

bool RenderedMask::Vector() const noexcept { return vector_; }

const std::string &RenderedMask::Problem() const noexcept { return problem_; }

std::optional<PreparedSource>
Compositor::PrepareSource(const Recipe &a_recipe, const Ref &a_ref,
                          const GeometryInputs &a_inputs, TextureSize a_size,
                          std::vector<Diagnostic> &a_out,
                          const std::string &a_where, std::uint32_t a_depth) {
  const Reporter report{a_out, a_where};
  if (a_recipe.FindMask(a_ref.name)) {
    auto rendered =
        PrepareRenderedMask(a_recipe, a_ref.name, a_inputs, a_size, a_depth);
    PreparedSource prepared;
    if (!rendered || !rendered->Problem().empty()) {
      prepared.problem =
          rendered ? rendered->Problem() : "mask could not be prepared";
      report.Warn(std::format("'@{}': {}; the active layer cannot be rendered",
                              a_ref.name, prepared.problem));
      return prepared;
    }
    prepared.texture = RE::NiPointer<RE::NiSourceTexture>{rendered->Texture()};
    prepared.sampling.channel =
        rendered->Vector() ? ShaderChannel::kRgb : ShaderChannel::kR;
    prepared.sampling.meshSpace = true;
    prepared.animated = rendered->Animated();
    prepared.rendered = std::move(rendered);
    return prepared;
  }
  const auto *source = a_recipe.FindSource(a_ref.name);
  if (!source) {
    report.Error(std::format("source '@{}' not found", a_ref.name));
    return std::nullopt;
  }
  PreparedSource prepared;
  prepared.animated = IsAnimated(a_recipe, *source);
  Match(
      source->kind,
      [&](const ImageSource &image) {
        prepared.texture = LoadImage(image.path);
        if (!prepared.texture) {
          prepared.problem = std::format("image '{}' did not load", image.path);
        }
        prepared.sampling.channel = ShaderChannelOf(image.channel);
        prepared.sampling.meshSpace = image.space == ImageSpace::kMesh;
        prepared.sampling.transform.mirrorU = image.mirror[0];
        prepared.sampling.transform.mirrorV = image.mirror[1];
        prepared.sampling.transform.transpose = image.transpose;
        prepared.sampling.transform.sourceMip = image.mip;
        prepared.scroll = image.scroll;
        prepared.tile = image.tile;
        if (prepared.texture && image.channel == ImageChannel::kRgb) {
          const float mean =
              TextureLab::GetSingleton()->MeanLuminance(prepared.texture.get());
          prepared.normalize = 0.5f / std::max(mean, 0.05f);
        }
      },
      [&](const MaterialSource &material) {
        auto pick = PickMaterialChannel(material.channel, a_inputs, true);
        prepared.texture = pick.texture;
        prepared.sampling.channel = pick.channel;
        prepared.sampling.meshSpace = true;
        prepared.problem = pick.problem;
        if (prepared.problem.empty() && !RealTexture(prepared.texture)) {
          prepared.problem = DescribeTexture(prepared.texture);
        }
      },
      [&](const BakeSource &bake) {
        auto target = PrepareBake(bake, a_inputs, a_size);
        if (!target) {
          prepared.problem = target.error();
          return;
        }
        prepared.texture =
            RE::NiPointer<RE::NiSourceTexture>{(*target)->Texture()};
        prepared.sampling.channel =
            Is<PositionBake>(bake.bake) || Is<LocalPositionBake>(bake.bake)
                ? ShaderChannel::kRgb
                : ShaderChannel::kR;
        prepared.sampling.meshSpace = true;
      },
      [&](const DistanceSource &distance) {
        auto target = PrepareDistance(distance, a_inputs, a_size);
        if (!target) {
          prepared.problem = target.error();
          return;
        }
        prepared.texture =
            RE::NiPointer<RE::NiSourceTexture>{(*target)->Texture()};
        prepared.sampling.channel = ShaderChannel::kR;
        prepared.sampling.meshSpace = true;
      },
      [&](const RippleSource &ripple) {
        const RecipeTextureKey key{a_recipe.id, source->name, a_size};
        auto rendered = PrepareRipple(key, ripple, a_inputs);
        if (!rendered) {
          prepared.problem = rendered.error();
          return;
        }
        prepared.texture =
            RE::NiPointer<RE::NiSourceTexture>{(*rendered)->Texture()};
        prepared.sampling.channel = ShaderChannel::kR;
        prepared.sampling.meshSpace = true;
        prepared.animated = true;
        prepared.ripple = std::move(*rendered);
      },
      [&](const UvSource &uv) {
        const auto entry = MeshOf(a_inputs.geometry.get());
        if (!entry) {
          prepared.problem = entry.error();
          return;
        }
        auto target = BakeInto(**entry, UvKeyOf(uv.axis, a_size), a_size, [&] {
          return BuildUvBake(*(*entry)->mesh, uv.axis);
        });
        if (!target) {
          prepared.problem = target.error();
          return;
        }
        prepared.texture =
            RE::NiPointer<RE::NiSourceTexture>{(*target)->Texture()};
        prepared.sampling.channel = ShaderChannel::kR;
        prepared.sampling.meshSpace = true;
      },
      [&](const MaterialClustersSource &clusters) {
        const auto target = RenderClusterMap(a_inputs, SettingsOf(clusters));
        if (!target) {
          prepared.problem = a_inputs.derived->clustersProblem;
          return;
        }
        prepared.texture =
            RE::NiPointer<RE::NiSourceTexture>{target->Texture()};
        prepared.sampling.channel = ShaderChannel::kR;
        prepared.sampling.meshSpace = true;
      });
  if (!prepared.problem.empty()) {
    report.Warn(std::format("'@{}': {}; the active layer cannot be rendered",
                            a_ref.name, prepared.problem));
  }
  return prepared;
}

std::optional<PreparedMask>
Compositor::PrepareMask(const Recipe &a_recipe, const Ref &a_ref,
                        const GeometryInputs &a_inputs, TextureSize a_size,
                        std::vector<Diagnostic> &a_out,
                        const std::string &a_where) {
  const Reporter report{a_out, a_where};
  const auto *mask = a_recipe.FindMask(a_ref.name);
  if (!mask) {
    report.Error(std::format("mask '@{}' not found", a_ref.name));
    return std::nullopt;
  }
  PreparedMask prepared;
  prepared.animated = IsAnimated(a_recipe, *mask);
  if (const auto channel = SingleChannelOf(a_recipe, *mask)) {
    auto pick = PickMaterialChannel(*channel, a_inputs, true);
    if (!pick.problem.empty() || !RealTexture(pick.texture)) {
      prepared.problem = pick.problem.empty() ? DescribeTexture(pick.texture) +
                                                    "; mask cannot be rendered"
                                              : pick.problem;
      report.Warn(std::format("mask '@{}': {}", a_ref.name, prepared.problem));
      return prepared;
    }
    prepared.texture = pick.texture;
    prepared.channel = pick.channel;
    return prepared;
  }
  auto rendered =
      PrepareRenderedMask(a_recipe, a_ref.name, a_inputs, a_size, 0);
  if (!rendered || !rendered->Problem().empty()) {
    prepared.problem =
        (rendered ? rendered->Problem() : "mask could not be prepared") +
        "; mask cannot be rendered";
    report.Warn(std::format("mask '@{}': {}", a_ref.name, prepared.problem));
    return prepared;
  }
  prepared.texture = RE::NiPointer<RE::NiSourceTexture>{rendered->Texture()};
  prepared.channel = ShaderChannel::kR;
  prepared.animated = rendered->Animated();
  prepared.rendered = std::move(rendered);
  return prepared;
}

std::shared_ptr<TextureLab::Lookup>
Compositor::BakeCurve(const Recipe &a_recipe, const CurveRef &a_curve,
                      const std::optional<PreparedSource> &a_source,
                      std::vector<Diagnostic> &a_out,
                      const std::string &a_where) {
  const Reporter report{a_out, a_where};
  std::string text = a_curve.text;
  if (const auto name = a_curve.Named()) {
    const auto *curve = a_recipe.FindCurve(*name);
    if (!curve) {
      report.Error(std::format("curve '@{}' not found; ignored", *name));
      return nullptr;
    }
    text = curve->text;
  }
  const auto program = ParseCurve(text);
  if (!program) {
    report.Error(std::format("curve: {}; ignored", program.error()));
    return nullptr;
  }
  float mean = 0.5f;
  if (a_source && a_source->texture) {
    auto *lab = TextureLab::GetSingleton();
    const auto channel = a_source->sampling.channel;
    mean = channel == ShaderChannel::kRgb || channel == ShaderChannel::kLuma
               ? lab->MeanLuminance(a_source->texture.get())
               : lab->MeanChannel(a_source->texture.get(), channel);
  }
  std::array<float, 256> values{};
  for (std::size_t i = 0; i < values.size(); ++i) {
    values[i] = ApplyCurve(*program, static_cast<float>(i) / 255.0f, mean);
  }
  auto lookup = TextureLab::GetSingleton()->CreateLookup(values);
  if (!lookup) {
    report.Warn("curve lookup could not be created; ignored");
  }
  return lookup;
}

std::optional<PreparedSource>
Compositor::InspectSource(const Recipe &a_recipe, std::string_view a_name,
                          const GeometryInputs &a_inputs) const {
  if (a_recipe.FindMask(a_name)) {
    PreparedSource prepared;
    if (auto rendered = CachedMask(a_inputs, a_recipe, a_name)) {
      prepared.texture =
          RE::NiPointer<RE::NiSourceTexture>{rendered->Texture()};
      prepared.sampling.channel =
          rendered->Vector() ? ShaderChannel::kRgb : ShaderChannel::kR;
      prepared.sampling.meshSpace = true;
      prepared.animated = rendered->Animated();
      prepared.problem = rendered->Problem();
      prepared.rendered = std::move(rendered);
    } else {
      prepared.problem = kNotRendered;
    }
    return prepared;
  }
  const auto *source = a_recipe.FindSource(a_name);
  if (!source) {
    return std::nullopt;
  }
  const auto entry = CachedMesh(a_inputs.geometry.get());
  PreparedSource prepared;
  prepared.animated = IsAnimated(a_recipe, *source);
  const auto notRendered = [&] {
    prepared.problem = entry && !entry->mesh && !entry->problem.empty()
                           ? "the mesh could not be read: " + entry->problem
                           : std::string{kNotRendered};
  };
  const auto baked = [&](const std::string &a_definition,
                         ShaderChannel a_channel) {
    prepared.sampling.meshSpace = true;
    if (const auto target = CachedBake(entry.get(), a_definition)) {
      prepared.texture = RE::NiPointer<RE::NiSourceTexture>{target->Texture()};
      prepared.sampling.channel = a_channel;
    } else {
      notRendered();
    }
  };
  Match(
      source->kind,
      [&](const ImageSource &image) {
        const auto loaded = images_.find(Lower(image.path));
        if (loaded == images_.end()) {
          prepared.problem = kNotRendered;
          return;
        }
        prepared.texture = loaded->second;
        if (!prepared.texture) {
          prepared.problem = std::format("image '{}' did not load", image.path);
        }
        prepared.sampling.channel = ShaderChannelOf(image.channel);
        prepared.sampling.meshSpace = image.space == ImageSpace::kMesh;
        prepared.sampling.transform.mirrorU = image.mirror[0];
        prepared.sampling.transform.mirrorV = image.mirror[1];
        prepared.sampling.transform.transpose = image.transpose;
        prepared.sampling.transform.sourceMip = image.mip;
        prepared.scroll = image.scroll;
        prepared.tile = image.tile;
      },
      [&](const MaterialSource &material) {
        auto pick = PickMaterialChannel(material.channel, a_inputs, false);
        prepared.texture = pick.texture;
        prepared.sampling.channel = pick.channel;
        prepared.sampling.meshSpace = true;
        prepared.problem = pick.problem;
        if (prepared.problem.empty() && !RealTexture(prepared.texture)) {
          prepared.problem = DescribeTexture(prepared.texture);
        }
      },
      [&](const BakeSource &bake) {
        baked(DefinitionOf(bake.bake),
              Is<PositionBake>(bake.bake) || Is<LocalPositionBake>(bake.bake)
                  ? ShaderChannel::kRgb
                  : ShaderChannel::kR);
      },
      [&](const DistanceSource &distance) {
        baked(DefinitionOf(distance), ShaderChannel::kR);
      },
      [&](const UvSource &uv) {
        baked(DefinitionOf(uv.axis), ShaderChannel::kR);
      },
      [&](const RippleSource &) {
        prepared.sampling.meshSpace = true;
        const auto rendered =
            a_inputs.ripples
                ? LargestRecipeTexture(*a_inputs.ripples, a_recipe.id, a_name)
                : nullptr;
        if (!rendered) {
          notRendered();
          return;
        }
        prepared.texture =
            RE::NiPointer<RE::NiSourceTexture>{rendered->Texture()};
        prepared.animated = true;
        prepared.ripple = rendered;
      },
      [&](const MaterialClustersSource &clusters) {
        prepared.sampling.meshSpace = true;
        const DerivedMaps &derived = *a_inputs.derived;
        if (derived.clusters &&
            derived.clusterSettings == SettingsOf(clusters)) {
          prepared.texture =
              RE::NiPointer<RE::NiSourceTexture>{derived.clusters->Texture()};
          return;
        }
        prepared.problem =
            derived.clustersTried && !derived.clustersProblem.empty()
                ? derived.clustersProblem
                : std::string{kNotRendered};
      });
  return prepared;
}

std::optional<PreparedMask>
Compositor::InspectMask(const Recipe &a_recipe, std::string_view a_name,
                        const GeometryInputs &a_inputs) const {
  const auto *mask = a_recipe.FindMask(a_name);
  if (!mask) {
    return std::nullopt;
  }
  PreparedMask prepared;
  prepared.animated = IsAnimated(a_recipe, *mask);
  if (auto rendered = CachedMask(a_inputs, a_recipe, a_name)) {
    prepared.texture = RE::NiPointer<RE::NiSourceTexture>{rendered->Texture()};
    prepared.animated = rendered->Animated();
    prepared.problem = rendered->Problem();
    prepared.rendered = std::move(rendered);
    return prepared;
  }
  if (const auto channel = SingleChannelOf(a_recipe, *mask)) {
    auto pick = PickMaterialChannel(*channel, a_inputs, false);
    if (!pick.problem.empty() || !RealTexture(pick.texture)) {
      prepared.problem = pick.problem.empty() ? DescribeTexture(pick.texture) +
                                                    "; mask cannot be rendered"
                                              : pick.problem;
      return prepared;
    }
    prepared.texture = pick.texture;
    prepared.channel = pick.channel;
    return prepared;
  }
  prepared.problem = kNotRendered;
  return prepared;
}

std::shared_ptr<RenderedMask>
Compositor::PrepareRenderedMask(const Recipe &a_recipe, std::string_view a_name,
                                const GeometryInputs &a_inputs,
                                TextureSize a_size, std::uint32_t a_depth) {
  constexpr std::uint32_t kMaxMaskDepth = 8;
  const auto *mask = a_recipe.FindMask(a_name);
  if (!mask || !a_inputs.masks) {
    return nullptr;
  }
  const RecipeTextureKey key{a_recipe.id, a_name, a_size};
  if (const auto it = a_inputs.masks->find(key); it != a_inputs.masks->end()) {
    return it->second;
  }
  auto rendered = std::make_shared<RenderedMask>();
  (*a_inputs.masks)[key] = rendered;
  auto &r = *rendered;
  const auto fail = [&](std::string a_problem) {
    r.problem_ = std::move(a_problem);
    r.program_.reset();
    r.target_.reset();
    return rendered;
  };
  if (a_depth > kMaxMaskDepth) {
    return fail(std::format("masks nest deeper than {}", kMaxMaskDepth));
  }
  auto program = Program::Parse(mask->text);
  if (!program) {
    return fail(program.error());
  }
  const auto graph = std::make_shared<const SignalGraph>(
      SignalGraph::Compile(a_recipe.signals, a_recipe.curves));

  std::vector<Diagnostic> ignored;
  for (const auto &name : program->References()) {
    RenderedMask::RefBinding binding;
    if (a_recipe.FindSource(name) || a_recipe.FindMask(name)) {
      if (r.textures_.size() >= TextureLab::kProgramTextures) {
        return fail(std::format("reads more than {} images",
                                TextureLab::kProgramTextures));
      }
      auto source = PrepareSource(a_recipe, Ref{name}, a_inputs, a_size,
                                  ignored, "mask", a_depth + 1);
      if (!source || !source->problem.empty()) {
        return fail(std::format("'@{}': {}", name,
                                source ? source->problem : "not found"));
      }
      if (source->rendered) {
        if (!source->rendered->program_ && source->rendered->problem_.empty()) {
          return fail(std::format("'@{}' reads back into this mask", name));
        }
        r.dependencies_.push_back(source->rendered);
      }
      binding.isTexture = true;
      binding.texture = static_cast<std::uint32_t>(r.textures_.size());
      r.animated_ = r.animated_ || source->animated;
      r.textures_.push_back(std::move(*source));
    } else if (graph && graph->Index(name)) {
      binding.signal = name;
      r.animated_ = true;
    } else {
      return fail(std::format("'@{}' is not a source, mask or signal", name));
    }
    r.refs_.push_back(std::move(binding));
    if (r.refs_.size() > TextureLab::kProgramRefs) {
      return fail(
          std::format("reads more than {} names", TextureLab::kProgramRefs));
    }
  }
  for (const auto &curveName : program->Curves()) {
    const auto *curve = a_recipe.FindCurve(curveName);
    if (!curve) {
      return fail(std::format("curve '@{}' not found", curveName));
    }
    const auto curveProgram = ParseCurve(curve->text);
    if (!curveProgram) {
      return fail(
          std::format("curve '@{}': {}", curveName, curveProgram.error()));
    }
    std::array<float, 256> values{};
    for (std::size_t i = 0; i < values.size(); ++i) {
      values[i] =
          ApplyCurve(*curveProgram, static_cast<float>(i) / 255.0f, 0.5f);
    }
    auto lookup = TextureLab::GetSingleton()->CreateLookup(values);
    if (!lookup) {
      return fail("curve lookup could not be created");
    }
    r.curves_.push_back(std::move(lookup));
    if (r.curves_.size() > TextureLab::kProgramCurves) {
      return fail(
          std::format("calls more than {} curves", TextureLab::kProgramCurves));
    }
  }
  const auto type =
      program->Check([&](std::string_view name) -> std::optional<ValueType> {
        if (const auto *source = a_recipe.FindSource(name)) {
          return SourceType(*source);
        }
        if (a_recipe.FindMask(name)) {
          const auto dep =
              FindRecipeTexture(*a_inputs.masks, a_recipe.id, name, a_size);
          return dep && dep->Vector() ? ValueType::kVec3 : ValueType::kScalar;
        }
        return graph ? graph->TypeOf(name) : std::nullopt;
      });
  if (!type) {
    return fail(type.error());
  }
  r.vector_ = *type != ValueType::kScalar;
  r.animated_ = r.animated_ || program->UsesTime();
  if (!TextureLab::GetSingleton()->InterpreterAvailable()) {
    return fail(
        "the interpreter shader did not compile (see the log at start)");
  }
  r.target_ = TextureLab::GetSingleton()->Acquire(a_size);
  if (!r.target_) {
    return fail("no render target available");
  }
  r.program_ = std::move(*program);
  return rendered;
}

std::expected<std::shared_ptr<RenderedRipple>, std::string>
Compositor::PrepareRipple(const RecipeTextureKey &a_key,
                          const RippleSource &a_ripple,
                          const GeometryInputs &a_inputs) {
  if (!a_inputs.ripples) {
    return std::unexpected("no geometry to ripple over");
  }
  const TextureSize size{a_key.pixels};
  if (const auto it = a_inputs.ripples->find(a_key);
      it != a_inputs.ripples->end()) {
    return it->second;
  }
  auto *lab = TextureLab::GetSingleton();
  if (!lab->Init() || !lab->RippleAvailable()) {
    return std::unexpected(
        "the ripple pass is unavailable (see the log at start)");
  }
  const auto entry = MeshOf(a_inputs.geometry.get());
  if (!entry) {
    return std::unexpected(entry.error());
  }
  auto positions =
      BakeInto(**entry, BakeKeyOf(PositionBake{}, size), size,
               [&] { return BuildBake(*(*entry)->mesh, PositionBake{}); });
  if (!positions) {
    return std::unexpected(positions.error());
  }
  auto ripple = std::make_shared<RenderedRipple>();
  ripple->target_ = lab->Acquire(size);
  if (!ripple->target_) {
    return std::unexpected("no render target available");
  }
  ripple->positions_ = *positions;
  ripple->source_ = a_ripple;
  ripple->fallbackOrigin_ = (*entry)->mesh->center;
  ripple->geometry_ = a_inputs.geometry;
  ripple->root_ = a_inputs.root;
  TextureLab::RipplePass clear;
  clear.positions = ripple->positions_->Texture();
  if (!lab->RenderRipple(*ripple->target_, clear)) {
    return std::unexpected("the ripple target could not be initialized");
  }
  (*a_inputs.ripples)[a_key] = ripple;
  return ripple;
}

bool Compositor::RenderRipple(RenderedRipple &a_ripple,
                              const SignalState &a_signals, float a_time) {
  if (!a_ripple.target_ || !a_ripple.positions_) {
    return false;
  }
  if (a_ripple.renderedTick_ == tick_) {
    return true;
  }
  TextureLab::RipplePass pass;
  pass.positions = a_ripple.positions_->Texture();
  pass.frame = kPositionFrame;
  pass.speed = a_signals.Resolve(a_ripple.source_.speed);
  pass.width = a_signals.Resolve(a_ripple.source_.width);
  pass.decay = a_signals.Resolve(a_ripple.source_.decay);
  pass.disc = a_ripple.source_.shape == RippleShape::kDisc;
  for (const auto &firing : a_signals.Firings(a_ripple.source_.trigger.name)) {
    if (pass.firingCount >= pass.firings.size()) {
      break;
    }
    std::optional<Vec3> origin;
    if (firing.payload.position) {
      origin = ToRootSpace(a_ripple.root_.get(), *firing.payload.position);
    } else if (!firing.payload.node.empty()) {
      origin = NodeBindPosition(a_ripple.geometry_.get(), a_ripple.root_.get(),
                                firing.payload.node);
    }
    pass.firings[pass.firingCount++] = {
        origin.value_or(a_ripple.fallbackOrigin_),
        std::max(0.0f, a_time - firing.startTime)};
  }
  if (pass.firingCount == 0 && !a_ripple.hadFirings_) {
    return true;
  }
  if (!TextureLab::GetSingleton()->RenderRipple(*a_ripple.target_, pass)) {
    return false;
  }
  a_ripple.hadFirings_ = pass.firingCount > 0;
  a_ripple.renderedTick_ = tick_;
  return true;
}

bool Compositor::RenderMask(RenderedMask &a_mask, const SignalState &a_signals,
                            float a_time) {
  if (!a_mask.program_ || !a_mask.target_ || !a_mask.problem_.empty()) {
    return false;
  }
  if (a_mask.renderedOnce_ &&
      (!a_mask.animated_ || a_mask.renderedTick_ == tick_)) {
    return true;
  }
  for (const auto &dependency : a_mask.dependencies_) {
    if (dependency && !RenderMask(*dependency, a_signals, a_time)) {
      return false;
    }
  }
  for (const auto &texture : a_mask.textures_) {
    if (texture.ripple && !RenderRipple(*texture.ripple, a_signals, a_time)) {
      return false;
    }
  }
  TextureLab::ProgramPass pass;
  pass.code = a_mask.program_->Code();
  for (const auto &binding : a_mask.refs_) {
    if (pass.refCount >= pass.refs.size()) {
      return false;
    }
    TextureLab::ProgramRef ref;
    ref.isTexture = binding.isTexture;
    ref.texture = binding.texture;
    if (!binding.isTexture) {
      ref.value = AsVec3(a_signals.ValueOf(binding.signal));
    }
    pass.refs[pass.refCount++] = ref;
  }
  for (const auto &source : a_mask.textures_) {
    if (pass.textureCount >= pass.textures.size()) {
      return false;
    }
    pass.textures[pass.textureCount++] = {
        source.texture.get(), SamplingNow(source, a_signals), source.normalize};
  }
  for (const auto &curve : a_mask.curves_) {
    if (pass.curveCount >= pass.curves.size()) {
      return false;
    }
    pass.curves[pass.curveCount++] = curve.get();
  }
  pass.time = a_time;
  pass.vectorResult = a_mask.vector_;
  if (TextureLab::GetSingleton()->RenderProgram(*a_mask.target_, pass)) {
    a_mask.renderedOnce_ = true;
    a_mask.renderedTick_ = tick_;
    return true;
  }
  return false;
}
}
