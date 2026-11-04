// GPL-3.0-only with the additional permission in COPYING.md.
#include "render/Compositor.h"
#include "planners/TextureIdentity.h"
#include "render/RenderInstance.h"

#include <cctype>
#include <cstdint>
#include <format>

namespace BetterEnchantmentEffects {
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

StepOutputRef RenderOutput::Result() const noexcept { return result_; }

std::unique_ptr<RenderOutput>
RenderOutput::DrawnBy(const std::shared_ptr<RenderInstance> &a_render) const {
  if (!a_render) {
    return nullptr;
  }
  const RenderPlan &plan = a_render->Plan();
  const auto *stack =
      result_.step < plan.steps.size()
          ? Get<CompositeStackStep>(plan.steps[result_.step].kind)
          : nullptr;
  if (!stack) {
    return nullptr;
  }
  return std::make_unique<RenderOutput>(
      a_render, result_, a_render->SizeAt(stack->requirements.side), animated_);
}

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
  if (const TextureRef *loaded = images_.Use(key, nowMS_)) {
    return *loaded;
  }
  if (failedImages_.Find(key)) {
    return nullptr;
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
  if (result) {
    static_cast<void>(images_.Insert(key, result, 0, nowMS_));
  } else {
    static_cast<void>(failedImages_.Insert(key, {}, 0, nowMS_));
  }
  return result;
}

void Compositor::SweepImages(Millis a_now) {
  const RetainedStore<std::string, TextureRef>::Evicted images =
      images_.Sweep(a_now, KeepNone{});
  const RetainedStore<std::string, std::monostate>::Evicted failures =
      failedImages_.Sweep(a_now, KeepNone{});
}

std::size_t Compositor::ClearImages() {
  const RetainedStore<std::string, TextureRef>::Evicted images =
      images_.Clear();
  const RetainedStore<std::string, std::monostate>::Evicted failures =
      failedImages_.Clear();
  return images.size() + failures.size();
}

std::uint64_t Compositor::ResidentTargets() const noexcept {
  return neutralHeight_ ? 1 : 0;
}

void Compositor::CollectCachedTargets(HolderSet &a_set) const {
  stepOutputs_.CollectTargets(a_set);
}

void Compositor::CollectResidentTargets(HolderSet &a_set) const {
  if (neutralHeight_)
    a_set.insert(TargetId(neutralHeight_.get()));
}

std::uint64_t Compositor::ResidentBytes() const noexcept {
  return neutralHeight_ ? neutralHeight_->Bytes() : 0;
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
