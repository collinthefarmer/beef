// GPL-3.0-only with the additional permission in COPYING.md.
#include "render/TextureLab.h"

#include "Identity.h"
#include "diagnostics/Metrics.h"
#include "planners/ProgramShader.h"
#include "render/D3DResult.h"
#include "render/RenderTargetPool.h"
#include "render/ShaderConstants.h"
#include "render/TexturePreviews.h"

#include <REX/W32/D3DCOMPILER.h>

#include <cstring>

namespace BetterEnchantmentEffects {
using namespace REX::W32;

extern const char *const kShaderSource;

namespace {
ComPtr<ID3DBlob> CompileEntry(const char *a_entry, const char *a_target) {
  const std::string sourceName{Identity::kName};
  ComPtr<ID3DBlob> out;
  ComPtr<ID3DBlob> errors;
  const HRESULT hr = D3DCompile(kShaderSource, std::strlen(kShaderSource),
                                sourceName.c_str(), nullptr, nullptr, a_entry,
                                a_target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                                out.GetAddressOf(), errors.GetAddressOf());
  if (Failed(hr)) {
    logger::error("TextureLab: {} compile failed: {}", a_entry,
                  errors.Get()
                      ? static_cast<const char *>(errors->GetBufferPointer())
                      : "no message");
    return {};
  }
  return out;
}
}

TextureLab::RenderTarget::~RenderTarget() {
  if (size != 0) {
    Metrics::CountTargetDestroyed(
        Metrics::MippedRgbaBytes(size) *
        (format == TextureFormat::kRgba32Float ? 4 : 1));
  }
  Trace::EmitSafely(Trace::Event::kTexture,
                    {{"action", "destroy"},
                     {"target", std::to_string(traceID_)},
                     {"generation", std::to_string(generation_)},
                     {"presenter", Trace::Pointer(presenter.get())}});
  if (presenter && originalData &&
      presenter->rendererTexture ==
          reinterpret_cast<RE::BSGraphics::Texture *>(ourData.get())) {
    presenter->rendererTexture =
        reinterpret_cast<RE::BSGraphics::Texture *>(originalData);
  }
}

RE::NiSourceTexture *TextureLab::RenderTarget::Texture() const noexcept {
  return presenter && ourData &&
                 presenter->rendererTexture ==
                     reinterpret_cast<RE::BSGraphics::Texture *>(ourData.get())
             ? presenter.get()
             : nullptr;
}

TextureLab::TextureLab()
    : targets_(std::make_unique<RenderTargetPool>()),
      previews_(std::make_unique<TexturePreviews>(*this)) {}

TextureLab::~TextureLab() = default;

TextureLab *TextureLab::GetSingleton() {
  static TextureLab lab;
  return &lab;
}

bool TextureLab::Available() const noexcept { return available_; }

bool TextureLab::ProgramPassAvailable() const noexcept {
  return Available() && gpu_->program.has_value();
}

bool TextureLab::RippleAvailable() const noexcept {
  return Available() && gpu_->ripple.has_value();
}

bool TextureLab::ClustersAvailable() const noexcept {
  return Available() && gpu_->clusters.has_value();
}

bool TextureLab::BakingAvailable() const noexcept {
  return Available() && gpu_->bake.has_value();
}

bool TextureLab::Init() {
  if (initTried_) {
    return Available();
  }
  initTried_ = true;
  borrowedDevice_ = RE::BSGraphics::Renderer::GetDevice();
  auto *rendererData = RE::BSGraphics::Renderer::GetRendererData();
  borrowedContext_ = rendererData ? rendererData->context : nullptr;
  if (!borrowedDevice_ || !borrowedContext_) {
    logger::error("TextureLab: no D3D11 device/context");
    return false;
  }

  auto resources = std::make_unique<GpuResources>();
  if (!CompileShaders(*resources)) {
    return false;
  }
  D3D11_BUFFER_DESC cbDesc{};
  cbDesc.byteWidth = sizeof(LayerConstants);
  cbDesc.usage = D3D11_USAGE_DEFAULT;
  cbDesc.bindFlags = D3D11_BIND_CONSTANT_BUFFER;
  if (Failed(borrowedDevice_->CreateBuffer(
          &cbDesc, nullptr, resources->constants.GetAddressOf()))) {
    logger::error("TextureLab: constant buffer creation failed");
    return false;
  }

  D3D11_SAMPLER_DESC sampDesc{};
  sampDesc.filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sampDesc.addressU = D3D11_TEXTURE_ADDRESS_WRAP;
  sampDesc.addressV = D3D11_TEXTURE_ADDRESS_WRAP;
  sampDesc.addressW = D3D11_TEXTURE_ADDRESS_WRAP;
  sampDesc.maxLOD = D3D11_FLOAT32_MAX;
  if (Failed(borrowedDevice_->CreateSamplerState(
          &sampDesc, resources->sampler.GetAddressOf()))) {
    logger::error("TextureLab: sampler creation failed");
    return false;
  }

  D3D11_BLEND_DESC blendDesc{};
  blendDesc.renderTarget[0].renderTargetWriteMask =
      D3D11_COLOR_WRITE_ENABLE_ALL;
  D3D11_DEPTH_STENCIL_DESC depthDesc{};
  D3D11_RASTERIZER_DESC rasterDesc{};
  rasterDesc.fillMode = D3D11_FILL_SOLID;
  rasterDesc.cullMode = D3D11_CULL_NONE;
  rasterDesc.depthClipEnable = true;
  if (Failed(borrowedDevice_->CreateBlendState(
          &blendDesc, resources->blend.GetAddressOf())) ||
      Failed(borrowedDevice_->CreateDepthStencilState(
          &depthDesc, resources->depth.GetAddressOf())) ||
      Failed(borrowedDevice_->CreateRasterizerState(
          &rasterDesc, resources->raster.GetAddressOf()))) {
    logger::error("TextureLab: pipeline state creation failed");
    return false;
  }

  gpu_ = std::move(resources);
  available_ = true;
  logger::info("TextureLab: ready (runtime layer textures)");
  return true;
}

bool TextureLab::CompileShaders(GpuResources &a_resources) {
  if (!CreateFullScreenShaders(a_resources)) {
    return false;
  }
  a_resources.program =
      CreatePixelPipeline("PSProgram", sizeof(ProgramConstants));
  a_resources.ripple = CreatePixelPipeline("PSRipple", sizeof(RippleConstants));
  a_resources.clusters =
      CreatePixelPipeline("PSClusters", sizeof(ClusterConstants));
  a_resources.dilate = CreatePixelPipeline("DilatePS", 0);
  a_resources.reduce =
      CreatePixelPipeline("PSReduce", sizeof(ReductionConstants));
  a_resources.stack = CreatePixelPipeline("PSStack", sizeof(StackConstants));
  a_resources.bake = CreateBakePipeline();
  return true;
}

bool TextureLab::CreateFullScreenShaders(GpuResources &a_resources) {
  const ComPtr<ID3DBlob> vertex = CompileEntry("VSMain", "vs_5_0");
  const ComPtr<ID3DBlob> pixel = CompileEntry("PSMain", "ps_5_0");
  if (!vertex.Get() || !pixel.Get() ||
      Failed(borrowedDevice_->CreateVertexShader(
          vertex->GetBufferPointer(), vertex->GetBufferSize(), nullptr,
          a_resources.vertex.GetAddressOf())) ||
      Failed(borrowedDevice_->CreatePixelShader(
          pixel->GetBufferPointer(), pixel->GetBufferSize(), nullptr,
          a_resources.pixel.GetAddressOf()))) {
    logger::error("TextureLab: required shader creation failed");
    return false;
  }
  return true;
}

std::optional<TextureLab::PixelPipeline>
TextureLab::CreatePixelPipeline(const char *a_entry,
                                std::uint32_t a_constantBytes) {
  const ComPtr<ID3DBlob> code = CompileEntry(a_entry, "ps_5_0");
  PixelPipeline pipeline;
  if (!code.Get() || Failed(borrowedDevice_->CreatePixelShader(
                         code->GetBufferPointer(), code->GetBufferSize(),
                         nullptr, pipeline.shader.GetAddressOf()))) {
    logger::error("TextureLab: {} pipeline unavailable", a_entry);
    return std::nullopt;
  }
  if (a_constantBytes > 0) {
    D3D11_BUFFER_DESC desc{};
    desc.byteWidth = a_constantBytes;
    desc.usage = D3D11_USAGE_DEFAULT;
    desc.bindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (Failed(borrowedDevice_->CreateBuffer(
            &desc, nullptr, pipeline.constants.GetAddressOf()))) {
      logger::error("TextureLab: {} pipeline unavailable", a_entry);
      return std::nullopt;
    }
  }
  return pipeline;
}

std::optional<TextureLab::BakePipeline> TextureLab::CreateBakePipeline() {
  const ComPtr<ID3DBlob> bakeVertex = CompileEntry("BakeVS", "vs_5_0");
  const ComPtr<ID3DBlob> bakePixel = CompileEntry("BakePS", "ps_5_0");
  BakePipeline bake;
  const D3D11_INPUT_ELEMENT_DESC elements[2]{
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 8,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
  };
  if (!bakeVertex.Get() || !bakePixel.Get() ||
      Failed(borrowedDevice_->CreateVertexShader(
          bakeVertex->GetBufferPointer(), bakeVertex->GetBufferSize(), nullptr,
          bake.vertex.GetAddressOf())) ||
      Failed(borrowedDevice_->CreatePixelShader(
          bakePixel->GetBufferPointer(), bakePixel->GetBufferSize(), nullptr,
          bake.pixel.GetAddressOf())) ||
      Failed(borrowedDevice_->CreateInputLayout(
          elements, 2, bakeVertex->GetBufferPointer(),
          bakeVertex->GetBufferSize(), bake.layout.GetAddressOf()))) {
    logger::error("TextureLab: bake pipeline unavailable");
    return std::nullopt;
  }
  return bake;
}

std::shared_ptr<TextureLab::RenderTarget>
TextureLab::Acquire(TextureSize a_size, std::string_view a_owner,
                    TextureFormat format, MipPolicy mips) {
  auto target =
      Init() ? targets_->Acquire(borrowedDevice_, a_size, a_owner, format)
             : nullptr;
  if (target)
    target->mips = mips;
  return target;
}

TextureLab::RenderTarget *
TextureLab::Scratch(TextureSize a_size, TextureFormat format, MipPolicy mips) {
  auto *target =
      Init() ? targets_->Scratch(borrowedDevice_, a_size, format) : nullptr;
  if (target)
    target->mips = mips;
  return target;
}

std::shared_ptr<TextureLab::RenderTarget>
TextureLab::Preview(RE::NiSourceTexture *a_source, ShaderChannel a_channel,
                    bool a_dynamic) {
  return previews_->Preview(a_source, a_channel, a_dynamic);
}

std::shared_ptr<TextureLab::RenderTarget>
TextureLab::SampledPreview(std::string a_context, RE::NiSourceTexture *a_source,
                           const PreviewSampling &a_sampling, bool a_dynamic) {
  return previews_->SampledPreview(std::move(a_context), a_source, a_sampling,
                                   a_dynamic);
}

TextureLab::PreviewDraw *
TextureLab::RetainPreviewDraw(std::shared_ptr<RenderTarget> a_target) {
  return previews_->RetainDraw(std::move(a_target));
}
void TextureLab::CollectPreviewDraws() { previews_->CollectDraws(); }

void TextureLab::RenderPreviews() { previews_->RenderPreviews(); }
void TextureLab::ClearPreviews() { previews_->ClearPreviews(); }
void TextureLab::InvalidatePreviews() noexcept {
  previews_->InvalidatePreviews();
}

void TextureLab::Clear() {
  targets_->ClearScratch();
  ClearPreviews();
  targets_->ClearUnused();
  luminance_.clear();
  channelMeans_.clear();
  reductionLevels_.clear();
  sampleWarned_.clear();
}
}
