// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "Core.h"
#include "PCH.h"
#include "diagnostics/GpuTiming.h"
#include "diagnostics/Trace.h"
#include "mesh/MaterialClusters.h"
#include "mesh/Mesh.h"
#include "mesh/TextureSize.h"
#include "planners/ConsumptionLeases.h"
#include "planners/FieldProgram.h"
#include "planners/GpuReduction.h"
#include "planners/StackShader.h"
#include "planners/TextureDemand.h"
#include "render/GeneratedShaders.h"

#include <REX/W32/COMPTR.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects {
struct ProgramConstants;
struct UploadedBakeBuffers;
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
    bool nearest = false;
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

  inline static constexpr std::uint32_t kPassSrvs =
      kProgramTextures + kProgramLookups;

  struct ProgramTexture {
    RE::NiSourceTexture *texture = nullptr;
    LayerInput sampling;
    float normalize = 1.0f;
  };
  struct ProgramBindings {
    std::array<Vec3, kProgramInputs> values{};
    std::uint32_t inputCount = 0;
    std::array<ProgramTexture, kProgramTextures> textures{};
    std::uint32_t textureCount = 0;
    std::array<const Lookup *, kProgramLookups> lookups{};
    std::uint32_t lookupCount = 0;
  };

  struct BoundLayerFields {
    ProgramPack pack;
    ProgramBindings bindings;
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
    Vec3 direction{};
    bool directional = false;
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
    std::optional<std::uint32_t> sourceSegment, maskSegment;
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
    [[nodiscard]] MipPolicy Mips() const noexcept { return mips; }

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
    TextureFormat format = TextureFormat::kRgba8;
    MipPolicy mips = MipPolicy::kGenerate;
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

  std::shared_ptr<RenderTarget>
  Acquire(TextureSize a_size, std::string_view a_owner,
          TextureFormat format = TextureFormat::kRgba8,
          MipPolicy mips = MipPolicy::kGenerate);
  void GenerateMipsFor(RenderTarget &a_target);

  bool Render(RenderTarget &a_target, RE::NiSourceTexture *a_source,
              const LayerParams &a_params);
  void SetGeneratedShaders(bool a_enabled) noexcept;
  bool RenderLayersOneByOne(RenderTarget &a_target, RE::NiSourceTexture *a_base,
                            std::span<const LayerPass> a_layers);
  bool RenderStack(RenderTarget &a_target, RE::NiSourceTexture *a_base,
                   std::span<const LayerPass> a_layers,
                   const BoundLayerFields &a_fields);
  struct LayersWithRenderedFields {
    std::vector<LayerPass> passes;
    std::vector<std::shared_ptr<RenderTarget>> targets;
  };
  [[nodiscard]] std::optional<LayersWithRenderedFields>
  RenderFieldsToTargets(std::span<const LayerPass> a_layers,
                        const BoundLayerFields &a_fields, TextureSize a_size);
  bool RenderProgram(RenderTarget &a_target, const FieldProgram &a_program,
                     const ProgramBindings &a_bindings);
  [[nodiscard]] bool ProgramPassAvailable() const noexcept;

  bool BakeMesh(RenderTarget &a_target, const BakeBuffers &a_bake);
  bool RenderRipple(RenderTarget &a_target, const RipplePass &a_pass);
  [[nodiscard]] bool RippleAvailable() const noexcept;

  class TimedSpan {
  public:
    TimedSpan(TextureLab &a_lab, std::string a_key);
    ~TimedSpan();
    TimedSpan(const TimedSpan &) = delete;
    TimedSpan &operator=(const TimedSpan &) = delete;
    TimedSpan(TimedSpan &&) = delete;
    TimedSpan &operator=(TimedSpan &&) = delete;

  private:
    TextureLab *lab_;
    std::optional<std::size_t> span_;
  };
  void BeginTimedTick(bool a_enabled);
  void EndTimedTick();
  void CollectTimings();
  [[nodiscard]] bool Timing() const noexcept;
  [[nodiscard]] GpuTiming::Totals DrainTimings();

  inline static constexpr std::uint32_t kSampleSide = 64;
  struct MaterialReadback {
    REX::W32::ComPtr<REX::W32::ID3D11Texture2D> rmaos;
    REX::W32::ComPtr<REX::W32::ID3D11Texture2D> diffuse;
    bool pending = false;
  };
  using MaterialSampleResult = std::expected<MaterialSample, std::string>;
  [[nodiscard]] bool SubmitMaterialSample(RE::NiSourceTexture *a_rmaos,
                                          RE::NiSourceTexture *a_diffuse,
                                          MaterialReadback &a_readback);
  [[nodiscard]] std::optional<MaterialSampleResult>
  CollectMaterialSample(MaterialReadback &a_readback);
  [[nodiscard]] std::optional<MaterialSample>
  SampleMaterial(RE::NiSourceTexture *a_rmaos, RE::NiSourceTexture *a_diffuse);

  bool RenderClusters(RenderTarget &a_target, RE::NiSourceTexture *a_rmaos,
                      RE::NiSourceTexture *a_diffuse,
                      const MaterialAnalysis &a_analysis);
  [[nodiscard]] bool ClustersAvailable() const noexcept;
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

  struct ReductionReadback {
    ReadbackRing ring;
    std::array<REX::W32::ComPtr<REX::W32::ID3D11Texture2D>, kReadbackSlots>
        staging;
  };
  [[nodiscard]] bool SubmitReduction(RenderTarget &a_field,
                                     ReductionKind a_kind, ValueType a_type,
                                     ReductionReadback &a_readback);
  [[nodiscard]] std::optional<ReductionResult>
  CollectReduction(ReductionKind a_kind, ValueType a_type,
                   ReductionExtent a_extent, ReductionReadback &a_readback);

  float MeanLuminance(RE::NiSourceTexture *a_source);
  float MeanChannel(RE::NiSourceTexture *a_source, ShaderChannel a_channel);

  std::shared_ptr<RenderTarget> Preview(RE::NiSourceTexture *a_source,
                                        ShaderChannel a_channel,
                                        bool a_dynamic);
  struct PreviewSampling {
    LayerInput input;
    float normalize = 1.0f;
    [[nodiscard]] bool operator==(const PreviewSampling &) const = default;
  };
  [[nodiscard]] std::shared_ptr<RenderTarget>
  SampledPreview(std::string a_context, RE::NiSourceTexture *a_source,
                 const PreviewSampling &a_sampling, bool a_dynamic);
  using PreviewDraw = ConsumptionLeases<RenderTarget>::Ticket;
  [[nodiscard]] PreviewDraw *
  RetainPreviewDraw(std::shared_ptr<RenderTarget> a_target);
  void CollectPreviewDraws();
  void RenderPreviews();
  void ClearPreviews();
  void InvalidatePreviews() noexcept;

  [[nodiscard]] RenderTarget *
  Scratch(TextureSize a_size, TextureFormat format = TextureFormat::kRgba8,
          MipPolicy mips = MipPolicy::kGenerate);

  void Clear();

private:
  struct RenderPass;

  struct PixelPipeline {
    REX::W32::ComPtr<REX::W32::ID3D11PixelShader> shader;
    REX::W32::ComPtr<REX::W32::ID3D11Buffer> constants;
  };
  struct ReductionLevel {
    REX::W32::ComPtr<REX::W32::ID3D11Texture2D> texture;
    REX::W32::ComPtr<REX::W32::ID3D11ShaderResourceView> srv;
    REX::W32::ComPtr<REX::W32::ID3D11RenderTargetView> rtv;
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
    std::optional<PixelPipeline> clusters;
    std::optional<PixelPipeline> dilate;
    std::optional<PixelPipeline> reduce;
    std::optional<PixelPipeline> stack;
    std::optional<BakePipeline> bake;
  };

  struct FullScreenDraw {
    REX::W32::ID3D11PixelShader *shader = nullptr;
    std::span<REX::W32::ID3D11ShaderResourceView *const> srvs;
    std::span<REX::W32::ID3D11Buffer *const> constants;
  };
  void DrawFullScreen(const RenderPass &a_pass, RenderTarget &a_target,
                      const FullScreenDraw &a_draw);
  void GenerateMips(const RenderPass &a_pass, RenderTarget &a_target);
  void BindTarget(const RenderPass &a_pass, RenderTarget &a_target);
  void UnbindTarget(const RenderPass &a_pass, std::uint32_t a_srvCount);
  void DrawBakeTriangles(const RenderPass &a_pass, RenderTarget &a_target,
                         const UploadedBakeBuffers &a_buffers);
  bool DrawDilation(const RenderPass &a_pass, RenderTarget &a_target);

  bool CompileShaders(GpuResources &a_resources);
  bool CreateFullScreenShaders(GpuResources &a_resources);
  [[nodiscard]] std::optional<PixelPipeline>
  CreatePixelPipeline(const char *a_entry, std::uint32_t a_constantBytes);
  [[nodiscard]] std::optional<BakePipeline> CreateBakePipeline();

  ReductionLevel *ReductionLevelFor(ReductionExtent a_extent);
  bool EnsureReductionStaging(ReductionReadback &a_readback);
  bool EnsureSampleStaging(MaterialReadback &a_readback);
  [[nodiscard]] bool CopyToStaging(RenderTarget &a_target,
                                   REX::W32::ID3D11Texture2D &a_staging);
  [[nodiscard]] std::optional<MaterialSampleResult>
  ReadMaterialSample(MaterialReadback &a_readback, bool a_wait);
  [[nodiscard]] bool DrawReduction(RenderTarget &a_field, ReductionKind a_kind,
                                   ValueType a_type,
                                   REX::W32::ID3D11Texture2D &a_staging);

  std::optional<float> ReadBackMean(RenderTarget &a_target);

  struct TimingQueries {
    REX::W32::ComPtr<REX::W32::ID3D11Query> disjoint;
    std::vector<REX::W32::ComPtr<REX::W32::ID3D11Query>> stamps;
  };
  [[nodiscard]] REX::W32::ID3D11Query *TimestampQuery(std::size_t a_slot,
                                                      std::size_t a_index);
  void Stamp(std::size_t a_span, bool a_end);
  static void
  FillLookups(std::span<REX::W32::ID3D11ShaderResourceView *> a_srvs,
              const ProgramBindings &a_bindings);
  bool DrawProgramPass(RenderTarget &a_target, const ProgramCode &a_code,
                       const ProgramBindings &a_bindings,
                       REX::W32::ID3D11PixelShader *a_shader = nullptr);
  bool DrawStackPass(RenderTarget &a_target, RE::NiSourceTexture *a_base,
                     std::span<const LayerPass> a_layers,
                     const BoundLayerFields &a_fields);
  [[nodiscard]] std::unique_ptr<ProgramConstants>
  StackProgramConstants(std::span<REX::W32::ID3D11ShaderResourceView *> a_srvs,
                        const BoundLayerFields &a_fields);
  [[nodiscard]] REX::W32::ID3D11PixelShader *
  StackShaderFor(const StackShape &a_shape);
  [[nodiscard]] bool CanRenderStack(const StackShape &a_shape,
                                    std::span<const LayerPass> a_layers,
                                    const BoundLayerFields &a_fields) const;

  std::atomic<bool> available_{false};
  bool initTried_ = false;
  REX::W32::ID3D11Device *borrowedDevice_ = nullptr;
  REX::W32::ID3D11DeviceContext *borrowedContext_ = nullptr;
  std::unique_ptr<GpuResources> gpu_;
  std::unordered_map<RE::NiSourceTexture *, float> luminance_;
  std::map<std::pair<RE::NiSourceTexture *, ShaderChannel>, float>
      channelMeans_;
  std::unordered_set<RE::NiSourceTexture *> sampleWarned_;
  std::map<std::pair<std::uint32_t, std::uint32_t>, ReductionLevel>
      reductionLevels_;

  std::array<TimingQueries, GpuTiming::kFrameSlots> timingQueries_;
  GpuTiming::Ring timingRing_;
  GpuTiming::Totals timingTotals_;
  std::optional<std::size_t> tickSpan_;
  bool generatedShaders_ = true;
  GeneratedShaders generated_;

  std::unique_ptr<RenderTargetPool> targets_;
  std::unique_ptr<TexturePreviews> previews_;
};
}
