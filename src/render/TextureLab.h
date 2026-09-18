#pragma once

#include "Core.h"
#include "PCH.h"
#include "diagnostics/Trace.h"
#include "mesh/MaterialClusters.h"
#include "mesh/Mesh.h"
#include "mesh/TextureSize.h"
#include "planners/ConsumptionLeases.h"
#include "recipe/Expression.h"

#include <REX/W32/COMPTR.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects {
class RenderTargetPool;
class TexturePreviews;

class TextureLab {
public:
  enum class Mode : std::uint32_t {
    kGlow = 0,
    kChannel = 4,
    kLayer = 6,
    kCopy = 7,
  };

  enum class MapReading : std::uint32_t {
    kNone = 0,
    kRmaos = 3,
    kNormalSlope = 4,
  };

  struct Scroll {
    float uOffset = 0.0f;
    float vOffset = 0.0f;
    float tileU = 1.0f;
    float tileV = 1.0f;
    bool mirrorU = false;
    bool mirrorV = false;
    bool transpose = false;
    float sourceMip = 0.0f;
    [[nodiscard]] bool operator==(const Scroll &) const = default;
  };

  struct InputMap {
    RE::NiSourceTexture *texture = nullptr;
    MapReading reading = MapReading::kNone;
  };

  struct ChannelParams {
    ShaderChannel channel = ShaderChannel::kRgb;
    bool slope = false;
  };

  struct LayerInput {
    ShaderChannel channel = ShaderChannel::kRgb;
    bool meshSpace = false;
    Scroll transform;
    [[nodiscard]] bool operator==(const LayerInput &) const = default;
  };

  class Lookup {
    class ConstructionKey {
      friend class TextureLab;
      ConstructionKey() = default;
    };

  public:
    explicit Lookup(ConstructionKey) {}
    Lookup(const Lookup &) = delete;
    Lookup &operator=(const Lookup &) = delete;
    Lookup(Lookup &&) = delete;
    Lookup &operator=(Lookup &&) = delete;

  private:
    friend class TextureLab;
    REX::W32::ComPtr<REX::W32::ID3D11Texture2D> texture;
    REX::W32::ComPtr<REX::W32::ID3D11ShaderResourceView> srv;
  };

  static_assert(!std::is_default_constructible_v<Lookup>);
  static_assert(!std::is_copy_constructible_v<Lookup>);
  static_assert(!std::is_copy_assignable_v<Lookup>);
  static_assert(!std::is_move_constructible_v<Lookup>);
  static_assert(!std::is_move_assignable_v<Lookup>);

  inline static constexpr std::uint32_t kProgramTextures = 8;
  inline static constexpr std::uint32_t kProgramRefs = 16;
  inline static constexpr std::uint32_t kProgramCurves = 4;
  inline static constexpr std::uint32_t kProgramStack = 32;

  struct ProgramTexture {
    RE::NiSourceTexture *texture = nullptr;
    LayerInput sampling;
    float normalize = 1.0f;
  };
  struct ProgramRef {
    bool isTexture = false;
    std::uint32_t texture = 0;
    Vec3 value{};
  };
  struct ProgramPass {
    std::span<const Program::Node> code;
    std::array<ProgramRef, kProgramRefs> refs{};
    std::uint32_t refCount = 0;
    std::array<ProgramTexture, kProgramTextures> textures{};
    std::uint32_t textureCount = 0;
    std::array<const Lookup *, kProgramCurves> curves{};
    std::uint32_t curveCount = 0;
    float time = 0.0f;
    bool vectorResult = false;
  };

  inline static constexpr std::uint32_t kRippleFirings = 8;
  struct RippleFiring {
    Vec3 origin;
    float age = 0.0f;
  };
  struct RipplePass {
    RE::NiSourceTexture *positions = nullptr;
    float frame = 128.0f;
    std::array<RippleFiring, kRippleFirings> firings{};
    std::uint32_t firingCount = 0;
    float speed = 100.0f;
    float width = 10.0f;
    float decay = 1.0f;
    bool disc = false;
  };

  struct LayerPass {
    RE::NiSourceTexture *previous = nullptr;
    RE::NiSourceTexture *source = nullptr;
    LayerInput input;
    float normalize = 1.0f;
    float color[3]{1.0f, 1.0f, 1.0f};
    float opacity = 1.0f;
    std::uint32_t blend = 0;
    std::uint32_t channels = 15;
    RE::NiSourceTexture *mask = nullptr;
    ShaderChannel maskChannel = ShaderChannel::kR;
    const Lookup *curve = nullptr;
  };

  struct LayerParams {
    Mode mode = Mode::kGlow;
    Scroll scroll;
    InputMap map;
    ChannelParams channel;
    LayerPass layer;
  };

  class RenderTarget {
  public:
    RenderTarget() = default;
    ~RenderTarget();
    RenderTarget(const RenderTarget &) = delete;
    RenderTarget &operator=(const RenderTarget &) = delete;
    RenderTarget(RenderTarget &&) = delete;
    RenderTarget &operator=(RenderTarget &&) = delete;
    [[nodiscard]] REX::W32::ID3D11ShaderResourceView *View() const noexcept {
      return srv.Get();
    }
    [[nodiscard]] RE::NiSourceTexture *Texture() const noexcept;
    [[nodiscard]] std::uint64_t Generation() const noexcept {
      return generation_;
    }

  private:
    friend class TextureLab;
    friend class RenderTargetPool;
    std::shared_ptr<const std::size_t> presenterSlot_;
    std::uint64_t traceID_ = Trace::NextID();
    std::uint64_t generation_ = 0;
    RE::NiPointer<RE::NiSourceTexture> presenter;
    RE::NiTexture::RendererData *originalData = nullptr;
    std::unique_ptr<RE::NiTexture::RendererData> ourData;
    REX::W32::ComPtr<REX::W32::ID3D11Texture2D> texture;
    REX::W32::ComPtr<REX::W32::ID3D11ShaderResourceView> srv;
    REX::W32::ComPtr<REX::W32::ID3D11RenderTargetView> rtv;
    std::uint32_t size = 0;
  };

  static_assert(!std::is_copy_constructible_v<RenderTarget>);
  static_assert(!std::is_copy_assignable_v<RenderTarget>);
  static_assert(!std::is_move_constructible_v<RenderTarget>);
  static_assert(!std::is_move_assignable_v<RenderTarget>);

  TextureLab();
  ~TextureLab();

  [[nodiscard]] static TextureLab *GetSingleton();

  bool Init();
  [[nodiscard]] bool Available() const noexcept;

  std::shared_ptr<RenderTarget> Acquire(TextureSize a_size);

  bool Render(RenderTarget &a_target, RE::NiSourceTexture *a_source,
              const LayerParams &a_params);
  bool RenderProgram(RenderTarget &a_target, const ProgramPass &a_pass);
  [[nodiscard]] bool InterpreterAvailable() const noexcept;

  bool BakeMesh(RenderTarget &a_target, const BakeBuffers &a_bake);
  bool RenderRipple(RenderTarget &a_target, const RipplePass &a_pass);
  [[nodiscard]] bool RippleAvailable() const noexcept;

  inline static constexpr std::uint32_t kSampleSide = 64;
  [[nodiscard]] std::optional<MaterialSample>
  SampleMaterial(RE::NiSourceTexture *a_rmaos, RE::NiSourceTexture *a_diffuse);

  bool RenderClusters(RenderTarget &a_target, RE::NiSourceTexture *a_rmaos,
                      RE::NiSourceTexture *a_diffuse,
                      const MaterialAnalysis &a_analysis);
  [[nodiscard]] bool ClassifyAvailable() const noexcept;
  [[nodiscard]] bool BakingAvailable() const noexcept;

  [[nodiscard]] std::vector<std::uint8_t>
  ReadBuffer(REX::W32::ID3D11Buffer *a_buffer, std::uint32_t a_bytes);

  std::shared_ptr<Lookup> CreateLookup(std::span<const float, 256> a_values);

  struct Extent {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
  };
  [[nodiscard]] static std::optional<Extent>
  ExtentOf(RE::NiSourceTexture *a_source);

  float MeanLuminance(RE::NiSourceTexture *a_source);
  float MeanChannel(RE::NiSourceTexture *a_source, ShaderChannel a_channel);

  std::shared_ptr<RenderTarget> Preview(RE::NiSourceTexture *a_source,
                                        ShaderChannel a_channel,
                                        bool a_dynamic);
  [[nodiscard]] std::shared_ptr<RenderTarget>
  SampledPreview(std::string a_context, RE::NiSourceTexture *a_source,
                 const LayerInput &a_sampling, float a_normalize,
                 bool a_dynamic);
  using PreviewDraw = ConsumptionLeases<RenderTarget>::Ticket;
  [[nodiscard]] PreviewDraw *
  RetainPreviewDraw(std::shared_ptr<RenderTarget> a_target);
  void CollectPreviewDraws();
  void RenderPreviews();
  void ClearPreviews();
  void InvalidatePreviews() noexcept;

  [[nodiscard]] RenderTarget *Scratch(TextureSize a_size);

  void Clear();

private:
  struct RenderPass;

  struct PixelPipeline {
    REX::W32::ComPtr<REX::W32::ID3D11PixelShader> shader;
    REX::W32::ComPtr<REX::W32::ID3D11Buffer> constants;
  };
  struct BakePipeline {
    REX::W32::ComPtr<REX::W32::ID3D11VertexShader> vertex;
    REX::W32::ComPtr<REX::W32::ID3D11PixelShader> pixel;
    REX::W32::ComPtr<REX::W32::ID3D11InputLayout> layout;
  };
  struct GpuResources {
    REX::W32::ComPtr<REX::W32::ID3D11VertexShader> vertex;
    REX::W32::ComPtr<REX::W32::ID3D11PixelShader> pixel;
    REX::W32::ComPtr<REX::W32::ID3D11Buffer> constants;
    REX::W32::ComPtr<REX::W32::ID3D11SamplerState> sampler;
    REX::W32::ComPtr<REX::W32::ID3D11BlendState> blend;
    REX::W32::ComPtr<REX::W32::ID3D11DepthStencilState> depth;
    REX::W32::ComPtr<REX::W32::ID3D11RasterizerState> raster;
    std::optional<PixelPipeline> program;
    std::optional<PixelPipeline> ripple;
    std::optional<PixelPipeline> classify;
    std::optional<BakePipeline> bake;
  };

  struct FullScreenDraw {
    REX::W32::ID3D11PixelShader *shader = nullptr;
    std::span<REX::W32::ID3D11ShaderResourceView *const> srvs;
    std::span<REX::W32::ID3D11Buffer *const> constants;
  };
  void DrawFullScreen(const RenderPass &a_pass, RenderTarget &a_target,
                      const FullScreenDraw &a_draw);

  bool CompileShaders(GpuResources &a_resources);

  std::optional<float> ReadBackMean(RenderTarget &a_target);
  std::vector<std::uint8_t> ReadBackPixels(RenderTarget &a_target);

  std::atomic<bool> available_{false};
  bool initTried_ = false;
  REX::W32::ID3D11Device *borrowedDevice_ = nullptr;
  REX::W32::ID3D11DeviceContext *borrowedContext_ = nullptr;
  std::unique_ptr<GpuResources> gpu_;
  std::unordered_map<RE::NiSourceTexture *, float> luminance_;
  std::map<std::pair<RE::NiSourceTexture *, ShaderChannel>, float>
      channelMeans_;
  std::unordered_set<RE::NiSourceTexture *> sampleWarned_;

  std::unique_ptr<RenderTargetPool> targets_;
  std::unique_ptr<TexturePreviews> previews_;
};
}
