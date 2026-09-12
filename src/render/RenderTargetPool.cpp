#include "render/RenderTargetPool.h"

#include "Identity.h"

namespace BetterEnchantmentEffects {
using namespace REX::W32;

namespace {
constexpr bool Failed(std::int32_t a_hr) noexcept { return a_hr < 0; }
constexpr std::uint32_t kPresenterCount = 512;

RE::NiTexture::RendererData *DataOf(RE::NiSourceTexture *a_texture) {
  return a_texture ? reinterpret_cast<RE::NiTexture::RendererData *>(
                         a_texture->rendererTexture)
                   : nullptr;
}
}

RE::NiPointer<RE::NiSourceTexture> RenderTargetPool::LoadPresenter() {
  if (nextPresenter_ >= kPresenterCount) {
    logger::error("TextureLab: out of presenter textures ({})",
                  kPresenterCount);
    return nullptr;
  }
  const auto path = Identity::PresenterTexturePath(nextPresenter_);
  RE::NiPointer<RE::NiTexture> texture;
  RE::BSShaderManager::GetTexture(path.c_str(), true, texture, false);
  auto *source =
      texture ? netimmerse_cast<RE::NiSourceTexture *>(texture.get()) : nullptr;
  if (!source || !source->rendererTexture) {
    logger::error(
        "TextureLab: presenter {} failed to load (is the slots folder "
        "installed?)",
        path);
    return nullptr;
  }
  ++nextPresenter_;
  return RE::NiPointer<RE::NiSourceTexture>{source};
}

bool RenderTargetPool::CreateTarget(ID3D11Device *a_device,
                                    RenderTarget &a_target,
                                    TextureSize a_size) {
  const std::uint32_t pixels = a_size.Pixels();
  D3D11_TEXTURE2D_DESC desc{};
  desc.width = pixels;
  desc.height = pixels;
  desc.mipLevels = 0;
  desc.arraySize = 1;
  desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.sampleDesc.count = 1;
  desc.usage = D3D11_USAGE_DEFAULT;
  desc.bindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  desc.miscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
  if (Failed(a_device->CreateTexture2D(&desc, nullptr,
                                       a_target.texture.GetAddressOf()))) {
    logger::error("TextureLab: CreateTexture2D({}) failed", pixels);
    return false;
  }
  if (Failed(a_device->CreateShaderResourceView(a_target.texture.Get(), nullptr,
                                                a_target.srv.GetAddressOf())) ||
      Failed(a_device->CreateRenderTargetView(a_target.texture.Get(), nullptr,
                                              a_target.rtv.GetAddressOf()))) {
    logger::error("TextureLab: view creation failed");
    return false;
  }
  a_target.presenter = LoadPresenter();
  if (!a_target.presenter) {
    return false;
  }
  a_target.originalData = DataOf(a_target.presenter.get());
  a_target.ourData = std::make_unique<RE::NiTexture::RendererData>(
      static_cast<std::uint16_t>(pixels), static_cast<std::uint16_t>(pixels));
  a_target.ourData->texture =
      reinterpret_cast<::ID3D11Texture2D *>(a_target.texture.Get());
  a_target.ourData->resourceView =
      reinterpret_cast<::ID3D11ShaderResourceView *>(a_target.srv.Get());
  a_target.presenter->rendererTexture =
      reinterpret_cast<RE::BSGraphics::Texture *>(a_target.ourData.get());
  a_target.size = pixels;
  return true;
}

std::shared_ptr<RenderTargetPool::RenderTarget>
RenderTargetPool::Acquire(ID3D11Device *a_device, TextureSize a_size) {
  if (!a_device) {
    return nullptr;
  }
  const auto deleter = [pool = std::weak_ptr<Pool>{pool_}](
                           RenderTarget *a_target) { Recycle(pool, a_target); };
  std::unique_ptr<RenderTarget> target;
  {
    std::scoped_lock lock{pool_->lock};
    for (auto it = pool_->targets.begin(); it != pool_->targets.end(); ++it) {
      if ((*it)->size == a_size.Pixels()) {
        target = std::move(*it);
        pool_->targets.erase(it);
        break;
      }
    }
  }
  if (!target) {
    target = std::make_unique<RenderTarget>();
    if (!CreateTarget(a_device, *target, a_size)) {
      return nullptr;
    }
  }
  return std::shared_ptr<RenderTarget>{target.release(), deleter};
}

RenderTargetPool::RenderTarget *
RenderTargetPool::Scratch(ID3D11Device *a_device, TextureSize a_size) {
  auto &target = scratch_[a_size.Pixels()];
  if (!target) {
    target = Acquire(a_device, a_size);
  }
  return target.get();
}

void RenderTargetPool::Recycle(const std::weak_ptr<Pool> &a_pool,
                               RenderTarget *a_target) noexcept {
  std::unique_ptr<RenderTarget> target{a_target};
  const auto pool = a_pool.lock();
  if (!pool) {
    return;
  }
  try {
    std::scoped_lock lock{pool->lock};
    pool->targets.push_back(std::move(target));
  } catch (...) {
    target.reset();
  }
}

void RenderTargetPool::ClearScratch() { scratch_.clear(); }

void RenderTargetPool::ClearUnused() {
  std::vector<std::unique_ptr<RenderTarget>> unused;
  {
    std::scoped_lock lock{pool_->lock};
    unused.swap(pool_->targets);
  }
}
}
