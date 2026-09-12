#pragma once

#include "render/RuntimeTextures.h"

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
  Acquire(REX::W32::ID3D11Device *a_device, TextureSize a_size);
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
  RE::NiPointer<RE::NiSourceTexture> LoadPresenter();
  static void Recycle(const std::weak_ptr<Pool> &a_pool,
                      RenderTarget *a_target) noexcept;

  std::uint32_t nextPresenter_ = 0;
  std::shared_ptr<Pool> pool_ = std::make_shared<Pool>();
  std::map<std::uint32_t, std::shared_ptr<RenderTarget>> scratch_;
};
}
