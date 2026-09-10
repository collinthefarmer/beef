#include "RuntimeTextures.h"

#include "Identity.h"

#include <REX/W32/D3DCOMPILER.h>

#include <cstring>

namespace BetterEnchantmentEffects {
using namespace REX::W32;

extern const char *const kShaderSource;

namespace {
constexpr bool Failed(std::int32_t a_hr) noexcept { return a_hr < 0; }

constexpr std::uint32_t kPresenterCount = 512;

struct alignas(16) ProgramConstants {
  float code[256][4];
  float refs[16][4];
  float refValues[16][4];
  float texParams[8][4];
  float texTransform[8][4];
  float texFlags[8][4];
  float misc[4];
};
static_assert(sizeof(ProgramConstants) % 16 == 0);
static_assert(static_cast<int>(Program::Op::kNumber) == 0 &&
              static_cast<int>(Program::Op::kRef) == 3 &&
              static_cast<int>(Program::Op::kIf) == 22 &&
              static_cast<int>(Program::Op::kClamp) == 26 &&
              static_cast<int>(Program::Op::kStep) == 35 &&
              static_cast<int>(Program::Op::kLerp) == 37);

struct alignas(16) RippleConstants {
  float firings[8][4];
  float shape[4];
  float misc[4];
};

struct alignas(16) ClassifyConstants {
  float centroidRmaos[kMaxClusters][4];
  float centroidLuma[kMaxClusters][4];
  float weights[4];
  float misc[4];
};
static_assert(kMaxClusters == 8 && sizeof(ClassifyConstants) % 16 == 0);

struct alignas(16) Constants {
  float offsetScale[4];
  float flags[4];
  float extra[4];
  float extra2[4];
  float layer[4];
  float layerColor[4];
  float layerMask[4];
  float layerCurve[4];
};

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
}

TextureLab::RenderTarget::~RenderTarget() {
  if (presenter && originalData) {
    presenter->rendererTexture =
        reinterpret_cast<RE::BSGraphics::Texture *>(originalData);
  }
  Release(rtv);
  Release(srv);
  Release(texture);
  delete ourData;
}

RE::NiSourceTexture *TextureLab::RenderTarget::Texture() const noexcept {
  return presenter.get();
}

TextureLab::Lookup::~Lookup() {
  Release(srv);
  Release(texture);
}

TextureLab *TextureLab::GetSingleton() {
  static TextureLab lab;
  return &lab;
}

bool TextureLab::Available() const noexcept { return available_; }

bool TextureLab::InterpreterAvailable() const noexcept {
  return programPs_ != nullptr;
}

bool TextureLab::RippleAvailable() const noexcept {
  return ripplePs_ != nullptr;
}

bool TextureLab::ClassifyAvailable() const noexcept {
  return classifyPs_ != nullptr;
}

bool TextureLab::BakingAvailable() const noexcept {
  return bakeVs_ != nullptr && bakeLayout_ != nullptr;
}

bool TextureLab::Init() {
  if (initTried_) {
    return available_;
  }
  initTried_ = true;

  device_ = RE::BSGraphics::Renderer::GetDevice();
  auto *rendererData = RE::BSGraphics::Renderer::GetRendererData();
  context_ = rendererData ? rendererData->context : nullptr;
  if (!device_ || !context_) {
    logger::error("TextureLab: no D3D11 device/context");
    return false;
  }
  if (!CompileShaders()) {
    return false;
  }

  D3D11_BUFFER_DESC cbDesc{};
  cbDesc.byteWidth = sizeof(Constants);
  cbDesc.usage = D3D11_USAGE_DEFAULT;
  cbDesc.bindFlags = D3D11_BIND_CONSTANT_BUFFER;
  if (Failed(device_->CreateBuffer(&cbDesc, nullptr, &constants_))) {
    logger::error("TextureLab: constant buffer creation failed");
    return false;
  }
  cbDesc.byteWidth = sizeof(ProgramConstants);
  if (Failed(device_->CreateBuffer(&cbDesc, nullptr, &programConstants_))) {
    logger::error("TextureLab: program constant buffer creation failed");
    return false;
  }
  cbDesc.byteWidth = sizeof(RippleConstants);
  if (Failed(device_->CreateBuffer(&cbDesc, nullptr, &rippleConstants_))) {
    logger::error("TextureLab: ripple constant buffer creation failed");
    return false;
  }
  cbDesc.byteWidth = sizeof(ClassifyConstants);
  if (Failed(device_->CreateBuffer(&cbDesc, nullptr, &classifyConstants_))) {
    logger::error("TextureLab: classify constant buffer creation failed");
    return false;
  }

  D3D11_SAMPLER_DESC sampDesc{};
  sampDesc.filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sampDesc.addressU = D3D11_TEXTURE_ADDRESS_WRAP;
  sampDesc.addressV = D3D11_TEXTURE_ADDRESS_WRAP;
  sampDesc.addressW = D3D11_TEXTURE_ADDRESS_WRAP;
  sampDesc.maxLOD = D3D11_FLOAT32_MAX;
  if (Failed(device_->CreateSamplerState(&sampDesc, &sampler_))) {
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
  if (Failed(device_->CreateBlendState(&blendDesc, &blend_)) ||
      Failed(device_->CreateDepthStencilState(&depthDesc, &depth_)) ||
      Failed(device_->CreateRasterizerState(&rasterDesc, &raster_))) {
    logger::error("TextureLab: pipeline state creation failed");
    return false;
  }

  available_ = true;
  logger::info("TextureLab: ready (runtime layer textures)");
  return true;
}

bool TextureLab::CompileShaders() {
  ID3DBlob *code = nullptr;
  ID3DBlob *errors = nullptr;
  const auto compile = [&](const char *a_entry,
                           const char *a_target) -> ID3DBlob * {
    ID3DBlob *out = nullptr;
    ID3DBlob *err = nullptr;
    const auto hr = D3DCompile(kShaderSource, std::strlen(kShaderSource),
                               Identity::kName.data(), nullptr, nullptr,
                               a_entry, a_target, 0, 0, &out, &err);
    if (Failed(hr)) {
      logger::error("TextureLab: {} compile failed: {}", a_entry,
                    err ? static_cast<const char *>(err->GetBufferPointer())
                        : "no message");
      Release(err);
      return nullptr;
    }
    Release(err);
    return out;
  };
  (void)code;
  (void)errors;

  auto *vsBlob = compile("VSMain", "vs_5_0");
  auto *psBlob = compile("PSMain", "ps_5_0");
  auto *programBlob = compile("PSProgram", "ps_5_0");
  bool ok = vsBlob && psBlob;
  if (ok) {
    ok = !Failed(device_->CreateVertexShader(vsBlob->GetBufferPointer(),
                                             vsBlob->GetBufferSize(), nullptr,
                                             &vs_)) &&
         !Failed(device_->CreatePixelShader(psBlob->GetBufferPointer(),
                                            psBlob->GetBufferSize(), nullptr,
                                            &ps_));
    if (!ok) {
      logger::error("TextureLab: shader object creation failed");
    }
  }
  if (ok && programBlob &&
      Failed(device_->CreatePixelShader(programBlob->GetBufferPointer(),
                                        programBlob->GetBufferSize(), nullptr,
                                        &programPs_))) {
    logger::error("TextureLab: interpreter shader object creation failed; "
                  "expression masks evaluate as white");
    programPs_ = nullptr;
  }
  if (ok && !programPs_) {
    logger::error("TextureLab: the interpreter pass is unavailable; expression "
                  "masks evaluate as white");
  }
  auto *rippleBlob = compile("PSRipple", "ps_5_0");
  if (ok && rippleBlob &&
      Failed(device_->CreatePixelShader(rippleBlob->GetBufferPointer(),
                                        rippleBlob->GetBufferSize(), nullptr,
                                        &ripplePs_))) {
    ripplePs_ = nullptr;
  }
  if (ok && !ripplePs_) {
    logger::error(
        "TextureLab: the ripple pass is unavailable; ripple sources are black");
  }
  Release(rippleBlob);
  auto *classifyBlob = compile("PSClassify", "ps_5_0");
  if (ok && classifyBlob &&
      Failed(device_->CreatePixelShader(classifyBlob->GetBufferPointer(),
                                        classifyBlob->GetBufferSize(), nullptr,
                                        &classifyPs_))) {
    classifyPs_ = nullptr;
  }
  if (ok && !classifyPs_) {
    logger::error("TextureLab: the classify pass is unavailable; material "
                  "cluster maps are black");
  }
  Release(classifyBlob);
  auto *bakeVsBlob = compile("BakeVS", "vs_5_0");
  auto *bakePsBlob = compile("BakePS", "ps_5_0");
  if (ok && bakeVsBlob && bakePsBlob) {
    const D3D11_INPUT_ELEMENT_DESC elements[2]{
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 8,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (Failed(device_->CreateVertexShader(bakeVsBlob->GetBufferPointer(),
                                           bakeVsBlob->GetBufferSize(), nullptr,
                                           &bakeVs_)) ||
        Failed(device_->CreatePixelShader(bakePsBlob->GetBufferPointer(),
                                          bakePsBlob->GetBufferSize(), nullptr,
                                          &bakePs_)) ||
        Failed(device_->CreateInputLayout(
            elements, 2, bakeVsBlob->GetBufferPointer(),
            bakeVsBlob->GetBufferSize(), &bakeLayout_))) {
      logger::error("TextureLab: bake pass objects failed; bakes are "
                    "unavailable");
      Release(bakeVs_);
      Release(bakePs_);
      Release(bakeLayout_);
    }
  } else if (ok) {
    logger::error("TextureLab: the bake pass did not compile; bakes are "
                  "unavailable");
  }
  Release(bakeVsBlob);
  Release(bakePsBlob);
  Release(vsBlob);
  Release(psBlob);
  Release(programBlob);
  return ok;
}

RE::NiPointer<RE::NiSourceTexture> TextureLab::LoadPresenter() {
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

bool TextureLab::CreateTarget(RenderTarget &a_target, TextureSize a_size) {
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
  if (Failed(device_->CreateTexture2D(&desc, nullptr, &a_target.texture))) {
    logger::error("TextureLab: CreateTexture2D({}) failed", pixels);
    return false;
  }
  if (Failed(device_->CreateShaderResourceView(a_target.texture, nullptr,
                                               &a_target.srv)) ||
      Failed(device_->CreateRenderTargetView(a_target.texture, nullptr,
                                             &a_target.rtv))) {
    logger::error("TextureLab: view creation failed");
    return false;
  }
  a_target.presenter = LoadPresenter();
  if (!a_target.presenter) {
    return false;
  }
  a_target.originalData = DataOf(a_target.presenter.get());
  a_target.ourData = new RE::NiTexture::RendererData(
      static_cast<std::uint16_t>(pixels), static_cast<std::uint16_t>(pixels));
  a_target.ourData->texture =
      reinterpret_cast<::ID3D11Texture2D *>(a_target.texture);
  a_target.ourData->resourceView =
      reinterpret_cast<::ID3D11ShaderResourceView *>(a_target.srv);
  a_target.presenter->rendererTexture =
      reinterpret_cast<RE::BSGraphics::Texture *>(a_target.ourData);
  a_target.size = pixels;
  return true;
}

std::shared_ptr<TextureLab::RenderTarget>
TextureLab::Acquire(TextureSize a_size) {
  if (!Init()) {
    return nullptr;
  }
  const auto deleter = [this](RenderTarget *a_target) { Recycle(a_target); };
  for (auto it = pool_.begin(); it != pool_.end(); ++it) {
    if ((*it)->size == a_size.Pixels()) {
      auto *raw = it->release();
      pool_.erase(it);
      return std::shared_ptr<RenderTarget>{raw, deleter};
    }
  }
  auto target = std::make_unique<RenderTarget>();
  if (!CreateTarget(*target, a_size)) {
    return nullptr;
  }
  return std::shared_ptr<RenderTarget>{target.release(), deleter};
}

TextureLab::RenderTarget *TextureLab::Scratch(TextureSize a_size) {
  auto &target = scratch_[a_size.Pixels()];
  if (!target) {
    target = Acquire(a_size);
  }
  return target.get();
}

void TextureLab::Recycle(RenderTarget *a_target) {
  pool_.emplace_back(a_target);
}

void TextureLab::Clear() {
  scratch_.clear();
  ClearPreviews();
  pool_.clear();
  luminance_.clear();
  channelMeans_.clear();
  sampleWarned_.clear();
}
}
