#include "render/RuntimeTextures.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

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
  RendererLock(RendererLock &&) = delete;
  RendererLock &operator=(RendererLock &&) = delete;

private:
  RE::BSGraphics::Renderer *renderer_ = nullptr;
};

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

float MipThatFits(const TextureLab::Extent &a_extent,
                  std::uint32_t a_side) noexcept {
  std::uint32_t side = (std::max)(a_extent.width, a_extent.height);
  std::uint32_t mip = 0;
  while (mip < 16 && (side >> (mip + 1)) >= a_side) {
    ++mip;
  }
  return static_cast<float>(mip);
}
}

struct TextureLab::SavedState {
  REX::W32::ID3D11RenderTargetView *rtv = nullptr;
  REX::W32::ID3D11DepthStencilView *dsv = nullptr;
  D3D11_VIEWPORT viewport{};
  std::uint32_t viewportCount = 1;
  REX::W32::ID3D11VertexShader *vs = nullptr;
  REX::W32::ID3D11PixelShader *ps = nullptr;
  REX::W32::ID3D11ShaderResourceView *srvs[12]{};
  REX::W32::ID3D11SamplerState *sampler = nullptr;
  REX::W32::ID3D11Buffer *cbs[4]{};
  REX::W32::ID3D11InputLayout *layout = nullptr;
  REX::W32::ID3D11Buffer *vertexBuffer = nullptr;
  std::uint32_t vertexStride = 0;
  std::uint32_t vertexOffset = 0;
  REX::W32::ID3D11Buffer *indexBuffer = nullptr;
  DXGI_FORMAT indexFormat{};
  std::uint32_t indexOffset = 0;
  D3D11_PRIMITIVE_TOPOLOGY topology{};
  REX::W32::ID3D11BlendState *blend = nullptr;
  float blendFactor[4]{};
  std::uint32_t sampleMask = 0;
  REX::W32::ID3D11DepthStencilState *depth = nullptr;
  std::uint32_t stencilRef = 0;
  REX::W32::ID3D11RasterizerState *raster = nullptr;

  void Capture(REX::W32::ID3D11DeviceContext *a_ctx) {
    a_ctx->OMGetRenderTargets(1, &rtv, &dsv);
    viewportCount = 1;
    a_ctx->RSGetViewports(&viewportCount, &viewport);
    a_ctx->VSGetShader(&vs, nullptr, nullptr);
    a_ctx->PSGetShader(&ps, nullptr, nullptr);
    a_ctx->PSGetShaderResources(0, 12, srvs);
    a_ctx->PSGetSamplers(0, 1, &sampler);
    a_ctx->PSGetConstantBuffers(0, 4, cbs);
    a_ctx->IAGetInputLayout(&layout);
    a_ctx->IAGetVertexBuffers(0, 1, &vertexBuffer, &vertexStride,
                              &vertexOffset);
    a_ctx->IAGetIndexBuffer(&indexBuffer, &indexFormat, &indexOffset);
    a_ctx->IAGetPrimitiveTopology(&topology);
    a_ctx->OMGetBlendState(&blend, blendFactor, &sampleMask);
    a_ctx->OMGetDepthStencilState(&depth, &stencilRef);
    a_ctx->RSGetState(&raster);
  }

  void Restore(REX::W32::ID3D11DeviceContext *a_ctx) {
    a_ctx->OMSetRenderTargets(1, &rtv, dsv);
    if (viewportCount) {
      a_ctx->RSSetViewports(1, &viewport);
    }
    a_ctx->VSSetShader(vs, nullptr, 0);
    a_ctx->PSSetShader(ps, nullptr, 0);
    a_ctx->PSSetShaderResources(0, 12, srvs);
    a_ctx->PSSetSamplers(0, 1, &sampler);
    a_ctx->PSSetConstantBuffers(0, 4, cbs);
    a_ctx->IASetInputLayout(layout);
    a_ctx->IASetVertexBuffers(0, 1, &vertexBuffer, &vertexStride,
                              &vertexOffset);
    a_ctx->IASetIndexBuffer(indexBuffer, indexFormat, indexOffset);
    a_ctx->IASetPrimitiveTopology(topology);
    a_ctx->OMSetBlendState(blend, blendFactor, sampleMask);
    a_ctx->OMSetDepthStencilState(depth, stencilRef);
    a_ctx->RSSetState(raster);
    Release(rtv);
    Release(dsv);
    Release(vs);
    Release(ps);
    for (auto &srv : srvs) {
      Release(srv);
    }
    Release(sampler);
    Release(cbs[0]);
    Release(cbs[1]);
    Release(cbs[2]);
    Release(cbs[3]);
    Release(vertexBuffer);
    Release(indexBuffer);
    Release(layout);
    Release(blend);
    Release(depth);
    Release(raster);
  }
};

bool TextureLab::Render(RenderTarget &a_target, RE::NiSourceTexture *a_source,
                        const LayerParams &a_params) {
  const RendererLock rendererLock;
  if (!available_ || !a_target.rtv) {
    return false;
  }
  const bool layerPass = a_params.mode == Mode::kLayer;
  auto *sourceData = DataOf(layerPass ? a_params.layer.source : a_source);
  if (!layerPass && (!sourceData || !sourceData->resourceView)) {
    sourceData = DataOf(a_params.map.texture);
    if (!sourceData || !sourceData->resourceView) {
      return false;
    }
  }
  const bool haveSource = sourceData && sourceData->resourceView;

  const auto &sc = layerPass ? a_params.layer.input.transform : a_params.scroll;
  Constants constants{};
  constants.offsetScale[0] = sc.uOffset;
  constants.offsetScale[1] = sc.vOffset;
  constants.offsetScale[2] = sc.tileU;
  constants.offsetScale[3] = sc.tileV;
  constants.flags[0] = sc.mirrorU ? 1.0f : 0.0f;
  constants.flags[1] = sc.mirrorV ? 1.0f : 0.0f;
  constants.flags[2] = sc.transpose ? 1.0f : 0.0f;
  constants.flags[3] = static_cast<float>(a_params.mode);
  constants.extra[0] = sc.sourceMip;
  auto *mapData =
      DataOf(layerPass ? a_params.layer.mask : a_params.map.texture);
  const bool haveMap = mapData && mapData->resourceView &&
                       (layerPass || a_params.map.reading != MapReading::kNone);
  constants.extra[1] =
      haveMap && !layerPass ? static_cast<float>(a_params.map.reading) : 0.0f;
  auto *prevData = layerPass ? DataOf(a_params.layer.previous) : nullptr;
  const bool havePrev = prevData && prevData->resourceView;
  if (layerPass) {
    const auto &lp = a_params.layer;
    constants.layer[0] =
        static_cast<float>(std::to_underlying(lp.input.channel));
    constants.layer[1] = lp.input.meshSpace ? 1.0f : 0.0f;
    constants.layer[2] = static_cast<float>(lp.blend);
    constants.layer[3] = lp.opacity;
    constants.layerColor[0] = lp.color[0];
    constants.layerColor[1] = lp.color[1];
    constants.layerColor[2] = lp.color[2];
    constants.layerColor[3] = lp.normalize;
    constants.layerMask[0] =
        haveMap ? static_cast<float>(std::to_underlying(lp.maskChannel))
                : -1.0f;
    constants.layerMask[1] = static_cast<float>(lp.channels);
    constants.layerMask[2] = havePrev ? 1.0f : 0.0f;
    constants.layerMask[3] = haveSource ? 1.0f : 0.0f;
    constants.layerCurve[0] = lp.curve && lp.curve->srv ? 1.0f : 0.0f;
  }
  if (a_params.mode == Mode::kChannel) {
    constants.extra[2] =
        static_cast<float>(std::to_underlying(a_params.channel.channel));
    constants.extra[3] = a_params.channel.slope ? 1.0f : 0.0f;
  }

  SavedState saved;
  saved.Capture(context_);

  context_->UpdateSubresource(constants_, 0, nullptr, &constants, 0, 0);
  REX::W32::ID3D11RenderTargetView *rtv = a_target.rtv;
  context_->OMSetRenderTargets(1, &rtv, nullptr);
  D3D11_VIEWPORT viewport{};
  viewport.width = static_cast<float>(a_target.size);
  viewport.height = static_cast<float>(a_target.size);
  viewport.maxDepth = 1.0f;
  context_->RSSetViewports(1, &viewport);
  context_->IASetInputLayout(nullptr);
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_->VSSetShader(vs_, nullptr, 0);
  context_->PSSetShader(ps_, nullptr, 0);
  REX::W32::ID3D11ShaderResourceView *srvs[4]{
      haveSource ? reinterpret_cast<REX::W32::ID3D11ShaderResourceView *>(
                       sourceData->resourceView)
                 : nullptr,
      haveMap ? reinterpret_cast<REX::W32::ID3D11ShaderResourceView *>(
                    mapData->resourceView)
              : nullptr,
      havePrev ? reinterpret_cast<REX::W32::ID3D11ShaderResourceView *>(
                     prevData->resourceView)
               : nullptr,
      layerPass && a_params.layer.curve ? a_params.layer.curve->srv : nullptr};
  context_->PSSetShaderResources(0, 4, srvs);
  context_->PSSetSamplers(0, 1, &sampler_);
  context_->PSSetConstantBuffers(0, 1, &constants_);
  const float blendFactor[4]{};
  context_->OMSetBlendState(blend_, blendFactor, 0xFFFFFFFF);
  context_->OMSetDepthStencilState(depth_, 0);
  context_->RSSetState(raster_);
  context_->Draw(3, 0);

  REX::W32::ID3D11RenderTargetView *none = nullptr;
  REX::W32::ID3D11ShaderResourceView *noSrvs[4]{};
  context_->OMSetRenderTargets(1, &none, nullptr);
  context_->PSSetShaderResources(0, 4, noSrvs);
  context_->GenerateMips(a_target.srv);

  saved.Restore(context_);
  return true;
}

bool TextureLab::RenderProgram(RenderTarget &a_target,
                               const ProgramPass &a_pass) {
  const RendererLock rendererLock;
  if (!available_ || !a_target.rtv || !programPs_ || a_pass.code.size() > 256 ||
      a_pass.refCount > a_pass.refs.size() ||
      a_pass.textureCount > a_pass.textures.size() ||
      a_pass.curveCount > a_pass.curves.size()) {
    return false;
  }
  auto constants = std::make_unique<ProgramConstants>();
  std::memset(constants.get(), 0, sizeof(ProgramConstants));
  for (std::size_t k = 0; k < a_pass.code.size(); ++k) {
    constants->code[k][0] = static_cast<float>(a_pass.code[k].op);
    constants->code[k][1] = a_pass.code[k].number;
    constants->code[k][2] = static_cast<float>(a_pass.code[k].index);
  }
  for (std::size_t r = 0; r < a_pass.refCount; ++r) {
    const auto &ref = a_pass.refs[r];
    constants->refs[r][0] = ref.isTexture ? 1.0f : 0.0f;
    constants->refs[r][1] = static_cast<float>(ref.texture);
    constants->refValues[r][0] = ref.value.x;
    constants->refValues[r][1] = ref.value.y;
    constants->refValues[r][2] = ref.value.z;
  }
  REX::W32::ID3D11ShaderResourceView *srvs[12]{};
  for (std::size_t t = 0; t < a_pass.textureCount; ++t) {
    const auto &tex = a_pass.textures[t];
    const auto *data = DataOf(tex.texture);
    srvs[t] = data ? reinterpret_cast<REX::W32::ID3D11ShaderResourceView *>(
                         data->resourceView)
                   : nullptr;
    const auto &sc = tex.sampling.transform;
    constants->texParams[t][0] =
        static_cast<float>(std::to_underlying(tex.sampling.channel));
    constants->texParams[t][1] = tex.sampling.meshSpace ? 1.0f : 0.0f;
    constants->texParams[t][2] = tex.normalize;
    constants->texParams[t][3] = sc.sourceMip;
    constants->texTransform[t][0] = sc.uOffset;
    constants->texTransform[t][1] = sc.vOffset;
    constants->texTransform[t][2] = sc.tileU;
    constants->texTransform[t][3] = sc.tileV;
    constants->texFlags[t][0] = sc.mirrorU ? 1.0f : 0.0f;
    constants->texFlags[t][1] = sc.mirrorV ? 1.0f : 0.0f;
    constants->texFlags[t][2] = sc.transpose ? 1.0f : 0.0f;
  }
  for (std::size_t c = 0; c < a_pass.curveCount; ++c) {
    srvs[8 + c] = a_pass.curves[c] ? a_pass.curves[c]->srv : nullptr;
  }
  constants->misc[0] = a_pass.time;
  constants->misc[1] = static_cast<float>(a_pass.code.size());
  constants->misc[2] = a_pass.vectorResult ? 1.0f : 0.0f;

  SavedState saved;
  saved.Capture(context_);
  context_->UpdateSubresource(programConstants_, 0, nullptr, constants.get(), 0,
                              0);
  REX::W32::ID3D11RenderTargetView *rtv = a_target.rtv;
  context_->OMSetRenderTargets(1, &rtv, nullptr);
  D3D11_VIEWPORT viewport{};
  viewport.width = static_cast<float>(a_target.size);
  viewport.height = static_cast<float>(a_target.size);
  viewport.maxDepth = 1.0f;
  context_->RSSetViewports(1, &viewport);
  context_->IASetInputLayout(nullptr);
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_->VSSetShader(vs_, nullptr, 0);
  context_->PSSetShader(programPs_, nullptr, 0);
  context_->PSSetShaderResources(0, 12, srvs);
  context_->PSSetSamplers(0, 1, &sampler_);
  REX::W32::ID3D11Buffer *cbs[2]{constants_, programConstants_};
  context_->PSSetConstantBuffers(0, 2, cbs);
  const float blendFactor[4]{};
  context_->OMSetBlendState(blend_, blendFactor, 0xFFFFFFFF);
  context_->OMSetDepthStencilState(depth_, 0);
  context_->RSSetState(raster_);
  context_->Draw(3, 0);
  REX::W32::ID3D11RenderTargetView *none = nullptr;
  REX::W32::ID3D11ShaderResourceView *noSrvs[12]{};
  context_->OMSetRenderTargets(1, &none, nullptr);
  context_->PSSetShaderResources(0, 12, noSrvs);
  context_->GenerateMips(a_target.srv);
  saved.Restore(context_);
  return true;
}

bool TextureLab::BakeMesh(RenderTarget &a_target, const BakeBuffers &a_bake) {
  const RendererLock rendererLock;
  if (!available_ || !a_target.rtv || !BakingAvailable() ||
      a_bake.vertices.empty() || a_bake.indices.empty()) {
    return false;
  }
  D3D11_BUFFER_DESC vbDesc{};
  vbDesc.byteWidth =
      static_cast<std::uint32_t>(a_bake.vertices.size() * sizeof(BakeVertex));
  vbDesc.usage = D3D11_USAGE_IMMUTABLE;
  vbDesc.bindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_BUFFER_DESC ibDesc{};
  ibDesc.byteWidth =
      static_cast<std::uint32_t>(a_bake.indices.size() * sizeof(std::uint32_t));
  ibDesc.usage = D3D11_USAGE_IMMUTABLE;
  ibDesc.bindFlags = D3D11_BIND_INDEX_BUFFER;
  D3D11_SUBRESOURCE_DATA vbData{a_bake.vertices.data(), 0, 0};
  D3D11_SUBRESOURCE_DATA ibData{a_bake.indices.data(), 0, 0};
  REX::W32::ID3D11Buffer *vb = nullptr;
  REX::W32::ID3D11Buffer *ib = nullptr;
  if (Failed(device_->CreateBuffer(&vbDesc, &vbData, &vb)) ||
      Failed(device_->CreateBuffer(&ibDesc, &ibData, &ib))) {
    Release(vb);
    Release(ib);
    logger::error("TextureLab: bake buffers could not be created");
    return false;
  }

  SavedState saved;
  saved.Capture(context_);
  REX::W32::ID3D11RenderTargetView *rtv = a_target.rtv;
  const float black[4]{0.0f, 0.0f, 0.0f, 1.0f};
  context_->ClearRenderTargetView(rtv, black);
  context_->OMSetRenderTargets(1, &rtv, nullptr);
  D3D11_VIEWPORT viewport{};
  viewport.width = static_cast<float>(a_target.size);
  viewport.height = static_cast<float>(a_target.size);
  viewport.maxDepth = 1.0f;
  context_->RSSetViewports(1, &viewport);
  const std::uint32_t stride = sizeof(BakeVertex);
  const std::uint32_t offset = 0;
  context_->IASetInputLayout(bakeLayout_);
  context_->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
  context_->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT, 0);
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_->VSSetShader(bakeVs_, nullptr, 0);
  context_->PSSetShader(bakePs_, nullptr, 0);
  REX::W32::ID3D11ShaderResourceView *noSrvs[12]{};
  context_->PSSetShaderResources(0, 12, noSrvs);
  const float blendFactor[4]{};
  context_->OMSetBlendState(blend_, blendFactor, 0xFFFFFFFF);
  context_->OMSetDepthStencilState(depth_, 0);
  context_->RSSetState(raster_);
  context_->DrawIndexed(static_cast<std::uint32_t>(a_bake.indices.size()), 0,
                        0);
  REX::W32::ID3D11RenderTargetView *none = nullptr;
  context_->OMSetRenderTargets(1, &none, nullptr);
  context_->GenerateMips(a_target.srv);
  saved.Restore(context_);
  Release(vb);
  Release(ib);
  return true;
}

bool TextureLab::RenderRipple(RenderTarget &a_target,
                              const RipplePass &a_pass) {
  const RendererLock rendererLock;
  const auto *positions = DataOf(a_pass.positions);
  if (!available_ || !a_target.rtv || !ripplePs_ || !positions ||
      !positions->resourceView) {
    return false;
  }
  RippleConstants constants{};
  const auto count =
      std::min<std::size_t>(a_pass.firingCount, a_pass.firings.size());
  for (std::size_t k = 0; k < count; ++k) {
    constants.firings[k][0] = a_pass.firings[k].origin.x;
    constants.firings[k][1] = a_pass.firings[k].origin.y;
    constants.firings[k][2] = a_pass.firings[k].origin.z;
    constants.firings[k][3] = a_pass.firings[k].age;
  }
  constants.shape[0] = a_pass.speed;
  constants.shape[1] = a_pass.width;
  constants.shape[2] = a_pass.decay;
  constants.shape[3] = a_pass.disc ? 1.0f : 0.0f;
  constants.misc[0] = static_cast<float>(count);
  constants.misc[1] = a_pass.frame;

  SavedState saved;
  saved.Capture(context_);
  context_->UpdateSubresource(rippleConstants_, 0, nullptr, &constants, 0, 0);
  REX::W32::ID3D11RenderTargetView *rtv = a_target.rtv;
  context_->OMSetRenderTargets(1, &rtv, nullptr);
  D3D11_VIEWPORT viewport{};
  viewport.width = static_cast<float>(a_target.size);
  viewport.height = static_cast<float>(a_target.size);
  viewport.maxDepth = 1.0f;
  context_->RSSetViewports(1, &viewport);
  context_->IASetInputLayout(nullptr);
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_->VSSetShader(vs_, nullptr, 0);
  context_->PSSetShader(ripplePs_, nullptr, 0);
  REX::W32::ID3D11ShaderResourceView *srvs[12]{
      reinterpret_cast<REX::W32::ID3D11ShaderResourceView *>(
          positions->resourceView)};
  context_->PSSetShaderResources(0, 12, srvs);
  context_->PSSetSamplers(0, 1, &sampler_);
  REX::W32::ID3D11Buffer *cbs[3]{constants_, programConstants_,
                                 rippleConstants_};
  context_->PSSetConstantBuffers(0, 3, cbs);
  const float blendFactor[4]{};
  context_->OMSetBlendState(blend_, blendFactor, 0xFFFFFFFF);
  context_->OMSetDepthStencilState(depth_, 0);
  context_->RSSetState(raster_);
  context_->Draw(3, 0);
  REX::W32::ID3D11RenderTargetView *none = nullptr;
  REX::W32::ID3D11ShaderResourceView *noSrvs[12]{};
  context_->OMSetRenderTargets(1, &none, nullptr);
  context_->PSSetShaderResources(0, 12, noSrvs);
  context_->GenerateMips(a_target.srv);
  saved.Restore(context_);
  return true;
}

std::optional<MaterialSample>
TextureLab::SampleMaterial(RE::NiSourceTexture *a_rmaos,
                           RE::NiSourceTexture *a_diffuse) {
  static_assert(static_cast<std::size_t>(kSampleSide) * kSampleSide <=
                kMaxSampleTexels);
  const auto fail =
      [&](RE::NiSourceTexture *a_texture,
          std::string_view a_why) -> std::optional<MaterialSample> {
    if (sampleWarned_.insert(a_texture).second) {
      logger::warn("TextureLab: material sample: {}", a_why);
    }
    return std::nullopt;
  };
  if (!Init()) {
    return fail(nullptr, "the lab is unavailable");
  }
  const auto rmaosExtent = ExtentOf(a_rmaos);
  if (!rmaosExtent) {
    return fail(a_rmaos, "the RMAOS map is null or not a resident 2D texture");
  }
  const auto diffuseExtent = ExtentOf(a_diffuse);
  if (!diffuseExtent) {
    return fail(a_diffuse,
                "the diffuse map is null or not a resident 2D texture");
  }
  auto target = Acquire(TextureSize(kSampleSide));
  if (!target || target->size != kSampleSide) {
    return fail(a_rmaos, "no sample target");
  }
  const auto copyBack =
      [&](RE::NiSourceTexture *a_map,
          const Extent &a_extent) -> std::vector<std::uint8_t> {
    LayerParams params;
    params.mode = Mode::kCopy;
    params.scroll.sourceMip = MipThatFits(a_extent, kSampleSide);
    if (!Render(*target, a_map, params)) {
      return {};
    }
    return ReadBackPixels(*target);
  };
  const std::size_t texelCount =
      static_cast<std::size_t>(kSampleSide) * kSampleSide;
  const auto rmaos = copyBack(a_rmaos, *rmaosExtent);
  if (rmaos.size() != texelCount * 4) {
    return fail(a_rmaos, "the RMAOS map could not be read back");
  }
  const auto diffuse = copyBack(a_diffuse, *diffuseExtent);
  if (diffuse.size() != texelCount * 4) {
    return fail(a_diffuse, "the diffuse map could not be read back");
  }
  MaterialSample sample;
  sample.width = kSampleSide;
  sample.height = kSampleSide;
  sample.texels.reserve(texelCount);
  constexpr float scale = 1.0f / 255.0f;
  for (std::size_t i = 0; i < texelCount; ++i) {
    const std::uint8_t *m = rmaos.data() + i * 4;
    const std::uint8_t *d = diffuse.data() + i * 4;
    MaterialTexel texel;
    texel.roughness = m[0] * scale;
    texel.metallic = m[1] * scale;
    texel.occlusion = m[2] * scale;
    texel.reflectance = m[3] * scale;
    texel.luma = (0.2126f * d[0] + 0.7152f * d[1] + 0.0722f * d[2]) * scale;
    sample.texels.push_back(texel);
  }
  return sample;
}

bool TextureLab::RenderClusters(RenderTarget &a_target,
                                RE::NiSourceTexture *a_rmaos,
                                RE::NiSourceTexture *a_diffuse,
                                const MaterialAnalysis &a_analysis) {
  const RendererLock rendererLock;
  const auto *rmaosData = DataOf(a_rmaos);
  const auto *diffuseData = DataOf(a_diffuse);
  if (!available_ || !a_target.rtv || !classifyPs_ || !rmaosData ||
      !rmaosData->resourceView || !diffuseData || !diffuseData->resourceView ||
      a_analysis.clusters.size() > kMaxClusters) {
    return false;
  }
  const auto scale = [](float a_weight) {
    return std::isfinite(a_weight) && a_weight > 0.0f ? a_weight : 0.0f;
  };
  ClassifyConstants constants{};
  for (std::size_t k = 0; k < a_analysis.clusters.size(); ++k) {
    const auto &cluster = a_analysis.clusters[k];
    constants.centroidRmaos[k][0] = cluster.centroid.roughness;
    constants.centroidRmaos[k][1] = cluster.centroid.metallic;
    constants.centroidRmaos[k][2] = cluster.centroid.occlusion;
    constants.centroidRmaos[k][3] = cluster.centroid.reflectance;
    constants.centroidLuma[k][0] = cluster.centroid.luma;
    constants.centroidLuma[k][1] = static_cast<float>(cluster.id);
  }
  const auto &w = a_analysis.settings.weights;
  constants.weights[0] = scale(w.roughness);
  constants.weights[1] = scale(w.metallic);
  constants.weights[2] = scale(w.occlusion);
  constants.weights[3] = scale(w.reflectance);
  constants.misc[0] = scale(w.luma);
  constants.misc[1] = static_cast<float>(a_analysis.clusters.size());

  SavedState saved;
  saved.Capture(context_);
  context_->UpdateSubresource(classifyConstants_, 0, nullptr, &constants, 0, 0);
  REX::W32::ID3D11RenderTargetView *rtv = a_target.rtv;
  context_->OMSetRenderTargets(1, &rtv, nullptr);
  D3D11_VIEWPORT viewport{};
  viewport.width = static_cast<float>(a_target.size);
  viewport.height = static_cast<float>(a_target.size);
  viewport.maxDepth = 1.0f;
  context_->RSSetViewports(1, &viewport);
  context_->IASetInputLayout(nullptr);
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_->VSSetShader(vs_, nullptr, 0);
  context_->PSSetShader(classifyPs_, nullptr, 0);
  REX::W32::ID3D11ShaderResourceView *srvs[12]{
      reinterpret_cast<REX::W32::ID3D11ShaderResourceView *>(
          diffuseData->resourceView),
      reinterpret_cast<REX::W32::ID3D11ShaderResourceView *>(
          rmaosData->resourceView)};
  context_->PSSetShaderResources(0, 12, srvs);
  context_->PSSetSamplers(0, 1, &sampler_);
  REX::W32::ID3D11Buffer *cbs[4]{constants_, programConstants_,
                                 rippleConstants_, classifyConstants_};
  context_->PSSetConstantBuffers(0, 4, cbs);
  const float blendFactor[4]{};
  context_->OMSetBlendState(blend_, blendFactor, 0xFFFFFFFF);
  context_->OMSetDepthStencilState(depth_, 0);
  context_->RSSetState(raster_);
  context_->Draw(3, 0);
  REX::W32::ID3D11RenderTargetView *none = nullptr;
  REX::W32::ID3D11ShaderResourceView *noSrvs[12]{};
  context_->OMSetRenderTargets(1, &none, nullptr);
  context_->PSSetShaderResources(0, 12, noSrvs);
  context_->GenerateMips(a_target.srv);
  saved.Restore(context_);
  return true;
}
}
