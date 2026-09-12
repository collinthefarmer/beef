#include "render/Compositor.h"

#include <algorithm>
#include <cctype>

namespace BetterEnchantmentEffects {
namespace {
std::string Lower(std::string_view a_text) {
  std::string out{a_text};
  for (char &c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

bool RealTexture(const RE::NiPointer<RE::NiSourceTexture> &a_texture) {
  const std::optional<TextureLab::Extent> extent =
      TextureLab::ExtentOf(a_texture.get());
  return extent && extent->width > 4 && extent->height > 4;
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

RE::NiPointer<RE::NiSourceTexture>
BaseMapFor(Slot a_slot, const MaterialInputs &a_material) {
  return MapOf(BaseMapOf(a_slot), a_material);
}

TextureSize SizeOverBase(TextureSize a_size, TextureSize a_maxSize,
                         std::uint32_t a_largestSide) {
  const std::uint32_t floor = a_size.Pixels();
  const std::uint32_t ceiling = std::max(a_maxSize.Pixels(), floor);
  return TextureSize(std::clamp(a_largestSide, floor, ceiling));
}

std::uint32_t ChannelBits(const ChannelSet &a_set) {
  return (a_set.r ? 1u : 0u) | (a_set.g ? 2u : 0u) | (a_set.b ? 4u : 0u) |
         (a_set.a ? 8u : 0u);
}

TextureLab::LayerInput SamplingNow(const PreparedSource &a_source,
                                   const SignalState &a_signals) {
  TextureLab::LayerInput input = a_source.sampling;
  if (a_source.scroll) {
    const Vec2 scroll = a_signals.Resolve(*a_source.scroll);
    input.transform.uOffset = scroll.x;
    input.transform.vOffset = scroll.y;
  }
  if (a_source.tile) {
    const Vec2 tile = a_signals.Resolve(*a_source.tile);
    input.transform.tileU = std::max(tile.x, 0.01f);
    input.transform.tileV = std::max(tile.y, 0.01f);
  }
  return input;
}
}

bool LayerFilter::Hides(std::size_t a_index) const noexcept {
  for (const std::size_t h : hidden) {
    if (h == a_index) {
      return true;
    }
  }
  return false;
}

RE::NiSourceTexture *RenderedStack::Texture() const noexcept {
  return latest_ ? latest_->Texture() : nullptr;
}

bool RenderedStack::Animated() const noexcept { return animated_; }

TextureSize RenderedStack::Size() const noexcept { return size_; }

std::span<const PreparedLayer> RenderedStack::Layers() const noexcept {
  return layers_;
}

std::span<const Diagnostic> RenderedStack::Diagnostics() const noexcept {
  return diagnostics_;
}

Compositor *Compositor::GetSingleton() {
  static Compositor compositor;
  return &compositor;
}

void Compositor::BeginTick(std::uint32_t a_nowMS) noexcept {
  ++tick_;
  nowMS_ = a_nowMS;
}

RE::NiPointer<RE::NiSourceTexture>
Compositor::LoadImage(std::string_view a_path) {
  if (a_path.empty()) {
    return nullptr;
  }
  const std::string key = Lower(a_path);
  if (const auto it = images_.find(key); it != images_.end()) {
    return it->second;
  }
  RE::NiPointer<RE::NiTexture> texture;
  RE::BSShaderManager::GetTexture(std::string{a_path}.c_str(), true, texture,
                                  false);
  RE::NiSourceTexture *source =
      texture ? netimmerse_cast<RE::NiSourceTexture *>(texture.get()) : nullptr;
  if (!source || !source->rendererTexture) {
    const std::string prefixed = "textures\\" + std::string{a_path};
    RE::BSShaderManager::GetTexture(prefixed.c_str(), true, texture, false);
    source = texture ? netimmerse_cast<RE::NiSourceTexture *>(texture.get())
                     : nullptr;
  }
  RE::NiPointer<RE::NiSourceTexture> result{
      source && source->rendererTexture ? source : nullptr};
  images_[key] = result;
  return result;
}

std::shared_ptr<TextureLab::RenderTarget> Compositor::NeutralHeight() {
  if (neutralHeight_ && neutralHeight_->Texture()) {
    return neutralHeight_;
  }
  TextureLab *lab = TextureLab::GetSingleton();
  std::shared_ptr<TextureLab::RenderTarget> target =
      lab->Acquire(TextureSize(64));
  if (!target) {
    return nullptr;
  }
  TextureLab::LayerParams params;
  params.mode = TextureLab::Mode::kLayer;
  params.layer.color[0] = params.layer.color[1] = params.layer.color[2] = 0.5f;
  params.layer.opacity = 1.0f;
  params.layer.blend = 0;
  if (!lab->Render(*target, nullptr, params)) {
    return nullptr;
  }
  neutralHeight_ = std::move(target);
  return neutralHeight_;
}

std::unique_ptr<RenderedStack>
Compositor::Prepare(const Recipe &a_recipe, const SurfaceOutput &a_output,
                    const GeometryInputs &a_inputs, TextureSize a_size,
                    TextureSize a_maxSize) {
  const MaterialInputs &material = a_inputs.material;
  TextureLab *lab = TextureLab::GetSingleton();
  if (!lab->Init()) {
    return nullptr;
  }
  auto stack = std::make_unique<RenderedStack>();
  stack->size_ = a_size;
  if (a_output.slot == Slot::kHeight && material.flatDisplacement) {
    stack->neutral_ = NeutralHeight();
    if (stack->neutral_ && stack->neutral_->Texture()) {
      stack->base_ = RE::NiPointer{stack->neutral_->Texture()};
    } else {
      stack->diagnostics_.push_back(
          {Severity::kWarning, "stack",
           "the neutral height base could not be rendered; the stack starts "
           "from black"});
    }
  } else if (const RE::NiPointer<RE::NiSourceTexture> base =
                 BaseMapFor(a_output.slot, material);
             RealTexture(base)) {
    stack->base_ = base;
    const std::optional<TextureLab::Extent> extent =
        TextureLab::ExtentOf(base.get());
    const std::uint32_t largest =
        extent ? std::max(extent->width, extent->height) : 0u;
    stack->size_ = SizeOverBase(a_size, a_maxSize, largest);
  }
  stack->animated_ = IsAnimated(a_recipe, Output{a_output});
  const TextureSize size = stack->size_;
  std::size_t index = 0;
  for (const Layer &layer : a_output.stack) {
    const std::string where = std::format("layer {}", index);
    PreparedLayer prepared;
    prepared.layer = &layer;
    prepared.index = index++;
    if (const Ref *ref = Get<Ref>(layer.source)) {
      prepared.source = PrepareSource(a_recipe, *ref, a_inputs, size,
                                      stack->diagnostics_, where);
      if (!prepared.source || !prepared.source->problem.empty()) {
        stack->layers_.push_back(std::move(prepared));
        continue;
      }
    }
    if (layer.curve) {
      prepared.curve = BakeCurve(a_recipe, *layer.curve, prepared.source,
                                 stack->diagnostics_, where);
    }
    if (layer.mask) {
      prepared.mask = PrepareMask(a_recipe, *layer.mask, a_inputs, size,
                                  stack->diagnostics_, where);
    }
    stack->layers_.push_back(std::move(prepared));
  }
  if (!stack->layers_.empty()) {
    stack->target_ = lab->Acquire(size);
    if (!stack->target_ || !lab->Scratch(size)) {
      stack->diagnostics_.push_back(
          {Severity::kError, "stack", "no render targets available"});
      stack->preparationFailed_ = true;
      stack->layers_.clear();
      stack->target_.reset();
    }
  }
  return stack;
}

bool Compositor::Render(RenderedStack &a_stack, const SignalState &a_signals,
                        float a_time, const LayerFilter &a_filter,
                        const StackBase &a_base) {
  if (a_stack.preparationFailed_) {
    return false;
  }
  if (a_stack.layers_.empty()) {
    return true;
  }
  RE::NiSourceTexture *base =
      a_base.texture ? a_base.texture : a_stack.base_.get();
  const bool filterChanged = a_stack.filter_ != a_filter;
  const bool baseChanged = a_stack.renderedBase_ != base;
  if (!a_stack.animated_ && !a_base.animated && a_stack.renderedOnce_ &&
      !filterChanged && !baseChanged) {
    return true;
  }
  a_stack.renderedOnce_ = false;
  std::size_t shown = 0;
  for (const PreparedLayer &prepared : a_stack.layers_) {
    if (a_filter.Hides(prepared.index)) {
      continue;
    }
    ++shown;
    if (!prepared.layer ||
        (Is<Ref>(prepared.layer->source) &&
         (!prepared.source || !prepared.source->problem.empty() ||
          !prepared.source->texture)) ||
        (prepared.layer->mask &&
         (!prepared.mask || !prepared.mask->problem.empty() ||
          !prepared.mask->texture))) {
      return false;
    }
    if (prepared.source && prepared.source->ripple) {
      if (!RenderRipple(*prepared.source->ripple, a_signals, a_time)) {
        return false;
      }
    }
    if (prepared.source && prepared.source->rendered) {
      if (!RenderMask(*prepared.source->rendered, a_signals, a_time)) {
        return false;
      }
    }
    if (prepared.mask && prepared.mask->rendered) {
      if (!RenderMask(*prepared.mask->rendered, a_signals, a_time)) {
        return false;
      }
    }
  }
  if (shown == 0) {
    a_stack.latest_ = nullptr;
    a_stack.renderedOnce_ = true;
    a_stack.filter_ = a_filter;
    a_stack.renderedBase_ = base;
    return true;
  }
  TextureLab *lab = TextureLab::GetSingleton();
  TextureLab::RenderTarget *own = a_stack.target_.get();
  TextureLab::RenderTarget *scratch = lab->Scratch(a_stack.size_);
  if (!own || !scratch) {
    return false;
  }
  TextureLab::RenderTarget *previous = nullptr;
  TextureLab::RenderTarget *write = shown % 2 == 1 ? own : scratch;
  TextureLab::RenderTarget *other = write == own ? scratch : own;
  for (const PreparedLayer &prepared : a_stack.layers_) {
    if (a_filter.Hides(prepared.index)) {
      continue;
    }
    const Layer &layer = *prepared.layer;
    TextureLab::LayerParams params;
    params.mode = TextureLab::Mode::kLayer;
    TextureLab::LayerPass &pass = params.layer;
    pass.previous = previous ? previous->Texture() : base;
    if (prepared.source) {
      pass.source = prepared.source->texture.get();
      pass.input = SamplingNow(*prepared.source, a_signals);
      pass.normalize = prepared.source->normalize;
    } else if (const Vec3 *constant = Get<Vec3>(layer.source)) {
      pass.color[0] = constant->x;
      pass.color[1] = constant->y;
      pass.color[2] = constant->z;
    }
    if (layer.color) {
      const Vec3 colour = a_signals.Resolve(*layer.color);
      pass.color[0] *= colour.x;
      pass.color[1] *= colour.y;
      pass.color[2] *= colour.z;
    }
    pass.opacity = a_signals.Resolve(layer.opacity);
    pass.blend = BlendShaderMode(layer.blend);
    pass.channels = ChannelBits(layer.channels);
    if (prepared.mask && prepared.mask->texture) {
      pass.mask = prepared.mask->texture.get();
      pass.maskChannel = prepared.mask->channel;
    }
    pass.curve = prepared.curve.get();
    if (!lab->Render(*write, nullptr, params)) {
      return false;
    }
    previous = write;
    std::swap(write, other);
  }
  a_stack.latest_ = previous;
  a_stack.renderedOnce_ = true;
  a_stack.filter_ = a_filter;
  a_stack.renderedBase_ = base;
  return true;
}
}
