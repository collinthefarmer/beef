// GPL-3.0-only with the additional permission in COPYING.md.
#include "render/VideoMemory.h"

#include "Core.h"
#include "render/D3DResult.h"

#include <REX/W32/DXGI.h>

namespace BetterEnchantmentEffects {
namespace {
std::expected<REX::W32::ComPtr<REX::W32::IDXGIAdapter3>, std::string>
AdapterOf(REX::W32::ID3D11Device &a_device) {
  REX::W32::ComPtr<REX::W32::IDXGIDevice> device;
  if (Failed(a_device.QueryInterface(
          REX::W32::IID_IDXGIDevice,
          reinterpret_cast<void **>(device.ReleaseAndGetAddressOf()))) ||
      !device.Get()) {
    return std::unexpected("the D3D11 device has no IDXGIDevice");
  }
  REX::W32::ComPtr<REX::W32::IDXGIAdapter> adapter;
  if (Failed(device->GetAdapter(adapter.ReleaseAndGetAddressOf())) ||
      !adapter.Get()) {
    return std::unexpected("the DXGI device has no adapter");
  }
  REX::W32::ComPtr<REX::W32::IDXGIAdapter3> adapter3;
  if (Failed(adapter->QueryInterface(
          REX::W32::IID_IDXGIAdapter3,
          reinterpret_cast<void **>(adapter3.ReleaseAndGetAddressOf()))) ||
      !adapter3.Get()) {
    return std::unexpected("the DXGI adapter has no IDXGIAdapter3");
  }
  return adapter3;
}
}

std::expected<VideoMemory, std::string> VideoMemoryProbe::Sample() {
  if (Is<Unreached>(adapter_)) {
    REX::W32::ID3D11Device *device = RE::BSGraphics::Renderer::GetDevice();
    if (!device) {
      return std::unexpected("no D3D11 device yet");
    }
    auto reached = AdapterOf(*device);
    if (reached) {
      adapter_ = std::move(*reached);
    } else {
      adapter_ = Unreachable{std::move(reached.error())};
    }
  }
  if (const Unreachable *unreachable = Get<Unreachable>(adapter_)) {
    return std::unexpected(unreachable->problem);
  }
  const Adapter *adapter = Get<Adapter>(adapter_);
  if (!adapter || !adapter->Get()) {
    return std::unexpected("no DXGI adapter");
  }
  REX::W32::DXGI_QUERY_VIDEO_MEMORY_INFO info{};
  if (Failed((*adapter)->QueryVideoMemoryInfo(
          0, REX::W32::DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
    return std::unexpected("QueryVideoMemoryInfo failed");
  }
  return VideoMemory{info.budget, info.currentUsage};
}
}
