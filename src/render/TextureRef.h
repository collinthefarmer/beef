// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/TargetHolders.h"
#include "render/TextureLab.h"

#include <vector>

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
  void CollectTarget(HolderSet &a_set) const;
  [[nodiscard]] const TextureLab::RenderTarget *Target() const noexcept {
    return target_.get();
  }

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
  std::uint64_t drawStamp = 0;
};

[[nodiscard]] bool RegisterTextureTarget(
    const std::shared_ptr<TextureLab::RenderTarget> &a_target);

[[nodiscard]] std::string TextureIdentity(const TextureRef &a_texture);
[[nodiscard]] std::uintptr_t
TargetId(const TextureLab::RenderTarget *a_target) noexcept;
void CollectView(const TextureView &a_view, HolderSet &a_set);
[[nodiscard]] std::vector<LiveTarget> LiveTextureTargets();
}
