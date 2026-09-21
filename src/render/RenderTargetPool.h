#pragma once

#include "planners/TargetPool.h"
#include "render/TextureLab.h"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace BetterEnchantmentEffects {
class RenderTargetPool {
public:
  using RenderTarget = TextureLab::RenderTarget;

  RenderTargetPool() = default;
  RenderTargetPool(const RenderTargetPool &) = delete;
  RenderTargetPool &operator=(const RenderTargetPool &) = delete;

  [[nodiscard]] std::shared_ptr<RenderTarget>
  Acquire(REX::W32::ID3D11Device *a_device, TextureSize a_size,
          std::string_view a_owner);
  [[nodiscard]] RenderTarget *Scratch(REX::W32::ID3D11Device *a_device,
                                      TextureSize a_size);
  void ClearScratch();
  void ClearUnused();

private:
  struct Pool {
    std::mutex lock;
    std::vector<std::unique_ptr<RenderTarget>> targets;
  };

  bool CreateTarget(REX::W32::ID3D11Device *a_device, RenderTarget &a_target,
                    TextureSize a_size);
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
  std::map<std::uint32_t, std::shared_ptr<RenderTarget>> scratch_;
};
}
