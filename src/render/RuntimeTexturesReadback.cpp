#include "render/RuntimeTextures.h"

#include <cstring>
#include <utility>

namespace BetterEnchantmentEffects {
using namespace REX::W32;

namespace {
constexpr bool Failed(std::int32_t a_hr) noexcept { return a_hr < 0; }

template <class T> void Release(T *&a_ptr) {
  if (a_ptr) {
    a_ptr->Release();
    a_ptr = nullptr;
  }
}

RE::NiTexture::RendererData *DataOf(RE::NiSourceTexture *a_texture) {
  return a_texture ? reinterpret_cast<RE::NiTexture::RendererData *>(
                         a_texture->rendererTexture)
                   : nullptr;
}

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

private:
  RE::BSGraphics::Renderer *renderer_ = nullptr;
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
  desc.byteWidth = a_bytes;
  desc.usage = D3D11_USAGE_STAGING;
  desc.cpuAccessFlags = D3D11_CPU_ACCESS_READ;
  REX::W32::ID3D11Buffer *staging = nullptr;
  if (Failed(device_->CreateBuffer(&desc, nullptr, &staging))) {
    return out;
  }
  const D3D11_BOX box{0, 0, 0, a_bytes, 1, 1};
  context_->CopySubresourceRegion(
      reinterpret_cast<REX::W32::ID3D11Resource *>(staging), 0, 0, 0, 0,
      reinterpret_cast<REX::W32::ID3D11Resource *>(a_buffer), 0, &box);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (!Failed(
          context_->Map(reinterpret_cast<REX::W32::ID3D11Resource *>(staging),
                        0, D3D11_MAP_READ, 0, &mapped)) &&
      mapped.data) {
    out.assign(static_cast<const std::uint8_t *>(mapped.data),
               static_cast<const std::uint8_t *>(mapped.data) + a_bytes);
    context_->Unmap(reinterpret_cast<REX::W32::ID3D11Resource *>(staging), 0);
  }
  Release(staging);
  return out;
}

std::optional<float> TextureLab::ReadBackMean(RenderTarget &a_target) {
  const RendererLock rendererLock;
  std::optional<float> result;
  D3D11_TEXTURE2D_DESC desc{};
  a_target.texture->GetDesc(&desc);
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
  REX::W32::ID3D11Texture2D *staging = nullptr;
  if (!Failed(device_->CreateTexture2D(&stagingDesc, nullptr, &staging))) {
    context_->CopySubresourceRegion(staging, 0, 0, 0, 0, a_target.texture,
                                    lastMip, nullptr);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (!Failed(context_->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
      const auto *px = static_cast<const std::uint8_t *>(mapped.data);
      result = (0.299f * px[0] + 0.587f * px[1] + 0.114f * px[2]) / 255.0f;
      context_->Unmap(staging, 0);
    } else {
      logger::warn("TextureLab: staging map failed; mean readback unavailable");
    }
    Release(staging);
  } else {
    logger::warn("TextureLab: staging texture creation failed; mean readback "
                 "unavailable");
  }
  return result;
}

std::vector<std::uint8_t> TextureLab::ReadBackPixels(RenderTarget &a_target) {
  const RendererLock rendererLock;
  std::vector<std::uint8_t> out;
  if (!a_target.texture) {
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
  REX::W32::ID3D11Texture2D *staging = nullptr;
  if (Failed(device_->CreateTexture2D(&stagingDesc, nullptr, &staging))) {
    logger::warn("TextureLab: staging texture creation failed; pixel readback "
                 "unavailable");
    return out;
  }
  context_->CopySubresourceRegion(staging, 0, 0, 0, 0, a_target.texture, 0,
                                  nullptr);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (!Failed(context_->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)) &&
      mapped.data) {
    const std::size_t rowBytes = static_cast<std::size_t>(desc.width) * 4;
    if (mapped.rowPitch >= rowBytes) {
      out.resize(rowBytes * desc.height);
      const auto *rows = static_cast<const std::uint8_t *>(mapped.data);
      for (std::uint32_t y = 0; y < desc.height; ++y) {
        std::memcpy(out.data() + y * rowBytes,
                    rows + static_cast<std::size_t>(y) * mapped.rowPitch,
                    rowBytes);
      }
    }
    context_->Unmap(staging, 0);
  } else {
    logger::warn("TextureLab: staging map failed; pixel readback unavailable");
  }
  Release(staging);
  return out;
}

std::shared_ptr<TextureLab::Lookup>
TextureLab::CreateLookup(std::span<const float, 256> a_values) {
  if (!Init()) {
    return nullptr;
  }
  auto lookup = std::make_shared<Lookup>();
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
  if (Failed(device_->CreateTexture2D(&desc, &data, &lookup->texture)) ||
      Failed(device_->CreateShaderResourceView(lookup->texture, nullptr,
                                               &lookup->srv))) {
    logger::error("TextureLab: could not create a curve lookup");
    return nullptr;
  }
  return lookup;
}

std::optional<TextureLab::Extent>
TextureLab::ExtentOf(RE::NiSourceTexture *a_source) {
  const auto *data = DataOf(a_source);
  if (!data || !data->resourceView) {
    return std::nullopt;
  }
  auto *srv = reinterpret_cast<REX::W32::ID3D11ShaderResourceView *>(
      data->resourceView);
  REX::W32::ID3D11Resource *resource = nullptr;
  srv->GetResource(&resource);
  if (!resource) {
    return std::nullopt;
  }
  REX::W32::ID3D11Texture2D *texture = nullptr;
  resource->QueryInterface(IID_ID3D11Texture2D,
                           reinterpret_cast<void **>(&texture));
  std::optional<Extent> extent;
  if (texture) {
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    extent = Extent{desc.width, desc.height};
  }
  Release(texture);
  Release(resource);
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
