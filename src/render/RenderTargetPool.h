// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/RetainedStore.h"
#include "planners/TargetPool.h"
#include "render/TextureLab.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace BetterEnchantmentEffects {
class RenderTargetPool {
public:
  using RenderTarget = TextureLab::RenderTarget;
  using Holdings = TextureLab::TargetHoldings;

  RenderTargetPool() = default;
  RenderTargetPool(const RenderTargetPool &) = delete;
  RenderTargetPool &operator=(const RenderTargetPool &) = delete;

  using TargetMips = TextureLab::TargetMips;

  struct TargetShape {
    TextureSize size;
    TextureFormat format = TextureFormat::kRgba8;
    TargetMips mips = TargetMips::kGeneratedOnDraw;
  };

  [[nodiscard]] std::shared_ptr<RenderTarget>
  Acquire(REX::W32::ID3D11Device *a_device, const TargetShape &a_shape,
          std::string_view a_owner);
  void ExpireIdle(std::uint64_t a_nowMs);
  void CollectIdleTargets(HolderSet &a_set) const;
  void ClearUnused();
  [[nodiscard]] Holdings Held() const;

private:
  static constexpr RetentionPolicy kIdleTargets{
      .maxCount = 16, .maxBytes = 64 * 1024 * 1024, .maxIdle = 5000};
  struct IdleShape {
    std::uint32_t pixels = 0;
    TextureFormat format = TextureFormat::kRgba8;
    bool chain = false;
    [[nodiscard]] bool operator==(const IdleShape &) const = default;
  };
  using IdleTargets = RetainedStore<IdleShape, std::unique_ptr<RenderTarget>>;
  struct Pool {
    std::mutex lock;
    IdleTargets targets{kIdleTargets};
    Millis now = 0;
  };
  [[nodiscard]] static IdleShape ShapeOf(const RenderTarget &a_target) noexcept;
  [[nodiscard]] static IdleShape ShapeOf(const TargetShape &a_shape) noexcept;

  bool CreateTarget(REX::W32::ID3D11Device *a_device, RenderTarget &a_target,
                    const TargetShape &a_shape);
  RE::NiPointer<RE::NiSourceTexture> LoadPresenter(std::size_t a_slot);
  bool ValidatePresenter(std::size_t a_slot, RE::NiSourceTexture *a_source,
                         const std::string &a_path) const;
  static void Recycle(const std::weak_ptr<Pool> &a_pool,
                      RenderTarget *a_target) noexcept;

#ifndef BEEF_PRESENTER_COUNT
#define BEEF_PRESENTER_COUNT 512
#endif
  static constexpr std::size_t kPresenterCount = BEEF_PRESENTER_COUNT;
  struct Presenter {
    RE::NiPointer<RE::NiSourceTexture> texture;
    RE::NiTexture::RendererData *original = nullptr;
  };
  TargetPool presenterSlots_{kPresenterCount};
  std::array<Presenter, kPresenterCount> presenters_{};
  std::uint64_t nextGeneration_ = 0;
  std::shared_ptr<Pool> pool_ = std::make_shared<Pool>();
};
}
