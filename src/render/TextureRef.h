#pragma once

#include "render/TextureLab.h"

namespace BetterEnchantmentEffects {
class TextureRef {
public:
  TextureRef() = default;
  TextureRef(std::nullptr_t) noexcept {}
  // Generated textures retain their producer directly; pointer lookup is for
  // textures arriving from the engine boundary.
  explicit TextureRef(std::shared_ptr<TextureLab::RenderTarget> a_target);
  TextureRef(RE::NiSourceTexture *a_texture);
  TextureRef(const RE::NiPointer<RE::NiSourceTexture> &a_texture);
  [[nodiscard]] bool Valid() const noexcept;
  [[nodiscard]] RE::NiSourceTexture *get() const noexcept;
  [[nodiscard]] RE::NiSourceTexture *operator->() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] std::uint64_t Generation() const noexcept;

private:
  std::shared_ptr<TextureLab::RenderTarget> target_;
  RE::NiPointer<RE::NiSourceTexture> texture_;
  std::uint64_t generation_ = 0;
  bool valid_ = true;
};

[[nodiscard]] bool RegisterTextureTarget(
    const std::shared_ptr<TextureLab::RenderTarget> &a_target);
}
