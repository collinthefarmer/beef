#include "render/TextureLab.h"

#include "render/D3DResult.h"
#include "render/ShaderConstants.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>

namespace BetterEnchantmentEffects {
using namespace REX::W32;

namespace {
template <class T> void Release(T *&a_ptr) {
  if (a_ptr) {
    a_ptr->Release();
    a_ptr = nullptr;
  }
}

class RendererLock {
public:
  explicit RendererLock(RE::BSGraphics::Renderer &a_renderer)
      : renderer_(a_renderer) {
    renderer_.Lock();
  }
  ~RendererLock() { renderer_.Unlock(); }
  RendererLock(const RendererLock &) = delete;
  RendererLock &operator=(const RendererLock &) = delete;
  RendererLock(RendererLock &&) = delete;
  RendererLock &operator=(RendererLock &&) = delete;

private:
  RE::BSGraphics::Renderer &renderer_;
};

float MipThatFits(const TextureLab::Extent &a_extent,
                  std::uint32_t a_side) noexcept {
  std::uint32_t side = (std::max)(a_extent.width, a_extent.height);
  std::uint32_t mip = 0;
  while (mip < 16 && (side >> (mip + 1)) >= a_side) {
    ++mip;
  }
  return static_cast<float>(mip);
}

template <class Shader> struct SavedStage {
  using GetShader = void (ID3D11DeviceContext::*)(Shader **,
                                                  ID3D11ClassInstance **,
                                                  std::uint32_t *);
  using SetShader = void (ID3D11DeviceContext::*)(Shader *,
                                                  ID3D11ClassInstance *const *,
                                                  std::uint32_t);
  using GetResources = void (ID3D11DeviceContext::*)(
      std::uint32_t, std::uint32_t, REX::W32::ID3D11ShaderResourceView **);
  using SetResources = void (ID3D11DeviceContext::*)(
      std::uint32_t, std::uint32_t,
      REX::W32::ID3D11ShaderResourceView *const *);

  SavedStage() = default;
  ~SavedStage() {
    Release(shader);
    for (auto &instance : instances) {
      Release(instance);
    }
    for (auto &resource : resources) {
      Release(resource);
    }
  }
  SavedStage(const SavedStage &) = delete;
  SavedStage &operator=(const SavedStage &) = delete;
  SavedStage(SavedStage &&) = delete;
  SavedStage &operator=(SavedStage &&) = delete;

  void Capture(ID3D11DeviceContext *a_context, GetShader a_shader,
               GetResources a_resources) {
    (a_context->*a_shader)(&shader, instances, &instanceCount);
    (a_context->*a_resources)(0, std::size(resources), resources);
  }

  void Restore(ID3D11DeviceContext *a_context, SetShader a_shader,
               SetResources a_resources) {
    (a_context->*a_shader)(shader, instances, instanceCount);
    (a_context->*a_resources)(0, std::size(resources), resources);
  }

  Shader *shader = nullptr;
  ID3D11ClassInstance *instances[D3D11_SHADER_MAX_INTERFACES]{};
  std::uint32_t instanceCount = std::size(instances);
  REX::W32::ID3D11ShaderResourceView
      *resources[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
};
}

struct TextureLab::RenderPass {
  RenderPass(RE::BSGraphics::Renderer &a_renderer,
             ID3D11DeviceContext &a_context)
      : rendererLock_(a_renderer), context(&a_context) {
    Capture(context);
    context->GSSetShader(nullptr, nullptr, 0);
    context->HSSetShader(nullptr, nullptr, 0);
    context->DSSetShader(nullptr, nullptr, 0);
    context->SetPredication(nullptr, false);
  }
  ~RenderPass() { Restore(context); }
  RenderPass(const RenderPass &) = delete;
  RenderPass &operator=(const RenderPass &) = delete;
  RenderPass(RenderPass &&) = delete;
  RenderPass &operator=(RenderPass &&) = delete;

  [[nodiscard]] ID3D11DeviceContext &Context() const { return *context; }

private:
  RendererLock rendererLock_;
  ID3D11DeviceContext *context;
  REX::W32::ID3D11RenderTargetView
      *rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
  std::uint32_t renderTargetCount = std::size(rtvs);
  REX::W32::ID3D11UnorderedAccessView *firstUav = nullptr;
  ID3D11Predicate *predicate = nullptr;
  REX::W32::BOOL predicateValue = false;
  REX::W32::ID3D11DepthStencilView *dsv = nullptr;
  D3D11_VIEWPORT
  viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
  std::uint32_t viewportCount = std::size(viewports);
  SavedStage<ID3D11VertexShader> vertex;
  SavedStage<ID3D11PixelShader> pixel;
  SavedStage<ID3D11GeometryShader> geometry;
  SavedStage<ID3D11HullShader> hull;
  SavedStage<ID3D11DomainShader> domain;
  REX::W32::ID3D11ShaderResourceView
      *computeResources[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
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

  void CaptureOutputs(ID3D11DeviceContext *a_ctx) {
    a_ctx->OMGetRenderTargets(std::size(rtvs), rtvs, &dsv);
    while (renderTargetCount && !rtvs[renderTargetCount - 1]) {
      --renderTargetCount;
    }
    if (renderTargetCount == 0) {
      a_ctx->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0,
                                                       1, &firstUav);
    }
  }

  void RestoreOutputs(ID3D11DeviceContext *a_ctx) {
    a_ctx->OMSetRenderTargets(renderTargetCount, rtvs, dsv);
    if (firstUav) {
      const std::uint32_t keepCounter =
          std::numeric_limits<std::uint32_t>::max();
      a_ctx->OMSetRenderTargetsAndUnorderedAccessViews(
          D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL, nullptr, nullptr, 0, 1,
          &firstUav, &keepCounter);
    }
    for (auto &rtv : rtvs) {
      Release(rtv);
    }
    Release(firstUav);
    Release(dsv);
  }

  void CaptureStages(ID3D11DeviceContext *a_ctx) {
    vertex.Capture(a_ctx, &ID3D11DeviceContext::VSGetShader,
                   &ID3D11DeviceContext::VSGetShaderResources);
    pixel.Capture(a_ctx, &ID3D11DeviceContext::PSGetShader,
                  &ID3D11DeviceContext::PSGetShaderResources);
    geometry.Capture(a_ctx, &ID3D11DeviceContext::GSGetShader,
                     &ID3D11DeviceContext::GSGetShaderResources);
    hull.Capture(a_ctx, &ID3D11DeviceContext::HSGetShader,
                 &ID3D11DeviceContext::HSGetShaderResources);
    domain.Capture(a_ctx, &ID3D11DeviceContext::DSGetShader,
                   &ID3D11DeviceContext::DSGetShaderResources);
    a_ctx->CSGetShaderResources(0, std::size(computeResources),
                                computeResources);
  }

  void RestoreStages(ID3D11DeviceContext *a_ctx) {
    vertex.Restore(a_ctx, &ID3D11DeviceContext::VSSetShader,
                   &ID3D11DeviceContext::VSSetShaderResources);
    pixel.Restore(a_ctx, &ID3D11DeviceContext::PSSetShader,
                  &ID3D11DeviceContext::PSSetShaderResources);
    geometry.Restore(a_ctx, &ID3D11DeviceContext::GSSetShader,
                     &ID3D11DeviceContext::GSSetShaderResources);
    hull.Restore(a_ctx, &ID3D11DeviceContext::HSSetShader,
                 &ID3D11DeviceContext::HSSetShaderResources);
    domain.Restore(a_ctx, &ID3D11DeviceContext::DSSetShader,
                   &ID3D11DeviceContext::DSSetShaderResources);
    a_ctx->CSSetShaderResources(0, std::size(computeResources),
                                computeResources);
    for (auto &resource : computeResources) {
      Release(resource);
    }
  }

  void Capture(REX::W32::ID3D11DeviceContext *a_ctx) {
    CaptureOutputs(a_ctx);
    a_ctx->GetPredication(&predicate, &predicateValue);
    a_ctx->RSGetViewports(&viewportCount, viewports);
    CaptureStages(a_ctx);
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
    RestoreOutputs(a_ctx);
    a_ctx->SetPredication(predicate, predicateValue);
    Release(predicate);
    a_ctx->RSSetViewports(viewportCount, viewports);
    RestoreStages(a_ctx);
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
  auto *renderer = RE::BSGraphics::Renderer::GetSingleton();
  if (!available_ || !renderer || !borrowedContext_ || !a_target.rtv.Get()) {
    return false;
  }
  const RenderPass pass{*renderer, *borrowedContext_};
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
  LayerConstants constants{};
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
    constants.layerCurve[0] = lp.curve && lp.curve->srv.Get() ? 1.0f : 0.0f;
  }
  if (a_params.mode == Mode::kChannel) {
    constants.extra[2] =
        static_cast<float>(std::to_underlying(a_params.channel.channel));
    constants.extra[3] = a_params.channel.slope ? 1.0f : 0.0f;
  }

  pass.Context().UpdateSubresource(gpu_->constants.Get(), 0, nullptr,
                                   &constants, 0, 0);
  REX::W32::ID3D11RenderTargetView *rtv = a_target.rtv.Get();
  pass.Context().OMSetRenderTargets(1, &rtv, nullptr);
  D3D11_VIEWPORT viewport{};
  viewport.width = static_cast<float>(a_target.size);
  viewport.height = static_cast<float>(a_target.size);
  viewport.maxDepth = 1.0f;
  pass.Context().RSSetViewports(1, &viewport);
  pass.Context().IASetInputLayout(nullptr);
  pass.Context().IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  pass.Context().VSSetShader(gpu_->vertex.Get(), nullptr, 0);
  pass.Context().PSSetShader(gpu_->pixel.Get(), nullptr, 0);
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
      layerPass && a_params.layer.curve ? a_params.layer.curve->srv.Get()
                                        : nullptr};
  pass.Context().PSSetShaderResources(0, 4, srvs);
  pass.Context().PSSetSamplers(0, 1, gpu_->sampler.GetAddressOf());
  pass.Context().PSSetConstantBuffers(0, 1, gpu_->constants.GetAddressOf());
  const float blendFactor[4]{};
  pass.Context().OMSetBlendState(gpu_->blend.Get(), blendFactor, 0xFFFFFFFF);
  pass.Context().OMSetDepthStencilState(gpu_->depth.Get(), 0);
  pass.Context().RSSetState(gpu_->raster.Get());
  pass.Context().Draw(3, 0);

  REX::W32::ID3D11RenderTargetView *none = nullptr;
  REX::W32::ID3D11ShaderResourceView *noSrvs[4]{};
  pass.Context().OMSetRenderTargets(1, &none, nullptr);
  pass.Context().PSSetShaderResources(0, 4, noSrvs);
  pass.Context().GenerateMips(a_target.srv.Get());

  return true;
}

bool TextureLab::RenderProgram(RenderTarget &a_target,
                               const ProgramPass &a_pass) {
  auto *renderer = RE::BSGraphics::Renderer::GetSingleton();
  if (!available_ || !renderer || !borrowedContext_ || !a_target.rtv.Get() ||
      !gpu_->program.has_value() || a_pass.code.size() > 256 ||
      a_pass.refCount > a_pass.refs.size() ||
      a_pass.textureCount > a_pass.textures.size() ||
      a_pass.curveCount > a_pass.curves.size()) {
    return false;
  }
  const PixelPipeline &pipeline = *gpu_->program;
  const RenderPass pass{*renderer, *borrowedContext_};
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
    srvs[8 + c] = a_pass.curves[c] ? a_pass.curves[c]->srv.Get() : nullptr;
  }
  constants->misc[0] = a_pass.time;
  constants->misc[1] = static_cast<float>(a_pass.code.size());
  constants->misc[2] = a_pass.vectorResult ? 1.0f : 0.0f;

  pass.Context().UpdateSubresource(pipeline.constants.Get(), 0, nullptr,
                                   constants.get(), 0, 0);
  REX::W32::ID3D11RenderTargetView *rtv = a_target.rtv.Get();
  pass.Context().OMSetRenderTargets(1, &rtv, nullptr);
  D3D11_VIEWPORT viewport{};
  viewport.width = static_cast<float>(a_target.size);
  viewport.height = static_cast<float>(a_target.size);
  viewport.maxDepth = 1.0f;
  pass.Context().RSSetViewports(1, &viewport);
  pass.Context().IASetInputLayout(nullptr);
  pass.Context().IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  pass.Context().VSSetShader(gpu_->vertex.Get(), nullptr, 0);
  pass.Context().PSSetShader(pipeline.shader.Get(), nullptr, 0);
  pass.Context().PSSetShaderResources(0, 12, srvs);
  pass.Context().PSSetSamplers(0, 1, gpu_->sampler.GetAddressOf());
  REX::W32::ID3D11Buffer *cbs[2]{gpu_->constants.Get(),
                                 pipeline.constants.Get()};
  pass.Context().PSSetConstantBuffers(0, 2, cbs);
  const float blendFactor[4]{};
  pass.Context().OMSetBlendState(gpu_->blend.Get(), blendFactor, 0xFFFFFFFF);
  pass.Context().OMSetDepthStencilState(gpu_->depth.Get(), 0);
  pass.Context().RSSetState(gpu_->raster.Get());
  pass.Context().Draw(3, 0);
  REX::W32::ID3D11RenderTargetView *none = nullptr;
  REX::W32::ID3D11ShaderResourceView *noSrvs[12]{};
  pass.Context().OMSetRenderTargets(1, &none, nullptr);
  pass.Context().PSSetShaderResources(0, 12, noSrvs);
  pass.Context().GenerateMips(a_target.srv.Get());
  return true;
}

bool TextureLab::BakeMesh(RenderTarget &a_target, const BakeBuffers &a_bake) {
  auto *renderer = RE::BSGraphics::Renderer::GetSingleton();
  if (!available_ || !renderer || !borrowedContext_ || !borrowedDevice_ ||
      !a_target.rtv.Get() || !gpu_->bake.has_value() ||
      a_bake.vertices.empty() || a_bake.indices.empty()) {
    return false;
  }
  const BakePipeline &pipeline = *gpu_->bake;
  if (a_bake.vertices.size() >
          std::numeric_limits<std::uint32_t>::max() / sizeof(BakeVertex) ||
      a_bake.indices.size() >
          std::numeric_limits<std::uint32_t>::max() / sizeof(std::uint32_t)) {
    logger::error("TextureLab: bake buffers exceed the D3D11 byte range");
    return false;
  }
  const RenderPass pass{*renderer, *borrowedContext_};
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
  ComPtr<ID3D11Buffer> vb;
  ComPtr<ID3D11Buffer> ib;
  if (Failed(
          borrowedDevice_->CreateBuffer(&vbDesc, &vbData, vb.GetAddressOf())) ||
      Failed(
          borrowedDevice_->CreateBuffer(&ibDesc, &ibData, ib.GetAddressOf()))) {
    logger::error("TextureLab: bake buffers could not be created");
    return false;
  }

  REX::W32::ID3D11RenderTargetView *rtv = a_target.rtv.Get();
  const float black[4]{0.0f, 0.0f, 0.0f, 1.0f};
  pass.Context().ClearRenderTargetView(rtv, black);
  pass.Context().OMSetRenderTargets(1, &rtv, nullptr);
  D3D11_VIEWPORT viewport{};
  viewport.width = static_cast<float>(a_target.size);
  viewport.height = static_cast<float>(a_target.size);
  viewport.maxDepth = 1.0f;
  pass.Context().RSSetViewports(1, &viewport);
  const std::uint32_t stride = sizeof(BakeVertex);
  const std::uint32_t offset = 0;
  pass.Context().IASetInputLayout(pipeline.layout.Get());
  pass.Context().IASetVertexBuffers(0, 1, vb.GetAddressOf(), &stride, &offset);
  pass.Context().IASetIndexBuffer(ib.Get(), DXGI_FORMAT_R32_UINT, 0);
  pass.Context().IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  pass.Context().VSSetShader(pipeline.vertex.Get(), nullptr, 0);
  pass.Context().PSSetShader(pipeline.pixel.Get(), nullptr, 0);
  REX::W32::ID3D11ShaderResourceView *noSrvs[12]{};
  pass.Context().PSSetShaderResources(0, 12, noSrvs);
  const float blendFactor[4]{};
  pass.Context().OMSetBlendState(gpu_->blend.Get(), blendFactor, 0xFFFFFFFF);
  pass.Context().OMSetDepthStencilState(gpu_->depth.Get(), 0);
  pass.Context().RSSetState(gpu_->raster.Get());
  pass.Context().DrawIndexed(static_cast<std::uint32_t>(a_bake.indices.size()),
                             0, 0);
  REX::W32::ID3D11RenderTargetView *none = nullptr;
  pass.Context().OMSetRenderTargets(1, &none, nullptr);
  pass.Context().GenerateMips(a_target.srv.Get());
  return true;
}

bool TextureLab::RenderRipple(RenderTarget &a_target,
                              const RipplePass &a_pass) {
  auto *renderer = RE::BSGraphics::Renderer::GetSingleton();
  if (!available_ || !renderer || !borrowedContext_ || !a_target.rtv.Get() ||
      !gpu_->ripple.has_value()) {
    return false;
  }
  const PixelPipeline &pipeline = *gpu_->ripple;
  const RenderPass pass{*renderer, *borrowedContext_};
  const auto *positions = DataOf(a_pass.positions);
  if (!positions || !positions->resourceView) {
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

  pass.Context().UpdateSubresource(pipeline.constants.Get(), 0, nullptr,
                                   &constants, 0, 0);
  REX::W32::ID3D11RenderTargetView *rtv = a_target.rtv.Get();
  pass.Context().OMSetRenderTargets(1, &rtv, nullptr);
  D3D11_VIEWPORT viewport{};
  viewport.width = static_cast<float>(a_target.size);
  viewport.height = static_cast<float>(a_target.size);
  viewport.maxDepth = 1.0f;
  pass.Context().RSSetViewports(1, &viewport);
  pass.Context().IASetInputLayout(nullptr);
  pass.Context().IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  pass.Context().VSSetShader(gpu_->vertex.Get(), nullptr, 0);
  pass.Context().PSSetShader(pipeline.shader.Get(), nullptr, 0);
  REX::W32::ID3D11ShaderResourceView *srvs[12]{
      reinterpret_cast<REX::W32::ID3D11ShaderResourceView *>(
          positions->resourceView)};
  pass.Context().PSSetShaderResources(0, 12, srvs);
  pass.Context().PSSetSamplers(0, 1, gpu_->sampler.GetAddressOf());
  REX::W32::ID3D11Buffer *cbs[3]{nullptr, nullptr, pipeline.constants.Get()};
  pass.Context().PSSetConstantBuffers(0, 3, cbs);
  const float blendFactor[4]{};
  pass.Context().OMSetBlendState(gpu_->blend.Get(), blendFactor, 0xFFFFFFFF);
  pass.Context().OMSetDepthStencilState(gpu_->depth.Get(), 0);
  pass.Context().RSSetState(gpu_->raster.Get());
  pass.Context().Draw(3, 0);
  REX::W32::ID3D11RenderTargetView *none = nullptr;
  REX::W32::ID3D11ShaderResourceView *noSrvs[12]{};
  pass.Context().OMSetRenderTargets(1, &none, nullptr);
  pass.Context().PSSetShaderResources(0, 12, noSrvs);
  pass.Context().GenerateMips(a_target.srv.Get());
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
  auto *renderer = RE::BSGraphics::Renderer::GetSingleton();
  if (!available_ || !renderer || !borrowedContext_ || !a_target.rtv.Get() ||
      !gpu_->classify.has_value()) {
    return false;
  }
  const PixelPipeline &pipeline = *gpu_->classify;
  const RenderPass pass{*renderer, *borrowedContext_};
  const auto *rmaosData = DataOf(a_rmaos);
  const auto *diffuseData = DataOf(a_diffuse);
  if (!rmaosData || !rmaosData->resourceView || !diffuseData ||
      !diffuseData->resourceView || a_analysis.clusters.size() > kMaxClusters) {
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

  pass.Context().UpdateSubresource(pipeline.constants.Get(), 0, nullptr,
                                   &constants, 0, 0);
  REX::W32::ID3D11RenderTargetView *rtv = a_target.rtv.Get();
  pass.Context().OMSetRenderTargets(1, &rtv, nullptr);
  D3D11_VIEWPORT viewport{};
  viewport.width = static_cast<float>(a_target.size);
  viewport.height = static_cast<float>(a_target.size);
  viewport.maxDepth = 1.0f;
  pass.Context().RSSetViewports(1, &viewport);
  pass.Context().IASetInputLayout(nullptr);
  pass.Context().IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  pass.Context().VSSetShader(gpu_->vertex.Get(), nullptr, 0);
  pass.Context().PSSetShader(pipeline.shader.Get(), nullptr, 0);
  REX::W32::ID3D11ShaderResourceView *srvs[12]{
      reinterpret_cast<REX::W32::ID3D11ShaderResourceView *>(
          diffuseData->resourceView),
      reinterpret_cast<REX::W32::ID3D11ShaderResourceView *>(
          rmaosData->resourceView)};
  pass.Context().PSSetShaderResources(0, 12, srvs);
  pass.Context().PSSetSamplers(0, 1, gpu_->sampler.GetAddressOf());
  REX::W32::ID3D11Buffer *cbs[4]{nullptr, nullptr, nullptr,
                                 pipeline.constants.Get()};
  pass.Context().PSSetConstantBuffers(0, 4, cbs);
  const float blendFactor[4]{};
  pass.Context().OMSetBlendState(gpu_->blend.Get(), blendFactor, 0xFFFFFFFF);
  pass.Context().OMSetDepthStencilState(gpu_->depth.Get(), 0);
  pass.Context().RSSetState(gpu_->raster.Get());
  pass.Context().Draw(3, 0);
  REX::W32::ID3D11RenderTargetView *none = nullptr;
  REX::W32::ID3D11ShaderResourceView *noSrvs[12]{};
  pass.Context().OMSetRenderTargets(1, &none, nullptr);
  pass.Context().PSSetShaderResources(0, 12, noSrvs);
  pass.Context().GenerateMips(a_target.srv.Get());
  return true;
}
}
