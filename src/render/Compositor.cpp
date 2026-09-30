// GPL-3.0-only with the additional permission in COPYING.md.
#include "render/Compositor.h"
#include "render/RenderInstance.h"
#include "render/SourceSampling.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <format>

namespace BetterEnchantmentEffects {
namespace {
TextureRef BaseMapFor(Slot a_slot, const MaterialInputs &a_material) {
  return MaterialTexture(BaseMapOf(a_slot), a_material);
}

TextureSize SizeOverBase(TextureSize a_size, TextureSize a_maxSize,
                         std::uint32_t a_largestSide) {
  const std::uint32_t floor = a_size.Pixels();
  const std::uint32_t ceiling = std::max(a_maxSize.Pixels(), floor);
  return TextureSize(std::clamp(a_largestSide, floor, ceiling));
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

RenderOutput::RenderOutput(const std::shared_ptr<RenderInstance> &render,
                           StepOutputRef result, TextureSize size,
                           bool animated)
    : render_(render), result_(result), size_(size), animated_(animated) {}

TextureRef RenderOutput::Texture() const noexcept { return latest_; }

std::uint64_t RenderOutput::ContentVersion() const noexcept {
  return latestVersion_;
}

bool RenderOutput::Animated() const noexcept { return animated_; }

TextureSize RenderOutput::Size() const noexcept { return size_; }

TextureRef RenderOutput::LayerTexture(std::size_t layer) const {
  const auto render = render_.lock();
  if (!render || result_.step >= render->Plan().steps.size())
    return {};
  const auto *stack =
      Get<CompositeStackStep>(render->Plan().steps[result_.step].kind);
  if (!stack || layer >= stack->layers.size())
    return {};
  const auto source = ReadValue(*stack, stack->layers[layer].source);
  if (!source)
    return {};
  const auto texture = render->Texture(*source);
  return texture ? texture->texture : TextureRef{};
}

std::span<const Diagnostic> RenderOutput::Diagnostics() const noexcept {
  return diagnostics_;
}

Compositor *Compositor::GetSingleton() {
  static Compositor compositor;
  return &compositor;
}

void Compositor::BeginTick(std::uint32_t a_nowMS) noexcept { nowMS_ = a_nowMS; }

TextureRef Compositor::LoadImage(std::string_view a_path) {
  if (a_path.empty()) {
    return nullptr;
  }
  const std::string key = ImageCacheKey(a_path);
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
  TextureRef result{source && source->rendererTexture ? source : nullptr};
  images_[key] = result;
  return result;
}

std::shared_ptr<TextureLab::RenderTarget> Compositor::NeutralHeight() {
  if (neutralHeight_ && neutralHeight_->Texture()) {
    return neutralHeight_;
  }
  TextureLab *lab = TextureLab::GetSingleton();
  std::shared_ptr<TextureLab::RenderTarget> target =
      lab->Acquire(TextureSize(64), "neutralHeight");
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

TextureSize Compositor::StackSize(const SurfaceOutput &output,
                                  const GeometryInputs &inputs,
                                  TextureSize requested,
                                  TextureSize maximum) const {
  if (output.slot == Slot::kHeight && inputs.material.flatDisplacement)
    return requested;
  const TextureRef base = BaseMapFor(output.slot, inputs.material);
  if (!IsNonPlaceholderTexture(base))
    return requested;
  const auto extent = TextureLab::ExtentOf(base.get());
  return SizeOverBase(requested, maximum,
                      extent ? std::max(extent->width, extent->height) : 0u);
}

StackRender Compositor::Render(RenderOutput &stack, const LayerFilter &filter,
                               const StackBase &base) {
  const auto render = stack.render_.lock();
  if (!render) {
    stack.latest_ = {};
    return StackRender::kFailed;
  }
  const auto result = render->Render(stack.result_, filter, base);
  stack.diagnostics_.clear();
  if (!result) {
    stack.latest_ = {};
    stack.diagnostics_.push_back({Severity::kError, "stack", result.error()});
    return StackRender::kFailed;
  }
  const auto *rendered = Get<StackResult>(*result);
  if (!rendered)
    return StackRender::kPending;
  stack.latest_ = rendered->texture;
  stack.latestVersion_ = rendered->contentVersion;
  return StackRender::kRendered;
}

std::string DescribeTexture(const TextureRef &a_texture) {
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
}
