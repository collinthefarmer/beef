// GPL-3.0-only with the additional permission in COPYING.md.
#include "render/RenderTargetPool.h"

#include "Identity.h"
#include "diagnostics/Metrics.h"
#include "render/D3DResult.h"
#include "render/TextureRef.h"

#include <algorithm>
#include <cctype>

namespace BetterEnchantmentEffects {
using namespace REX::W32;

namespace {
std::string TextureName(std::string a_name) {
  std::ranges::transform(a_name, a_name.begin(), [](unsigned char a_char) {
    return a_char == '/' ? '\\' : static_cast<char>(std::tolower(a_char));
  });
  if (a_name.starts_with("textures\\")) {
    a_name.erase(0, 9);
  }
  return a_name;
}
}

RE::NiPointer<RE::NiSourceTexture>
RenderTargetPool::LoadPresenter(std::size_t a_slot) {
  const auto path =
      Identity::PresenterTexturePath(static_cast<std::uint32_t>(a_slot));
  RE::BSResourceNiBinaryStream resource{path};
  if (!resource.good()) {
    logger::error("TextureLab: presenter asset missing: {}", path);
    Trace::EmitSafely(Trace::Event::kTexture, {{"action", "presenter_rejected"},
                                               {"path", path},
                                               {"reason", "missing_asset"}});
    return nullptr;
  }
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
  if (!ValidatePresenter(a_slot, source, path))
    return nullptr;
  auto &presenter = presenters_[a_slot];
  presenter = {RE::NiPointer<RE::NiSourceTexture>{source}, DataOf(source)};
  Trace::EmitSafely(Trace::Event::kTexture,
                    {{"action", "presenter_loaded"},
                     {"path", path},
                     {"loaded_name", source->name.c_str()},
                     {"presenter", Trace::Pointer(source)}});
  return RE::NiPointer<RE::NiSourceTexture>{source};
}

bool RenderTargetPool::ValidatePresenter(std::size_t a_slot,
                                         RE::NiSourceTexture *a_source,
                                         const std::string &a_path) const {
  if (TextureName(a_source->name.c_str()) != TextureName(a_path)) {
    logger::error("TextureLab: presenter fallback rejected: {} loaded {}",
                  a_path, a_source->name.c_str());
    Trace::EmitSafely(Trace::Event::kTexture,
                      {{"action", "presenter_rejected"},
                       {"path", a_path},
                       {"loaded_name", a_source->name.c_str()},
                       {"reason", "unexpected_texture"}});
    return false;
  }
  for (std::size_t i = 0; i < presenters_.size(); ++i) {
    if (i != a_slot && presenters_[i].texture.get() == a_source) {
      logger::error("TextureLab: presenter alias rejected: {} ({})", a_path,
                    Trace::Pointer(a_source));
      Trace::EmitSafely(Trace::Event::kTexture,
                        {{"action", "presenter_rejected"},
                         {"path", a_path},
                         {"reason", "duplicate_presenter"},
                         {"presenter", Trace::Pointer(a_source)}});
      return false;
    }
  }
  const auto &presenter = presenters_[a_slot];
  if (presenter.texture && (presenter.texture.get() != a_source ||
                            presenter.original != DataOf(a_source))) {
    logger::error("TextureLab: retired presenter {} changed renderer ownership",
                  a_path);
    return false;
  }
  return true;
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
  a_target.presenterSlot_ = presenterSlots_.Acquire();
  if (!a_target.presenterSlot_) {
    logger::error("TextureLab: all {} presenter slots are retained",
                  kPresenterCount);
    return false;
  }
  a_target.presenter = LoadPresenter(*a_target.presenterSlot_);
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
  Metrics::CountTargetCreated(Metrics::MippedRgbaBytes(pixels));
  return true;
}

std::shared_ptr<RenderTargetPool::RenderTarget>
RenderTargetPool::Acquire(ID3D11Device *a_device, TextureSize a_size,
                          std::string_view a_owner) {
  if (!a_device) {
    return nullptr;
  }
  const auto deleter = [pool = std::weak_ptr<Pool>{pool_}](
                           RenderTarget *a_target) { Recycle(pool, a_target); };
  std::unique_ptr<RenderTarget> target;
  {
    std::scoped_lock lock{pool_->lock};
    target = pool_->targets.Take([&](const RenderTarget &a_target) {
      return a_target.size == a_size.Pixels();
    });
  }
  if (!target) {
    target = std::make_unique<RenderTarget>();
    if (!CreateTarget(a_device, *target, a_size)) {
      return nullptr;
    }
  }
  if (!target->Texture()) {
    logger::error("TextureLab: target {} lost its presenter renderer",
                  target->traceID_);
    Trace::EmitSafely(Trace::Event::kTexture,
                      {{"action", "presenter_rejected"},
                       {"reason", "renderer_replaced"},
                       {"target", std::to_string(target->traceID_)}});
    return nullptr;
  }
  target->generation_ = ++nextGeneration_;
  Trace::EmitSafely(
      Trace::Event::kTexture,
      {{"action", "acquire"},
       {"target", std::to_string(target->traceID_)},
       {"owner", std::string{a_owner}},
       {"generation", std::to_string(target->generation_)},
       {"address", Trace::Pointer(target.get())},
       {"presenter", Trace::Pointer(target->presenter.get())},
       {"renderer", Trace::Pointer(target->ourData.get())},
       {"current_renderer",
        Trace::Pointer(target->presenter ? target->presenter->rendererTexture
                                         : nullptr)},
       {"srv", Trace::Pointer(target->srv.Get())},
       {"size", std::to_string(target->size)}});
  auto acquired = std::shared_ptr<RenderTarget>{target.release(), deleter};
  if (!RegisterTextureTarget(acquired)) {
    Trace::EmitSafely(
        Trace::Event::kTexture,
        {{"action", "lease_rejected"}, {"reason", "registration_conflict"}});
    return nullptr;
  }
  return acquired;
}

RenderTargetPool::RenderTarget *
RenderTargetPool::Scratch(ID3D11Device *a_device, TextureSize a_size) {
  auto &target = scratch_[a_size.Pixels()];
  if (!target) {
    target = Acquire(a_device, a_size, "scratch");
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
    Trace::EmitSafely(Trace::Event::kTexture,
                      {{"action", "recycle"},
                       {"target", std::to_string(target->traceID_)},
                       {"generation", std::to_string(target->generation_)},
                       {"presenter", Trace::Pointer(target->presenter.get())}});
    std::scoped_lock lock{pool->lock};
    const std::uint64_t bytes = Metrics::MippedRgbaBytes(target->size);
    pool->targets.Retain(std::move(target), bytes);
  } catch (...) {
    target.reset();
  }
}

void RenderTargetPool::ClearScratch() { scratch_.clear(); }

void RenderTargetPool::ClearUnused() {
  ResourcePool<RenderTarget> unused{kMaxIdleTargets, kMaxIdleBytes};
  {
    std::scoped_lock lock{pool_->lock};
    std::swap(unused, pool_->targets);
  }
}
}
