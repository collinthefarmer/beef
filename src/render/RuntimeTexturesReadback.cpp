#include "render/RuntimeTextures.h"

#include "render/D3DResult.h"

#include <REX/W32/COMPTR.h>

#include <cstring>
#include <utility>

namespace BetterEnchantmentEffects {
using namespace REX::W32;

namespace {
class RendererLock {
public:
  RendererLock() : renderer_(RE::BSGraphics::Renderer::GetSingleton()) {
    if (renderer_) {
      renderer_->Lock();
    }
  }
  ~RendererLock() {
    if (renderer_) {
      renderer_->Unlock();
    }
  }
  RendererLock(const RendererLock &) = delete;
  RendererLock &operator=(const RendererLock &) = delete;
  RendererLock(RendererLock &&) = delete;
  RendererLock &operator=(RendererLock &&) = delete;

private:
  RE::BSGraphics::Renderer *renderer_ = nullptr;
};

class UnconditionalReadback {
public:
  explicit UnconditionalReadback(ID3D11DeviceContext *a_context)
      : borrowedContext_(a_context) {
    borrowedContext_->GetPredication(predicate_.GetAddressOf(), &value_);
    borrowedContext_->SetPredication(nullptr, false);
  }
  ~UnconditionalReadback() {
    borrowedContext_->SetPredication(predicate_.Get(), value_);
  }
  UnconditionalReadback(const UnconditionalReadback &) = delete;
  UnconditionalReadback &operator=(const UnconditionalReadback &) = delete;
  UnconditionalReadback(UnconditionalReadback &&) = delete;
  UnconditionalReadback &operator=(UnconditionalReadback &&) = delete;

private:
  ID3D11DeviceContext *borrowedContext_;
  ComPtr<ID3D11Predicate> predicate_;
  REX::W32::BOOL value_ = false;
};

class ReadMapping {
public:
  ReadMapping(ID3D11DeviceContext *a_context, ID3D11Resource *a_resource)
      : borrowedContext_(a_context), resource_(a_resource),
        active_(!Failed(borrowedContext_->Map(resource_, 0, D3D11_MAP_READ, 0,
                                              &mapped_))) {}
  ~ReadMapping() {
    if (active_) {
      borrowedContext_->Unmap(resource_, 0);
    }
  }
  ReadMapping(const ReadMapping &) = delete;
  ReadMapping &operator=(const ReadMapping &) = delete;
  ReadMapping(ReadMapping &&) = delete;
  ReadMapping &operator=(ReadMapping &&) = delete;

  [[nodiscard]] const std::uint8_t *Data() const noexcept {
    return active_ ? static_cast<const std::uint8_t *>(mapped_.data) : nullptr;
  }
  [[nodiscard]] std::uint32_t RowPitch() const noexcept {
    return mapped_.rowPitch;
  }

private:
  ID3D11DeviceContext *borrowedContext_;
  ID3D11Resource *resource_;
  D3D11_MAPPED_SUBRESOURCE mapped_{};
  bool active_ = false;
};
}

std::vector<std::uint8_t>
TextureLab::ReadBuffer(REX::W32::ID3D11Buffer *a_buffer,
                       std::uint32_t a_bytes) {
  const RendererLock rendererLock;
  std::vector<std::uint8_t> out;
  if (!a_buffer || a_bytes == 0 || !Init()) {
    return out;
  }
  D3D11_BUFFER_DESC desc{};
  a_buffer->GetDesc(&desc);
  if (a_bytes > desc.byteWidth) {
    return out;
  }
  desc = {};
  desc.byteWidth = a_bytes;
  desc.usage = D3D11_USAGE_STAGING;
  desc.cpuAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Buffer> staging;
  if (Failed(borrowedDevice_->CreateBuffer(&desc, nullptr,
                                           staging.GetAddressOf()))) {
    return out;
  }
  const D3D11_BOX box{0, 0, 0, a_bytes, 1, 1};
  const UnconditionalReadback unconditional{borrowedContext_};
  borrowedContext_->CopySubresourceRegion(
      reinterpret_cast<REX::W32::ID3D11Resource *>(staging.Get()), 0, 0, 0, 0,
      reinterpret_cast<REX::W32::ID3D11Resource *>(a_buffer), 0, &box);
  const ReadMapping mapped{borrowedContext_,
                           reinterpret_cast<ID3D11Resource *>(staging.Get())};
  if (const auto *data = mapped.Data()) {
    out.assign(data, data + a_bytes);
  }
  return out;
}

std::optional<float> TextureLab::ReadBackMean(RenderTarget &a_target) {
  const RendererLock rendererLock;
  std::optional<float> result;
  if (!a_target.texture.Get() || !available_) {
    return result;
  }
  D3D11_TEXTURE2D_DESC desc{};
  a_target.texture->GetDesc(&desc);
  if (desc.mipLevels == 0 || desc.format != DXGI_FORMAT_R8G8B8A8_UNORM) {
    return result;
  }
  const auto lastMip = desc.mipLevels - 1;
  D3D11_TEXTURE2D_DESC stagingDesc{};
  stagingDesc.width = 1;
  stagingDesc.height = 1;
  stagingDesc.mipLevels = 1;
  stagingDesc.arraySize = 1;
  stagingDesc.format = desc.format;
  stagingDesc.sampleDesc.count = 1;
  stagingDesc.usage = D3D11_USAGE_STAGING;
  stagingDesc.cpuAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<REX::W32::ID3D11Texture2D> staging;
  if (!Failed(borrowedDevice_->CreateTexture2D(&stagingDesc, nullptr,
                                               staging.GetAddressOf()))) {
    const UnconditionalReadback unconditional{borrowedContext_};
    borrowedContext_->CopySubresourceRegion(
        staging.Get(), 0, 0, 0, 0, a_target.texture.Get(), lastMip, nullptr);
    const ReadMapping mapped{borrowedContext_, staging.Get()};
    if (const auto *px = mapped.Data(); px && mapped.RowPitch() >= 4) {
      result = (0.299f * px[0] + 0.587f * px[1] + 0.114f * px[2]) / 255.0f;
    } else {
      logger::warn("TextureLab: staging map failed; mean readback unavailable");
    }
  } else {
    logger::warn("TextureLab: staging texture creation failed; mean readback "
                 "unavailable");
  }
  return result;
}

std::vector<std::uint8_t> TextureLab::ReadBackPixels(RenderTarget &a_target) {
  const RendererLock rendererLock;
  std::vector<std::uint8_t> out;
  if (!a_target.texture.Get() || !available_) {
    return out;
  }
  D3D11_TEXTURE2D_DESC desc{};
  a_target.texture->GetDesc(&desc);
  if (desc.format != DXGI_FORMAT_R8G8B8A8_UNORM || desc.width == 0 ||
      desc.height == 0 || desc.width != a_target.size ||
      desc.height != a_target.size) {
    logger::warn("TextureLab: pixel readback refused: target {}x{} format {}",
                 desc.width, desc.height,
                 static_cast<std::uint32_t>(desc.format));
    return out;
  }
  D3D11_TEXTURE2D_DESC stagingDesc{};
  stagingDesc.width = desc.width;
  stagingDesc.height = desc.height;
  stagingDesc.mipLevels = 1;
  stagingDesc.arraySize = 1;
  stagingDesc.format = desc.format;
  stagingDesc.sampleDesc.count = 1;
  stagingDesc.usage = D3D11_USAGE_STAGING;
  stagingDesc.cpuAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<REX::W32::ID3D11Texture2D> staging;
  if (Failed(borrowedDevice_->CreateTexture2D(&stagingDesc, nullptr,
                                              staging.GetAddressOf()))) {
    logger::warn("TextureLab: staging texture creation failed; pixel readback "
                 "unavailable");
    return out;
  }
  const UnconditionalReadback unconditional{borrowedContext_};
  borrowedContext_->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0,
                                          a_target.texture.Get(), 0, nullptr);
  const ReadMapping mapped{borrowedContext_, staging.Get()};
  if (const auto *rows = mapped.Data()) {
    const std::size_t rowBytes = static_cast<std::size_t>(desc.width) * 4;
    if (mapped.RowPitch() >= rowBytes) {
      out.resize(rowBytes * desc.height);
      for (std::uint32_t y = 0; y < desc.height; ++y) {
        std::memcpy(out.data() + y * rowBytes,
                    rows + static_cast<std::size_t>(y) * mapped.RowPitch(),
                    rowBytes);
      }
    }
  } else {
    logger::warn("TextureLab: staging map failed; pixel readback unavailable");
  }
  return out;
}

std::shared_ptr<TextureLab::Lookup>
TextureLab::CreateLookup(std::span<const float, 256> a_values) {
  if (!Init()) {
    return nullptr;
  }
  auto lookup = std::make_shared<Lookup>(Lookup::ConstructionKey{});
  D3D11_TEXTURE2D_DESC desc{};
  desc.width = 256;
  desc.height = 1;
  desc.mipLevels = 1;
  desc.arraySize = 1;
  desc.format = DXGI_FORMAT_R32_FLOAT;
  desc.sampleDesc.count = 1;
  desc.usage = D3D11_USAGE_IMMUTABLE;
  desc.bindFlags = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA data{a_values.data(), 256 * sizeof(float), 0};
  if (Failed(borrowedDevice_->CreateTexture2D(
          &desc, &data, lookup->texture.GetAddressOf())) ||
      Failed(borrowedDevice_->CreateShaderResourceView(
          lookup->texture.Get(), nullptr, lookup->srv.GetAddressOf()))) {
    logger::error("TextureLab: could not create a curve lookup");
    return nullptr;
  }
  return lookup;
}

std::optional<TextureLab::Extent>
TextureLab::ExtentOf(RE::NiSourceTexture *a_source) {
  const RendererLock rendererLock;
  const auto *data = DataOf(a_source);
  if (!data || !data->resourceView) {
    return std::nullopt;
  }
  auto *srv = reinterpret_cast<REX::W32::ID3D11ShaderResourceView *>(
      data->resourceView);
  ComPtr<ID3D11Resource> resource;
  srv->GetResource(resource.GetAddressOf());
  if (!resource.Get()) {
    return std::nullopt;
  }
  ComPtr<REX::W32::ID3D11Texture2D> texture;
  resource->QueryInterface(IID_ID3D11Texture2D,
                           reinterpret_cast<void **>(texture.GetAddressOf()));
  std::optional<Extent> extent;
  if (texture.Get()) {
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    extent = Extent{desc.width, desc.height};
  }
  return extent;
}

float TextureLab::MeanLuminance(RE::NiSourceTexture *a_source) {
  if (const auto it = luminance_.find(a_source); it != luminance_.end()) {
    return it->second;
  }
  float result = 0.5f;
  auto target = Acquire(TextureSize(64));
  if (target && Render(*target, a_source, LayerParams{})) {
    result = ReadBackMean(*target).value_or(0.5f);
  } else {
    logger::warn("TextureLab: mean luminance render failed; using 0.5");
  }
  luminance_[a_source] = result;
  return result;
}

float TextureLab::MeanChannel(RE::NiSourceTexture *a_source,
                              ShaderChannel a_channel) {
  const auto key = std::make_pair(a_source, a_channel);
  if (const auto it = channelMeans_.find(key); it != channelMeans_.end()) {
    return it->second;
  }
  float result = 0.5f;
  auto target = Acquire(TextureSize(64));
  if (target) {
    LayerParams p;
    p.mode = Mode::kChannel;
    p.map = {a_source, MapReading::kRmaos};
    p.channel.channel = a_channel;
    if (Render(*target, nullptr, p)) {
      result = ReadBackMean(*target).value_or(0.5f);
    } else {
      logger::warn("TextureLab: channel mean render failed; using 0.5");
    }
  } else {
    logger::warn("TextureLab: no target for channel mean; using 0.5");
  }
  channelMeans_[key] = result;
  return result;
}
}
