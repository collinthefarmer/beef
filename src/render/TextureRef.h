// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "render/TextureLab.h"

namespace BetterEnchantmentEffects {
class TextureRef {
public:
  TextureRef() = default;
  TextureRef(std::nullptr_t) noexcept {}
  explicit TextureRef(std::shared_ptr<TextureLab::RenderTarget> a_target);
  TextureRef(RE::NiSourceTexture *a_texture);
  TextureRef(const RE::NiPointer<RE::NiSourceTexture> &a_texture);
  [[nodiscard]] bool Valid() const noexcept;
  [[nodiscard]] RE::NiSourceTexture *get() const noexcept;
  [[nodiscard]] RE::NiSourceTexture *operator->() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] std::uint64_t Generation() const noexcept;
  [[nodiscard]] bool Holds(
      const std::shared_ptr<TextureLab::RenderTarget> &a_target) const noexcept;

private:
  std::shared_ptr<TextureLab::RenderTarget> target_;
  RE::NiPointer<RE::NiSourceTexture> texture_;
  std::uint64_t generation_ = 0;
  bool valid_ = true;
};

struct TextureView {
  TextureRef texture;
  TextureLab::LayerInput sampling;
  float normalize = 1;
  std::shared_ptr<TextureLab::RenderTarget> target;
};

[[nodiscard]] bool RegisterTextureTarget(
    const std::shared_ptr<TextureLab::RenderTarget> &a_target);

[[nodiscard]] std::string TextureIdentity(const TextureRef &a_texture);
}
