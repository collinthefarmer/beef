#include "render/TextureRef.h"

#include "diagnostics/Trace.h"
#include "planners/TextureLeases.h"

namespace BetterEnchantmentEffects {
namespace {
TextureLeases<TextureLab::RenderTarget> &Leases() {
  static TextureLeases<TextureLab::RenderTarget> leases;
  return leases;
}
}

bool RegisterTextureTarget(
    const std::shared_ptr<TextureLab::RenderTarget> &a_target) {
  if (a_target && a_target->Texture()) {
    return Leases().Register(
        reinterpret_cast<std::uintptr_t>(a_target->Texture()),
        a_target->Generation(), a_target);
  }
  return false;
}

TextureRef::TextureRef(std::shared_ptr<TextureLab::RenderTarget> a_target)
    : target_(std::move(a_target)) {
  if (!target_)
    return;
  generation_ = target_->Generation();
  texture_ = RE::NiPointer<RE::NiSourceTexture>{target_->Texture()};
  valid_ = static_cast<bool>(texture_);
}

TextureRef::TextureRef(RE::NiSourceTexture *a_texture) {
  if (!a_texture) {
    return;
  }
  auto lease = Leases().Retain(reinterpret_cast<std::uintptr_t>(a_texture));
  if (lease.generated &&
      (!lease.target || lease.target->Texture() != a_texture)) {
    valid_ = false;
    Trace::Safely([&] {
      Trace::Emit(Trace::Event::kTexture,
                  {{"action", "lease_rejected"},
                   {"presenter", Trace::Pointer(a_texture)},
                   {"generation", std::to_string(lease.generation)}});
    });
    return;
  }
  target_ = std::move(lease.target);
  generation_ = lease.generation;
  texture_ = RE::NiPointer<RE::NiSourceTexture>{a_texture};
}

TextureRef::TextureRef(const RE::NiPointer<RE::NiSourceTexture> &a_texture)
    : TextureRef(a_texture.get()) {}

bool TextureRef::Valid() const noexcept {
  return valid_ && (!target_ || (target_->Generation() == generation_ &&
                                 target_->Texture() == texture_.get()));
}

RE::NiSourceTexture *TextureRef::get() const noexcept {
  return Valid() ? texture_.get() : nullptr;
}

RE::NiSourceTexture *TextureRef::operator->() const noexcept { return get(); }
TextureRef::operator bool() const noexcept { return get() != nullptr; }
std::uint64_t TextureRef::Generation() const noexcept { return generation_; }
}
